#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERSION = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
README = ROOT / "README.md"
BADGE_RE = re.compile(r"version-([0-9A-Za-z.\-]+)-")


def updated_readme(text: str) -> str:
    return BADGE_RE.sub(f"version-{VERSION}-", text)


def main() -> int:
    parser = argparse.ArgumentParser(description="Synchronize README version badge with VERSION.")
    parser.add_argument("--check", action="store_true", help="Only check; do not write.")
    args = parser.parse_args()

    original = README.read_text(encoding="utf-8")
    updated = updated_readme(original)
    if args.check:
        if original != updated:
            print("README version badge is out of sync with VERSION.", file=sys.stderr)
            return 1
        return 0

    if original != updated:
        README.write_text(updated, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
