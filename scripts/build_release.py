#!/usr/bin/env python3
"""Run a fresh Meson build and bind its source inputs, SDK and outputs."""
from __future__ import annotations

import argparse
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys

from package_source_companion import build_metadata, source_inventory
from source_companion_common import (SOURCE_DIRECTORIES, SOURCE_ROOT_FILES, file_hashes, new_output,
                                     read_json, reject_links, sha256, unique_object, verify_files, walk_files, write_json)


def capture_sources(source: Path) -> dict[str, str]:
    reject_links(source)
    files = {name: source / name for name in SOURCE_ROOT_FILES if (source / name).is_file()}
    for directory in SOURCE_DIRECTORIES:
        files.update({directory + '/' + name: path for name, path in walk_files(source / directory).items()})
    hashes = file_hashes({name: sha256(path) for name, path in sorted(files.items())})
    source_inventory(source, {'sourceHashes': hashes})
    return hashes


def run_logged(name: str, command: list[str], directory: Path, cwd: Path, env: dict, timeout: int | None = None) -> dict:
    log = directory / (name + '.txt')
    print(f'{name}: running; log {log}', file=sys.stderr, flush=True)
    with log.open('x', encoding='utf-8') as stream:
        process = subprocess.run(command, cwd=cwd, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
    result = {'name': name, 'command': command, 'exitCode': process.returncode,
              'log': log.name, 'logSha256': sha256(log)}
    write_json(directory / (name + '.json'), result)
    if process.returncode:
        raise ValueError(f'{name} failed with exit code {process.returncode}; inspect {log}')
    return result


def introspection_list(path: Path) -> list[dict]:
    reject_links(path)
    if not path.is_file() or path.stat().st_size > 32 * 1024 * 1024:
        raise ValueError('Missing or oversized Meson introspection: ' + str(path))
    value = json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=unique_object)
    if not isinstance(value, list) or not all(isinstance(item, dict) for item in value):
        raise ValueError('Expected Meson introspection array: ' + str(path))
    return value


def qt_sdk_inputs(build: Path, sdk: Path) -> dict:
    reject_links(sdk)
    dependencies = introspection_list(build / 'meson-info/intro-dependencies.json')
    qt = [item for item in dependencies if item.get('name') == 'qt6']
    versions = {item.get('version') for item in qt}
    files = {}
    for item in qt:
        for argument in item.get('link_args', []):
            if not argument.lower().endswith('.lib'):
                continue
            path = Path(argument)
            reject_links(path)
            if not path.is_absolute() or not path.resolve().is_relative_to(sdk.resolve()):
                raise ValueError('Meson Qt import library is outside the selected SDK: ' + argument)
            files[path.resolve().relative_to(sdk.resolve()).as_posix()] = sha256(path)
    if len(versions) != 1 or not {'lib/Qt6Core.lib', 'lib/Qt6Gui.lib'}.issubset(files):
        raise ValueError('Meson did not record the expected Qt SDK import libraries/version.')
    return {'prefix': str(sdk.resolve()), 'version': next(iter(versions)), 'importLibraries': files}


