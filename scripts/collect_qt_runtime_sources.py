#!/usr/bin/env python3
"""Collect unchanged source archives for a reviewed Qt runtime profile.

Only HTTPS downloads and independent file copies are performed. Archives are
never extracted and no downloaded program, build recipe or installer is run.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
from pathlib import Path
import sys
import urllib.error
import urllib.request

from source_companion_common import (DEFAULT_PROFILE, copy_checked, load_profile, new_output,
                                     reject_links, sha256, write_json)


def collect(profile_path: Path, output: Path, cached_archives: Path | None = None) -> dict:
    reject_links(profile_path)
    profile_digest = sha256(profile_path)
    profile = load_profile(profile_path)
    if sha256(profile_path) != profile_digest:
        raise ValueError('Runtime source profile changed while reading it.')
    protected = [profile_path]
    if cached_archives is not None:
        reject_links(cached_archives)
        protected.append(cached_archives)
    output = new_output(output, protected)
    output.mkdir(parents=True)
    (output / 'archives').mkdir()

    def acquire(item):
        destination = output / 'archives' / item['file']
        cached = cached_archives / item['file'] if cached_archives else None
        if cached is not None and cached.exists():
            reject_links(cached)
            if cached.stat().st_size != item['bytes'] or sha256(cached) != item['sha256']:
                raise ValueError(f'Cached source archive differs from the profile: {cached}')
            copy_checked(cached, destination, item['sha256'])
            origin = 'verified-independent-cache-copy'
        else:
            request = urllib.request.Request(item['url'], headers={'User-Agent': 'VibeStudio-Source-Companion'})
            digest, total = hashlib.sha256(), 0
            with urllib.request.urlopen(request, timeout=45) as response, destination.open('xb') as stream:
                if not response.url.startswith('https://'):
                    raise ValueError('Refusing a source download redirected away from HTTPS.')
                for data in iter(lambda: response.read(1024 * 1024), b''):
                    total += len(data)
                    if total > item['bytes']:
                        raise ValueError(f'Source download exceeds its pinned size: {item["file"]}')
                    digest.update(data)
                    stream.write(data)
            if total != item['bytes'] or digest.hexdigest() != item['sha256']:
                raise ValueError(f'Source download differs from its pinned size/SHA-256: {item["file"]}')
            origin = 'https-download'
        print(f'Verified {item["file"]}', file=sys.stderr, flush=True)
        return {'component': item['component'], 'file': 'archives/' + item['file'],
                'sha256': item['sha256'], 'bytes': item['bytes'], 'origin': origin}

    with ThreadPoolExecutor(max_workers=4) as workers:
        files = list(workers.map(acquire, profile['archives']))
    copy_checked(profile_path, output / 'runtime-source-profile.json', profile_digest)
    result = {'schemaVersion': 1, 'status': 'verified-archives', 'profile': profile['id'],
              'profileSha256': profile_digest, 'files': files,
              'scope': 'Unchanged source archives only; build, binary provenance and publication acceptance are separate.'}
    write_json(output / 'source-acquisition.json', result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', type=Path, default=DEFAULT_PROFILE, help='Reviewed profile with exact archive URLs, sizes and SHA-256 values.')
    parser.add_argument('--output', type=Path, required=True, help='New project-local directory for archives and provenance.')
    parser.add_argument('--cached-archives', type=Path, help='Optional local archive directory; matching files are independently copied and verified.')
    args = parser.parse_args()
    try:
        import json
        print(json.dumps(collect(args.profile, args.output, args.cached_archives)))
    except (OSError, ValueError, KeyError, TypeError, urllib.error.URLError) as error:
        print(f'Runtime source acquisition failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
