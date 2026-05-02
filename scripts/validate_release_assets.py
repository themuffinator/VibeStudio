#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
import tempfile
import time
import zipfile
from pathlib import Path


TARGETS = {"windows", "macos", "linux"}
REQUIRED_LICENSE_FILES = [
    "licenses/THIRD_PARTY_LICENSES.md",
    "licenses/external/compilers/ericw-tools/COPYING",
    "licenses/external/compilers/q3map2-nrc/LICENSE",
    "licenses/external/compilers/ZDBSP/COPYING",
    "licenses/external/compilers/ZokumBSP/COPYING",
]


def fail(message: str) -> int:
    print(message, file=sys.stderr)
    return 1


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def run(command: list[str], *, cwd: Path | None = None, allowed_codes: set[int] | None = None, timeout: int = 30) -> subprocess.CompletedProcess[str]:
    process = subprocess.run(command, cwd=cwd, text=True, capture_output=True, check=False, timeout=timeout)
    if allowed_codes is None:
        allowed_codes = {0}
    if process.returncode not in allowed_codes:
        hint = ""
        if process.returncode == 0xC0000135:
            hint = "\nHint: on Windows this usually means the Qt runtime DLL directory is not on PATH."
        raise RuntimeError(
            f"Command failed ({process.returncode}): {' '.join(command)}{hint}\nSTDOUT:\n{process.stdout}\nSTDERR:\n{process.stderr}"
        )
    return process


def run_json(command: list[str], *, allowed_codes: set[int] | None = None, timeout: int = 30) -> dict:
    process = run(command, allowed_codes=allowed_codes, timeout=timeout)
    try:
        return json.loads(process.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"Command did not emit JSON: {' '.join(command)}\n{process.stdout}\n{process.stderr}") from exc


def le32(value: int) -> bytes:
    return struct.pack("<I", value)


def le16(value: int) -> bytes:
    return struct.pack("<H", value)


def write_pak(path: Path) -> None:
    payload = b"echo release validation\n"
    directory_offset = 12 + len(payload)
    name = b"scripts/autoexec.cfg"
    record = name + (b"\0" * (56 - len(name))) + le32(12) + le32(len(payload))
    path.write_bytes(b"PACK" + le32(directory_offset) + le32(len(record)) + payload + record)


def write_wad(path: Path) -> None:
    first = b"MAPDATA"
    second = b"PLAYPAL"
    first_offset = 12
    second_offset = first_offset + len(first)
    directory_offset = second_offset + len(second)
    data = bytearray(b"PWAD" + le32(2) + le32(directory_offset) + first + second)
    for name, offset, size in ((b"MAP01", first_offset, len(first)), (b"PLAYPAL", second_offset, len(second))):
        data.extend(le32(offset))
        data.extend(le32(size))
        data.extend(name + (b"\0" * (8 - len(name))))
    path.write_bytes(bytes(data))


def write_pk3(path: Path) -> None:
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_STORED) as archive:
        archive.writestr("maps/test.map", "{\n}\n")
        archive.writestr("scripts/test.shader", "textures/test/wall\n")


def create_package_fixtures(root: Path) -> dict[str, Path]:
    fixtures = root / "fixtures"
    folder = fixtures / "folder-package"
    (folder / "textures").mkdir(parents=True)
    (folder / "maps").mkdir(parents=True)
    (folder / "textures" / "wall.txt").write_text("wall\n", encoding="utf-8")
    (folder / "maps" / "start.map").write_text("{\n}\n", encoding="utf-8")

    pak = fixtures / "tiny.pak"
    wad = fixtures / "tiny.wad"
    pk3 = fixtures / "tiny.pk3"
    pak.parent.mkdir(parents=True, exist_ok=True)
    write_pak(pak)
    write_wad(wad)
    write_pk3(pk3)
    return {"folder": folder, "pak": pak, "wad": wad, "pk3": pk3}


def validate_offline_guide(root: Path) -> None:
    run([sys.executable, str(root / "scripts" / "generate_offline_guide.py"), "--check"], cwd=root)


