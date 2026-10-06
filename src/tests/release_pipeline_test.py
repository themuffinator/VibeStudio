"""Fresh-build provenance, failure boundaries and paired artifact publication."""
from pathlib import Path
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import unittest
import wave
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import source_companion_test as fixtures
import build_release as builder
import package_windows_release as pipeline
from source_companion_common import read_json, sha256, verify_files, walk_files


class ReleasePipeline(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.SourceCompanion('test_complete_payload_and_zip_preserve_exact_inputs')
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.root, self.source = self.fixture.root, self.fixture.source
        self.build, self.evidence, self.sdk = [self.root / name for name in ['build', 'evidence', 'sdk']]
        self.calls = []
        self.put(self.source / 'i18n/vibestudio_en.ts', 'fixture source catalog')
        for _, name in pipeline.COMPILER_LICENSE_SOURCES:
            self.put(self.source / name, 'independent compiler notice ' + name)
        for name in ['Qt6Core.lib', 'Qt6Gui.lib']:
            self.put(self.sdk / 'lib' / name, 'fixture import library ' + name)

    def tearDown(self):
        self.fixture.tearDown()

    def put(self, path, value):
        self.fixture.put(path, value)

    def write_json(self, path, value):
        self.fixture.write_json(path, value)

    def run_fixture(self, name, command, directory, cwd, env):
        self.calls.append(name)
        if name == 'configure':
            self.write_json(self.build / 'meson-info/intro-dependencies.json', [
                {'name': 'qt6', 'version': self.fixture.profile['qtVersion'],
                 'link_args': [str((self.sdk / 'lib' / name).resolve()) for name in ['Qt6Core.lib', 'Qt6Gui.lib']]}])
            self.write_json(self.build / 'meson-info/intro-buildoptions.json', [
                {'name': 'buildtype', 'value': 'release'}, {'name': 'audio_playback', 'value': 'enabled'},
                {'name': 'audio_duplex', 'value': 'enabled'},
                {'name': 'werror', 'value': True}])
            self.write_json(self.build / 'meson-info/intro-compilers.json', self.fixture.build['compilers'])
            self.write_json(self.build / 'meson-info/intro-machines.json', {'host': {'system': 'windows', 'cpu_family': 'x86_64'}})
        elif name == 'compile':
            for executable in ['vibestudio', 'vibestudio.exe']:
                self.put(self.build / 'src' / executable, (self.fixture.package / 'bin/vibestudio.exe').read_bytes())
            self.put(self.build / 'i18n/vibestudio_en.qm', (self.fixture.package / 'i18n/vibestudio_en.qm').read_bytes())
        return {'name': name, 'command': command, 'exitCode': 0}

    def build_fixture(self, callback=None):
        with patch.object(builder, 'run_logged', side_effect=callback or self.run_fixture):
            return builder.build_release(self.source, self.build, self.evidence, build_type='release',
                                         playback='enabled', qt_prefix=self.sdk, jobs=2)

    def test_fresh_build_binds_inputs_outputs_sdk_and_commands(self):
        result = self.build_fixture()
        self.assertEqual(self.calls, ['configure', 'compile'])
        self.assertEqual(result['sourceHashes'], builder.capture_sources(self.source))
        self.assertEqual(result['binarySha256'], sha256(self.build / 'src/vibestudio.exe'))
        self.assertIn('-Dwerror=true', result['checks'][0]['command'])
        self.assertIn('-Daudio_duplex=enabled', result['checks'][0]['command'])
        self.assertEqual(result['audioDuplex'], 'enabled')
        self.assertEqual(result['qtSdk'], builder.qt_sdk_inputs(self.build, self.sdk))
        self.assertFalse((self.evidence / 'INCOMPLETE.txt').exists())
        self.assertEqual(pipeline.verify_build(self.source, self.build, self.evidence / 'build-evidence.json',
                                               self.sdk, self.fixture.profile), result)

    def test_changed_source_or_sdk_during_compile_never_attests(self):
        for target in [self.source / 'src/core/original.cpp', self.sdk / 'lib/Qt6Core.lib']:
            with self.subTest(target=target.name):
                original = target.read_bytes()
                def changed(*args):
                    result = self.run_fixture(*args)
                    if args[0] == 'compile':
                        target.write_bytes(b'changed during compiler execution')
                    return result
                with self.assertRaisesRegex(ValueError, 'changed during'):
                    self.build_fixture(changed)
                self.assertFalse((self.evidence / 'build-evidence.json').exists())
                self.assertTrue((self.evidence / 'INCOMPLETE.txt').exists())
                target.write_bytes(original)
                self.build = self.root / 'second-build'
                self.evidence = self.root / 'second-evidence'

    def test_existing_and_overlapping_outputs_are_preserved_before_commands(self):
        self.put(self.build / 'keep.txt', 'preserve build')
        with self.assertRaises(ValueError):
            self.build_fixture()
        self.assertEqual((self.build / 'keep.txt').read_text(), 'preserve build')
        self.assertFalse(self.evidence.exists())
        self.assertFalse(self.calls)
        self.build = self.source / 'src/new-build'
        with self.assertRaises(ValueError):
            self.build_fixture()
        self.assertFalse(self.build.exists())

    def test_missing_catalog_or_wrong_options_never_attest(self):
        def incomplete(*args):
            result = self.run_fixture(*args)
            if args[0] == 'compile':
                (self.build / 'i18n/vibestudio_en.qm').unlink()
            return result
        with self.assertRaisesRegex(ValueError, 'catalog'):
            self.build_fixture(incomplete)
        self.assertFalse((self.evidence / 'build-evidence.json').exists())
        self.build, self.evidence = self.root / 'options-build', self.root / 'options-evidence'
        def options(*args):
            result = self.run_fixture(*args)
            if args[0] == 'compile':
                self.write_json(self.build / 'meson-info/intro-buildoptions.json', [{'name': 'werror', 'value': False}])
            return result
        with self.assertRaisesRegex(ValueError, 'options differ'):
            self.build_fixture(options)
        self.assertFalse((self.evidence / 'build-evidence.json').exists())

    def test_failed_configure_stops_before_compile(self):
        def failed(*args):
            self.calls.append(args[0])
            raise ValueError('configure failed')
        with self.assertRaisesRegex(ValueError, 'configure failed'):
            self.build_fixture(failed)
        self.assertEqual(self.calls, ['configure'])
        self.assertFalse((self.evidence / 'build-evidence.json').exists())

    def test_subprocess_exit_failure_is_logged_and_propagated(self):
        self.evidence.mkdir()
        with self.assertRaisesRegex(ValueError, 'exit code 7'):
            builder.run_logged('failure', [sys.executable, '-B', '-c', 'raise SystemExit(7)'],
                               self.evidence, self.root, os.environ.copy())
        self.assertEqual(read_json(self.evidence / 'failure.json')['exitCode'], 7)

    def test_mixed_qt_sdk_rejected(self):
        self.build_fixture()
        self.write_json(self.build / 'meson-info/intro-dependencies.json', [
            {'name': 'qt6', 'version': '6.10.1', 'link_args': [str(self.root / 'other/Qt6Core.lib')]}])
        with self.assertRaisesRegex(ValueError, 'outside'):
            builder.qt_sdk_inputs(self.build, self.sdk)

    def test_post_build_tampering_rejected_before_packaging(self):
        self.build_fixture()
        for path in [self.source / 'src/core/original.cpp', self.build / 'src/vibestudio.exe',
                     self.build / 'i18n/vibestudio_en.qm', self.sdk / 'lib/Qt6Core.lib']:
            with self.subTest(path=path.name):
                original = path.read_bytes()
                path.write_bytes(b'tampered after build')
                with self.assertRaises(ValueError):
                    pipeline.verify_build(self.source, self.build, self.evidence / 'build-evidence.json', self.sdk, self.fixture.profile)
                path.write_bytes(original)

    def stage_fixture(self, name, smoke=None):
        def stage(binary, output, version, **kwargs):
            self.assertEqual(kwargs['source_root'], self.source)
            target = output / self.fixture.package.name
            shutil.copytree(self.fixture.package, target)
            return target, None
        with patch.object(pipeline.sys, 'platform', 'win32'), patch.object(pipeline, 'create_package', side_effect=stage), \
                patch.object(pipeline, 'deploy'), patch.object(pipeline, 'runtime_smoke', side_effect=smoke or (lambda *args: [])):
            return pipeline.package_release(self.source, self.build, self.evidence / 'build-evidence.json', self.sdk, self.root / name,
                                            profile_path=self.fixture.profile_path, license_texts=self.root / 'notices',
                                            runtime_sources=self.fixture.runtime)

    def test_published_pair_binds_both_archives_to_the_build(self):
        self.build_fixture()
        result = self.stage_fixture('complete')
        publish = self.root / 'complete/publish'
        self.assertEqual(read_json(publish / 'release-set.json'), result)
        for archive in result['archives'].values():
            self.assertEqual(sha256(publish / archive['file']), archive['sha256'])
            self.assertEqual((publish / (archive['file'] + '.sha256')).read_text(), archive['sha256'] + '  ' + archive['file'] + '\n')
        self.assertEqual(result['buildEvidenceSha256'], sha256(publish / 'build-evidence.json'))
        self.assertFalse((self.root / 'complete/INCOMPLETE.txt').exists())

    def test_runtime_failure_leaves_no_uploadable_pair(self):
        self.build_fixture()
        def fail(*args):
            raise ValueError('runtime fixture failed')
        with self.assertRaisesRegex(ValueError, 'runtime fixture failed'):
            self.stage_fixture('failed', fail)
        self.assertTrue((self.root / 'failed/INCOMPLETE.txt').exists())
        self.assertFalse((self.root / 'failed/publish').exists())

    def test_late_source_change_leaves_no_uploadable_pair(self):
        self.build_fixture()
        def changed(*args):
            self.put(self.source / 'src/core/original.cpp', 'changed after runtime verification')
            return []
        with self.assertRaises(ValueError):
            self.stage_fixture('changed', changed)
        self.assertFalse((self.root / 'changed/publish').exists())

    def test_runtime_search_path_excludes_ambient_sdks_and_qt_overrides(self):
        with patch.dict(os.environ, {'SystemRoot': 'C:/Windows', 'PATH': 'unwanted-sdk',
                                     'QT_PLUGIN_PATH': 'unwanted-plugins', 'QML2_IMPORT_PATH': 'unwanted-qml'}):
            env = pipeline.runtime_environment(self.root / 'package/bin', self.root / 'temp')
        self.assertNotIn('unwanted', env['PATH'])
        self.assertNotIn('QT_PLUGIN_PATH', env)
        self.assertNotIn('QML2_IMPORT_PATH', env)
        self.assertEqual(env['QT_QPA_PLATFORM'], 'offscreen')

    def test_runtime_probes_require_cli_mode_isolated_settings_and_correct_pcm_results(self):
        self.put(self.fixture.package / 'VERSION', 'fixture\n')
        observed = []
        bad_statistics = False
        def program(command, *, cwd, env, stdout, stderr, **kwargs):
            # Model the application's contract: without --cli it enters a GUI
            # event loop. A version probe must also avoid real user settings.
            self.assertIn('--cli', command)
            self.assertIn('--settings-file', command)
            settings = Path(command[command.index('--settings-file') + 1])
            self.assertTrue(settings.is_relative_to(self.root))
            self.assertEqual(env['QT_QPA_PLATFORM'], 'offscreen')
            observed.append(command)
            if '--version' in command:
                stdout.write('VibeStudio fixture\n')
            else:
                path = Path(command[command.index('audio-analyze') + 1])
                with wave.open(str(path), 'rb') as audio:
                    channels, frames, rate = audio.getnchannels(), audio.getnframes(), audio.getframerate()
                    pcm = struct.unpack('<' + 'h' * (channels * frames), audio.readframes(frames))
                reports = []
                for index in range(channels):
                    samples = [sample / 32768.0 for sample in pcm[index::channels]]
                    reports.append({'peak': max(abs(sample) for sample in samples),
                                    'rms': 0 if bad_statistics else math.sqrt(sum(sample * sample for sample in samples) / len(samples)),
                                    'dc': sum(samples) / len(samples)})
                stdout.write(json.dumps({'audioAnalysis': {'channelCount': channels, 'sampleRate': rate,
                                                           'frames': frames, 'channels': reports}}))
            return subprocess.CompletedProcess(command, 0)
        with patch.dict(os.environ, {'SystemRoot': 'C:/Windows'}), patch.object(pipeline.subprocess, 'run', side_effect=program):
            self.assertEqual(len(pipeline.runtime_smoke(self.fixture.package, self.root / 'runtime-good')), 2)
            bad_statistics = True
            with self.assertRaisesRegex(ValueError, 'statistics differ'):
                pipeline.runtime_smoke(self.fixture.package, self.root / 'runtime-bad')
        self.assertEqual(len(observed), 4)


if __name__ == '__main__':
    unittest.main()
