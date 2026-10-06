#!/usr/bin/env python3
"""Stage a reviewed Qt Widgets/Audio runtime into a fresh portable package.

Uses windeployqt only for discovery. Every copied DLL must come from the selected
SDK; Windows ICU is a recorded OS prerequisite. No compiler/system/Vulkan DLLs,
software OpenGL or shader compilers are copied. Current VibeStudio uses raster
Qt Widgets; future GPU surfaces must extend this deployment profile explicitly.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys

from package_portable import staged_license_files, write_checksums
from qt_runtime_inventory import QtInventory, relative_name, sha256


def reject_links(path: Path) -> None:
    for candidate in [path, *path.parents]:
        try:
            info = candidate.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0):
            raise ValueError(f'Runtime output contains a link/reparse point: {candidate}')


def plan_files(report: dict, sdk: Path, package: Path, system: Path) -> tuple[list[dict], list[dict]]:
    """Validate discovery before copying anything; no filename-only trust."""
    files, excluded = [], []
    seen = set()
    for item in report['files']:
        source = Path(item['source']).resolve()
        proposed = (Path(item['target']) / source.name).absolute()
        reject_links(proposed)
        destination = proposed.resolve()
        if not destination.is_relative_to(package / 'bin'):
            raise ValueError(f'Qt deployment target escapes package bin: {destination}')
        relative = destination.relative_to(package).as_posix()
        if relative.casefold() in seen:
            raise ValueError(f'Duplicate Qt deployment target: {relative}')
        seen.add(relative.casefold())
        if source.parent == system / 'System32' and source.name.lower() in {'icu.dll', 'icuuc.dll', 'icuin.dll'}:
            excluded.append({'name': source.name, 'source': str(source), 'reason': 'Windows ICU system component; use the target OS copy, never redistribute the build machine copy.'})
            continue
        if not source.is_file() or not source.is_relative_to(sdk):
            raise ValueError(f'Runtime dependency is outside the selected Qt SDK: {source}')
        if source.suffix.lower() != '.dll':
            raise ValueError(f'Unexpected discovered runtime type: {source}')
        if source.name.lower() in {'dxcompiler.dll', 'dxil.dll', 'd3dcompiler_47.dll', 'opengl32sw.dll'}:
            raise ValueError(f'Unexpected GPU dependency in the raster Widgets deployment profile: {source}')
        files.append({'source': source, 'target': relative, 'sha256': sha256(source)})
    if not files:
        raise ValueError('Qt deployment returned no DLLs.')
    return files, excluded


def ffmpeg_metadata(sdk: Path, sources: list[Path], inventory: QtInventory) -> dict | None:
    candidates = [path for path in sources if re.fullmatch(r'avutil-\d+\.dll', path.name, re.IGNORECASE)]
    if not candidates:
        if any(re.match(r'(avcodec|avformat|swresample|swscale)-', path.name, re.IGNORECASE) for path in sources):
            raise ValueError('Incomplete FFmpeg runtime: avutil is missing.')
        return None
    if len(candidates) != 1:
        raise ValueError('Ambiguous FFmpeg runtime.')
    packages = inventory.ffmpeg_packages()
    ffmpeg = [inventory.packages[key] for key in packages if inventory.packages[key]['name'] == 'FFmpeg']
    if len(ffmpeg) != 1:
        raise ValueError('Qt SPDX metadata does not identify one FFmpeg version.')
    # Only immutable SDK binaries selected by windeployqt are loaded. These
    # functions expose static version/licence/configuration strings, not devices.
    with os.add_dll_directory(str(sdk / 'bin')):
        library = ctypes.CDLL(str(candidates[0]))
        values = {}
        for label, symbol in [('version', 'av_version_info'), ('licence', 'avutil_license'), ('configuration', 'avutil_configuration')]:
            function = getattr(library, symbol)
            function.argtypes = []
            function.restype = ctypes.c_char_p
            values[label] = function().decode('utf-8')
        values['libraries'] = []
        for source in sources:
            match = re.fullmatch(r'(avcodec|avformat|avutil|swresample|swscale)-(\d+)\.dll', source.name, re.IGNORECASE)
            if not match:
                continue
            prefix = match[1].lower()
            component = ctypes.CDLL(str(source))
            version_function = getattr(component, prefix + '_version')
            version_function.argtypes = []
            version_function.restype = ctypes.c_uint
            version = version_function()
            if version >> 16 != int(match[2]):
                raise ValueError(f'FFmpeg DLL filename does not match its ABI: {source}')
            description = {'file': source.name, 'version': f'{version >> 16}.{(version >> 8) & 255}.{version & 255}'}
            for label, suffix in [('licence', '_license'), ('configuration', '_configuration')]:
                function = getattr(component, prefix + suffix)
                function.argtypes = []
                function.restype = ctypes.c_char_p
                description[label] = function().decode('utf-8')
                if description[label] != values[label]:
                    raise ValueError(f'FFmpeg libraries have inconsistent {label}: {source}')
            values['libraries'].append(description)
    if values['version'] != ffmpeg[0]['versionInfo']:
        raise ValueError(f'FFmpeg binary version differs from Qt SPDX metadata: {values["version"]}')
    if '--enable-gpl' in values['configuration'] or '--enable-nonfree' in values['configuration']:
        raise ValueError('FFmpeg configuration differs from Qt\'s documented LGPL runtime profile.')
    values['packages'] = packages
    values['provenance'] = 'Qt SDK DLLs and FFmpeg self-reported version/configuration; Qt SPDX records FFmpeg as a system dependency without individual binary hashes.'
    return values


def deploy(package: Path, sdk: Path, license_texts: Path, archive: bool, offscreen: bool) -> dict:
    if sys.platform != 'win32':
        raise ValueError('Native Windows is required for Qt deployment and FFmpeg version inspection.')
    reject_links(package)
    package, sdk, license_texts = package.resolve(), sdk.resolve(), license_texts.resolve()
    manifest_path = package / 'package-manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if manifest.get('schemaVersion') != 2 or manifest.get('targetPlatform') != 'windows':
        raise ValueError('Expected a fresh Windows portable package (manifest schema 2).')
    binary = package / relative_name(manifest['binary']['stagedPath'])
    reject_links(binary)
    if binary.parent != package / 'bin' or sha256(binary) != manifest['binary']['sha256']:
        raise ValueError('The staged application does not match its package manifest.')
    if set((package / 'bin').iterdir()) != {binary}:
        raise ValueError('Use a fresh package whose bin directory contains only its application binary.')
    if manifest.get('archivePath') or (package.parent / (package.name + '.zip')).exists():
        raise ValueError('Deploy before creating an archive; use this command\'s --archive for the final ZIP.')
    notice_root = package / 'licenses/qt-runtime'
    if notice_root.exists():
        raise ValueError('The runtime notice destination already exists.')
    for parent, directories, files in os.walk(package, followlinks=False):
        for name in directories + files:
            reject_links(Path(parent) / name)
    tool = sdk / 'bin/windeployqt.exe'
    qtpaths = sdk / 'bin/qtpaths.exe'
    lconvert = sdk / 'bin/lconvert.exe'
    if not all(path.is_file() for path in [tool, qtpaths, lconvert]):
        raise ValueError('The Qt SDK must include windeployqt, qtpaths and lconvert.')
    system = Path(os.environ['SystemRoot']).resolve()
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(('QT_', 'QML')) and key not in {'VULKAN_SDK', 'VULKAN_ROOT'}}
    env['PATH'] = os.pathsep.join(map(str, [sdk / 'bin', system / 'System32', system]))
    command = [str(tool), '--release', '--dry-run', '--json', '--no-patchqt', '--no-translations',
               '--no-compiler-runtime', '--no-system-d3d-compiler', '--no-system-dxc-compiler', '--no-opengl-sw',
               '--qtpaths', str(qtpaths), '--dir', str(package / 'bin')]
    if offscreen:
        command += ['--include-plugins', 'qoffscreen']
    command.append(str(binary))
    result = subprocess.run(command, cwd=package, env=env, capture_output=True, text=True, encoding='utf-8', timeout=120)
    if result.returncode:
        raise ValueError('Qt deployment discovery failed: ' + result.stderr)
    discovered = json.loads(result.stdout)
    files, excluded = plan_files(discovered, sdk, package, system)
    inventory = QtInventory(sdk)
    ffmpeg = ffmpeg_metadata(sdk, [item['source'] for item in files], inventory)
    roots, descriptions = [], []
    for item in files:
        source = item['source']
        if re.fullmatch(r'(avcodec|avformat|avutil|swresample|swscale)-\d+\.dll', source.name, re.IGNORECASE):
            if not ffmpeg:
                raise ValueError('FFmpeg dependency lacks matching version evidence.')
            description = {'sdkPath': source.relative_to(sdk).as_posix(), 'sha256': item['sha256'],
                           'vendorChecksumMatch': 'not-provided-by-Qt', 'packages': ffmpeg['packages']}
        else:
            description = inventory.describe_file(source)
        if description['sha256'] != item['sha256']:
            raise ValueError(f'SDK file changed during inspection: {source}')
        roots.extend(description['packages'])
        descriptions.append({'path': item['target'], **description})

    catalogs = []
    for base in sorted((sdk / 'translations').glob('qtbase_*.qm')):
        locale = base.stem.removeprefix('qtbase_')
        if not re.fullmatch('[A-Za-z0-9_]+', locale):
            raise ValueError(f'Unexpected Qt locale name: {base}')
        sources = [base]
        multimedia = sdk / 'translations' / ('qtmultimedia_' + locale + '.qm')
        if ffmpeg and multimedia.is_file():
            sources.append(multimedia)
        origin = [inventory.describe_file(source) for source in sources]
        for item in origin:
            roots.extend(item['packages'])
        # installRuntimeTranslations loads the qtbase prefix on every platform.
        # Merge Multimedia contexts into that catalog so both sets are installed.
        catalogs.append({'path': f'bin/translations/qtbase_{locale}.qm', 'sources': sources, 'origin': origin})
    if not catalogs:
        raise ValueError('Qt native-widget translation catalogs are missing.')
    notices, notice_inventory = inventory.notice_bundle(roots, license_texts)
    # All dependency, source and notice checks above precede the first package write.
    for item in files:
        destination = package / item['target']
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(item['source'], destination)
        if sha256(destination) != item['sha256']:
            raise ValueError(f'Copied runtime checksum differs: {destination}')
    (package / 'bin/translations').mkdir()
    for item in catalogs:
        destination = package / item['path']
        translated = subprocess.run([str(lconvert), '-o', str(destination), *map(str, item['sources'])],
                                    cwd=package, env=env, capture_output=True, text=True, encoding='utf-8', timeout=30)
        if translated.returncode or not destination.is_file():
            raise ValueError('Qt catalog merge failed: ' + translated.stderr)
        item['sha256'] = sha256(destination)
        del item['sources']
    for relative, data in notices.items():
        destination = notice_root / relative_name(relative)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
    (notice_root / 'deployment-plan.json').write_text(json.dumps(discovered, indent=2) + '\n', encoding='utf-8')
    (package / 'bin/qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=.\nTranslations=translations\n', encoding='utf-8')
    report = {'status': 'staged-with-runtime-notices', 'tool': str(tool), 'toolSha256': sha256(tool),
              'profile': 'Qt Widgets raster / optional Audio; no implicit system, compiler, Vulkan or software-OpenGL redistribution.',
              'files': descriptions, 'qtCatalogs': catalogs, 'ffmpeg': ffmpeg,
              'osDependenciesNotCopied': excluded, 'noticeInventory': 'licenses/qt-runtime/inventory.json',
              'noticeComponents': len(notice_inventory['components']),
              'noticeLicenseTexts': len(notice_inventory['licenseTexts']),
              'remainingAcceptance': ['Supported native Windows and clean-machine verification, including physical Audio devices.',
                                      'Install the official Microsoft Visual C++ Redistributable required by the selected Qt build.',
                                      'Provide corresponding source/build information through the chosen release distribution channel before publication.',
                                      'Source matching and PE checksum normalization do not verify Authenticode trust or certify licence compliance.']}
    (notice_root / 'runtime.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    manifest['qtDeployment'] = report
    manifest['licenseFiles'] = staged_license_files(package)
    if archive:
        manifest['archivePath'] = str(package.parent / (package.name + '.zip'))
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    with (package / 'platform/README.txt').open('a', encoding='utf-8') as stream:
        stream.write('\nQt Widgets/Audio runtime staged with licences and provenance.\n'
                     'See licenses/qt-runtime/NOTICES.md and runtime.json.\n'
                     'Windows ICU and the official Microsoft Visual C++ Redistributable are OS/install prerequisites.\n'
                     'This package uses raster Widgets; software OpenGL and external shader compilers are not bundled.\n'
                     'Staging does not establish clean-machine, device or publication acceptance.\n')
    with (package / 'licenses/THIRD_PARTY_LICENSES.md').open('a', encoding='utf-8') as stream:
        stream.write('\n## Qt and FFmpeg Runtime\n\n'
                     'Original runtime copyrights and full licence texts: [Qt runtime notices](qt-runtime/NOTICES.md).\n'
                     'Exact DLL/catalog origins, vendor SPDX and build/source references: `qt-runtime/inventory.json` and `qt-runtime/runtime.json`.\n')
    write_checksums(package)
    if archive:
        shutil.make_archive(str(package.parent / package.name), 'zip', package.parent, package.name)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True, help='Fresh Windows package from package_portable.py, without --archive.')
    parser.add_argument('--qt-prefix', type=Path, required=True, help='Exact Qt SDK used to build the application; includes SPDX JSON inventories.')
    parser.add_argument('--license-texts', type=Path, required=True, help='Pinned full SPDX licence texts and a hash/URL SOURCES.json provenance map.')
    parser.add_argument('--archive', action='store_true', help='Create the ZIP after runtime, notices, manifest and checksums are final.')
    parser.add_argument('--include-offscreen', action='store_true', help='Include Qt\'s offscreen platform plugin for unattended runtime checks.')
    args = parser.parse_args()
    try:
        report = deploy(args.package, args.qt_prefix, args.license_texts, args.archive, args.include_offscreen)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f'Runtime deployment failed: {error}', file=sys.stderr)
        return 1
    print(json.dumps({'status': report['status'], 'runtimeFiles': len(report['files']),
                      'qtCatalogs': len(report['qtCatalogs']), 'noticeComponents': report['noticeComponents'],
                      'noticeLicenseTexts': report['noticeLicenseTexts']}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
