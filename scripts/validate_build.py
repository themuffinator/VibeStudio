#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def run_command(command: list[str]) -> tuple[int, str, str]:
    process = subprocess.run(
        command,
        text=True,
        capture_output=True,
        env=os.environ.copy(),
        check=False,
    )
    return process.returncode, process.stdout, process.stderr


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate a built VibeStudio binary.")
    parser.add_argument("--binary", required=True, help="Path to vibestudio executable.")
    parser.add_argument("--expected-version", required=True, help="Expected version string.")
    args = parser.parse_args()

    binary = Path(args.binary).resolve()
    if not binary.exists():
        print(f"Binary not found: {binary}", file=sys.stderr)
        return 1

    if os.name != "nt":
        binary.chmod(binary.stat().st_mode | 0o111)

    with tempfile.TemporaryDirectory(prefix="vibestudio-package-smoke-") as package_root:
        package_root_path = Path(package_root)
        package_file = package_root_path / "textures" / "wall.txt"
        package_file.parent.mkdir(parents=True, exist_ok=True)
        package_file.write_text("wall", encoding="utf-8")

        checks = [
            (["--cli", "--version"], args.expected_version),
            (["--cli", "--help"], "Usage: vibestudio --cli"),
            (["--cli", "--platform-report"], "VibeStudio platform report"),
            (["--cli", "--operation-states"], "Operation states"),
            (["--cli", "--ui-primitives"], "UI primitives"),
            (["--cli", "--package-formats"], "Package archive interfaces"),
            (["--cli", "--check-package-path", "textures/wall.tga"], "Safe: yes"),
            (["--cli", "--info", str(package_root_path)], "Package info"),
            (["--cli", "--list", str(package_root_path)], "textures/wall.txt"),
            (["--cli", "--studio-report"], "Level Editor"),
            (["--cli", "--compiler-report"], "NetRadiant Custom q3map2"),
            (["--cli", "--setup-report"], "First-run setup"),
            (["--cli", "--preferences-report"], "Accessibility and language preferences"),
        ]

        for cli_args, expected in checks:
            code, out, err = run_command([str(binary), *cli_args])
            merged = f"{out}\n{err}"
            if code != 0:
                print(f"Command failed: {' '.join(cli_args)}", file=sys.stderr)
                print(merged, file=sys.stderr)
                return 1
            if expected not in merged:
                print(f"Expected text not found for {' '.join(cli_args)}: {expected}", file=sys.stderr)
                print(merged, file=sys.stderr)
                return 1

    print(f"Build validation passed for {binary.name} ({args.expected_version}).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
