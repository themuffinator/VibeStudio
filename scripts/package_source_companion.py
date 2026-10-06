#!/usr/bin/env python3
"""Assemble exact application sources and pinned runtime sources beside a binary release.

The supplied build evidence must bind sourceHashes to binarySha256. This tool
checks that binding and all input bytes; it does not infer what an unrecorded
compiler invocation built. Publication and native acceptance remain separate.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import zipfile

from source_companion_common import (DEFAULT_PROFILE, SOURCE_DIRECTORIES, SOURCE_ROOT_FILES, copy_checked,
                                     file_hashes, load_profile, new_output, read_json, reject_links,
                                     relative_name, sha256, verify_files, walk_files, write_json)


def package_inventory(package: Path) -> dict[str, str]:
    path = package / 'CHECKSUMS.sha256'
    reject_links(path)
    if not path.is_file() or path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError('Binary package requires a bounded CHECKSUMS.sha256 inventory.')
    expected = {}
    for line in path.read_text(encoding='utf-8').splitlines():
        if len(line) < 67 or line[64:66] != '  ' or line[66:] in expected:
            raise ValueError('Malformed or duplicate package checksum entry.')
        expected[line[66:]] = line[:64]
    file_hashes(expected)
    if set(walk_files(package)) != set(expected) | {'CHECKSUMS.sha256'}:
        raise ValueError('Binary package file set differs from its checksum inventory.')
    verify_files(package, expected)
    return expected


def source_inventory(source: Path, build: dict) -> dict[str, str]:
    expected = file_hashes(build.get('sourceHashes'))
    required = {'meson.build', 'meson_options.txt', 'VERSION', 'LICENSE', 'README.md', 'src/meson.build',
                'external/audio/meson.build', 'external/modelling/meson.build'}
    if not required.issubset(expected):
        raise ValueError('Build source inventory omits required application/build/licence inputs.')
    for name in expected:
        if name not in SOURCE_ROOT_FILES and not any(name.startswith(prefix + '/') for prefix in SOURCE_DIRECTORIES):
            raise ValueError(f'Unreviewed source inventory location: {name}')
    # Directory closure catches files introduced after capture, including a new
    # parser/header that an incomplete manifest could otherwise omit silently.
    for prefix in SOURCE_DIRECTORIES:
        actual = {prefix + '/' + name for name in walk_files(source / prefix)}
        declared = {name for name in expected if name.startswith(prefix + '/')}
        if actual != declared:
            raise ValueError(f'Source directory differs from its captured file set: {prefix}')
    verify_files(source, expected)
    return expected


def runtime_coverage(package: Path, package_manifest: dict, profile: dict, package_files: dict) -> None:
    if (package_manifest.get('targetPlatform') != profile['targetPlatform']
            or package_manifest.get('targetArchitecture') != profile['targetArchitecture']):
        raise ValueError('Runtime source profile does not match the binary package target.')
    runtime = package_manifest.get('qtDeployment', {})
    if runtime.get('status') != 'staged-with-runtime-notices':
        raise ValueError('Binary package requires a verified runtime/notice deployment.')
    ffmpeg = runtime.get('ffmpeg', {})
    if ffmpeg.get('version') != profile['ffmpegVersion'] or ffmpeg.get('configuration') != profile['ffmpegConfiguration']:
        raise ValueError('FFmpeg version or build configuration differs from the source profile.')
    modules = set()
    for item in runtime.get('files', []):
        name = relative_name(item['path'])
        if package_files.get(name) != item['sha256']:
            raise ValueError('Runtime file differs from the verified binary package inventory.')
        if item.get('spdxDocument'):
            modules.add(item['spdxDocument'])
        elif not re.fullmatch(r'bin/(avcodec|avformat|avutil|swresample|swscale)-\d+\.dll', name):
            raise ValueError(f'Runtime dependency has no source-module association: {name}')
    for item in runtime.get('qtCatalogs', []):
        if package_files.get(relative_name(item['path'])) != item['sha256']:
            raise ValueError('Qt catalog differs from the verified binary package inventory.')
        modules.update(origin['spdxDocument'] for origin in item['origin'])
    if modules != set(profile['qtModules']):
        raise ValueError('Runtime module coverage differs from the reviewed source profile.')
    inventory_name = relative_name(runtime['noticeInventory'])
    if inventory_name not in package_files:
        raise ValueError('Runtime notice inventory is absent from package checksums.')
    inventory = read_json(package / inventory_name)
    for module in modules:
        components = [item for name, item in inventory['components'].items()
                      if name.startswith(module + ':') and '-qt-' in name and '-3rdparty-' not in name]
        if not components or any(item.get('versionInfo') != profile['qtVersion'] for item in components):
            raise ValueError(f'Qt module version differs from the source profile: {module}')
        for item in components:
            location = item.get('downloadLocation', '')
            if '.git@' in location and location.rsplit('.git@', 1)[1] != profile['qtModules'][module]:
                raise ValueError(f'Qt module commit differs from the source profile: {module}')
        for suffix in ['opt', 'summary']:
            if f'licenses/qt-runtime/build/config_{module}.{suffix}' not in package_files:
                raise ValueError(f'Missing recorded Qt build configuration: {module}')


def build_metadata(build: dict) -> tuple[dict, list[str]]:
    if build.get('buildType') not in {'plain', 'debug', 'debugoptimized', 'release', 'minsize', 'custom'}:
        raise ValueError('Build evidence requires its Meson build type.')
    if build.get('audioPlayback') not in {'enabled', 'disabled', 'auto'}:
        raise ValueError('Build evidence requires the configured audio_playback value.')
    compilers = build.get('compilers')
    if not isinstance(compilers, dict) or not isinstance(compilers.get('host'), dict) or 'cpp' not in compilers['host']:
        raise ValueError('Build evidence requires the host C++ compiler identity.')
    checked = {}
    for machine, languages in compilers.items():
        if not isinstance(languages, dict):
            raise ValueError('Malformed compiler metadata.')
        checked[machine] = {}
        for language, values in languages.items():
            if not isinstance(values, dict) or not all(isinstance(values.get(key), str) for key in ['id', 'version']):
                raise ValueError('Compiler metadata requires an ID and version.')
            checked[machine][language] = {key: values.get(key) for key in ['id', 'version', 'linker_id', 'linker_version']}
    checks = build.get('checks')
    if not isinstance(checks, list) or not all(isinstance(item, dict) for item in checks):
        raise ValueError('Build evidence requires recorded configure checks.')
    configurations = [item.get('command') for item in checks if item.get('name') == 'configure']
    if len(configurations) != 1 or not isinstance(configurations[0], list) or not all(isinstance(arg, str) for arg in configurations[0]):
        raise ValueError('Build evidence requires one recorded Meson configuration command.')
    options = sorted({arg for arg in configurations[0] if arg.startswith('-D') or arg.startswith('--buildtype=') or arg == '--vsenv'})
    return checked, options


def archive_bundle(output: Path, archive: Path, expected: dict[str, str]) -> str:
    reject_links(archive)
    with zipfile.ZipFile(archive, 'x') as zipped:
        for name in sorted(expected):
            info = zipfile.ZipInfo(output.name + '/' + name, date_time=(1980, 1, 1, 0, 0, 0))
            info.external_attr = 0o100644 << 16
            info.compress_type = zipfile.ZIP_STORED if name.endswith(('.tar.xz', '.tar.gz')) else zipfile.ZIP_DEFLATED
            with (output / name).open('rb') as reader, zipped.open(info, 'w', force_zip64=True) as writer:
                for chunk in iter(lambda: reader.read(1024 * 1024), b''):
                    writer.write(chunk)
    with zipfile.ZipFile(archive) as zipped:
        if set(zipped.namelist()) != {output.name + '/' + name for name in expected}:
            raise ValueError('Source ZIP member set differs from the companion inventory.')
        for name, value in expected.items():
            result = hashlib.sha256()
            with zipped.open(output.name + '/' + name) as stream:
                for data in iter(lambda: stream.read(1024 * 1024), b''):
                    result.update(data)
            if result.hexdigest() != value:
                raise ValueError(f'Source ZIP bytes differ: {name}')
    return sha256(archive)


def assemble(source: Path, build_evidence: Path, package: Path, runtime_sources: Path, output: Path,
             profile_path: Path = DEFAULT_PROFILE, archive: bool = False, progress=None) -> dict:
    def report(message):
        if progress:
            progress(message)

    for path in [source, build_evidence, package, runtime_sources, profile_path]:
        reject_links(path)
    build_digest, profile_digest = sha256(build_evidence), sha256(profile_path)
    build, profile = read_json(build_evidence), load_profile(profile_path)
    compilers, options = build_metadata(build)
    report('Checking captured application sources and binary-package checksums.')
    sources = source_inventory(source, build)
    package_files = package_inventory(package)
    package_manifest = read_json(package / 'package-manifest.json')
    binary = package_manifest['binary']
    if (package_files.get(relative_name(binary['stagedPath'])) != binary['sha256']
            or binary['sha256'] != build.get('binarySha256')):
        raise ValueError('Application binary differs from the captured build evidence.')
    if (source / 'VERSION').read_text(encoding='utf-8').strip() != package_manifest['version']:
        raise ValueError('Source and binary package versions differ.')
    if package_manifest.get('externalCompilersBundled') is not False:
        raise ValueError('Bundled external compiler sources require a separate reviewed source profile.')
    catalogs = file_hashes(build.get('compiledCatalogs'))
    if any('/' in name or package_files.get('i18n/' + name) != value for name, value in catalogs.items()):
        raise ValueError('Compiled application catalogs differ from the captured build evidence.')
    if set(package_manifest.get('compiledLocalization', {}).get('catalogs', [])) != {'i18n/' + name for name in catalogs}:
        raise ValueError('Application catalog coverage differs from the captured build.')
    runtime_coverage(package, package_manifest, profile, package_files)
    acquisition_path = runtime_sources / 'source-acquisition.json'
    reject_links(acquisition_path)
    acquisition_digest = sha256(acquisition_path)
    acquired = read_json(acquisition_path)
    if acquired.get('status') != 'verified-archives' or acquired.get('profileSha256') != profile_digest:
        raise ValueError('Source acquisition does not match the selected runtime profile.')
    if sha256(runtime_sources / 'runtime-source-profile.json') != profile_digest:
        raise ValueError('Collected source profile differs from the selected profile.')
    archive_hashes = {'archives/' + item['file']: item['sha256'] for item in profile['archives']}
    if (not isinstance(acquired.get('files'), list) or len(acquired['files']) != len(archive_hashes)
            or {item['file']: item['sha256'] for item in acquired['files']} != archive_hashes):
        raise ValueError('Collected source archive coverage differs from the profile.')
    verify_files(runtime_sources, archive_hashes)
    for item in profile['archives']:
        if (runtime_sources / 'archives' / item['file']).stat().st_size != item['bytes']:
            raise ValueError('Collected source archive size differs from the profile.')

    # Output may be inside a repository's .agents/tmp area, but never inside a
    # captured source directory or any runtime/package input tree.
    protected = [build_evidence, profile_path, package, runtime_sources]
    protected += [source / name for name in sources]
    protected += [source / prefix for prefix in SOURCE_DIRECTORIES]
    output = new_output(output, protected)
    archive_path = output.with_name(output.name + '.zip')
    checksum_path = archive_path.with_name(archive_path.name + '.sha256')
    if archive:
        for path in [archive_path, checksum_path]:
            new_output(path, protected)
    project = Path(__file__).resolve().parents[1]
    plan = {'vibestudio/' + name: (source / name, value) for name, value in sources.items()}
    plan.update({'runtime/' + name: (runtime_sources / name, value) for name, value in archive_hashes.items()})
    plan.update({'binary-notices/' + name: (package / name, value) for name, value in package_files.items() if name.startswith('licenses/')})
    for name in ['scripts/source_companion_common.py', 'scripts/collect_qt_runtime_sources.py', 'scripts/package_source_companion.py',
                 'src/tests/source_companion_test.py', 'docs/RUNTIME_SOURCE_BUILD.md']:
        reject_links(project / name)
        plan[name] = (project / name, sha256(project / name))
    plan['README.md'] = plan['docs/RUNTIME_SOURCE_BUILD.md']
    profile_name = 'scripts/runtime_sources/' + profile['id'] + '.json'
    plan[profile_name] = (profile_path, profile_digest)
    plan['runtime/source-acquisition.json'] = (acquisition_path, acquisition_digest)
    file_hashes({name: value for name, (_, value) in plan.items()})
    output.mkdir(parents=True)
    marker = output / 'INCOMPLETE.txt'
    with marker.open('x', encoding='utf-8') as stream:
        stream.write('Assembly is incomplete until source-companion.json and CHECKSUMS.sha256 are present.\n')
    for index, (name, (origin, value)) in enumerate(sorted(plan.items()), 1):
        copy_checked(origin, output / name, value)
        if index % 100 == 0 or index == len(plan):
            report(f'Copied and checked {index}/{len(plan)} source and notice files.')
    report('Rechecking input stability and creating the companion manifest.')
    verify_files(source, sources)
    if package_inventory(package) != package_files:
        raise ValueError('Binary package inventory changed during assembly.')
    verify_files(runtime_sources, archive_hashes)
    if sha256(build_evidence) != build_digest or sha256(profile_path) != profile_digest:
        raise ValueError('Build evidence or source profile changed during assembly.')
    if any(sha256(origin) != value for origin, value in plan.values()):
        raise ValueError('Companion input changed during assembly.')
    # Recheck directory closure, not just files known before the copy.
    source_inventory(source, build)
    receipt = {'schemaVersion': 1, 'status': 'assembled-and-hash-verified', 'version': package_manifest['version'],
               'binaryPackageName': package_manifest['packageName'], 'binarySha256': binary['sha256'],
               'binaryPackageManifestSha256': package_files['package-manifest.json'], 'buildEvidenceSha256': build_digest,
               'targetPlatform': package_manifest['targetPlatform'], 'targetArchitecture': package_manifest['targetArchitecture'],
               'sourceHashes': sources, 'sourceRoot': 'vibestudio', 'runtimeProfile': profile['id'],
               'runtimeProfileSha256': profile_digest, 'runtimeArchives': archive_hashes,
               'buildType': build.get('buildType'), 'audioPlayback': build.get('audioPlayback'),
               'buildOptions': options, 'compilers': compilers, 'compiledCatalogs': catalogs,
               'ffmpegConfiguration': profile['ffmpegConfiguration'], 'zlibRecipeAdjustments': profile['zlibRecipeAdjustments'],
               'payloadHashes': {name: value for name, (_, value) in sorted(plan.items())},
               'scope': 'Application inputs match the recorded build and packaged executable. Pinned runtime implementation/build-tool archives and original notices are included. This does not claim vendor bit-for-bit rebuilds, native acceptance, a completed publication channel or legal certification.'}
    write_json(output / 'source-companion.json', receipt)
    marker.unlink()
    files = {name: sha256(path) for name, path in walk_files(output).items()}
    with (output / 'CHECKSUMS.sha256').open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(''.join(value + '  ' + name + '\n' for name, value in sorted(files.items())))
    files['CHECKSUMS.sha256'] = sha256(output / 'CHECKSUMS.sha256')
    result = {'output': str(output), 'sourceFiles': len(sources), 'runtimeArchives': len(archive_hashes),
              'files': len(files), 'binarySha256': binary['sha256'], 'manifestSha256': sha256(output / 'source-companion.json')}
    if archive:
        report('Writing and independently checking every ZIP member.')
        archive_digest = archive_bundle(output, archive_path, files)
        with checksum_path.open('x', encoding='utf-8', newline='\n') as stream:
            stream.write(archive_digest + '  ' + archive_path.name + '\n')
        result.update({'archive': str(archive_path), 'archiveSha256': archive_digest, 'archiveBytes': archive_path.stat().st_size})
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True, help='Immutable application source capture used by the recorded build.')
    parser.add_argument('--build-evidence', type=Path, required=True, help='Build JSON binding sourceHashes, compiledCatalogs and binarySha256; see PACKAGING.md.')
    parser.add_argument('--binary-package', type=Path, required=True, help='Verified portable binary package, including Qt deployment and checksums.')
    parser.add_argument('--runtime-sources', type=Path, required=True, help='Directory completed by collect_qt_runtime_sources.py.')
    parser.add_argument('--profile', type=Path, default=DEFAULT_PROFILE)
    parser.add_argument('--output', type=Path, required=True, help='New project-local source companion directory; existing outputs are preserved.')
    parser.add_argument('--archive', action='store_true', help='Also write a verified ZIP and its SHA-256 sidecar.')
    args = parser.parse_args()
    try:
        print(json.dumps(assemble(args.source_root, args.build_evidence, args.binary_package, args.runtime_sources,
                                  args.output, args.profile, args.archive, lambda message: print(message, file=sys.stderr, flush=True))))
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
        print(f'Source companion assembly failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
