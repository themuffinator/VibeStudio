#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


REQUIRED_DIRS = [
    "src/app",
    "src/cli",
    "src/core",
    "src/tests",
    "docs",
    "scripts",
    "samples/projects",
    "external/compilers",
    "i18n",
]

REQUIRED_FILES = [
    "meson.build",
    "src/meson.build",
    "VERSION",
    "README.md",
    "docs/ROADMAP.md",
    "docs/STACK.md",
    "docs/CREDITS.md",
    ".gitmodules",
]

GENERATED_PREFIXES = (
    "builddir/",
    "dist/",
    "packages/",
)

GENERATED_SUFFIXES = (
    ".moc",
    ".obj",
    ".pdb",
    ".ilk",
    ".exe",
)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def fail(message: str) -> int:
    print(message, file=sys.stderr)
    return 1


def git_ls_files(root: Path) -> list[str]:
    result = subprocess.run(["git", "ls-files"], cwd=root, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "git ls-files failed")
    return [line.strip().replace("\\", "/") for line in result.stdout.splitlines() if line.strip()]


def validate_meson_coverage(root: Path) -> list[str]:
    errors: list[str] = []
    meson_text = (root / "src" / "meson.build").read_text(encoding="utf-8")
    for source in sorted((root / "src" / "core").glob("*.cpp")):
        token = f"core/{source.name}"
        if token not in meson_text:
            errors.append(f"Core source missing from src/meson.build: {token}")
    for header in sorted((root / "src" / "core").glob("*.h")):
        token = f"core/{header.name}"
        if token not in meson_text:
            errors.append(f"Core header missing from install_headers: {token}")
    for test in sorted((root / "src" / "tests").glob("*_test.cpp")):
        token = f"tests/{test.name}"
        if token not in meson_text:
            errors.append(f"Smoke test missing from src/meson.build: {token}")
    return errors


def main() -> int:
    root = repo_root()
    errors: list[str] = []

    for relative in REQUIRED_DIRS:
        if not (root / relative).is_dir():
            errors.append(f"Required directory missing: {relative}")
    for relative in REQUIRED_FILES:
        if not (root / relative).is_file():
            errors.append(f"Required file missing: {relative}")

    try:
        tracked = git_ls_files(root)
    except RuntimeError as exc:
        return fail(str(exc))

    for path in tracked:
        lower = path.lower()
        if lower.startswith(GENERATED_PREFIXES):
            errors.append(f"Generated/build artifact is tracked: {path}")
        if lower.endswith(GENERATED_SUFFIXES):
            errors.append(f"Generated binary/object-like file is tracked: {path}")

    errors.extend(validate_meson_coverage(root))

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print("Source layout validation passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
