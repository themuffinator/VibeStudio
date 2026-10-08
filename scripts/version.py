#!/usr/bin/env python3
"""Show, set and bump VibeStudio's version (the VERSION file).

    python scripts/version.py                      # current version and what it implies
    python scripts/version.py json                 # the same, for tools
    python scripts/version.py set 0.2.0-beta.1     # set an exact version
    python scripts/version.py bump pre             # 0.1.0-alpha.1 -> 0.1.0-alpha.2
    python scripts/version.py bump pre --label rc  # 0.1.0-beta.3  -> 0.1.0-rc.1
    python scripts/version.py bump release         # 0.1.0-rc.2    -> 0.1.0
    python scripts/version.py bump minor           # 0.1.0         -> 0.2.0
    python scripts/version.py bump minor --pre alpha  # 0.1.0      -> 0.2.0-alpha.1

Changing the version also refreshes everything that quotes it: the README
badge (scripts/sync_doc_versions.py) and the generated offline guide
(scripts/generate_offline_guide.py). See docs/RELEASING.md.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from release_meta import (PRERELEASE_LABELS, ROOT, Version, asset_names, read_version, release_title, tag_for,
                          write_version)


def bumped(current: Version, part: str, label: str | None, pre: str | None) -> Version:
    if part == "pre":
        current_label, number = current.prerelease_label()
        if not current.is_prerelease:
            raise ValueError("bump pre needs a pre-release version; start one with "
                             "'bump patch|minor|major --pre alpha'.")
        target = label or current_label
        if target not in PRERELEASE_LABELS:
            raise ValueError(f"Pre-release labels are {', '.join(PRERELEASE_LABELS)}; got {target!r}.")
        if target == current_label:
            if number is None:
                raise ValueError(f"Cannot increment {current.prerelease!r}; set the version explicitly.")
            return Version(current.major, current.minor, current.patch, f"{target}.{number + 1}")
        if current_label in PRERELEASE_LABELS and PRERELEASE_LABELS.index(target) < PRERELEASE_LABELS.index(current_label):
            raise ValueError(f"{target} comes before {current_label}; versions only move forward.")
        return Version(current.major, current.minor, current.patch, f"{target}.1")
    if part == "release":
        if not current.is_prerelease:
            raise ValueError(f"{current} is already a final version.")
        return Version(current.major, current.minor, current.patch)
    if part == "major":
        result = Version(current.major + 1, 0, 0)
    elif part == "minor":
        result = Version(current.major, current.minor + 1, 0)
    elif part == "patch":
        result = Version(current.major, current.minor, current.patch + 1)
    else:
        raise ValueError(f"Unknown part: {part}")
    if pre:
        if pre not in PRERELEASE_LABELS:
            raise ValueError(f"Pre-release labels are {', '.join(PRERELEASE_LABELS)}; got {pre!r}.")
        result = Version(result.major, result.minor, result.patch, f"{pre}.1")
    return result


def refresh_dependents() -> None:
    """Rewrite the files that quote the version."""
    for script in ("sync_doc_versions.py", "generate_offline_guide.py"):
        subprocess.run([sys.executable, str(ROOT / "scripts" / script)], cwd=ROOT, check=True)


def describe(version: Version) -> dict:
    return {
        "version": str(version),
        "tag": tag_for(version),
        "title": release_title(version),
        "prerelease": version.is_prerelease,
        "assets": asset_names(str(version.without_build())),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Show, set and bump VibeStudio's version.")
    commands = parser.add_subparsers(dest="command")
    commands.add_parser("show", help="Print the current version and its release names (default).")
    commands.add_parser("json", help="Print the current version and release names as JSON.")
    set_parser = commands.add_parser("set", help="Set an exact Semantic Versioning version.")
    set_parser.add_argument("version")
    bump = commands.add_parser("bump", help="Move the version forward.")
    bump.add_argument("part", choices=["major", "minor", "patch", "pre", "release"])
    bump.add_argument("--label", choices=PRERELEASE_LABELS, help="For 'bump pre': switch to this pre-release label.")
    bump.add_argument("--pre", choices=PRERELEASE_LABELS, help="For major/minor/patch: start a pre-release series.")
    for sub in (set_parser, bump):
        sub.add_argument("--no-refresh", action="store_true", help="Only write VERSION; leave the README badge and offline guide.")
    args = parser.parse_args()

    try:
        current = read_version()
        if args.command in (None, "show"):
            info = describe(current)
            print(f"{info['title']}  ({'pre-release' if info['prerelease'] else 'release'})")
            print(f"tag:    {info['tag']}")
            for role, name in info["assets"].items():
                print(f"{role + ':':<24}{name}")
            return 0
        if args.command == "json":
            print(json.dumps(describe(current), indent=2))
            return 0
        if args.command == "set":
            target = Version.parse(args.version)
            if target.build:
                raise ValueError("Build metadata (+...) is added by CI, never stored in VERSION.")
        else:
            target = bumped(current, args.part, args.label, args.pre)
        if target.sort_key() <= current.sort_key() and args.command == "bump":
            raise ValueError(f"{target} does not come after {current}.")
        write_version(target)
        if not args.no_refresh:
            refresh_dependents()
        print(f"{current} -> {target}")
        print("Next: python scripts/changelog.py release   (then commit, and tag or run the release workflow)")
        return 0
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"version: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
