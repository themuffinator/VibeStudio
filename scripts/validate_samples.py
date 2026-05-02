#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class SampleProject:
    id: str
    root: Path
    package_folder: Path
    compiler_profile: str
    compiler_input: Path


def runtime_environment() -> dict[str, str]:
    env = os.environ.copy()
    if os.name != "nt":
        return env

    candidates: list[Path] = []
    qmake = env.get("QMAKE")
    if qmake:
        candidates.append(Path(qmake).resolve().parent)
    for variable in ("Qt6_DIR", "QTDIR", "QT_DIR"):
        root = env.get(variable)
        if root:
            candidates.extend(path.parent for path in Path(root).glob("**/qmake6.exe"))
    qt_root = Path("C:/Qt")
    if qt_root.exists():
        candidates.extend(path.parent for path in qt_root.glob("**/qmake6.exe"))
    candidates.sort(key=lambda path: (0 if any(token in str(path).lower() for token in ("msvc", "clang")) else 1, str(path).lower()))

    existing = env.get("PATH", "")
    for candidate in candidates:
        if candidate.exists():
            env["PATH"] = f"{candidate}{os.pathsep}{existing}"
            break
    return env


def run_command(command: list[str]) -> tuple[int, str]:
    process = subprocess.run(
        command,
        text=True,
        capture_output=True,
        env=runtime_environment(),
        check=False,
    )
    return process.returncode, f"{process.stdout}\n{process.stderr}"


def fail(message: str) -> int:
    print(message, file=sys.stderr)
    return 1


def load_manifest(sample: SampleProject) -> dict:
    manifest_path = sample.root / ".vibestudio" / "project.json"
    with manifest_path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def validate_manifest_shape(sample: SampleProject) -> list[str]:
    errors: list[str] = []
    manifest_path = sample.root / ".vibestudio" / "project.json"
    if not manifest_path.exists():
        return [f"Missing project manifest: {manifest_path}"]

    manifest = load_manifest(sample)
    if manifest.get("schemaVersion") != 1:
        errors.append(f"{sample.id}: expected schemaVersion 1")
    if manifest.get("projectId") != sample.id:
        errors.append(f"{sample.id}: projectId mismatch")
    for key in ("displayName", "sourceFolders", "packageFolders", "outputFolder", "tempFolder"):
        if key not in manifest:
            errors.append(f"{sample.id}: missing manifest key {key}")

    for source_folder in manifest.get("sourceFolders", []):
        if not (sample.root / source_folder).is_dir():
            errors.append(f"{sample.id}: missing source folder {source_folder}")
    for package_folder in manifest.get("packageFolders", []):
        if not (sample.root / package_folder).is_dir():
            errors.append(f"{sample.id}: missing package folder {package_folder}")
    for folder_key in ("outputFolder", "tempFolder"):
        folder = sample.root / str(manifest.get(folder_key, ""))
        if not folder.is_dir():
            errors.append(f"{sample.id}: missing {folder_key} {folder}")
    if not sample.compiler_input.exists():
        errors.append(f"{sample.id}: missing compiler smoke input {sample.compiler_input}")
    return errors


def validate_cli(sample: SampleProject, binary: Path) -> list[str]:
    checks = [
        (["--cli", "project", "info", str(sample.root)], "Project health"),
        (["--cli", "--json", "project", "validate", str(sample.root)], '"health"'),
        (["--cli", "package", "info", str(sample.package_folder)], "Package info"),
        (["--cli", "compiler", "plan", sample.compiler_profile, "--input", str(sample.compiler_input)], "Command line:"),
    ]

    errors: list[str] = []
    for args, expected in checks:
        code, output = run_command([str(binary), *args])
        if code != 0:
            errors.append(f"{sample.id}: command failed {' '.join(args)}\n{output}")
            continue
        if expected not in output:
            errors.append(f"{sample.id}: expected {expected!r} from {' '.join(args)}\n{output}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate VibeStudio sample projects.")
    parser.add_argument("--binary", help="Optional built vibestudio executable used for CLI smoke checks.")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    samples_root = repo_root / "samples" / "projects"
    samples = [
        SampleProject(
            "sample-doom-minimal",
            samples_root / "doom-minimal",
            samples_root / "doom-minimal" / "packages",
            "zdbsp-nodes",
            samples_root / "doom-minimal" / "maps" / "doom_stub.wad",
        ),
        SampleProject(
            "sample-quake-minimal",
            samples_root / "quake-minimal",
            samples_root / "quake-minimal" / "packages",
            "ericw-qbsp",
            samples_root / "quake-minimal" / "maps" / "start.map",
        ),
        SampleProject(
            "sample-quake3-minimal",
            samples_root / "quake3-minimal",
            samples_root / "quake3-minimal" / "packages",
            "q3map2-bsp",
            samples_root / "quake3-minimal" / "maps" / "sample_q3.map",
        ),
    ]

    errors: list[str] = []
    for sample in samples:
        errors.extend(validate_manifest_shape(sample))

    if args.binary:
        binary = Path(args.binary).resolve()
        if not binary.exists():
            return fail(f"Binary not found: {binary}")
        for sample in samples:
            errors.extend(validate_cli(sample, binary))

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print(f"Validated {len(samples)} VibeStudio sample projects.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
