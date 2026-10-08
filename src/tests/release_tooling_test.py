#!/usr/bin/env python3
"""Release tooling: versions, the changelog, staged release assets, release
notes, the HTML documentation build and the generated offline guide."""
from __future__ import annotations

import importlib
import json
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

release_meta = importlib.import_module("release_meta")
changelog = importlib.import_module("changelog")
version_tool = importlib.import_module("version")
release = importlib.import_module("release")
offline_guide = importlib.import_module("generate_offline_guide")

Version = release_meta.Version


class VersionTests(unittest.TestCase):
    def test_parse_and_format(self):
        version = Version.parse("0.1.0-alpha.1")
        self.assertEqual((version.major, version.minor, version.patch, version.prerelease), (0, 1, 0, "alpha.1"))
        self.assertTrue(version.is_prerelease)
        self.assertEqual(str(Version.parse("1.2.3+20261007.abcdef12")), "1.2.3+20261007.abcdef12")
        for bad in ("1.2", "01.2.3", "1.2.3-", "v1.2.3", "1.2.3-alpha..1"):
            with self.assertRaises(ValueError, msg=bad):
                Version.parse(bad)

    def test_precedence_follows_semver(self):
        ordered = ["0.1.0-alpha.1", "0.1.0-alpha.2", "0.1.0-alpha.10", "0.1.0-beta.1", "0.1.0-rc.1", "0.1.0", "0.1.1", "0.2.0"]
        keys = [Version.parse(text).sort_key() for text in ordered]
        self.assertEqual(keys, sorted(keys))
        self.assertEqual(Version.parse("1.0.0+a").sort_key(), Version.parse("1.0.0+b").sort_key())

    def test_bumps(self):
        bump = version_tool.bumped
        self.assertEqual(str(bump(Version.parse("0.1.0-alpha.1"), "pre", None, None)), "0.1.0-alpha.2")
        self.assertEqual(str(bump(Version.parse("0.1.0-alpha.3"), "pre", "beta", None)), "0.1.0-beta.1")
        self.assertEqual(str(bump(Version.parse("0.1.0-rc.2"), "release", None, None)), "0.1.0")
        self.assertEqual(str(bump(Version.parse("0.1.0"), "minor", None, "alpha")), "0.2.0-alpha.1")
        self.assertEqual(str(bump(Version.parse("0.1.4-beta.1"), "patch", None, None)), "0.1.5")
        self.assertEqual(str(bump(Version.parse("0.9.2"), "major", None, None)), "1.0.0")
        with self.assertRaises(ValueError):
            bump(Version.parse("0.1.0-beta.1"), "pre", "alpha", None)  # labels only move forward
        with self.assertRaises(ValueError):
            bump(Version.parse("0.1.0"), "pre", None, None)

    def test_names_derive_from_the_version(self):
        version = Version.parse("0.1.0-alpha.1")
        self.assertEqual(release_meta.tag_for(version), "v0.1.0-alpha.1")
        self.assertEqual(str(release_meta.version_from_tag("refs/tags/v0.1.0-alpha.1")), "0.1.0-alpha.1")
        names = release_meta.asset_names("0.1.0-alpha.1")
        self.assertEqual(names["windows-installer"], "VibeStudio-0.1.0-alpha.1-windows-x64-setup.exe")
        self.assertEqual(names["linux-appimage"], "VibeStudio-0.1.0-alpha.1-linux-x86_64.AppImage")
        self.assertEqual(names["docs"], "VibeStudio-0.1.0-alpha.1-docs.zip")

    def test_repository_version_is_valid(self):
        version = release_meta.read_version()
        self.assertFalse(version.build, "VERSION must not carry build metadata")


SAMPLE = textwrap.dedent("""\
    # Changelog

    Intro text.

    ## [Unreleased]

    ### Fixed

    - **Levels:** Undo keeps hidden brushes hidden.

    ### Added

    - **Packages:** Compare two packages.
      Continued on a second line.

    ## [0.1.0-alpha.1] - 2026-10-07

    ### Added

    - First preview.

    [Unreleased]: https://github.com/themuffinator/VibeStudio/compare/v0.1.0-alpha.1...HEAD
    [0.1.0-alpha.1]: https://github.com/themuffinator/VibeStudio/releases/tag/v0.1.0-alpha.1
    """)


class ChangelogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / "CHANGELOG.md"
        self.path.write_text(SAMPLE, encoding="utf-8")
        patcher = mock.patch.object(changelog, "read_version", return_value=Version.parse("0.1.0-alpha.2"))
        patcher.start()
        self.addCleanup(patcher.stop)
        self.addCleanup(self.temp.cleanup)

    def test_parse_render_round_trip_orders_types(self):
        log = changelog.load(self.path)
        self.assertEqual([s.label for s in log.sections], ["Unreleased", "0.1.0-alpha.1"])
        rendered = changelog.render(log)
        self.assertLess(rendered.index("### Added"), rendered.index("### Fixed"))
        self.assertIn("  Continued on a second line.", rendered)
        self.assertEqual(changelog.check(changelog.parse(rendered)), [])

    def test_release_moves_unreleased_and_updates_links(self):
        log = changelog.load(self.path)
        unreleased = log.section("Unreleased")
        released = changelog.Section("0.1.0-alpha.2", "2026-10-20", dict(unreleased.entries))
        unreleased.entries = {}
        log.sections.insert(1, released)
        log.links = changelog.compare_links(log)
        self.assertEqual(changelog.check(log, "0.1.0-alpha.2"), [])
        self.assertEqual(log.links["0.1.0-alpha.2"],
                         "https://github.com/themuffinator/VibeStudio/compare/v0.1.0-alpha.1...v0.1.0-alpha.2")
        self.assertTrue(log.links["Unreleased"].endswith("compare/v0.1.0-alpha.2...HEAD"))
        notes = changelog.notes_text(released)
        self.assertIn("### Fixed", notes)
        self.assertIn("Undo keeps hidden brushes hidden", notes)

    def test_check_reports_problems(self):
        broken = SAMPLE.replace("### Fixed", "### Bugfixes").replace("## [0.1.0-alpha.1] - 2026-10-07", "## [0.1.0-alpha.1]")
        problems = changelog.check(changelog.parse(broken), "9.9.9")
        joined = "\n".join(problems)
        self.assertIn("Bugfixes", joined)
        self.assertIn("needs a release date", joined)
        self.assertIn("No '## [9.9.9]' section", joined)

    def test_out_of_order_versions_fail(self):
        text = SAMPLE.replace(
            "## [0.1.0-alpha.1] - 2026-10-07",
            "## [0.1.0-alpha.1] - 2026-10-07\n\n### Added\n\n- Older.\n\n## [0.2.0] - 2026-10-01",
        )
        problems = changelog.check(changelog.parse(text))
        self.assertTrue(any("out of order" in problem for problem in problems))

    def test_repository_changelog_is_valid(self):
        self.assertEqual(changelog.check(changelog.load(release_meta.CHANGELOG)), [])


