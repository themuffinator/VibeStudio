#!/usr/bin/env python3
"""Shared file-integrity rules for source acquisition and release companions."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
from urllib.parse import urlsplit


DEFAULT_PROFILE = Path(__file__).parent / 'runtime_sources/qt-6.10.1-msvc-x64.json'
# assets/branding and packaging are build inputs too: the icon resources and the
# Windows .rc template compile into the binary, and meson.build includes packaging/.
SOURCE_DIRECTORIES = ('src', 'scripts', 'docs', 'i18n', 'samples', 'assets', 'packaging', 'external/audio',
                      'external/modelling')
SOURCE_ROOT_FILES = {'.editorconfig', '.gitattributes', '.gitignore', '.gitmodules', 'AGENTS.md', 'CHANGELOG.md',
                     'LICENSE', 'COPYING', 'NOTICE', 'README.md', 'VERSION', 'meson.build', 'meson_options.txt'}


def reject_links(path: Path) -> None:
    for candidate in (path, *path.parents):
        try:
            info = candidate.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0):
            raise ValueError(f'Source companion input/output contains a link or reparse point: {candidate}')


def relative_name(value: str) -> str:
    if not isinstance(value, str) or not value or PurePosixPath(value).is_absolute():
        raise ValueError(f'Unsafe relative path: {value!r}')
    reserved = {'con', 'prn', 'aux', 'nul', *(f'com{n}' for n in range(1, 10)), *(f'lpt{n}' for n in range(1, 10))}
    for part in value.split('/'):
        if (part in {'', '.', '..'} or part.rstrip(' .') != part or part.split('.')[0].casefold() in reserved
                or any(ord(c) < 32 or c in '\\:*?"<>|' for c in part)):
            raise ValueError(f'Unsafe relative path: {value!r}')
    return value


def sha256(path: Path) -> str:
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(data)
    return result.hexdigest()


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'Duplicate JSON key: {key}')
        result[key] = value
    return result


def read_json(path: Path) -> dict:
    reject_links(path)
    if not path.is_file() or path.stat().st_size > 32 * 1024 * 1024:
        raise ValueError(f'Missing or oversized JSON input: {path}')
    data = json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=unique_object)
    if not isinstance(data, dict):
        raise ValueError(f'Expected a JSON object: {path}')
    return data


def file_hashes(values: dict) -> dict[str, str]:
    if not isinstance(values, dict) or not values or len(values) > 100000:
        raise ValueError('Expected a nonempty bounded file-hash inventory.')
    folded = set()
    for name, digest in values.items():
        relative_name(name)
        if name.casefold() in folded or not isinstance(digest, str) or not re.fullmatch('[0-9a-f]{64}', digest):
            raise ValueError(f'Duplicate portable filename or invalid SHA-256: {name}')
        folded.add(name.casefold())
    return values


def walk_files(root: Path) -> dict[str, Path]:
    reject_links(root)
    result = {}
    for parent, directories, files in os.walk(root, followlinks=False):
        for name in directories + files:
            reject_links(Path(parent) / name)
        for name in files:
            path = Path(parent) / name
            relative = relative_name(path.relative_to(root).as_posix())
            if not path.is_file():
                raise ValueError(f'Expected a regular file: {path}')
            result[relative] = path
    return result


def verify_files(root: Path, expected: dict[str, str]) -> None:
    for name, digest in file_hashes(expected).items():
        path = root / name
        reject_links(path)
        if not path.is_file() or sha256(path) != digest:
            raise ValueError(f'Input differs from recorded SHA-256: {path}')


def copy_checked(source: Path, destination: Path, expected: str) -> None:
    reject_links(source)
    reject_links(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    result = hashlib.sha256()
    with source.open('rb') as reader, destination.open('xb') as writer:
        for data in iter(lambda: reader.read(1024 * 1024), b''):
            writer.write(data)
            result.update(data)
    if result.hexdigest() != expected:
        raise ValueError(f'Input changed while copying: {source}')


def load_profile(path: Path) -> dict:
    profile = read_json(path)
    if profile.get('schemaVersion') != 1 or not re.fullmatch('[a-z0-9][a-z0-9.-]+', profile.get('id', '')):
        raise ValueError('Unsupported runtime-source profile schema or identifier.')
    archives = profile.get('archives')
    if not isinstance(archives, list) or not 1 <= len(archives) <= 32:
        raise ValueError('A runtime-source profile requires 1–32 pinned archives.')
    hashes, components = {}, set()
    for item in archives:
        name = relative_name(item['file'])
        if '/' in name or not name.endswith(('.tar.gz', '.tar.xz')) or name in hashes:
            raise ValueError(f'Invalid or duplicate archive filename: {name}')
        if item['component'] in components or not re.fullmatch('[A-Za-z0-9][A-Za-z0-9_-]*', item['component']):
            raise ValueError('Duplicate or unsafe source component.')
        if not isinstance(item['bytes'], int) or not 0 < item['bytes'] <= 512 * 1024 * 1024:
            raise ValueError('Source archive size is outside the supported bound.')
        if urlsplit(item['url']).scheme != 'https' or not urlsplit(item['url']).netloc:
            raise ValueError('Source archives require an HTTPS upstream URL.')
        if '/' in relative_name(item['archiveRoot']):
            raise ValueError('Archive root must be a single portable directory name.')
        hashes[name] = item['sha256']
        components.add(item['component'])
    file_hashes(hashes)
    if not isinstance(profile.get('qtModules'), dict) or not set(profile['qtModules']).issubset(components):
        raise ValueError('Qt runtime modules must have pinned source archives.')
    if not profile['qtModules'] or any(not re.fullmatch('qt[a-z0-9]+', module) or not isinstance(commit, str)
                                      or not re.fullmatch('[0-9a-f]{40}', commit) for module, commit in profile['qtModules'].items()):
        raise ValueError('Qt runtime modules require exact source commits.')
    if profile.get('targetPlatform') not in {'windows', 'linux', 'macos'} or not re.fullmatch('[A-Za-z0-9_-]+', profile.get('targetArchitecture', '')):
        raise ValueError('Invalid source-profile target platform or architecture.')
    for key in ['qtVersion', 'ffmpegVersion']:
        if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', profile.get(key, '')):
            raise ValueError(f'Source profile requires an exact version: {key}')
    if not isinstance(profile.get('ffmpegConfiguration'), str) or not 0 < len(profile['ffmpegConfiguration']) <= 8192:
        raise ValueError('Source profile requires a bounded FFmpeg configuration.')
    if not isinstance(profile.get('zlibRecipeAdjustments'), list) or not all(isinstance(item, str) for item in profile['zlibRecipeAdjustments']):
        raise ValueError('Source profile requires explicit zlib build adjustments.')
    if not {'qt5-build-recipes', 'FFmpeg', 'zlib'}.issubset(components):
        raise ValueError('Runtime sources require Qt build recipes, FFmpeg and zlib.')
    return profile


def new_output(path: Path, protected: list[Path]) -> Path:
    reject_links(path)
    path = path.resolve()
    if path.exists():
        raise ValueError(f'Choose a new source output; existing contents are preserved: {path}')
    for item in protected:
        reject_links(item)
        if item.resolve().is_relative_to(path.resolve()) or path.resolve().is_relative_to(item.resolve()):
            raise ValueError(f'Source output overlaps a required input: {item}')
    return path


def write_json(path: Path, value: dict) -> None:
    reject_links(path)
    with path.open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, indent=2, ensure_ascii=True)
        stream.write('\n')
