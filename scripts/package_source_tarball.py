#!/usr/bin/env python3
"""Write the release's complete source archive, compiler submodules included.

    python scripts/package_source_tarball.py --output dist

Every file Git tracks, in this repository and its submodules, goes into
VibeStudio-<version>-source.tar.gz under one top-level folder. Owners,
permissions and timestamps are normalised (to the commit time), so the same
commit always produces the same archive contents.
"""
from __future__ import annotations

import argparse
import gzip
import io
import subprocess
import sys
import tarfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from release_meta import ROOT, asset_names, read_version


def tracked_files() -> list[str]:
    result = subprocess.run(["git", "ls-files", "-z", "--recurse-submodules"], cwd=ROOT, capture_output=True, check=True)
    return sorted(name for name in result.stdout.decode("utf-8").split("\0") if name)


def commit_time() -> int:
    result = subprocess.run(["git", "log", "-1", "--format=%ct"], cwd=ROOT, capture_output=True, text=True, check=True)
    return int(result.stdout.strip() or 0)


def build(output: Path, label: str) -> Path:
    name = asset_names(label)["source"]
    prefix = name.removesuffix(".tar.gz")
    output.mkdir(parents=True, exist_ok=True)
    target = output / name
    mtime = commit_time()
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w", format=tarfile.PAX_FORMAT) as tar:
        for relative in tracked_files():
            path = ROOT / relative
            if path.is_symlink() or not path.is_file():
                continue  # submodule gitlinks and links are not source files
            info = tarfile.TarInfo(f"{prefix}/{relative}")
            info.size = path.stat().st_size
            info.mtime = mtime
            info.mode = 0o755 if path.stat().st_mode & 0o111 else 0o644
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            with path.open("rb") as handle:
                tar.addfile(info, handle)
    with target.open("wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=mtime) as compressed:
        compressed.write(buffer.getvalue())
    return target


def main() -> int:
    parser = argparse.ArgumentParser(description="Write the complete source archive.")
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--label", help="Version label for the file name (defaults to VERSION).")
    args = parser.parse_args()
    try:
        target = build(args.output, args.label or str(read_version()))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Source archive failed: {error}", file=sys.stderr)
        return 1
    print(target)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
