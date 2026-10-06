#!/usr/bin/env python3
"""Collect unchanged licence documents for a pinned Qt 6 release.

Downloads licence text only, never implementation code or executable files.
Each download is verified against the Git blob reported by the Qt repository.
Module-specific variants are retained instead of normalizing legal notices.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import sys
import urllib.error
import urllib.request

from deploy_windows_runtime import reject_links


MODULES = ['qtbase', 'qtmultimedia', 'qtimageformats', 'qtsvg', 'qttranslations']


def fetch(url: str) -> bytes:
    request = urllib.request.Request(url, headers={'User-Agent': 'VibeStudio-Runtime-Notices'})
    with urllib.request.urlopen(request, timeout=45) as response:
        data = response.read(1024 * 1024 + 1)
    if len(data) > 1024 * 1024:
        raise ValueError(f'Licence document/list exceeds 1 MiB: {url}')
    return data


def assemble(downloads: list[tuple[str, bytes, dict]]) -> tuple[dict[str, bytes], dict]:
    files, provenance = {}, {}
    for name, data, source in downloads:
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9.+-]*\.txt', name):
            raise ValueError(f'Unexpected licence filename: {name}')
        if not data.strip():
            raise ValueError(f'Empty licence text: {name}')
        data.decode('utf-8')
        source = {**source, 'sha256': hashlib.sha256(data).hexdigest()}
        if name not in provenance:
            files[name], provenance[name] = data, source
        elif files[name] == data:
            provenance[name].setdefault('additionalOrigins', []).append(source)
        else:
            module = source['module']
            if not re.fullmatch(r'qt[a-z0-9-]+', module):
                raise ValueError(f'Invalid licence source module: {module}')
            relative = 'variants/' + module + '/' + name
            if relative in files:
                raise ValueError(f'Duplicate licence variant: {relative}')
            files[relative] = data
            provenance[name].setdefault('variants', []).append({'file': relative, **source})
    return files, provenance


def collect(version: str, output: Path) -> dict:
    if not re.fullmatch(r'6\.\d+\.\d+', version):
        raise ValueError('Use an exact Qt 6 release, for example 6.10.1.')
    reject_links(output)
    output = output.resolve()
    if output.exists():
        raise ValueError('Choose a new licence output directory; existing notices are never replaced.')
    locations = [(module, 'LICENSES') for module in MODULES]
    locations.append(('qtmultimedia', 'src/3rdparty/ffmpeg'))

    def listing(location):
        module, directory = location
        url = f'https://api.github.com/repos/qt/{module}/contents/{directory}?ref=v{version}'
        items = json.loads(fetch(url))
        if not isinstance(items, list) or len(items) > 200:
            raise ValueError(f'Unexpected Qt licence directory response: {url}')
        return [(module, directory, item) for item in items if item.get('type') == 'file'
                and item['name'].endswith('.txt') and (directory == 'LICENSES' or item['name'].startswith('LICENSE.'))]

    with ThreadPoolExecutor(max_workers=6) as workers:
        entries = [entry for group in workers.map(listing, locations) for entry in group]
    if not entries:
        raise ValueError('The selected Qt release contains no licence documents.')

    def download(entry):
        module, directory, item = entry
        url = item['download_url']
        expected = f'https://raw.githubusercontent.com/qt/{module}/v{version}/{directory}/{item["name"]}'
        if url != expected:
            raise ValueError(f'Unexpected licence download location: {url}')
        data = fetch(url)
        blob = hashlib.sha1(b'blob ' + str(len(data)).encode('ascii') + b'\0' + data).hexdigest()
        if blob != item['sha']:
            raise ValueError(f'Licence document differs from the upstream Git blob: {url}')
        ffmpeg = directory != 'LICENSES'
        name = item['name'].removeprefix('LICENSE.') if ffmpeg else item['name']
        return name, data, {'url': url, 'gitBlobSha1': blob, 'revision': 'v' + version,
                            'module': module + ('-ffmpeg' if ffmpeg else '')}

    with ThreadPoolExecutor(max_workers=8) as workers:
        downloaded = list(workers.map(download, entries))
    files, provenance = assemble(downloaded)
    # Network and hash verification complete before the first output write.
    reject_links(output)
    output.mkdir(parents=True)
    for relative, data in files.items():
        destination = output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
    (output / 'SOURCES.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
    return {'qtVersion': version, 'upstreamDocuments': len(downloaded), 'preservedTexts': len(files),
            'output': str(output)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qt-version', required=True, help='Exact release tag matching the deployed SDK, such as 6.10.1.')
    parser.add_argument('--output', required=True, type=Path, help='New directory for full licence documents and SOURCES.json.')
    args = parser.parse_args()
    try:
        print(json.dumps(collect(args.qt_version, args.output)))
    except (OSError, ValueError, KeyError, urllib.error.URLError) as error:
        print(f'Qt licence collection failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
