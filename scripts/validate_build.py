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
        map_file = package_root_path / "maps" / "start.map"
        map_file.parent.mkdir(parents=True, exist_ok=True)
        map_file.write_text("// tiny map placeholder\n", encoding="utf-8")
        wad_file = package_root_path / "maps" / "doom.wad"
        wad_file.write_bytes(b"PWAD\x00\x00\x00\x00\x0c\x00\x00\x00")
        manifest_file = package_root_path / "build" / "qbsp-manifest.json"
        extract_dir = package_root_path / "extracted"
        extract_dry_run_dir = package_root_path / "extract-dry-run"
        extract_json_dir = package_root_path / "extract-json"
        steam_root = package_root_path / "steam"
        steam_package = steam_root / "steamapps" / "common" / "Quake" / "id1" / "pak0.pak"
        steam_package.parent.mkdir(parents=True, exist_ok=True)
        steam_package.write_text("pak", encoding="utf-8")
        gog_root = package_root_path / "gog"
        gog_package = gog_root / "Quake III Arena" / "baseq3" / "pak0.pk3"
        gog_package.parent.mkdir(parents=True, exist_ok=True)
        gog_package.write_text("pk3", encoding="utf-8")

        checks = [
            (["--cli", "--version"], args.expected_version),
            (["--cli", "--help"], "Usage: vibestudio --cli"),
            (["--cli", "--about"], "GPLv3"),
            (["--cli", "--json", "about"], '"licenseSummary"'),
            (["--cli", "--exit-codes"], "usage-error"),
            (["--cli", "--json", "--exit-codes"], '"exitCodes"'),
            (["--cli", "--platform-report"], "VibeStudio platform report"),
            (["--cli", "--operation-states"], "Operation states"),
            (["--cli", "--ui-primitives"], "UI primitives"),
            (["--cli", "--package-formats"], "Package archive interfaces"),
            (["--cli", "--check-package-path", "textures/wall.tga"], "Safe: yes"),
            (["--cli", "--info", str(package_root_path)], "Package info"),
            (["--cli", "--json", "--info", str(package_root_path)], '"entryCount"'),
            (["--cli", "--list", str(package_root_path)], "textures/wall.txt"),
            (["--cli", "package", "info", str(package_root_path)], "Package info"),
            (["--cli", "package", "list", str(package_root_path)], "textures/wall.txt"),
            (["--cli", "--json", "package", "list", str(package_root_path)], '"entries"'),
            (["--cli", "--preview-package", str(package_root_path), "--preview-entry", "textures/wall.txt"], "Kind: text"),
            (["--cli", "--json", "package", "preview", str(package_root_path), "textures/wall.txt"], '"kind": "text"'),
            (["--cli", "--validate-package", str(package_root_path)], "Package validation"),
            (["--cli", "--json", "package", "validate", str(package_root_path)], '"validation"'),
            (["--cli", "--extract", str(package_root_path), "--output", str(extract_dry_run_dir), "--extract-entry", "textures/wall.txt", "--dry-run"], "would write"),
            (["--cli", "package", "extract", str(package_root_path), "--output", str(extract_dir), "--entry", "textures/wall.txt"], "wrote"),
            (["--cli", "--json", "package", "extract", str(package_root_path), "--output", str(extract_json_dir), "--entry", "maps/start.map", "--dry-run"], '"outputPath"'),
            (["--cli", "--project-init", str(package_root_path)], "Project manifest saved"),
            (["--cli", "--project-init", str(package_root_path), "--project-editor-profile", "trenchbroom", "--project-ai-free", "on"], "Override editor profile: trenchbroom"),
            (["--cli", "--project-info", str(package_root_path)], "Project health"),
            (["--cli", "--project-validate", str(package_root_path)], "Project health"),
            (["--cli", "project", "info", str(package_root_path)], "Project health"),
            (["--cli", "--json", "project", "info", str(package_root_path)], '"health"'),
            (["--cli", "project", "validate", str(package_root_path)], "Project health"),
            (["--cli", "--studio-report"], "Level Editor"),
            (["--cli", "--compiler-report"], "NetRadiant Custom q3map2"),
            (["--cli", "--compiler-registry"], "Compiler registry"),
            (["--cli", "compiler", "list"], "Compiler registry"),
            (["--cli", "--json", "compiler", "list"], '"tools"'),
            (["--cli", "compiler", "profiles"], "ericw-qbsp"),
            (["--cli", "compiler", "profiles"], "zdbsp-nodes"),
            (["--cli", "compiler", "profiles"], "q3map2-probe"),
            (["--cli", "--json", "compiler", "profiles"], '"profiles"'),
            (["--cli", "compiler", "plan", "ericw-qbsp", "--input", str(map_file)], "Command line:"),
            (["--cli", "--json", "compiler", "plan", "ericw-qbsp", "--input", str(map_file)], '"commandLine"'),
            (["--cli", "compiler", "plan", "zdbsp-nodes", "--input", str(wad_file), "--output", str(package_root_path / "maps" / "doom-nodes.wad")], "zdbsp-nodes"),
            (["--cli", "compiler", "plan", "q3map2-probe"], "-help"),
            (["--cli", "--json", "compiler", "plan", "q3map2-bsp", "--input", str(map_file)], '"q3map2-bsp"'),
            (["--cli", "compiler", "manifest", "ericw-qbsp", "--input", str(map_file), "--manifest", str(manifest_file)], "Manifest saved"),
            (["--cli", "--json", "compiler", "manifest", "q3map2-probe"], '"taskLog"'),
            (["--cli", "--setup-report"], "First-run setup"),
            (["--cli", "--preferences-report"], "Accessibility and language preferences"),
            (["--cli", "--editor-profiles"], "GtkRadiant 1.6.0 Style"),
            (["--cli", "--json", "editor", "profiles"], '"layoutPresetId"'),
            (["--cli", "editor", "current"], "Selected editor profile"),
            (["--cli", "--ai-status"], "AI automation"),
            (["--cli", "ai", "connectors"], "OpenAI"),
            (["--cli", "--json", "ai", "status"], '"aiFreeMode"'),
            (["--cli", "--installations-report"], "Game installations"),
            (["--cli", "install", "list"], "Game installations"),
            (["--cli", "--json", "install", "list"], '"installations"'),
            (["--cli", "--detect-installations", "--detect-install-root", str(steam_root)], "Detected game installations"),
            (["--cli", "--json", "install", "detect", "--root", str(gog_root)], '"candidateCount"'),
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

        if not manifest_file.exists():
            print(f"Compiler manifest was not written: {manifest_file}", file=sys.stderr)
            return 1
        if '"schemaVersion"' not in manifest_file.read_text(encoding="utf-8"):
            print(f"Compiler manifest did not contain a schema version: {manifest_file}", file=sys.stderr)
            return 1
        extracted_wall = extract_dir / "textures" / "wall.txt"
        if extracted_wall.read_text(encoding="utf-8") != "wall":
            print(f"Extracted package entry did not match expected content: {extracted_wall}", file=sys.stderr)
            return 1
        if (extract_dry_run_dir / "textures" / "wall.txt").exists():
            print(f"Dry-run extraction unexpectedly wrote a file: {extract_dry_run_dir}", file=sys.stderr)
            return 1

    print(f"Build validation passed for {binary.name} ({args.expected_version}).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
