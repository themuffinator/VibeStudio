#!/usr/bin/env python3
"""Stage and verify a paired Windows binary/source release set for CI or local review."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import wave

from build_release import capture_sources, qt_sdk_inputs, run_logged
from collect_qt_runtime_licenses import collect as collect_licenses
from collect_qt_runtime_sources import collect as collect_sources
from deploy_windows_runtime import deploy
from package_portable import COMPILER_LICENSE_SOURCES, create_package, read_version
from package_source_companion import archive_bundle, assemble, build_metadata, package_inventory
from source_companion_common import (DEFAULT_PROFILE, SOURCE_DIRECTORIES, copy_checked, load_profile, new_output,
                                     read_json, reject_links, sha256, verify_files, walk_files, write_json)


def verify_build(source: Path, build_dir: Path, evidence: Path, sdk: Path, profile: dict) -> dict:
    for path in [source, build_dir, evidence, sdk]:
        reject_links(path)
    build = read_json(evidence)
    build_metadata(build)
    if (build.get('status') != 'source-verified-build' or build.get('audioPlayback') != 'enabled'
            or Path(build.get('buildDirectory', '')).resolve() != build_dir.resolve()):
        raise ValueError('Windows release requires the recorded fresh build with playback enabled.')
    host = build.get('machines', {}).get('host', {})
    if host.get('system') != 'windows' or host.get('cpu_family') != 'x86_64':
        raise ValueError('The current Windows runtime profile requires a Windows x86_64 build.')
    if any(not any(check.get('name') == name and check.get('exitCode') == 0 for check in build['checks'])
           for name in ['configure', 'compile']):
        raise ValueError('Release build evidence must include successful configure and compile commands.')
    if capture_sources(source) != build['sourceHashes']:
        raise ValueError('Application sources differ from the recorded build.')
    binary = build_dir / 'src/vibestudio.exe'
    reject_links(binary)
    if sha256(binary) != build['binarySha256']:
        raise ValueError('Application binary differs from the recorded build.')
    catalogs = {name: sha256(path) for name, path in walk_files(build_dir / 'i18n').items() if name.endswith('.qm')}
    if catalogs != build['compiledCatalogs']:
        raise ValueError('Compiled catalogs differ from the recorded build.')
    qt = qt_sdk_inputs(build_dir, sdk)
    if qt != build.get('qtSdk') or qt['version'] != profile['qtVersion']:
        raise ValueError('Runtime SDK differs from the selected build SDK/source profile.')
    auxiliary = build.get('auxiliaryHashes', {})
    if not {name for _, name in COMPILER_LICENSE_SOURCES}.issubset(auxiliary):
        raise ValueError('Build capture must include all external compiler notices used by the package.')
    verify_files(source, auxiliary)
    return build


def runtime_environment(bindir: Path, temporary: Path) -> dict:
    env = {key: value for key, value in os.environ.items() if not key.upper().startswith(('QT_', 'QML'))}
    system = Path(os.environ['SystemRoot'])
    env['PATH'] = os.pathsep.join(map(str, [bindir, system / 'System32', system]))
    env['QT_QPA_PLATFORM'] = 'offscreen'
    env['TEMP'] = env['TMP'] = env['TMPDIR'] = str(temporary)
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    return env


def runtime_smoke(package: Path, directory: Path) -> list[dict]:
    directory.mkdir()
    fixture = directory / 'independent-stereo.wav'
    # Original exact PCM fixture: balanced channels at quarter/eighth full scale.
    with wave.open(str(fixture), 'wb') as stream:
        stream.setnchannels(2)
        stream.setsampwidth(2)
        stream.setframerate(48000)
        stream.writeframes(struct.pack('<hhhh', 8192, 4096, -8192, -4096) * 2400)
    fixture_hash = sha256(fixture)
    bindir = package / 'bin'
    env = runtime_environment(bindir, directory)
    binary = bindir / 'vibestudio.exe'
    checks = [run_logged('runtime-version', [str(binary), '--settings-file', str(directory / 'settings.ini'),
                                              '--cli', '--version'], directory, package, env, timeout=30)]
    version = read_version(package)
    if (directory / 'runtime-version.txt').read_text(encoding='utf-8').strip() != 'VibeStudio ' + version:
        raise ValueError('Packaged application reports an unexpected version.')
    command = [str(binary), '--settings-file', str(directory / 'settings.ini'), '--cli', '--json',
               'asset', 'audio-analyze', str(fixture), '--no-loudness']
    # JSON stdout is separate from backend diagnostics so both remain inspectable.
    stdout, stderr = directory / 'runtime-analysis.json', directory / 'runtime-analysis-stderr.txt'
    with stdout.open('x', encoding='utf-8') as out, stderr.open('x', encoding='utf-8') as err:
        result = subprocess.run(command, cwd=package, env=env, stdout=out, stderr=err, timeout=60)
    if result.returncode:
        raise ValueError('Packaged Audio analysis failed; inspect ' + str(stderr))
    analysis = read_json(stdout)['audioAnalysis']
    if (analysis['channelCount'] != 2 or analysis['sampleRate'] != 48000 or analysis['frames'] != 4800
            or len(analysis['channels']) != 2):
        raise ValueError('Packaged Audio analysis changed the independent PCM fixture layout.')
    for channel, level in zip(analysis['channels'], [0.25, 0.125]):
        if any(abs(channel[key] - value) > 1e-12 for key, value in [('peak', level), ('rms', level), ('dc', 0)]):
            raise ValueError('Packaged Audio statistics differ from the independent PCM fixture.')
    if sha256(fixture) != fixture_hash:
        raise ValueError('Runtime validation changed the source fixture.')
    checks.append({'name': 'runtime-audio-analysis', 'command': command, 'exitCode': result.returncode,
                   'log': stdout.name, 'logSha256': sha256(stdout), 'stderr': stderr.name,
                   'fixtureSha256': fixture_hash, 'scope': 'Offscreen CLI and independent PCM statistics; no listening or device acceptance.'})
    return checks


def package_release(source: Path, build_dir: Path, evidence: Path, sdk: Path, output: Path, *,
                    profile_path: Path = DEFAULT_PROFILE, license_texts: Path | None = None,
                    runtime_sources: Path | None = None) -> dict:
    if sys.platform != 'win32':
        raise ValueError('Windows runtime packaging requires native Windows.')
    for path in [source, build_dir, evidence, sdk, profile_path]:
        reject_links(path)
    profile = load_profile(profile_path)
    profile_hash, evidence_hash = sha256(profile_path), sha256(evidence)
    source, build_dir, sdk = source.resolve(), build_dir.resolve(), sdk.resolve()
    build = verify_build(source, build_dir, evidence, sdk, profile)
    protected = [build_dir, evidence, sdk, profile_path, *[source / name for name in build['sourceHashes']],
                 *[source / name for name in SOURCE_DIRECTORIES], *[source / name for name in build['auxiliaryHashes']]]
    protected += [path for path in [license_texts, runtime_sources] if path is not None]
    output = new_output(output, protected)
    output.mkdir(parents=True)
    marker = output / 'INCOMPLETE.txt'
    marker.write_text('Only a completed publish/release-set.json describes a verified binary/source pair.\n', encoding='utf-8')
    if license_texts is None:
        license_texts = output / 'qt-licenses'
        print('Collecting pinned Qt licence documents.', file=sys.stderr, flush=True)
        collect_licenses(profile['qtVersion'], license_texts)
    if runtime_sources is None:
        runtime_sources = output / 'runtime-sources'
        print('Collecting pinned runtime implementation/build sources.', file=sys.stderr, flush=True)
        collect_sources(profile_path, runtime_sources)
    print('Staging the application and verified Qt runtime.', file=sys.stderr, flush=True)
    package, _ = create_package(build_dir / 'src/vibestudio.exe', output / 'binary', read_version(source),
                                include_samples=True, archive=False, target_platform='windows', target_architecture='x86_64',
                                compiled_translations=build_dir / 'i18n', source_root=source)
    deploy(package, sdk, license_texts, archive=False, offscreen=True)
    checks = runtime_smoke(package, output / 'runtime-checks')
    companion = assemble(source, evidence, package, runtime_sources, output / 'source' / (package.name + '-source'),
                         profile_path, archive=True, progress=lambda message: print(message, file=sys.stderr, flush=True))
    inventory = package_inventory(package)
    inventory['CHECKSUMS.sha256'] = sha256(package / 'CHECKSUMS.sha256')
    binary_zip = package.with_name(package.name + '.zip')
    binary_hash = archive_bundle(package, binary_zip, inventory)
    verify_build(source, build_dir, evidence, sdk, profile)
    if sha256(evidence) != evidence_hash or sha256(profile_path) != profile_hash:
        raise ValueError('Build evidence or runtime profile changed during release packaging.')
    source_manifest = read_json(Path(companion['output']) / 'source-companion.json')
    if source_manifest['binaryPackageManifestSha256'] != inventory['package-manifest.json']:
        raise ValueError('Source companion no longer matches the finalized binary package.')
    publish = output / 'publish'
    publish.mkdir()
    archives = {}
    for kind, path, digest in [('binary', binary_zip, binary_hash),
                               ('source', Path(companion['archive']), companion['archiveSha256'])]:
        destination = publish / path.name
        copy_checked(path, destination, digest)
        if sha256(destination) != digest:
            raise ValueError('Published archive copy differs: ' + destination.name)
        (publish / (path.name + '.sha256')).write_text(digest + '  ' + path.name + '\n', encoding='utf-8')
        archives[kind] = {'file': path.name, 'sha256': digest, 'bytes': path.stat().st_size}
    receipt = {'schemaVersion': 1, 'status': 'verified-binary-source-pair', 'version': read_version(source),
               'binarySha256': build['binarySha256'], 'buildEvidenceSha256': evidence_hash,
               'sourceManifestSha256': companion['manifestSha256'], 'runtimeProfileSha256': profile_hash,
               'archives': archives, 'checks': checks,
               'remainingAcceptance': ['Hosted CI execution and release-channel publication must be verified separately.',
                                       'Vendor Qt/FFmpeg rebuild, native accessibility/devices and clean-machine acceptance remain separate.']}
    copy_checked(evidence, publish / 'build-evidence.json', evidence_hash)
    write_json(publish / 'release-set.json', receipt)
    marker.unlink()
    return receipt


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--build-evidence', type=Path, required=True)
    parser.add_argument('--qt-prefix', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='Fresh output; publish/ is ready only when release-set.json exists.')
    parser.add_argument('--profile', type=Path, default=DEFAULT_PROFILE)
    parser.add_argument('--license-texts', type=Path, help='Optional previously collected, verified licence texts.')
    parser.add_argument('--runtime-sources', type=Path, help='Optional previously collected, verified source archives.')
    args = parser.parse_args()
    try:
        result = package_release(args.source_root, args.build_dir, args.build_evidence, args.qt_prefix, args.output,
                                 profile_path=args.profile, license_texts=args.license_texts, runtime_sources=args.runtime_sources)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f'Windows release packaging failed: {error}', file=sys.stderr)
        return 1
    print(json.dumps(result))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
