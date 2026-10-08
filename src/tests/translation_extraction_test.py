"""Extraction must preserve saved translations, including Qt-unsupported locales."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("extract_translations", ROOT / "scripts/extract_translations.py")
extraction = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = extraction
SPEC.loader.exec_module(extraction)


class TranslationExtraction(unittest.TestCase):
    def setUp(self):
        temporary_root = ROOT / ".agents/tmp/translation-extraction-tests"
        temporary_root.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=temporary_root)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.catalogs = [self.root / "vibestudio_en.ts", self.root / "vibestudio_ar.ts"]
        self.originals = {}
        for catalog, language in zip(self.catalogs, ("en", "ar")):
            content = (
                f'<TS language="{language}"><context><name>Example</name>'
                '<message><source>Old source</source><translation>Existing translation</translation>'
                '</message><message numerus="yes"><source>%n files</source>'
                '<translation><numerusform>%n file</numerusform><numerusform>%n files</numerusform>'
                '</translation></message></context></TS>'
            ).encode()
            catalog.write_bytes(content)
            self.originals[catalog] = content
        self.targets = []
        self.write = False
        # Keep the implementation's temporary output inside the test workspace.
        self.addCleanup(patch.stopall)
        patch.object(tempfile, "tempdir", str(self.root)).start()

    def fake_lupdate(self, command, **kwargs):
        self.assertEqual(command[1], str(self.root / "src"))
        self.assertEqual(kwargs["cwd"], self.root)
        self.targets = [Path(value) for value in command[command.index("-ts") + 1:]]
        self.assertEqual(len(self.targets), len(self.catalogs))
        for original, target in zip(self.catalogs, self.targets):
            self.assertEqual(target.name, original.name)
            if self.write:
                self.assertEqual(target.read_bytes(), self.originals[original])
            else:
                # Retain source entries to avoid rebuilding each locale from
                # zero, but keep the dry run independent of translated text.
                tree = ET.parse(target)
                self.assertEqual(tree.getroot().get("language"), ET.fromstring(self.originals[original]).get("language"))
                self.assertEqual(tree.findtext(".//source"), "Old source")
                self.assertEqual(tree.findtext(".//translation"), "")
                self.assertEqual(tree.find(".//translation").get("type"), "unfinished")
                self.assertTrue(all(not list(value) and not value.text for value in tree.iter("translation")))
                self.assertEqual(tree.find(".//message[@numerus='yes']/source").text, "%n files")
            target.write_text('<TS><context><message><source>Fresh source</source>'
                              '</message></context></TS>', encoding="utf-8")
        return subprocess.CompletedProcess(command, 0, "Updated catalogs", "")

    def test_dry_run_merges_independent_copies_and_cleans_up(self):
        with patch.object(extraction.subprocess, "run", side_effect=self.fake_lupdate):
            report = extraction.run_lupdate(Path("lupdate"), self.root, self.catalogs, False)
        self.assertEqual(report["messageCounts"], {path.name: 1 for path in self.catalogs})
        for catalog in self.catalogs:
            self.assertEqual(catalog.read_bytes(), self.originals[catalog])
        self.assertTrue(all(not path.parent.exists() for path in self.targets))

    def test_failed_dry_run_preserves_originals_and_cleans_up(self):
        def fail(command, **kwargs):
            self.fake_lupdate(command, **kwargs)
            return subprocess.CompletedProcess(command, 1, "", "Extraction failed")

        with patch.object(extraction.subprocess, "run", side_effect=fail):
            with self.assertRaisesRegex(RuntimeError, "Extraction failed"):
                extraction.run_lupdate(Path("lupdate"), self.root, self.catalogs, False)
        for catalog in self.catalogs:
            self.assertEqual(catalog.read_bytes(), self.originals[catalog])
        self.assertTrue(all(not path.parent.exists() for path in self.targets))

    def test_write_still_updates_original_catalogs(self):
        self.write = True
        with patch.object(extraction.subprocess, "run", side_effect=self.fake_lupdate):
            extraction.run_lupdate(Path("lupdate"), self.root, self.catalogs, True)
        self.assertEqual(self.targets, self.catalogs)
        self.assertTrue(all(b"Fresh source" in path.read_bytes() for path in self.catalogs))

    def test_real_lupdate_extracts_new_messages_and_removes_stale_ones(self):
        tool = extraction.find_lupdate()
        if tool is None:
            self.skipTest("Qt lupdate is not installed")
        (self.root / "src").mkdir()
        (self.root / "src/example.cpp").write_text(
            'void example(int count) {\n'
            'QCoreApplication::translate("Example", "Fresh source");\n'
            'QCoreApplication::translate("Example", "%n files", nullptr, count);\n'
            '}\n', encoding="utf-8")
        run = subprocess.run

        def inspect(command, **kwargs):
            result = run(command, **kwargs)
            for value in command[command.index("-ts") + 1:]:
                tree = ET.parse(value)
                self.assertEqual({item.text for item in tree.iter("source")}, {"Fresh source", "%n files"})
                self.assertIsNotNone(tree.find(".//message[@numerus='yes']"))
                self.assertNotIn("Existing translation", Path(value).read_text(encoding="utf-8"))
            return result

        with patch.object(extraction.subprocess, "run", side_effect=inspect):
            report = extraction.run_lupdate(tool, self.root, self.catalogs, False)
        self.assertEqual(report["minimumMessageCount"], 2)
        self.assertEqual(report["skippedCatalogs"], [])
        for catalog in self.catalogs:
            self.assertEqual(catalog.read_bytes(), self.originals[catalog])

    def unsupported_catalog(self):
        catalog = self.root / "vibestudio_pcm.ts"
        catalog.write_text(
            '<TS version="2.1" language="pcm"><context><name>Example</name>'
            '<message><location filename="src/old.cpp" line="+3"/>'
            '<source>Keep source</source><comment>noun</comment>'
            '<translatorcomment>Review this wording</translatorcomment>'
            '<translation type="unfinished">Keep Naijá &amp; wording</translation></message>'
            '<message numerus="yes"><source>%n files</source>'
            '<translation type="unfinished"><numerusform>First form %n</numerusform>'
            '<numerusform>Second form %n</numerusform><numerusform>Third saved form %n</numerusform>'
            '</translation><translatorcomment>Preserve every saved variant</translatorcomment></message>'
            '<message><source>Removed source</source><translation>Saved removed wording</translation>'
            '<translatorcomment>Keep this note too</translatorcomment></message>'
            '<message><source>Stale untranslated</source><translation type="unfinished"/></message>'
            '</context><context><name>RemovedContext</name>'
            '<message id="removed-id"><source>Removed context source</source>'
            '<translation>Other saved wording</translation></message></context></TS>', encoding="utf-8")
        return catalog

    def unsupported_lupdate(self, catalog, *, failure=None):
        original = catalog.read_bytes()
        calls = []

        def run(command, **kwargs):
            targets = [Path(value) for value in command[command.index("-ts") + 1:]]
            self.assertEqual(len(targets), 1)
            target = targets[0]
            calls.append(target)
            self.assertEqual(catalog.read_bytes(), original)
            if target == catalog:
                return subprocess.CompletedProcess(command, 0, "", f"File {catalog.as_posix()} won't be updated: it contains translation but the target language is not recognized\n")
            self.assertEqual(ET.parse(target).getroot().get("language"), "pcm")
            self.assertTrue(all(not list(value) and not value.text for value in ET.parse(target).iter("translation")))
            if failure:
                return subprocess.CompletedProcess(command, 1, "", failure)
            filename = Path(os.path.relpath(self.root / "src/example.cpp", target.parent)).as_posix()
            target.write_text(
                '<TS version="2.1" language="pcm"><context><name>Example</name>'
                f'<message><location filename="{filename}" line="+12"/>'
                '<source>Keep source</source><comment>noun</comment><translation type="unfinished"/></message>'
                '<message><source>Keep source</source><comment>verb</comment><translation type="unfinished"/></message>'
                '<message><source>Fresh source</source><translation type="unfinished"/></message>'
                '<message numerus="yes"><source>%n files</source><translation type="unfinished"/></message>'
                '<message><source>%n files</source><translation type="unfinished"/></message>'
                '</context><context><name>DifferentContext</name><message>'
                '<source>Keep source</source><comment>noun</comment><translation type="unfinished"/>'
                '</message></context></TS>', encoding="utf-8")
            return subprocess.CompletedProcess(command, 0, "Updated source-only catalog", "")

        return run, calls

    def test_unsupported_write_preserves_translations_variants_and_notes(self):
        catalog = self.unsupported_catalog()
        run, calls = self.unsupported_lupdate(catalog)
        with patch.object(extraction.subprocess, "run", side_effect=run):
            report = extraction.run_lupdate(Path("lupdate"), self.root, [catalog], True)
        tree = ET.parse(catalog)
        self.assertEqual(tree.getroot().get("language"), "pcm")
        messages = extraction.catalog_messages(tree)
        kept = messages[("Example", "", "Keep source", "noun", "no")]
        self.assertEqual(kept.findtext("translation"), "Keep Naijá & wording")
        self.assertEqual(kept.find("translation").get("type"), "unfinished")
        self.assertEqual(kept.findtext("translatorcomment"), "Review this wording")
        self.assertEqual(kept.find("location").get("filename"), "src/example.cpp")
        plural = messages[("Example", "", "%n files", "", "yes")]
        self.assertEqual([entry.text for entry in plural.findall("translation/numerusform")],
                         ["First form %n", "Second form %n", "Third saved form %n"])
        self.assertEqual(plural.findtext("translatorcomment"), "Preserve every saved variant")
        for key in [("Example", "", "Fresh source", "", "no"),
                    ("Example", "", "Keep source", "verb", "no"),
                    ("Example", "", "%n files", "", "no"),
                    ("DifferentContext", "", "Keep source", "noun", "no")]:
            self.assertFalse(messages[key].findtext("translation"))
            self.assertEqual(messages[key].find("translation").get("type"), "unfinished")
            self.assertIsNone(messages[key].find("translatorcomment"))
        removed = messages[("Example", "", "Removed source", "", "no")]
        self.assertEqual(removed.findtext("translation"), "Saved removed wording")
        self.assertEqual(removed.find("translation").get("type"), "vanished")
        self.assertEqual(removed.findtext("translatorcomment"), "Keep this note too")
        self.assertIsNone(removed.find("location"))
        self.assertEqual(messages[("RemovedContext", "removed-id", "Removed context source", "", "no")].findtext("translation"), "Other saved wording")
        self.assertNotIn(("Example", "", "Stale untranslated", "", "no"), messages)
        self.assertEqual(report["skippedCatalogs"], [])
        self.assertEqual(report["sourceOnlyMergedCatalogs"], [catalog.name])
        self.assertEqual(report["messageCounts"], {catalog.name: 8})
        self.assertEqual(len(calls), 2)
        self.assertFalse(calls[-1].parent.exists())

    def test_unsupported_failed_extraction_preserves_original_bytes(self):
        catalog = self.unsupported_catalog()
        original = catalog.read_bytes()
        run, calls = self.unsupported_lupdate(catalog, failure="Source extraction failed")
        with patch.object(extraction.subprocess, "run", side_effect=run):
            with self.assertRaisesRegex(RuntimeError, "Source extraction failed"):
                extraction.run_lupdate(Path("lupdate"), self.root, [catalog], True)
        self.assertEqual(catalog.read_bytes(), original)
        self.assertFalse(calls[-1].parent.exists())

    def test_unsupported_failed_atomic_replace_preserves_original_bytes(self):
        catalog = self.unsupported_catalog()
        original = catalog.read_bytes()
        run, calls = self.unsupported_lupdate(catalog)
        with patch.object(extraction.subprocess, "run", side_effect=run), \
                patch.object(extraction.os, "replace", side_effect=OSError("Cannot publish")):
            with self.assertRaisesRegex(OSError, "Cannot publish"):
                extraction.run_lupdate(Path("lupdate"), self.root, [catalog], True)
        self.assertEqual(catalog.read_bytes(), original)
        self.assertFalse(list(catalog.parent.glob(f".{catalog.name}.*.tmp")))
        self.assertFalse(calls[-1].parent.exists())

    def test_unsupported_invalid_merge_preserves_original_bytes(self):
        for invalid in ("language", "duplicate", "concurrent"):
            with self.subTest(invalid=invalid):
                catalog = self.unsupported_catalog()
                original = catalog.read_bytes()
                run, calls = self.unsupported_lupdate(catalog)

                def corrupt(command, **kwargs):
                    result = run(command, **kwargs)
                    if len(calls) == 2:
                        tree = ET.parse(calls[-1])
                        if invalid == "language":
                            tree.getroot().set("language", "en")
                        elif invalid == "duplicate":
                            context = tree.find("context")
                            context.append(context.find("message"))
                        else:
                            catalog.write_bytes(original + b"\n<!-- Concurrent translator edit -->\n")
                        tree.write(calls[-1], encoding="utf-8")
                    return result

                with patch.object(extraction.subprocess, "run", side_effect=corrupt):
                    with self.assertRaises(RuntimeError):
                        extraction.run_lupdate(Path("lupdate"), self.root, [catalog], True)
                expected = original + b"\n<!-- Concurrent translator edit -->\n" if invalid == "concurrent" else original
                self.assertEqual(catalog.read_bytes(), expected)
                self.assertFalse(calls[-1].parent.exists())

    def test_real_unsupported_locale_preserves_saved_plural_forms(self):
        tool = extraction.find_lupdate()
        if tool is None:
            self.skipTest("Qt lupdate is not installed")
        catalog = self.unsupported_catalog()
        (self.root / "src").mkdir()
        (self.root / "src/example.cpp").write_text(
            'void example(int count) {\n'
            'QCoreApplication::translate("Example", "Keep source", "noun");\n'
            'QCoreApplication::translate("Example", "Fresh source");\n'
            'QCoreApplication::translate("Example", "%n files", nullptr, count);\n'
            '}\n', encoding="utf-8")
        report = extraction.run_lupdate(tool, self.root, [catalog], True)
        tree = ET.parse(catalog)
        self.assertEqual(tree.getroot().get("language"), "pcm")
        self.assertEqual(report["skippedCatalogs"], [])
        self.assertEqual(len(tree.findall(".//numerusform")), 3)
        self.assertEqual(tree.findtext(".//translatorcomment"), "Review this wording")
        self.assertEqual(tree.find(".//location").get("filename"), "src/example.cpp")
        self.assertIn("Saved removed wording", catalog.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
