"""Source/binary binding, tamper rejection and preservation on packaging failure."""
from pathlib import Path
import hashlib
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import collect_qt_runtime_sources as collector
import package_source_companion as companion
from source_companion_common import file_hashes, read_json, reject_links, sha256, walk_files


class SourceCompanion(unittest.TestCase):
    def setUp(self):
        parent = Path(os.environ.get('VIBESTUDIO_TEST_TMP_ROOT', ROOT / '.agents/tmp/source-companion-tests')).resolve()
        reject_links(parent)
        parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='sources-', dir=parent)
        self.root = Path(self.temporary.name)

        def cleanup():
            self.assertTrue(self.root.resolve().is_relative_to(parent))
            walk_files(self.root)
            self.temporary.cleanup()

        self.addCleanup(cleanup)
        self.source, self.package, self.runtime = (self.root / name for name in ['source', 'binary', 'runtime'])
        for name in ['meson.build', 'meson_options.txt', 'VERSION', 'LICENSE', 'README.md', 'src/meson.build',
                     'src/core/original.cpp', 'external/audio/meson.build', 'external/modelling/meson.build', 'i18n/vibestudio_en.ts']:
            self.put(self.source / name, 'fixture\n' if name == 'VERSION' else 'Original source — café\n')
        self.put(self.package / 'bin/vibestudio.exe', b'opaque application binary fixture')
        self.put(self.package / 'i18n/vibestudio_en.qm', b'opaque compiled catalog fixture')
        self.put(self.package / 'bin/Qt6Core.dll', b'opaque Qt runtime fixture')
        self.put(self.package / 'bin/translations/qtbase_en.qm', b'opaque native Qt catalog fixture')
        self.profile = {'schemaVersion': 1, 'id': 'qt-fixture', 'qtVersion': '6.10.1', 'targetPlatform': 'windows',
                        'targetArchitecture': 'x86_64', 'qtModules': {'qtbase': 'a' * 40, 'qttranslations': 'b' * 40},
                        'ffmpegVersion': '7.1.2', 'ffmpegConfiguration': '--enable-shared --disable-static',
                        'zlibRecipeAdjustments': ['Fixture build instructions'], 'archives': []}
        for component in ['qtbase', 'qttranslations', 'qt5-build-recipes', 'FFmpeg', 'zlib']:
            filename = component + '.tar.gz'
            data = ('Original source archive ' + component).encode()
            self.put(self.runtime / 'archives' / filename, data)
            self.profile['archives'].append({'component': component, 'file': filename, 'archiveRoot': component,
                                             'url': 'https://example.invalid/' + filename, 'bytes': len(data),
                                             'sha256': hashlib.sha256(data).hexdigest()})
        self.profile_path = self.root / 'profile.json'
        self.write_json(self.profile_path, self.profile)
        self.put(self.runtime / 'runtime-source-profile.json', self.profile_path.read_bytes())
        self.write_json(self.runtime / 'source-acquisition.json', {
            'status': 'verified-archives', 'profileSha256': sha256(self.profile_path),
            'files': [{'file': 'archives/' + a['file'], 'sha256': a['sha256']} for a in self.profile['archives']]})
        components = {}
        for module in self.profile['qtModules']:
            components[module + ':SPDXRef-Package-' + module + '-qt-module-Fixture'] = {'versionInfo': '6.10.1'}
            for suffix in ['opt', 'summary']:
                self.put(self.package / f'licenses/qt-runtime/build/config_{module}.{suffix}', 'Original build configuration\n')
        self.write_json(self.package / 'licenses/qt-runtime/inventory.json', {'components': components})
        self.put(self.package / 'licenses/qt-runtime/NOTICES.md', 'Original licence and copyright\n')
        self.manifest = {'packageName': 'vibestudio-fixture-win64', 'version': 'fixture', 'targetPlatform': 'windows',
                         'targetArchitecture': 'x86_64', 'externalCompilersBundled': False,
                         'binary': {'stagedPath': 'bin/vibestudio.exe', 'sha256': sha256(self.package / 'bin/vibestudio.exe')},
                         'compiledLocalization': {'catalogs': ['i18n/vibestudio_en.qm']},
                         'qtDeployment': {'status': 'staged-with-runtime-notices',
                                          'ffmpeg': {'version': '7.1.2', 'configuration': self.profile['ffmpegConfiguration']},
                                          'files': [{'path': 'bin/Qt6Core.dll', 'sha256': sha256(self.package / 'bin/Qt6Core.dll'), 'spdxDocument': 'qtbase'}],
                                          'qtCatalogs': [{'path': 'bin/translations/qtbase_en.qm', 'sha256': sha256(self.package / 'bin/translations/qtbase_en.qm'),
                                                          'origin': [{'spdxDocument': 'qttranslations'}]}],
                                          'noticeInventory': 'licenses/qt-runtime/inventory.json'}}
        self.save_package()
        self.build = {'sourceHashes': {name: sha256(path) for name, path in walk_files(self.source).items()},
                      'binarySha256': self.manifest['binary']['sha256'],
                      'compiledCatalogs': {'vibestudio_en.qm': sha256(self.package / 'i18n/vibestudio_en.qm')},
                      'buildType': 'release', 'audioPlayback': 'enabled', 'checks': [
                          {'name': 'configure', 'command': ['meson', 'setup', 'machine-specific-path', '--buildtype=release', '-Daudio_playback=enabled']}],
                      'compilers': {'host': {'cpp': {'id': 'fixture', 'version': '1.0', 'exelist': ['private-user-path']}}}}
        self.build_path = self.root / 'build.json'
        self.save_build()

    def put(self, path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data.encode('utf-8') if isinstance(data, str) else data)

    def write_json(self, path, value):
        self.put(path, json.dumps(value, indent=2) + '\n')

    def save_package(self):
        self.write_json(self.package / 'package-manifest.json', self.manifest)
        hashes = {name: sha256(path) for name, path in walk_files(self.package).items() if name != 'CHECKSUMS.sha256'}
        self.put(self.package / 'CHECKSUMS.sha256', ''.join(h + '  ' + n + '\n' for n, h in sorted(hashes.items())))

    def save_build(self):
        self.write_json(self.build_path, self.build)

    def assemble(self, name='companion', archive=False):
        return companion.assemble(self.source, self.build_path, self.package, self.runtime,
                                  self.root / name, self.profile_path, archive)

    def test_complete_payload_and_zip_preserve_exact_inputs(self):
        before = {name: sha256(path) for name, path in walk_files(self.root).items()}
        result = self.assemble(archive=True)
        output = Path(result['output'])
        receipt = read_json(output / 'source-companion.json')
        self.assertEqual(receipt['sourceHashes'], self.build['sourceHashes'])
        self.assertEqual(receipt['binarySha256'], self.build['binarySha256'])
        self.assertEqual(receipt['runtimeArchives'], {'archives/' + a['file']: a['sha256'] for a in self.profile['archives']})
        self.assertNotIn('private-user-path', (output / 'source-companion.json').read_text(encoding='utf-8'))
        self.assertFalse((output / 'INCOMPLETE.txt').exists())
        files = walk_files(output)
        self.assertEqual(len(companion.package_inventory(output)), len(files) - 1)
        with zipfile.ZipFile(result['archive']) as zipped:
            self.assertEqual(set(zipped.namelist()), {output.name + '/' + name for name in files})
            for name, path in files.items():
                self.assertEqual(zipped.read(output.name + '/' + name), path.read_bytes())
        for name, value in before.items():
            self.assertEqual(sha256(self.root / name), value)

    def test_changed_missing_and_unrecorded_application_sources_fail_before_output(self):
        original = self.source / 'src/core/original.cpp'
        content = original.read_bytes()
        for variation in ['modified', 'missing', 'added']:
            with self.subTest(variation=variation):
                if variation == 'modified':
                    original.write_bytes(b'changed source')
                elif variation == 'missing':
                    original.unlink()
                else:
                    self.put(self.source / 'src/core/new.cpp', b'unrecorded source')
                with self.assertRaises(ValueError):
                    self.assemble(variation)
                self.assertFalse((self.root / variation).exists())
                original.write_bytes(content)
                added = self.source / 'src/core/new.cpp'
                if added.exists():
                    added.unlink()

    def test_binary_and_catalog_binding_rejects_even_rechecksummed_packages(self):
        for name in ['bin/vibestudio.exe', 'i18n/vibestudio_en.qm']:
            with self.subTest(name=name):
                path = self.package / name
                original = path.read_bytes()
                path.write_bytes(b'other build')
                if name.endswith('.exe'):
                    self.manifest['binary']['sha256'] = sha256(path)
                self.save_package()
                with self.assertRaises(ValueError):
                    self.assemble('mismatch')
                self.assertFalse((self.root / 'mismatch').exists())
                path.write_bytes(original)
                self.manifest['binary']['sha256'] = self.build['binarySha256']
                self.save_package()

    def test_package_tampering_and_extra_files_fail(self):
        notices = self.package / 'licenses/qt-runtime/NOTICES.md'
        original = notices.read_bytes()
        notices.write_bytes(b'changed notice')
        with self.assertRaises(ValueError):
            self.assemble('tampered')
        notices.write_bytes(original)
        self.put(self.package / 'unrecorded.dll', b'unrecorded runtime')
        with self.assertRaisesRegex(ValueError, 'file set'):
            self.assemble('extra')
        self.assertFalse((self.root / 'tampered').exists())
        self.assertFalse((self.root / 'extra').exists())

    def test_runtime_configuration_module_and_archive_mismatch_fail(self):
        self.manifest['qtDeployment']['ffmpeg']['configuration'] += ' --enable-gpl'
        self.save_package()
        with self.assertRaisesRegex(ValueError, 'FFmpeg'):
            self.assemble('flags')
        self.manifest['qtDeployment']['ffmpeg']['configuration'] = self.profile['ffmpegConfiguration']
        self.manifest['qtDeployment']['files'][0]['spdxDocument'] = 'unrecorded-module'
        self.save_package()
        with self.assertRaisesRegex(ValueError, 'module coverage'):
            self.assemble('module')
        self.manifest['qtDeployment']['files'][0]['spdxDocument'] = 'qtbase'
        self.save_package()
        (self.runtime / 'archives/FFmpeg.tar.gz').write_bytes(b'wrong archive')
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            self.assemble('archive')

    def test_existing_or_overlapping_output_preserves_user_files(self):
        sentinel = self.root / 'companion/keep.txt'
        self.put(sentinel, b'preserve me')
        with self.assertRaises(ValueError):
            self.assemble()
        self.assertEqual(sentinel.read_bytes(), b'preserve me')
        with self.assertRaises(ValueError):
            self.assemble('source/src/generated')
        self.assertFalse((self.source / 'src/generated').exists())
        self.put(self.root / 'fresh.zip', b'existing archive')
        with self.assertRaises(ValueError):
            self.assemble('fresh', archive=True)
        self.assertEqual((self.root / 'fresh.zip').read_bytes(), b'existing archive')
        self.assertFalse((self.root / 'fresh').exists())

    def test_late_source_change_has_no_success_manifest_or_zip(self):
        original_copy = companion.copy_checked
        path = self.source / 'src/core/original.cpp'

        def change_after_copy(origin, destination, digest):
            original_copy(origin, destination, digest)
            if origin == path:
                path.write_bytes(b'concurrent edit')

        with patch.object(companion, 'copy_checked', side_effect=change_after_copy), self.assertRaisesRegex(ValueError, 'recorded SHA-256'):
            self.assemble(archive=True)
        self.assertFalse((self.root / 'companion/source-companion.json').exists())
        self.assertFalse((self.root / 'companion.zip').exists())
        self.assertTrue((self.root / 'companion/INCOMPLETE.txt').exists())

    def test_portable_paths_duplicate_json_and_case_aliases_fail(self):
        for value in ['../escape', '/root', 'C:/root', 'a\\b', 'a:b', 'a//b', 'NUL.txt', 'a./file', 'a /file', 'a\nfile']:
            with self.subTest(value=value), self.assertRaises(ValueError):
                file_hashes({value: '0' * 64})
        with self.assertRaises(ValueError):
            file_hashes({'src/Case.cpp': '0' * 64, 'src/case.cpp': '1' * 64})
        self.put(self.root / 'duplicate.json', '{"key":1,"key":2}')
        with self.assertRaisesRegex(ValueError, 'Duplicate JSON'):
            read_json(self.root / 'duplicate.json')

    def test_duplicate_acquisition_entries_fail_before_output(self):
        path = self.runtime / 'source-acquisition.json'
        data = read_json(path)
        data['files'].append(data['files'][0])
        self.write_json(path, data)
        with self.assertRaisesRegex(ValueError, 'archive coverage'):
            self.assemble()
        self.assertFalse((self.root / 'companion').exists())

    def test_incomplete_build_metadata_fails_before_output(self):
        for key in ['buildType', 'audioPlayback', 'compilers', 'checks']:
            with self.subTest(key=key):
                value = self.build.pop(key)
                self.save_build()
                with self.assertRaises(ValueError):
                    self.assemble(key)
                self.assertFalse((self.root / key).exists())
                self.build[key] = value
        self.save_build()

    def test_late_package_addition_has_no_success_receipt(self):
        original_copy = companion.copy_checked

        def change_after_copy(origin, destination, digest):
            original_copy(origin, destination, digest)
            if origin == self.source / 'src/core/original.cpp':
                self.put(self.package / 'new-runtime.dll', b'concurrent runtime change')

        with patch.object(companion, 'copy_checked', side_effect=change_after_copy), self.assertRaisesRegex(ValueError, 'file set'):
            self.assemble(archive=True)
        self.assertFalse((self.root / 'companion/source-companion.json').exists())
        self.assertFalse((self.root / 'companion.zip').exists())

    def test_linked_input_or_output_is_rejected(self):
        link = self.root / 'linked'
        try:
            link.symlink_to(self.package, target_is_directory=True)
        except OSError as error:
            self.skipTest('Symlink creation unavailable: ' + str(error))
        try:
            with self.assertRaisesRegex(ValueError, 'link or reparse'):
                companion.assemble(self.source, self.build_path, link, self.runtime, self.root / 'linked-input', self.profile_path)
            with self.assertRaisesRegex(ValueError, 'link or reparse'):
                self.assemble('linked/new-output')
        finally:
            link.unlink()

    @unittest.skipUnless(sys.platform == 'win32', 'Windows junction behavior')
    def test_windows_junction_output_is_rejected_without_touching_target(self):
        link = self.root / 'junction'
        result = subprocess.run(['cmd', '/c', 'mklink', '/J', str(link), str(self.package)], capture_output=True, text=True)
        if result.returncode:
            self.skipTest('Junction creation unavailable: ' + result.stderr)
        try:
            with self.assertRaisesRegex(ValueError, 'link or reparse'):
                self.assemble('junction/new-output')
            self.assertTrue((self.package / 'bin/vibestudio.exe').is_file())
        finally:
            os.rmdir(link)

    def test_collector_checks_cached_bytes_without_network(self):
        with patch.object(collector.urllib.request, 'urlopen', side_effect=AssertionError('Unexpected network request')):
            result = collector.collect(self.profile_path, self.root / 'collected', self.runtime / 'archives')
        self.assertEqual(len(result['files']), 5)
        for item in result['files']:
            self.assertEqual(sha256(self.root / 'collected' / item['file']), item['sha256'])
        (self.runtime / 'archives/FFmpeg.tar.gz').write_bytes(b'bad cache')
        with patch.object(collector.urllib.request, 'urlopen', side_effect=AssertionError('Unexpected network request')):
            with self.assertRaisesRegex(ValueError, 'Cached source archive'):
                collector.collect(self.profile_path, self.root / 'bad-cache', self.runtime / 'archives')
        self.assertFalse((self.root / 'bad-cache/source-acquisition.json').exists())

    def test_collector_rejects_oversize_download_before_success_receipt(self):
        data = {item['url']: (self.runtime / 'archives' / item['file']).read_bytes() for item in self.profile['archives']}

        def download(request, **kwargs):
            response = io.BytesIO(data[request.full_url] + b'changed upstream bytes')
            response.url = request.full_url
            return response

        with patch.object(collector.urllib.request, 'urlopen', side_effect=download), self.assertRaisesRegex(ValueError, 'pinned size'):
            collector.collect(self.profile_path, self.root / 'oversize')
        self.assertFalse((self.root / 'oversize/source-acquisition.json').exists())


if __name__ == '__main__':
    unittest.main()
