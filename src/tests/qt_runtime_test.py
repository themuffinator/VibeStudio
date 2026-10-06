"""Independent runtime provenance, path isolation and full-notice contracts."""
from pathlib import Path
import hashlib
import json
import os
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from deploy_windows_runtime import plan_files
from collect_qt_runtime_licenses import assemble, collect
from qt_runtime_inventory import QtInventory, pe_checksum_match, relative_name


def signed_fixture(wide=True, padding=0):
    optional_size, directories = (240, 112) if wide else (224, 96)
    data = bytearray(1024 - padding)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 60, 64)
    data[64:68] = b'PE\0\0'
    struct.pack_into('<H', data, 70, 1)
    struct.pack_into('<H', data, 84, optional_size)
    struct.pack_into('<H', data, 88, 0x20b if wide else 0x10b)
    struct.pack_into('<I', data, 88 + directories - 4, 16)
    data[600:610] = b'code-bytes'
    expected = hashlib.sha1(data).hexdigest()
    data.extend(bytes(padding))
    certificate = struct.pack('<IHH', 13, 0x200, 2) + b'proof' + bytes(3)
    struct.pack_into('<I', data, 88 + 64, 987654)
    struct.pack_into('<II', data, 88 + directories + 32, len(data), len(certificate))
    return bytes(data) + certificate, expected


class SigningProvenance(unittest.TestCase):
    def test_exact_and_pre_signing_hashes_without_mutation(self):
        for wide in [True, False]:
            for padding in range(8):
                with self.subTest(wide=wide, padding=padding):
                    data, expected = signed_fixture(wide, padding)
                    before = hashlib.sha256(data).hexdigest()
                    self.assertEqual(pe_checksum_match(data, hashlib.sha1(data).hexdigest()), 'exact')
                    self.assertEqual(pe_checksum_match(data, expected), 'pre-signing-pe')
                    self.assertEqual(hashlib.sha256(data).hexdigest(), before)

    def test_changed_code_and_malformed_signing_envelopes_fail(self):
        data, expected = signed_fixture()
        variants = [data[:-1], data + b'overlay', b'not a PE', data[:60]]
        for offset, replacement in [(600, b'X'), (60, struct.pack('<I', 0xffffffff)),
                                    (88, b'\x00\x00'), (84, b'\x01\x00'),
                                    (88 + 108, struct.pack('<I', 4)),
                                    (88 + 144, struct.pack('<I', 100)),
                                    (1024, struct.pack('<I', 7)), (1028, b'\x00\x00'),
                                    (1030, b'\x01\x00'), (1039, b'X')]:
            changed = bytearray(data)
            changed[offset:offset + len(replacement)] = replacement
            variants.append(bytes(changed))
        for variant in variants:
            self.assertIsNone(pe_checksum_match(variant, expected))

    def test_paths_reject_traversal_and_windows_aliases(self):
        self.assertEqual(relative_name('./bin/Qt6Core.dll'), 'bin/Qt6Core.dll')
        for path in ['', '/root', '../outside', 'bin/../outside', 'C:/root', 'bin\\foo', 'a//b', 'a/./b']:
            with self.subTest(path=path), self.assertRaises(ValueError):
                relative_name(path)