def validate_portable_targets(root: Path, binary: Path, temp_root: Path) -> list[Path]:
    output = temp_root / "packages"
    run(
        [
            sys.executable,
            str(root / "scripts" / "package_portable.py"),
            "--binary",
            str(binary),
            "--output",
            str(output),
            "--archive",
            "--target-platform",
            "all",
        ],
        cwd=root,
        timeout=60,
    )

    package_dirs = sorted(path for path in output.iterdir() if path.is_dir())
    archives = sorted(path for path in output.iterdir() if path.suffix == ".zip")
    if len(package_dirs) != 3 or len(archives) != 3:
        raise RuntimeError(f"Expected three staged directories and three zip archives, found {len(package_dirs)} dirs and {len(archives)} zips")

    seen_targets: set[str] = set()
    for package_dir in package_dirs:
        manifest_path = package_dir / "package-manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        target = manifest.get("targetPlatform")
        if target not in TARGETS:
            raise RuntimeError(f"Unexpected package target {target} in {manifest_path}")
        seen_targets.add(target)
        if manifest.get("schemaVersion") != 2:
            raise RuntimeError(f"Unexpected package manifest schema in {manifest_path}")
        if manifest.get("offlineUserGuide") != "docs/OFFLINE_USER_GUIDE.md":
            raise RuntimeError(f"Offline guide missing from manifest {manifest_path}")
        if manifest.get("licenseBundle") != "licenses/THIRD_PARTY_LICENSES.md":
            raise RuntimeError(f"License bundle missing from manifest {manifest_path}")
        for relative in [
            "bin/" + binary.name,
            "docs/OFFLINE_USER_GUIDE.md",
            "platform/README.txt",
            "package-manifest.json",
            "CHECKSUMS.sha256",
            "samples/README.md",
            *REQUIRED_LICENSE_FILES,
        ]:
            if not (package_dir / relative).exists():
                raise RuntimeError(f"Missing packaged file for {target}: {relative}")
        checksums = (package_dir / "CHECKSUMS.sha256").read_text(encoding="utf-8")
        if "package-manifest.json" not in checksums or "docs/OFFLINE_USER_GUIDE.md" not in checksums:
            raise RuntimeError(f"Checksums do not cover required files in {package_dir}")
    if seen_targets != TARGETS:
        raise RuntimeError(f"Expected targets {sorted(TARGETS)}, got {sorted(seen_targets)}")
    return package_dirs


def validate_package_cli(binary: Path, fixtures: dict[str, Path], temp_root: Path) -> None:
    for fixture in fixtures.values():
        info = run_json([str(binary), "--cli", "--json", "package", "info", str(fixture)], timeout=30)
        if "package" not in info:
            raise RuntimeError(f"Missing package info JSON for {fixture}")
        listing = run_json([str(binary), "--cli", "--json", "package", "list", str(fixture)], timeout=30)
        if "package" not in listing:
            raise RuntimeError(f"Missing package list JSON for {fixture}")
        validation = run_json([str(binary), "--cli", "--json", "package", "validate", str(fixture)], timeout=30)
        if validation.get("ok") is not True:
            raise RuntimeError(f"Package validation failed unexpectedly for {fixture}: {validation}")

    extraction = run_json(
        [
            str(binary),
            "--cli",
            "--json",
            "package",
            "extract",
            str(fixtures["folder"]),
            "--output",
            str(temp_root / "extract-out"),
            "--dry-run",
        ],
        timeout=30,
    )
    if not extraction.get("extraction", {}).get("dryRun"):
        raise RuntimeError("Package extraction dry-run JSON did not report dryRun=true")


def validate_feedback_cli(binary: Path, temp_root: Path) -> None:
    map_path = temp_root / "compile" / "start.map"
    map_path.parent.mkdir(parents=True, exist_ok=True)
    map_path.write_text("{\n}\n", encoding="utf-8")
    compiler = run_json(
        [
            str(binary),
            "--cli",
            "--json",
            "compiler",
            "run",
            "ericw-qbsp",
            str(map_path),
            "--dry-run",
            "--task-state",
        ],
        allowed_codes={0, 4, 5},
        timeout=30,
    )
    if "taskState" not in compiler:
        raise RuntimeError("Compiler dry-run did not emit taskState JSON")

    ai = run_json(
        [
            str(binary),
            "--cli",
            "--json",
            "ai",
            "explain-log",
            "--text",
            "qbsp failed: leak near entity 1",
        ],
        allowed_codes={0, 4, 5},
        timeout=30,
    )
    if "workflow" not in ai:
        raise RuntimeError("AI explain-log did not emit workflow JSON")


def time_command(command: list[str], *, allowed_codes: set[int] | None = None, timeout: int = 30) -> float:
    started = time.perf_counter()
    run(command, allowed_codes=allowed_codes, timeout=timeout)
    return time.perf_counter() - started


