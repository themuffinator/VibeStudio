"""Portable catalog staging contracts; runtime QTranslator acceptance is separate."""
from pathlib import Path
import importlib.util
import json
import os
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("package_portable", ROOT / "scripts/package_portable.py")
packaging = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(packaging)


class CompiledCatalogPackaging(unittest.TestCase):
    def setUp(self):
        temporary_root = Path(os.environ.get("VIBESTUDIO_TEST_TMP_ROOT", ROOT / ".agents/tmp/package-translations")).resolve()
        temporary_root.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="catalogs-", dir=temporary_root)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        (self.source / "i18n").mkdir(parents=True)
        (self.source / "docs").mkdir()
        (self.source / "README.md").write_text("Packaging fixture\n")
        (self.source / "VERSION").write_text("fixture\n")
        for locale in ["en", "ar"]:
            (self.source / "i18n" / f"vibestudio_{locale}.ts").write_text(f'<TS language="{locale}"/>')
        # A source-tree artifact must not masquerade as the selected build's runtime catalog.
        (self.source / "i18n/vibestudio_en.qm").write_bytes(b"stale source-tree bytes")
        self.binary = self.root / "build/src/vibestudio"
        self.binary.parent.mkdir(parents=True)
        self.binary.write_bytes(b"opaque executable fixture")
        self.compiled = self.root / "build/i18n"
        self.compiled.mkdir()
        self.addCleanup(patch.stopall)
        patch.object(packaging, "repo_root", return_value=self.source).start()
        # License bundling has its own whole-package validator; isolate these byte-copy contracts.
        patch.object(packaging, "write_license_bundle", return_value=("licenses/fixture", [])).start()

    def stage(self, explicit=None, output="output", archive=False):
        package, _ = packaging.create_package(self.binary, self.root / output, "fixture", False, archive,
                                               "linux", "x86_64", explicit)
        return package, json.loads((package / "package-manifest.json").read_text())

    def test_automatic_complete_and_partial_inventory(self):
        for locale in ["en", "ar"]:
            (self.compiled / f"vibestudio_{locale}.qm").write_bytes(f"compiled {locale}".encode())
        (self.compiled / "unrelated.qm").write_bytes(b"unrelated")
        package, manifest = self.stage()
        self.assertEqual(manifest["compiledLocalization"]["status"], "complete")
        self.assertEqual((package / "i18n/vibestudio_en.qm").read_bytes(), b"compiled en")
        self.assertFalse((package / "i18n/unrelated.qm").exists())
        self.assertIn("i18n/vibestudio_ar.qm", (package / "CHECKSUMS.sha256").read_text())
        (self.compiled / "vibestudio_ar.qm").unlink()
        package, manifest = self.stage(output="partial")
        self.assertEqual(manifest["compiledLocalization"]["status"], "partial")
        self.assertEqual(manifest["compiledLocalization"]["missingCatalogs"], ["i18n/vibestudio_ar.qm"])

    def test_missing_runtime_keeps_source_language_fallback_explicit(self):
        package, manifest = self.stage()
        self.assertEqual(manifest["compiledLocalization"]["status"], "unavailable")
        self.assertEqual(manifest["compiledLocalization"]["catalogs"], [])
        self.assertTrue((package / "i18n/vibestudio_en.ts").exists())
        self.assertFalse((package / "i18n/vibestudio_en.qm").exists())

    def test_required_catalogs_fail_before_replacing_existing_package(self):
        package, _ = self.stage()
        sentinel = package / "sentinel"
        sentinel.write_bytes(b"preserve existing output")
        with self.assertRaisesRegex(ValueError, "every application catalog"):
            self.stage(self.compiled)
        self.assertEqual(sentinel.read_bytes(), b"preserve existing output")
        override = self.root / "override"
        override.mkdir()
        for locale in ["en", "ar"]:
            (override / f"vibestudio_{locale}.qm").write_bytes(f"override {locale}".encode())
        package, manifest = self.stage(override)
        self.assertTrue(manifest["compiledLocalization"]["required"])
        self.assertEqual((package / "i18n/vibestudio_en.qm").read_bytes(), b"override en")

    def test_repackaging_cannot_delete_its_binary_or_catalog_inputs(self):
        for locale in ["en", "ar"]:
            (self.compiled / f"vibestudio_{locale}.qm").write_bytes(f"compiled {locale}".encode())
        package, _ = self.stage()
        self.binary = package / "bin/vibestudio"
        with self.assertRaisesRegex(ValueError, "required input"):
            self.stage()
        self.assertEqual(self.binary.read_bytes(), b"opaque executable fixture")
        self.binary = self.root / "build/src/vibestudio"
        with self.assertRaisesRegex(ValueError, "required input"):
            self.stage(package / "i18n")
        self.assertEqual((package / "i18n/vibestudio_en.qm").read_bytes(), b"compiled en")

    def link(self, path, target):
        try:
            path.symlink_to(target, target_is_directory=target.is_dir())
        except OSError as error:
            self.skipTest(f"Symbolic links are unavailable: {error}")
        # Remove the fixture link before TemporaryDirectory's recursive cleanup.
        self.addCleanup(path.unlink)

    def test_linked_package_output_preserves_target(self):
        output = self.root / "output"
        target = output / "keep"
        target.mkdir(parents=True)
        sentinel = target / "sentinel"
        sentinel.write_bytes(b"untouched")
        self.link(output / "vibestudio-fixture-linux-x86_64", target)
        with self.assertRaisesRegex(ValueError, "link or reparse point"):
            self.stage()
        self.assertEqual(sentinel.read_bytes(), b"untouched")

    @unittest.skipUnless(os.name == "nt", "Windows junction fixture")
    def test_junction_package_output_preserves_target(self):
        output = self.root / "output"
        target = output / "keep"
        target.mkdir(parents=True)
        sentinel = target / "sentinel"
        sentinel.write_bytes(b"untouched")
        link = output / "vibestudio-fixture-linux-x86_64"
        env = os.environ.copy()
        env["VIBESTUDIO_TEST_LINK_PATH"] = str(link)
        env["VIBESTUDIO_TEST_LINK_TARGET"] = str(target)
        subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
                        "$ErrorActionPreference = 'Stop'; New-Item -ItemType Junction "
                        "-Path $env:VIBESTUDIO_TEST_LINK_PATH -Target $env:VIBESTUDIO_TEST_LINK_TARGET | Out-Null"],
                       env=env, check=True, capture_output=True, text=True)
        # rmdir removes the junction itself without traversing the target.
        self.addCleanup(link.rmdir)
        with self.assertRaisesRegex(ValueError, "link or reparse point"):
            self.stage()
        self.assertEqual(sentinel.read_bytes(), b"untouched")

    def test_nested_output_link_preserves_existing_package(self):
        package, _ = self.stage()
        original = (package / "bin/vibestudio").read_bytes()
        target = self.root / "keep"
        target.write_bytes(b"untouched")
        link = package / "linked-file"
        self.link(link, target)
        with self.assertRaisesRegex(ValueError, "link or reparse point"):
            self.stage()
        self.assertEqual(target.read_bytes(), b"untouched")
        self.assertEqual((package / "bin/vibestudio").read_bytes(), original)

    def test_linked_archive_preserves_target_and_package(self):
        package, _ = self.stage()
        sentinel = package / "sentinel"
        sentinel.write_bytes(b"existing package")
        target = self.root / "keep.zip"
        target.write_bytes(b"untouched")
        self.link(package.parent / (package.name + ".zip"), target)
        with self.assertRaisesRegex(ValueError, "link or reparse point"):
            self.stage(archive=True)
        self.assertEqual(target.read_bytes(), b"untouched")
        self.assertEqual(sentinel.read_bytes(), b"existing package")


if __name__ == "__main__":
    unittest.main()