class RuntimeInventory(unittest.TestCase):
    def setUp(self):
        parent = Path(os.environ.get('VIBESTUDIO_TEST_TMP_ROOT', ROOT / '.agents/tmp/qt-runtime-tests')).resolve()
        parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='runtime-', dir=parent)
        self.root = Path(self.temporary.name)
        def cleanup():
            from deploy_windows_runtime import reject_links
            self.assertTrue(self.root.resolve().is_relative_to(parent))
            for directory, names, files in os.walk(self.root, followlinks=False):
                for name in names + files:
                    reject_links(Path(directory) / name)
            self.temporary.cleanup()
        self.addCleanup(cleanup)
        self.sdk = self.root / 'sdk'
        self.package = self.root / 'package'
        self.system = self.root / 'Windows'
        for path in [self.sdk / 'bin', self.sdk / 'sbom', self.package / 'bin', self.system / 'System32']:
            path.mkdir(parents=True)
        self.binary = self.sdk / 'bin/Qt6Core.dll'
        self.binary.write_bytes(b'original DLL bytes')
        self.document = {'spdxVersion': 'SPDX-2.3', 'dataLicense': 'CC0-1.0', 'name': 'qtbase-fixture',
                         'packages': [self.component('Core', 'LGPL-3.0-only'), self.component('Codec', 'MIT')],
                         'files': [{'SPDXID': 'SPDXRef-file', 'fileName': './bin/Qt6Core.dll',
                                    'checksums': [{'algorithm': 'SHA1', 'checksumValue': hashlib.sha1(self.binary.read_bytes()).hexdigest()}]}],
                         'relationships': [{'spdxElementId': 'SPDXRef-Core', 'relationshipType': 'CONTAINS', 'relatedSpdxElement': 'SPDXRef-file'},
                                           {'spdxElementId': 'SPDXRef-Core', 'relationshipType': 'DEPENDS_ON', 'relatedSpdxElement': 'SPDXRef-Codec'}]}
        self.write_document()
        self.texts = self.root / 'texts'
        self.texts.mkdir()
        self.provenance = {}
        for identifier in ['MIT', 'LGPL-3.0-only', 'GPL-3.0-only']:
            data = ('Full original fixture licence: ' + identifier).encode()
            (self.texts / (identifier + '.txt')).write_bytes(data)
            self.provenance[identifier + '.txt'] = {'sha256': hashlib.sha256(data).hexdigest(), 'url': 'https://example.test/' + identifier}
        self.write_provenance()

    def component(self, name, licence):
        return {'SPDXID': 'SPDXRef-' + name, 'name': name, 'licenseConcluded': licence,
                'versionInfo': '1.0', 'downloadLocation': 'https://example.test/source',
                'copyrightText': 'Copyright Example ' + name}

    def write_document(self):
        (self.sdk / 'sbom/qtbase-fixture.spdx.json').write_text(json.dumps(self.document))

    def write_provenance(self):
        (self.texts / 'SOURCES.json').write_text(json.dumps(self.provenance))

    def test_transitive_notices_preserve_original_bytes_and_copyrights(self):
        inventory = QtInventory(self.sdk)
        descriptor = inventory.describe_file(self.binary)
        bundle, report = inventory.notice_bundle(descriptor['packages'], self.texts)
        self.assertEqual(len(report['components']), 2)
        self.assertEqual(bundle['texts/MIT.txt'], (self.texts / 'MIT.txt').read_bytes())
        self.assertIn(b'Copyright Example Codec', bundle['NOTICES.md'])
        self.assertEqual(bundle['sbom/qtbase-fixture.spdx.json'], (self.sdk / 'sbom/qtbase-fixture.spdx.json').read_bytes())

    def test_changed_unknown_and_unowned_binary_fail(self):
        inventory = QtInventory(self.sdk)
        self.binary.write_bytes(b'different DLL bytes')
        with self.assertRaisesRegex(ValueError, 'differs'):
            inventory.describe_file(self.binary)
        unknown = self.sdk / 'bin/unknown.dll'
        unknown.write_bytes(b'unknown')
        with self.assertRaisesRegex(ValueError, 'absent'):
            inventory.describe_file(unknown)
        self.binary.write_bytes(b'original DLL bytes')
        self.document['relationships'] = []
        self.write_document()
        with self.assertRaisesRegex(ValueError, 'unambiguous'):
            QtInventory(self.sdk).describe_file(self.binary)

    def test_missing_text_tampered_provenance_and_unresolved_dependency_fail(self):
        inventory = QtInventory(self.sdk)
        with self.assertRaisesRegex(ValueError, 'Unresolved'):
            inventory.closure(['qtmissing:SPDXRef-any'])
        (self.texts / 'MIT.txt').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing full'):
            inventory.notice_bundle(['qtbase:SPDXRef-Core'], self.texts)
        (self.texts / 'MIT.txt').write_bytes(b'changed notice')
        with self.assertRaisesRegex(ValueError, 'differs from provenance'):
            inventory.notice_bundle(['qtbase:SPDXRef-Core'], self.texts)
        self.document['packages'][1]['licenseConcluded'] = 'NOASSERTION'
        self.write_document()
        with self.assertRaisesRegex(ValueError, 'no licence conclusion'):
            QtInventory(self.sdk).notice_bundle(['qtbase:SPDXRef-Core'], self.texts)

    def test_module_specific_notice_variants_survive(self):
        variant = self.texts / 'variants/qtother/MIT.txt'
        variant.parent.mkdir(parents=True)
        variant.write_bytes(b'Another full licence with preserved additional notice')
        self.provenance['MIT.txt']['variants'] = [{'file': variant.relative_to(self.texts).as_posix(),
            'sha256': hashlib.sha256(variant.read_bytes()).hexdigest(), 'url': 'https://example.test/variant'}]
        self.write_provenance()
        bundle, _ = QtInventory(self.sdk).notice_bundle(['qtbase:SPDXRef-Core'], self.texts)
        self.assertEqual(bundle['texts/variants/qtother/MIT.txt'], variant.read_bytes())
        variant.write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError, 'variant differs'):
            QtInventory(self.sdk).notice_bundle(['qtbase:SPDXRef-Core'], self.texts)

    def test_cross_module_dependency_and_cycles_are_resolved(self):
        second = {'spdxVersion': 'SPDX-2.3', 'dataLicense': 'CC0-1.0', 'packages': [self.component('Other', 'MIT')],
                  'relationships': [{'spdxElementId': 'SPDXRef-Other', 'relationshipType': 'DEPENDS_ON',
                                     'relatedSpdxElement': 'DocumentRef-qtbase:SPDXRef-Core'}]}
        (self.sdk / 'sbom/qtmultimedia-fixture.spdx.json').write_text(json.dumps(second))
        self.document['relationships'].append({'spdxElementId': 'SPDXRef-Core', 'relationshipType': 'DEPENDS_ON',
                                               'relatedSpdxElement': 'DocumentRef-qtmultimedia:SPDXRef-Other'})
        self.write_document()
        self.assertEqual(len(QtInventory(self.sdk).closure(['qtbase:SPDXRef-Core'])), 3)

    def test_sdk_only_plan_records_system_icu_without_copying(self):
        icu = self.system / 'System32/icuuc.dll'
        icu.write_bytes(b'OS component')
        report = {'files': [{'source': str(path), 'target': str(self.package / 'bin')} for path in [self.binary, icu]]}
        files, excluded = plan_files(report, self.sdk, self.package, self.system)
        self.assertEqual([item['target'] for item in files], ['bin/Qt6Core.dll'])
        self.assertEqual(excluded[0]['name'], 'icuuc.dll')
        self.assertEqual(list((self.package / 'bin').iterdir()), [])

    def test_plan_rejects_foreign_source_duplicate_and_escaping_target(self):
        foreign = self.root / 'Vulkan/dxcompiler.dll'
        foreign.parent.mkdir()
        foreign.write_bytes(b'foreign SDK')
        good = {'source': str(self.binary), 'target': str(self.package / 'bin')}
        reports = [[{'source': str(foreign), 'target': good['target']}], [good, good],
                   [{'source': good['source'], 'target': str(self.package / 'bin/../../outside')}]]
        for files in reports:
            with self.assertRaises(ValueError):
                plan_files({'files': files}, self.sdk, self.package, self.system)
        self.assertFalse((self.root / 'outside').exists())

    def test_plan_rejects_linked_output_without_writing_its_target(self):
        link = self.package / 'bin/plugins'
        try:
            link.symlink_to(self.sdk / 'bin', target_is_directory=True)
        except OSError as error:
            self.skipTest(f'Symlink creation is unavailable: {error}')
        try:
            before = self.binary.read_bytes()
            with self.assertRaisesRegex(ValueError, 'link/reparse'):
                plan_files({'files': [{'source': str(self.binary), 'target': str(link)}]}, self.sdk, self.package, self.system)
            self.assertEqual(self.binary.read_bytes(), before)
        finally:
            link.unlink()

    def test_licence_collection_preserves_differing_originals_and_rejects_paths(self):
        first = {'url': 'https://example.test/one', 'module': 'qtbase'}
        second = {'url': 'https://example.test/two', 'module': 'qtmultimedia'}
        files, sources = assemble([('MIT.txt', b'first', first), ('MIT.txt', b'first', second),
                                   ('MIT.txt', b'full variant', second)])
        self.assertEqual(files, {'MIT.txt': b'first', 'variants/qtmultimedia/MIT.txt': b'full variant'})
        self.assertEqual(sources['MIT.txt']['sha256'], hashlib.sha256(b'first').hexdigest())
        self.assertEqual(sources['MIT.txt']['variants'][0]['file'], 'variants/qtmultimedia/MIT.txt')
        with self.assertRaises(ValueError):
            assemble([('../escape.txt', b'bad', first)])
        with self.assertRaises(ValueError):
            assemble([('MIT.txt', b'', first)])
        with self.assertRaisesRegex(ValueError, 'exact Qt 6'):
            collect('latest', self.root / 'unused')
        with self.assertRaisesRegex(ValueError, 'never replaced'):
            collect('6.10.1', self.texts)


if __name__ == '__main__':
    unittest.main()