def build_release(source: Path, build: Path, evidence: Path, *, build_type: str, playback: str,
                  repository: str = '', channel: str = 'dev', jobs: int = 0,
                  qt_prefix: Path | None = None, duplex: str | None = None) -> dict:
    reject_links(source)
    source = source.resolve()
    hashes = capture_sources(source)
    protected = [source / name for name in hashes] + [source / name for name in SOURCE_DIRECTORIES]
    if qt_prefix is not None:
        reject_links(qt_prefix)
        protected.append(qt_prefix)
    build = new_output(build, protected + [evidence])
    evidence = new_output(evidence, protected + [build])
    duplex = playback if duplex is None else duplex
    if build_type not in {'release', 'debugoptimized'} or playback not in {'enabled', 'disabled'} or duplex not in {'enabled', 'disabled', 'auto'} or jobs < 0:
        raise ValueError('Release build requires an explicit build type, playback mode and nonnegative job count.')
    auxiliary = {}
    for directory in ['.github/workflows']:
        auxiliary.update({directory + '/' + name: sha256(path) for name, path in walk_files(source / directory).items()})
    # These submodules are not linked, but their notices accompany packages.
    from package_portable import COMPILER_LICENSE_SOURCES
    for _, name in COMPILER_LICENSE_SOURCES:
        path = source / name
        if path.is_file():
            reject_links(path)
            auxiliary[name] = sha256(path)
    evidence.mkdir(parents=True)
    marker = evidence / 'INCOMPLETE.txt'
    marker.write_text('No successful source/build association exists until build-evidence.json is present.\n', encoding='utf-8')
    write_json(evidence / 'source-before-build.json', {'sourceHashes': hashes, 'auxiliaryHashes': auxiliary})
    env = os.environ.copy()
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    env['PYTHONUTF8'] = '1'
    command = ['meson', 'setup', str(build), str(source), '--backend=ninja', '--buildtype=' + build_type,
               '-Dwerror=true', '-Daudio_playback=' + playback, '-Daudio_duplex=' + duplex, '-Dupdate_channel=' + channel]
    if repository:
        command += ['-Dgithub_repo=' + repository]
    if sys.platform == 'win32':
        command += ['--vsenv', '-Db_vscrt=md']
    checks = [run_logged('configure', command, evidence, source, env)]
    sdk = qt_sdk_inputs(build, qt_prefix) if qt_prefix is not None else None
    command = ['meson', 'compile', '-C', str(build)] + (['-j', str(jobs)] if jobs else [])
    checks.append(run_logged('compile', command, evidence, source, env))
    if capture_sources(source) != hashes:
        raise ValueError('Application source inputs changed during the build.')
    if auxiliary:
        verify_files(source, auxiliary)
    if sdk is not None and qt_sdk_inputs(build, qt_prefix) != sdk:
        raise ValueError('Qt SDK import libraries changed during the build.')
    binary = build / 'src' / ('vibestudio.exe' if sys.platform == 'win32' else 'vibestudio')
    reject_links(binary)
    catalogs = {name: sha256(path) for name, path in walk_files(build / 'i18n').items() if name.endswith('.qm')}
    required = {Path(name).with_suffix('.qm').name for name in hashes if name.startswith('i18n/') and name.endswith('.ts')}
    if not required or set(catalogs) != required:
        raise ValueError('Build must produce exactly the complete application catalog set.')
    options = {item['name']: item['value'] for item in introspection_list(build / 'meson-info/intro-buildoptions.json')}
    if options.get('buildtype') != build_type or options.get('audio_playback') != playback or options.get('audio_duplex') != duplex or options.get('werror') is not True:
        raise ValueError('Meson build options differ from the recorded release configuration.')
    result = {'schemaVersion': 1, 'status': 'source-verified-build',
              'verifiedAtUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'sourceRoot': str(source), 'buildDirectory': str(build), 'binary': str(binary),
              'sourceHashes': hashes, 'auxiliaryHashes': auxiliary, 'binarySha256': sha256(binary),
              'compiledCatalogs': file_hashes(catalogs), 'buildType': build_type, 'audioPlayback': playback, 'audioDuplex': duplex,
              'compilers': read_json(build / 'meson-info/intro-compilers.json'),
              'machines': read_json(build / 'meson-info/intro-machines.json'), 'qtSdk': sdk, 'checks': checks,
              'scope': 'Fresh configure/compile with unchanged application inputs. Test execution, deployment and native acceptance are separate checks.'}
    build_metadata(result)
    write_json(evidence / 'build-evidence.json', result)
    marker.unlink()
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--build-dir', type=Path, required=True, help='Fresh Meson build directory; never replaced.')
    parser.add_argument('--evidence', type=Path, required=True, help='Fresh directory for source inventory, logs and build-evidence.json.')
    parser.add_argument('--buildtype', choices=['release', 'debugoptimized'], default='release')
    parser.add_argument('--audio-playback', choices=['enabled', 'disabled'], required=True)
    parser.add_argument('--audio-duplex', choices=['enabled', 'disabled', 'auto'],
                        help='Native recording backend; defaults to the explicit playback mode.')
    parser.add_argument('--github-repo', default='')
    parser.add_argument('--update-channel', choices=['dev', 'beta', 'stable'], default='dev')
    parser.add_argument('--jobs', type=int, default=0)
    parser.add_argument('--qt-prefix', type=Path, help='Record and verify Windows Qt SDK import-library inputs.')
    args = parser.parse_args()
    try:
        result = build_release(args.source_root, args.build_dir, args.evidence, build_type=args.buildtype,
                               playback=args.audio_playback, repository=args.github_repo, channel=args.update_channel,
                               jobs=args.jobs, qt_prefix=args.qt_prefix, duplex=args.audio_duplex)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f'Release build failed: {error}', file=sys.stderr)
        return 1
    print(json.dumps({'status': result['status'], 'binarySha256': result['binarySha256'], 'sourceFiles': len(result['sourceHashes'])}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