class ReleaseTests(unittest.TestCase):
    def test_metadata_for_a_tag_push_and_a_dry_run(self):
        with mock.patch.object(release, "read_version", return_value=Version.parse("0.1.0-alpha.1")):
            tagged = release.resolve("push", "refs/tags/v0.1.0-alpha.1", False, "0123456789abcdef")
            self.assertEqual(tagged["publish"], "true")
            self.assertEqual(tagged["prerelease"], "true")
            self.assertEqual(tagged["label"], "0.1.0-alpha.1")
            self.assertEqual(tagged["update_channel"], "beta")
            dry = release.resolve("workflow_dispatch", "refs/heads/main", False, "0123456789abcdef")
            self.assertEqual(dry["publish"], "false")
            self.assertRegex(dry["label"], r"^0\.1\.0-alpha\.1\+\d{8}\.01234567$")
            self.assertNotIn("+", dry["file_label"])
            with self.assertRaises(ValueError):
                release.resolve("push", "refs/tags/v0.2.0", False, "0123456789abcdef")

    def test_stage_renames_and_checksums(self):
        with tempfile.TemporaryDirectory() as temp:
            artifacts = Path(temp) / "artifacts"
            files = {
                "release-windows/VibeStudio-0.1.0-alpha.1-windows-x64-setup.exe": b"installer",
                "release-windows/vibestudio-0.1.0-alpha.1-win64-x86_64.zip": b"portable",
                "release-windows/vibestudio-0.1.0-alpha.1-win64-x86_64-source.zip": b"runtime source",
                "release-macos/VibeStudio-0.1.0-alpha.1-macos-arm64.dmg": b"dmg",
                "release-linux/VibeStudio-0.1.0-alpha.1-linux-x86_64.AppImage": b"appimage",
                "release-docs/VibeStudio-0.1.0-alpha.1-docs.zip": b"docs",
                "release-source/VibeStudio-0.1.0-alpha.1-source.tar.gz": b"source",
            }
            for name, data in files.items():
                path = artifacts / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            output = Path(temp) / "dist"
            manifest = release.stage(artifacts, output, "0.1.0-alpha.1")
            published = sorted(p.name for p in output.iterdir())
            self.assertIn("VibeStudio-0.1.0-alpha.1-windows-x64-portable.zip", published)
            self.assertIn("VibeStudio-0.1.0-alpha.1-windows-x64-runtime-source.zip", published)
            self.assertIn("SHA256SUMS.txt", published)
            self.assertEqual(len(manifest["assets"]), 7)
            sums = (output / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines()
            self.assertEqual(len(sums), 7)
            self.assertTrue(all(len(line.split("  ")[0]) == 64 for line in sums))
            (artifacts / "release-macos" / "VibeStudio-0.1.0-alpha.1-macos-arm64.dmg").unlink()
            with self.assertRaises(ValueError):
                release.stage(artifacts, output, "0.1.0-alpha.1")

    def test_notes_include_status_downloads_and_checksums(self):
        manifest = {"assets": [{"file": "VibeStudio-0.1.0-alpha.1-docs.zip", "bytes": 2 << 20}]}
        notes = release.render_notes(Version.parse("0.1.0-alpha.1"), "0.1.0-alpha.1", "HEAD", manifest, True)
        self.assertIn("[!WARNING]", notes)
        self.assertIn("VibeStudio-0.1.0-alpha.1-docs.zip` (2.0 MB)", notes)
        self.assertIn("SHA256SUMS.txt", notes)
        self.assertIn("## Build details", notes)


class DocumentationTests(unittest.TestCase):
    def test_offline_guide_is_current(self):
        guide = offline_guide.guide_text(ROOT)
        committed = (ROOT / "docs" / "OFFLINE_USER_GUIDE.md").read_text(encoding="utf-8")
        self.assertEqual(guide, committed, "run python scripts/generate_offline_guide.py")

    def test_offline_guide_rewrites_links(self):
        title, body = offline_guide.page_text("# Title\n\nSee [x](levels.md#a), [y](../ROADMAP.md) and [z](#here).\n"
                                              "```sh\n# not a heading\n```\n", "tour")
        self.assertEqual(title, "Title")
        self.assertIn("## Title", body)
        self.assertIn("](manual/levels.md#a)", body)
        self.assertIn("](ROADMAP.md)", body)
        self.assertIn("](manual/tour.md#here)", body)
        self.assertIn("# not a heading", body)

    def test_every_manual_page_is_in_the_navigation(self):
        nav = json.loads((ROOT / "docs" / "manual" / "manual.json").read_text(encoding="utf-8"))
        listed = {page for section in nav["sections"] for page in section["pages"]}
        pages = {path.stem for path in (ROOT / "docs" / "manual").glob("*.md")}
        self.assertEqual(pages - listed, set())

    def test_site_builds_with_checked_links(self):
        site = importlib.import_module("build_docs_site")
        if site.MISSING_TOOLS is not None:
            self.skipTest("Markdown and Pygments are not installed")
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "site"
            errors = site.build(output, "0.0.0-test", "main")
            self.assertEqual(errors, [])
            index = (output / "index.html").read_text(encoding="utf-8")
            self.assertIn('class="hero"', index)
            self.assertIn('aria-current="page"', index)
            search = (output / "assets" / "search-index.js").read_text(encoding="utf-8")
            self.assertTrue(search.startswith("window.VIBESTUDIO_SEARCH="))
            levels = (output / "levels.html").read_text(encoding="utf-8")
            self.assertIn("chip-partial", levels)
            self.assertNotIn('href="levels.md', levels)

    def test_alerts_become_callouts(self):
        site = importlib.import_module("build_docs_site")
        if site.MISSING_TOOLS is not None:
            self.skipTest("Markdown and Pygments are not installed")
        body, _ = site.render_markdown("# T\n\n> [!TIP]\n> Use **Search**.\n\n<details>\n<summary>More</summary>\n\n| A | B |\n| --- | --- |\n| 1 | Partial |\n\n</details>\n")
        body = site.decorate(body)
        self.assertIn('class="callout callout-tip"', body)
        self.assertIn("<strong>Search</strong>", body)
        self.assertIn('<span class="chip chip-partial">Partial</span>', body)
        self.assertIn("<table>", body)


if __name__ == "__main__":
    unittest.main(verbosity=2)
