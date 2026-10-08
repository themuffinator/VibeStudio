#!/usr/bin/env python3
"""Keep the README's version badge in step with VERSION.

shields.io static badges separate label, message and colour with single
hyphens, so hyphens and underscores inside the version are doubled
("0.1.0-alpha.1" becomes "0.1.0--alpha.1").
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERSION = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
README = ROOT / "README.md"
BADGE_RE = re.compile(r"(img\.shields\.io/badge/version-)(.+?)(-[0-9A-Fa-f]{6}(?:\?|\"|\)))")


def badge_text(version: str) -> str:
    return version.replace("-", "--").replace("_", "__")


def updated_readme(text: str) -> str:
    return BADGE_RE.sub(lambda m: m.group(1) + badge_text(VERSION) + m.group(3), text)


def main() -> int:
    parser = argparse.ArgumentParser(description="Synchronize README version badge with VERSION.")
    parser.add_argument("--check", action="store_true", help="Only check; do not write.")
    args = parser.parse_args()

    original = README.read_text(encoding="utf-8")
    if not BADGE_RE.search(original):
        print("README.md has no shields.io version badge to keep in sync.", file=sys.stderr)
        return 1
    updated = updated_readme(original)
    if args.check:
        if original != updated:
            print("README version badge is out of sync with VERSION; run python scripts/sync_doc_versions.py.",
                  file=sys.stderr)
            return 1
        return 0

    if original != updated:
        README.write_text(updated, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