def create_timed_folders(root: Path) -> dict[str, Path]:
    counts = {"small": 3, "medium": 50, "large": 200}
    folders: dict[str, Path] = {}
    for name, count in counts.items():
        folder = root / f"{name}-folder"
        folder.mkdir(parents=True, exist_ok=True)
        for index in range(count):
            (folder / f"entry-{index:03}.txt").write_text(f"{name} {index}\n", encoding="utf-8")
        folders[name] = folder
    return folders


def validate_performance_smoke(binary: Path, temp_root: Path, report_path: Path | None) -> None:
    measurements: dict[str, float] = {}
    measurements["startupVersionSeconds"] = time_command([str(binary), "--cli", "--version"], timeout=30)
    for name, folder in create_timed_folders(temp_root / "timing").items():
        measurements[f"packageOpen_{name}_seconds"] = time_command(
            [str(binary), "--cli", "--json", "package", "info", str(folder)],
            timeout=30,
        )

    thresholds = {
        "startupVersionSeconds": 5.0,
        "packageOpen_small_seconds": 8.0,
        "packageOpen_medium_seconds": 10.0,
        "packageOpen_large_seconds": 15.0,
    }
    for key, threshold in thresholds.items():
        if measurements[key] > threshold:
            raise RuntimeError(f"Release timing smoke exceeded {threshold:.1f}s for {key}: {measurements[key]:.3f}s")

    if report_path:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps({"measurements": measurements}, indent=2) + "\n", encoding="utf-8")


def validate_static_release_checks(root: Path) -> None:
    workflow = (root / ".github" / "workflows" / "pr-ci.yml").read_text(encoding="utf-8")
    for token in ("windows-latest", "macos-latest", "ubuntu-latest", "validate_release_assets.py"):
        if token not in workflow:
            raise RuntimeError(f"PR CI workflow is missing release validation token: {token}")

    shell = (root / "src" / "app" / "application_shell.cpp").read_text(encoding="utf-8")
    settings = (root / "src" / "core" / "studio_settings.cpp").read_text(encoding="utf-8")
    accessibility = (root / "docs" / "ACCESSIBILITY_LOCALIZATION.md").read_text(encoding="utf-8")
    setup = (root / "docs" / "INITIAL_SETUP.md").read_text(encoding="utf-8")
    for label, haystack, tokens in [
        ("high-visibility theme audit", shell + accessibility, ["HighContrastDark", "HighContrastLight", "200%"]),
        ("keyboard and accessibility audit", shell + accessibility, ["setAccessibleName", "keyboard-only", "DetailDrawer"]),
        ("localization audit", shell + accessibility, ["QLocale", "pseudo-localization", "Arabic", "Urdu"]),
        ("TTS smoke path", shell + setup + accessibility, ["textToSpeechEnabled", "TTS", "test phrase"]),
        ("task history persistence", shell + settings, ["recentActivityTasks", "persistActivityTask", "Compiler Run"]),
        ("loading/progress/detail coverage", shell, ["LoadingPane", "setProgress", "Raw Task"]),
    ]:
        missing = [token for token in tokens if token not in haystack]
        if missing:
            raise RuntimeError(f"Static release check failed for {label}; missing {missing}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate MVP release packaging, smoke fixtures, feedback surfaces, and release docs.")
    parser.add_argument("--binary", required=True, help="Built vibestudio executable to validate.")
    parser.add_argument("--report", help="Optional JSON path for release timing measurements.")
    args = parser.parse_args()

    root = repo_root()
    binary = Path(args.binary).resolve()
    if not binary.exists():
        return fail(f"Binary not found: {binary}")
    report_path = Path(args.report).resolve() if args.report else None

    try:
        with tempfile.TemporaryDirectory(prefix="vibestudio-release-") as temp_dir:
            temp_root = Path(temp_dir)
            validate_offline_guide(root)
            validate_portable_targets(root, binary, temp_root)
            fixtures = create_package_fixtures(temp_root)
            validate_package_cli(binary, fixtures, temp_root)
            validate_feedback_cli(binary, temp_root)
            validate_performance_smoke(binary, temp_root, report_path)
            validate_static_release_checks(root)
    except Exception as exc:  # noqa: BLE001 - command-line smoke reports concise failure.
        return fail(f"Release asset validation failed: {exc}")

    print("Release assets validated for Windows, macOS, and Linux package targets.")
    if report_path:
        print(f"Release timing report: {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
