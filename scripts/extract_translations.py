#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path


SOURCE_EXTENSIONS = {".cpp", ".h"}
TRANSLATABLE_RE = re.compile(
    r"\b(?:tr|translate|QT_TR_NOOP|QT_TR_NOOP3|QT_TRANSLATE_NOOP|QT_TRANSLATE_NOOP3)\s*\("
)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def expected_catalogs(root: Path) -> list[Path]:
    return sorted((root / "i18n").glob("vibestudio_*.ts"))


def source_files(root: Path) -> list[Path]:
    files: list[Path] = []
    for path in sorted((root / "src").rglob("*")):
        if path.is_file() and path.suffix in SOURCE_EXTENSIONS:
            files.append(path)
    return files


def translatable_call_count(files: list[Path]) -> int:
    count = 0
    for path in files:
        count += len(TRANSLATABLE_RE.findall(path.read_text(encoding="utf-8", errors="replace")))
    return count


def qmake_bin_dir() -> Path | None:
    for name in ("qmake6", "qmake"):
        qmake = shutil.which(name)
        if not qmake:
            continue
        result = subprocess.run(
            [qmake, "-query", "QT_INSTALL_BINS"],
            text=True,
            encoding="utf-8",
            errors="replace",
            capture_output=True,
            check=False,
        )
        if result.returncode == 0 and result.stdout.strip():
            return Path(result.stdout.strip())
    return None


def find_lupdate() -> Path | None:
    env_tool = os.environ.get("LUPDATE")
    if env_tool and Path(env_tool).is_file():
        return Path(env_tool)
    for env_dir in ("QT_BIN_DIR", "QTDIR", "QT_DIR"):
        value = os.environ.get(env_dir)
        if not value:
            continue
        candidates = [Path(value)]
        if env_dir != "QT_BIN_DIR":
            candidates.append(Path(value) / "bin")
        for directory in candidates:
            for name in ("lupdate", "lupdate6", "lupdate.exe", "lupdate6.exe"):
                tool = directory / name
                if tool.is_file():
                    return tool
    for name in ("lupdate", "lupdate6"):
        found = shutil.which(name)
        if found:
            return Path(found)
    bin_dir = qmake_bin_dir()
    if bin_dir:
        for name in ("lupdate", "lupdate6", "lupdate.exe", "lupdate6.exe"):
            tool = bin_dir / name
            if tool.is_file():
                return tool
    return None


def message_count(ts_path: Path) -> int:
    try:
        tree = ET.parse(ts_path)
    except ET.ParseError as exc:
        raise RuntimeError(f"Invalid TS file {ts_path}: {exc}") from exc
    return len(tree.findall(".//message"))


def run_lupdate(lupdate: Path, root: Path, catalogs: list[Path], write: bool) -> dict:
    if write:
        target_paths = catalogs
        temp_dir: tempfile.TemporaryDirectory[str] | None = None
    else:
        temp_dir = tempfile.TemporaryDirectory(prefix="vibestudio-lupdate-")
        temp_root = Path(temp_dir.name)
        target_paths = [temp_root / catalog.name for catalog in catalogs]

    try:
        command = [
            str(lupdate),
            str(root / "src"),
            "-extensions",
            "cpp,h",
            "-locations",
            "relative",
            "-no-obsolete",
            "-ts",
            *[str(path) for path in target_paths],
        ]
        result = subprocess.run(
            command,
            cwd=root,
            text=True,
            encoding="utf-8",
            errors="replace",
            capture_output=True,
            check=False,
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"lupdate failed with {result.returncode}\n{result.stdout}\n{result.stderr}"
            )
        counts = {path.name: message_count(path) for path in target_paths if path.exists()}
        return {
            "command": command,
            "stdout": result.stdout.strip(),
            "stderr": result.stderr.strip(),
            "messageCounts": counts,
            "minimumMessageCount": min(counts.values()) if counts else 0,
        }
    finally:
        if temp_dir is not None:
            temp_dir.cleanup()


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate or run the Qt Linguist translation extraction workflow.")
    parser.add_argument("--check", action="store_true", help="Validate source/catalog coverage. This is the default when --write is not used.")
    parser.add_argument("--dry-run", action="store_true", help="Run lupdate against temporary TS files without modifying i18n.")
    parser.add_argument("--write", action="store_true", help="Run lupdate against the checked-in i18n catalogs.")
    parser.add_argument("--require-tool", action="store_true", help="Fail when lupdate cannot be found.")
    parser.add_argument("--json", action="store_true", help="Emit JSON instead of text.")
    args = parser.parse_args()

    root = repo_root()
    catalogs = expected_catalogs(root)
    sources = source_files(root)
    lupdate = find_lupdate()
    call_count = translatable_call_count(sources)
    errors: list[str] = []

    if len(catalogs) < 21:
        errors.append("Expected 20 target catalogs plus pseudo-localization catalog in i18n/.")
    if not any(catalog.name == "vibestudio_pseudo.ts" for catalog in catalogs):
        errors.append("Expected i18n/vibestudio_pseudo.ts.")
    if len(sources) < 20:
        errors.append("Expected C++ source/header files under src/ for extraction.")
    if call_count < 20:
        errors.append("Expected at least 20 Qt translation API call sites in source.")
    if args.require_tool and lupdate is None:
        errors.append("Qt lupdate was not found. Put Qt bin on PATH or set LUPDATE/QT_BIN_DIR/QTDIR/QT_DIR.")

    lupdate_report: dict | None = None
    should_run_lupdate = args.write or args.dry_run
    if should_run_lupdate and lupdate is not None and not errors:
        try:
            lupdate_report = run_lupdate(lupdate, root, catalogs, args.write)
            if lupdate_report["minimumMessageCount"] < 20:
                errors.append("lupdate extracted too few messages from the source tree.")
        except RuntimeError as exc:
            errors.append(str(exc))
    elif should_run_lupdate and lupdate is None and not args.require_tool:
        errors.append("lupdate dry-run requested but Qt lupdate was not found.")

    payload = {
        "ok": not errors,
        "catalogCount": len(catalogs),
        "sourceFileCount": len(sources),
        "translationCallSiteCount": call_count,
        "lupdate": str(lupdate) if lupdate else "",
        "lupdateAvailable": lupdate is not None,
        "lupdateReport": lupdate_report,
        "errors": errors,
    }

    if args.json:
        print(json.dumps(payload, indent=2))
    elif errors:
        for error in errors:
            print(error, file=sys.stderr)
    else:
        print(
            "Translation extraction validation passed "
            f"({len(catalogs)} catalogs, {len(sources)} source files, {call_count} translation call sites)."
        )
        if lupdate_report:
            print(f"lupdate dry-run minimum messages: {lupdate_report['minimumMessageCount']}")

    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
