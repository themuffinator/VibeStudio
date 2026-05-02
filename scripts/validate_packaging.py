#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path


REQUIRED_PACKAGE_FILES = [
    "README.md",
    "VERSION",
    "docs/CREDITS.md",
    "docs/OFFLINE_USER_GUIDE.md",
    "docs/PACKAGING.md",
    "licenses/THIRD_PARTY_LICENSES.md",
    "licenses/docs/CREDITS.md",
    "licenses/external/compilers/ericw-tools/COPYING",
    "licenses/external/compilers/q3map2-nrc/LICENSE",
    "licenses/external/compilers/ZDBSP/COPYING",
    "licenses/external/compilers/ZokumBSP/COPYING",
    "platform/README.txt",
    "CHECKSUMS.sha256",
    "samples/README.md",
]


def fail(message: str) -> int:
    print(message, file=sys.stderr)
    return 1


def host_target() -> str:
    if sys.platform.startswith("win"):
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    return "linux"


def main() -> int:
    parser = argparse.ArgumentParser(description="Smoke-test a current-platform portable package.")
    parser.add_argument("--binary", required=True, help="Built vibestudio executable to package.")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    binary = Path(args.binary).resolve()
    if not binary.exists():
        return fail(f"Binary not found: {binary}")

    with tempfile.TemporaryDirectory(prefix="vibestudio-package-") as temp_dir:
        command = [
            sys.executable,
            str(repo_root / "scripts" / "package_portable.py"),
            "--binary",
            str(binary),
            "--output",
            temp_dir,
            "--archive",
            "--target-platform",
            "current",
        ]
        process = subprocess.run(command, text=True, capture_output=True, check=False)
        if process.returncode != 0:
            print(process.stdout, file=sys.stderr)
            print(process.stderr, file=sys.stderr)
            return process.returncode

        staged_dirs = [path for path in Path(temp_dir).iterdir() if path.is_dir()]
        archives = [path for path in Path(temp_dir).iterdir() if path.suffix == ".zip"]
        if len(staged_dirs) != 1:
            return fail(f"Expected one staged package directory, found {len(staged_dirs)}")
        if len(archives) != 1:
            return fail(f"Expected one package archive, found {len(archives)}")

        package_dir = staged_dirs[0]
        manifest_path = package_dir / "package-manifest.json"
        if not manifest_path.exists():
            return fail(f"Missing package manifest: {manifest_path}")
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("schemaVersion") != 2:
            return fail("Expected package manifest schemaVersion 2")
        if manifest.get("targetPlatform") != host_target():
            return fail(f"Expected current target platform {host_target()}, got {manifest.get('targetPlatform')}")
        if manifest.get("externalCompilersBundled") is not False:
            return fail("Expected compiler executable bundling to remain disabled in the portable package")
        if manifest.get("offlineUserGuide") != "docs/OFFLINE_USER_GUIDE.md":
            return fail("Expected offline user guide to be recorded in the manifest")
        if manifest.get("licenseBundle") != "licenses/THIRD_PARTY_LICENSES.md":
            return fail("Expected third-party license bundle to be recorded in the manifest")
        staged_binary = package_dir / "bin" / binary.name
        if not staged_binary.exists():
            return fail(f"Missing staged binary: {staged_binary}")
        for relative in REQUIRED_PACKAGE_FILES:
            if not (package_dir / relative).exists():
                return fail(f"Missing packaged file: {relative}")
        if "package-manifest.json" not in (package_dir / "CHECKSUMS.sha256").read_text(encoding="utf-8"):
            return fail("Expected package manifest to be covered by CHECKSUMS.sha256")

    print("Portable packaging validated.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
