"""Shared release metadata: the version in VERSION and the names derived from it.

VERSION holds one Semantic Versioning 2.0 version (``MAJOR.MINOR.PATCH`` with an
optional ``-prerelease`` label). Everything a release needs to be called (the
Git tag, the GitHub release title, every download's file name) is derived here,
so the workflow, the packaging scripts and the documentation cannot drift.
See docs/RELEASING.md.
"""
from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION_FILE = ROOT / "VERSION"
CHANGELOG = ROOT / "CHANGELOG.md"
CURATED_NOTES = ROOT / "docs" / "releases"

PROJECT = "VibeStudio"
REPOSITORY = "themuffinator/VibeStudio"
TAG_PREFIX = "v"
PRERELEASE_LABELS = ("alpha", "beta", "rc")

SEMVER_RE = re.compile(
    r"^(?P<major>0|[1-9]\d*)\.(?P<minor>0|[1-9]\d*)\.(?P<patch>0|[1-9]\d*)"
    r"(?:-(?P<pre>[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?"
    r"(?:\+(?P<build>[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)

# Every published download, keyed by the role a build job gives it. The release
# workflow and docs/manual/install.md both rely on these exact names.
ASSETS = {
    "windows-installer": ("{prefix}-windows-x64-setup.exe", "Windows 10/11 installer (x64)"),
    "windows-portable": ("{prefix}-windows-x64-portable.zip", "Windows portable ZIP (x64)"),
    "windows-runtime-source": ("{prefix}-windows-x64-runtime-source.zip",
                               "Source for the Qt and audio runtime bundled with the Windows builds"),
    "macos-dmg": ("{prefix}-macos-arm64.dmg", "macOS disk image (Apple silicon)"),
    "linux-appimage": ("{prefix}-linux-x86_64.AppImage", "Linux AppImage (x86_64)"),
    "docs": ("{prefix}-docs.zip", "Offline HTML documentation"),
    "source": ("{prefix}-source.tar.gz", "Complete source, including compiler submodules"),
}


@dataclass(frozen=True)
class Version:
    major: int
    minor: int
    patch: int
    prerelease: str = ""
    build: str = ""

    @classmethod
    def parse(cls, text: str) -> "Version":
        match = SEMVER_RE.match(text.strip())
        if not match:
            raise ValueError(f"Not a Semantic Versioning 2.0 version: {text.strip()!r}")
        return cls(int(match["major"]), int(match["minor"]), int(match["patch"]),
                   match["pre"] or "", match["build"] or "")

    def __str__(self) -> str:
        text = f"{self.major}.{self.minor}.{self.patch}"
        if self.prerelease:
            text += f"-{self.prerelease}"
        if self.build:
            text += f"+{self.build}"
        return text

    @property
    def core(self) -> str:
        return f"{self.major}.{self.minor}.{self.patch}"

    @property
    def is_prerelease(self) -> bool:
        return bool(self.prerelease)

    def without_build(self) -> "Version":
        return Version(self.major, self.minor, self.patch, self.prerelease)

    def sort_key(self) -> tuple:
        """Semantic Versioning precedence (build metadata ignored)."""
        def identifiers(pre: str) -> tuple:
            parts = []
            for part in pre.split("."):
                parts.append((0, int(part), "") if part.isdigit() else (1, 0, part))
            return tuple(parts)

        pre = (1,) if not self.prerelease else (0, identifiers(self.prerelease))
        return (self.major, self.minor, self.patch, pre)

    def prerelease_label(self) -> tuple[str, int | None]:
        """('alpha', 2) for 'alpha.2'; ('', None) for a final version."""
        if not self.prerelease:
            return "", None
        head, _, tail = self.prerelease.partition(".")
        return head, int(tail) if tail.isdigit() else None


def read_version() -> Version:
    return Version.parse(VERSION_FILE.read_text(encoding="utf-8"))


def write_version(version: Version) -> None:
    with VERSION_FILE.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"{version}\n")


def tag_for(version: Version) -> str:
    return f"{TAG_PREFIX}{version.without_build()}"


def version_from_tag(tag: str) -> Version:
    name = tag.removeprefix("refs/tags/")
    if not name.startswith(TAG_PREFIX):
        raise ValueError(f"Release tags start with {TAG_PREFIX!r}: {tag}")
    return Version.parse(name[len(TAG_PREFIX):])


def asset_prefix(label: str) -> str:
    return f"{PROJECT}-{label}"


def asset_names(label: str) -> dict[str, str]:
    prefix = asset_prefix(label)
    return {role: pattern.format(prefix=prefix) for role, (pattern, _) in ASSETS.items()}


def asset_descriptions() -> dict[str, str]:
    return {role: description for role, (_, description) in ASSETS.items()}


def release_title(version: Version) -> str:
    return f"{PROJECT} {version.without_build()}"


def repository_url(path: str = "") -> str:
    return f"https://github.com/{REPOSITORY}{path}"
