"""Read Qt SPDX inventories and preserve notices for an explicit runtime set.

Original implementation; Qt's SPDX documents are CC0 data. PE offsets follow
Microsoft's PE/COFF specification, credited in docs/CREDITS.md. This module
never executes metadata, downloads files, or edits an installed SDK.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def relative_name(value: str) -> str:
    value = value.removeprefix('./')
    path = PurePosixPath(value)
    if not value or '\\' in value or ':' in value or path.is_absolute() or any(part in ('', '.', '..') for part in value.split('/')):
        raise ValueError(f'Unsafe inventory path: {value!r}')
    return path.as_posix()


def pe_checksum_match(data: bytes, expected: str) -> str | None:
    """Compare raw SHA1, then a canonical PE before its signing envelope.

    Qt's Windows SPDX hashes precede Authenticode signing. Only remove a
    well-formed certificate table at EOF, zero its directory and image checksum,
    and allow up to seven zero alignment bytes. Original signed bytes are always
    copied and SHA256-recorded. This is NOT certificate/signature verification.
    """
    if hashlib.sha1(data).hexdigest() == expected:
        return 'exact'
    if len(data) < 64 or data[:2] != b'MZ':
        return None
    pe = struct.unpack_from('<I', data, 60)[0]
    if pe < 64 or pe + 24 > len(data) or data[pe:pe + 4] != b'PE\0\0':
        return None
    optional = pe + 24
    size = struct.unpack_from('<H', data, pe + 20)[0]
    if size < 2 or optional + size > len(data):
        return None
    magic = struct.unpack_from('<H', data, optional)[0]
    directories = {0x10b: 96, 0x20b: 112}.get(magic)
    if directories is None or size < directories + 40:
        return None
    if struct.unpack_from('<I', data, optional + directories - 4)[0] < 5:
        return None
    security = optional + directories + 32
    offset, length = struct.unpack_from('<II', data, security)
    sections = struct.unpack_from('<H', data, pe + 6)[0]
    headers_end = optional + size + sections * 40
    if not length or offset < headers_end or offset % 8 or offset + length != len(data):
        return None
    cursor = offset
    while cursor < len(data):
        if cursor + 8 > len(data):
            return None
        record_length, revision, kind = struct.unpack_from('<IHH', data, cursor)
        if record_length < 8 or revision != 0x200 or kind != 2 or cursor + record_length > len(data):
            return None
        end = cursor + ((record_length + 7) & ~7)
        if end > len(data) or any(data[cursor + record_length:end]):
            return None
        cursor = end
    original = bytearray(data[:offset])
    original[security:security + 8] = bytes(8)
    original[optional + 64:optional + 68] = bytes(4)
    for padding in range(8):
        end = len(original) - padding
        if end < headers_end or (padding and any(original[end:])):
            break
        if hashlib.sha1(original[:end]).hexdigest() == expected:
            return 'pre-signing-pe'
    return None


class QtInventory:
    def __init__(self, sdk: Path):
        self.sdk = sdk.resolve()
        self.documents: dict[str, tuple[Path, dict]] = {}
        self.packages: dict[str, dict] = {}
        self.files: dict[str, list[tuple[str, dict]]] = {}
        self.owners: dict[str, list[str]] = {}
        self.dependencies: dict[str, list[str]] = {}
        for path in sorted((sdk / 'sbom').glob('*.spdx.json')):
            if path.stat().st_size > 32 * 1024 * 1024:
                raise ValueError(f'SPDX document exceeds 32 MiB: {path}')
            doc = json.loads(path.read_text(encoding='utf-8'))
            if doc.get('spdxVersion') != 'SPDX-2.3' or doc.get('dataLicense') != 'CC0-1.0':
                raise ValueError(f'Expected Qt SPDX 2.3 / CC0 data: {path}')
            name = path.name.split('-')[0]
            if name in self.documents or not re.fullmatch(r'qt[a-z0-9]+', name):
                raise ValueError(f'Ambiguous Qt module inventory: {path}')
            self.documents[name] = (path, doc)
            for item in doc['packages']:
                key = self.key(name, item['SPDXID'])
                if key in self.packages:
                    raise ValueError(f'Duplicate SPDX package: {key}')
                self.packages[key] = item
            for item in doc.get('files', []):
                filename = relative_name(item['fileName'])
                self.files.setdefault(filename.casefold(), []).append((name, item))
            for item in doc['relationships']:
                first = self.key(name, item['spdxElementId'])
                second = self.key(name, item['relatedSpdxElement'])
                relationship = item['relationshipType']
                if relationship == 'CONTAINS':
                    self.owners.setdefault(second, []).append(first)
                if relationship == 'DEPENDS_ON':
                    self.dependencies.setdefault(first, []).append(second)
                if relationship == 'DEPENDENCY_OF':
                    self.dependencies.setdefault(second, []).append(first)
        if not self.documents:
            raise ValueError('The selected Qt SDK has no SPDX JSON inventories (Qt 6.8 or newer required).')

    @staticmethod
    def key(document: str, identifier: str) -> str:
        if identifier.startswith('DocumentRef-'):
            reference, identifier = identifier.split(':', 1)
            document = reference.removeprefix('DocumentRef-')
        return document + ':' + identifier

    def describe_file(self, source: Path) -> dict:
        relative = source.resolve().relative_to(self.sdk).as_posix()
        if source.stat().st_size > 256 * 1024 * 1024:
            raise ValueError(f'Runtime binary exceeds 256 MiB: {source}')
        data = source.read_bytes()
        candidates = self.files.get(relative.casefold(), [])
        for document, item in candidates:
            comparisons = []
            for check in item.get('checksums', []):
                algorithm, expected = check['algorithm'], check['checksumValue'].lower()
                if algorithm == 'SHA1':
                    comparisons.append(pe_checksum_match(data, expected))
                elif algorithm == 'SHA256':
                    comparisons.append('exact' if hashlib.sha256(data).hexdigest() == expected else None)
            if not comparisons or not all(comparisons):
                continue
            owners = sorted(set(self.owners.get(self.key(document, item['SPDXID']), [])))
            if not owners or not all(owner in self.packages for owner in owners):
                raise ValueError(f'Runtime file has no unambiguous SPDX package: {source}')
            return {'sdkPath': relative, 'sha256': hashlib.sha256(data).hexdigest(),
                    'spdxDocument': document, 'spdxFile': item['SPDXID'], 'packages': owners,
                    'vendorChecksumMatch': 'pre-signing-pe' if 'pre-signing-pe' in comparisons else 'exact'}
        if candidates:
            raise ValueError(f'Runtime differs from its Qt SPDX checksum, including signing normalization: {source}')
        raise ValueError(f'Runtime file is absent from the Qt SPDX inventory: {source}')

    def ffmpeg_packages(self) -> list[str]:
        return [key for key, value in self.packages.items() if key.startswith('qtmultimedia:')
                and (value['name'] == 'FFmpeg' or value['name'].startswith('FFmpeg__'))]

    def closure(self, roots: list[str]) -> dict[str, dict]:
        found = {}
        pending = list(roots)
        while pending:
            key = pending.pop()
            if key in found:
                continue
            if key not in self.packages:
                raise ValueError(f'Unresolved runtime SPDX dependency: {key}')
            found[key] = self.packages[key]
            pending.extend(self.dependencies.get(key, []))
        return dict(sorted(found.items()))

    def notice_bundle(self, roots: list[str], license_texts: Path) -> tuple[dict[str, bytes], dict]:
        components = self.closure(roots)
        documents = sorted({key.split(':', 1)[0] for key in components})
        output = {}
        extracted = {}
        for name in documents:
            path, document = self.documents[name]
            output['sbom/' + path.name] = path.read_bytes()
            source = path.with_name(path.name.replace('.spdx.json', '.source.spdx'))
            if source.is_file():
                output['sbom/' + source.name] = source.read_bytes()
            for item in document.get('hasExtractedLicensingInfos', []):
                identifier = item['licenseId']
                if identifier in extracted and extracted[identifier] != item['extractedText']:
                    raise ValueError(f'Conflicting extracted licence text: {identifier}')
                extracted[identifier] = item['extractedText']
            for suffix in ['opt', 'summary']:
                config = self.sdk / f'config_{name}.{suffix}'
                if config.is_file():
                    output['build/' + config.name] = config.read_bytes()

        needed = {'LGPL-3.0-only', 'GPL-3.0-only'}
        system = []
        for key, item in components.items():
            expression = item.get('licenseConcluded', 'NOASSERTION')
            if expression == 'NOASSERTION':
                if key == 'qtbase:SPDXRef-Package-qtbase-system-3rdparty-WrapAtomic':
                    system.append({'package': key, 'reason': 'Windows compiler-provided std::atomic; Qt FindWrapAtomic is a link probe, not a separately copied runtime.'})
                    continue
                raise ValueError(f'Runtime dependency has no licence conclusion: {key}')
            needed.update(re.findall(r'[A-Za-z0-9][A-Za-z0-9.+-]*', expression))
        needed -= {'AND', 'OR', 'WITH'}
        licenses = {}
        for identifier in sorted(needed):
            if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9.+-]*', identifier):
                raise ValueError(f'Unsafe licence identifier: {identifier}')
            filename = 'texts/' + identifier + '.txt'
            source = license_texts / (identifier + '.txt')
            if identifier in extracted:
                data = extracted[identifier].encode('utf-8')
                origin = 'Qt SPDX extracted licensing information'
            elif source.is_file() and source.stat().st_size <= 1024 * 1024:
                data = source.read_bytes()
                if not data.strip():
                    raise ValueError(f'Empty licence text: {source}')
                origin = str(source.resolve())
            else:
                raise ValueError(f'Missing full runtime licence text: {source}')
            output[filename] = data
            licenses[identifier] = {'file': filename, 'sha256': hashlib.sha256(data).hexdigest(), 'origin': origin}
        provenance = license_texts / 'SOURCES.json'
        if not provenance.is_file():
            raise ValueError(f'Licence text provenance is required: {provenance}')
        provenance_data = json.loads(provenance.read_text(encoding='utf-8'))
        for identifier, item in licenses.items():
            if identifier in extracted:
                continue
            source = provenance_data.get(identifier + '.txt', {})
            if source.get('sha256') != item['sha256'] or not str(source.get('url', '')).startswith('https://'):
                raise ValueError(f'Licence text hash/source differs from provenance: {identifier}')
            for variant in source.get('variants', []):
                relative = relative_name(variant['file'])
                path = (license_texts / relative).resolve()
                if not path.is_relative_to(license_texts.resolve()) or path.stat().st_size > 1024 * 1024:
                    raise ValueError(f'Invalid licence text variant: {relative}')
                data = path.read_bytes()
                if hashlib.sha256(data).hexdigest() != variant['sha256'] or not variant['url'].startswith('https://'):
                    raise ValueError(f'Licence text variant differs from provenance: {relative}')
                output['texts/' + relative] = data
        output['texts/SOURCES.json'] = provenance.read_bytes()
        report = {'components': components, 'licenseTexts': licenses, 'systemPrerequisites': system,
                  'qtLicenceChoice': 'LGPL-3.0-only',
                  'scope': 'Qt vendor SPDX dependency/attribution records, complete referenced licence texts and original copyrights. Alternative licence expressions are retained verbatim; a commercial Qt licence is not claimed. Source distribution and native/clean-machine acceptance require separate release evidence.'}
        output['inventory.json'] = (json.dumps(report, indent=2, ensure_ascii=False) + '\n').encode('utf-8')
        lines = ['# Qt Runtime Notices', '', report['scope'], '',
                 'The distributed signed DLLs remain unchanged. See inventory.json for exact metadata and build/source references.', '',
                 '## Components', '']
        for key, item in components.items():
            lines += ['### ' + item['name'], '', '- Version: ' + item.get('versionInfo', 'unknown'),
                      '- Licence expression: ' + item.get('licenseConcluded', 'NOASSERTION'),
                      '- Source: ' + item.get('downloadLocation', 'NOASSERTION'),
                      '- SPDX identity: `' + key + '`', '', item.get('copyrightText', 'NOASSERTION'), '']
        output['NOTICES.md'] = ('\n'.join(lines) + '\n').encode('utf-8')
        return output, report
