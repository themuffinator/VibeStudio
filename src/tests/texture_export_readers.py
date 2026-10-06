#!/usr/bin/env python3
"""Independently verify synthetic export fixtures using optional Pillow.

Run texture-export-smoke with VIBESTUDIO_TEST_CAPTURE_ROOT set to an existing
project-local directory, then pass that directory to this script.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image


def verify(root: Path) -> dict:
    checks = []
    alpha = [255, 128, 0, 32, 64, 255]
    expected = [(x * 90, y * 120, 30 + x + y, alpha[y * 3 + x])
                for y in range(2) for x in range(3)]
    for name in ["export-rgba.png", "export-rgba.tga"]:
        with Image.open(root / name) as source:
            image = source.convert("RGBA")
            assert image.size == (3, 2) and list(image.getdata()) == expected, name
        checks.append(name)
    with Image.open(root / "export-clear.tga") as source:
        assert all(pixel[3] == 0 for pixel in source.convert("RGBA").getdata())
    checks.append("export-clear.tga")
    for name, size, indices in [("export-odd.pcx", (7, 5), list(range(35))),
                                ("export-runs.pcx", (130, 1), [197] * 130)]:
        with Image.open(root / name) as source:
            assert source.size == size, name
            assert list(source.convert("RGB").getdata()) == [(i, i, i) for i in indices], name
        checks.append(name)
    with Image.open(root / "export-color.pcx") as source:
        assert source.mode == "P" and list(source.getdata()) == list(range(35))
        assert source.getpalette() == [c for i in range(256) for c in (i, (i * 3) % 256, 255 - i)]
    checks.append("export-color.pcx")
    for name in ["export-indexed-gray.png", "export-indexed-alpha.png"]:
        with Image.open(root / name) as source:
            source.verify()  # Includes independent chunk CRC checks.
        with Image.open(root / name) as source:
            assert source.mode == "P", name
            if name == "export-indexed-gray.png":
                assert list(source.getdata()) == [197] * 130
                assert source.getpalette() == [i for i in range(256) for _ in range(3)]
            else:
                assert list(source.convert("RGBA").getdata()) == [(7, 7, 7, 64)] * 9
        checks.append(name)
    return {"reader": "Pillow", "version": Image.__version__, "verified": checks, "result": "passed"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixtures", type=Path)
    parser.add_argument("--report", type=Path, help="Optional JSON evidence file")
    args = parser.parse_args()
    if not __debug__:
        parser.error("Run without -O; assertions are the verification checks.")
    report = json.dumps(verify(args.fixtures.resolve()), indent=2) + "\n"
    if args.report:
        args.report.resolve().write_text(report, encoding="utf-8")
    print(report, end="")


if __name__ == "__main__":
    main()
