#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def run_command(command: list[str]) -> tuple[int, str, str]:
    process = subprocess.run(
        command,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        env=os.environ.copy(),
        check=False,
    )
    return process.returncode, process.stdout, process.stderr


def tiny_bmp_bytes() -> bytes:
    width = 2
    height = 2
    row_size = ((width * 3 + 3) // 4) * 4
    pixel_data = (
        b"\x00\x00\xff\x00\xff\x00" + b"\x00\x00"
        + b"\xff\x00\x00\xff\xff\xff" + b"\x00\x00"
    )
    file_size = 14 + 40 + len(pixel_data)
    return (
        b"BM"
        + struct.pack("<IHHI", file_size, 0, 0, 54)
        + struct.pack("<IIIHHIIIIII", 40, width, height, 1, 24, 0, len(pixel_data), 2835, 2835, 0, 0)
        + pixel_data
    )


def tiny_wav_bytes() -> bytes:
    samples = bytes.fromhex("0000ff7f00800100")
    sample_rate = 11025
    return (
        b"RIFF"
        + struct.pack("<I", 36 + len(samples))
        + b"WAVEfmt "
        + struct.pack("<IHHIIHH", 16, 1, 1, sample_rate, sample_rate * 2, 2, 16)
        + b"data"
        + struct.pack("<I", len(samples))
        + samples
    )


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
        unicode_root = package_root_path / "Spaced Unicode Ω Project"
        unicode_file = unicode_root / "textures" / "Müller wall Ω.txt"
        unicode_file.parent.mkdir(parents=True, exist_ok=True)
        unicode_file.write_text("unicode_path_token", encoding="utf-8")
        unicode_map_file = unicode_root / "maps" / "start map Ω.map"
        unicode_map_file.parent.mkdir(parents=True, exist_ok=True)
        unicode_map_file.write_text(
            """{
"classname" "worldspawn"
{
( 0 0 0 ) ( 64 0 0 ) ( 64 64 0 ) WALL1 0 0 0 1 1
}
}
""",
            encoding="utf-8",
        )
        image_file = package_root_path / "textures" / "wall.bmp"
        image_file.write_bytes(tiny_bmp_bytes())
        map_file = package_root_path / "maps" / "start.map"
        map_file.parent.mkdir(parents=True, exist_ok=True)
        map_file.write_text(
            """{
"classname" "worldspawn"
"wad" "C:\\Users\\Mapper\\quake\\id1\\gfx.wad"
{
( 0 0 0 ) ( 128 0 0 ) ( 128 128 0 ) WALL1 0 0 0 1 1
( 0 0 16 ) ( 128 128 16 ) ( 128 0 16 ) CEIL1 0 0 0 1 1
}
}
{
"classname" "light"
"origin" "32 32 64"
"targetname" "lamp"
}
""",
            encoding="utf-8",
        )
        shader_file = package_root_path / "scripts" / "common.shader"
        shader_file.parent.mkdir(parents=True, exist_ok=True)
        shader_file.write_text(
            """textures/base/wall
{
	qer_editorimage textures/base/wall_editor
	{
		map textures/base/wall
		rgbGen identity
	}
	{
		map textures/base/glow
		blendFunc GL_ONE GL_ONE
	}
}
""",
            encoding="utf-8",
        )
        shader_edit_file = package_root_path / "scripts" / "common-edited.shader"
        for texture_name in ("wall.tga", "wall_editor.tga", "glow.tga"):
            texture_path = package_root_path / "textures" / "base" / texture_name
            texture_path.parent.mkdir(parents=True, exist_ok=True)
            texture_path.write_text("texture", encoding="utf-8")
        map_edit_file = package_root_path / "maps" / "start-edited.map"
        map_move_file = package_root_path / "maps" / "start-moved.map"
        map_dry_run_file = package_root_path / "maps" / "start-dry-run.map"
        script_file = package_root_path / "scripts" / "autoexec.cfg"
        script_file.parent.mkdir(parents=True, exist_ok=True)
        script_file.write_text("set milestone6_find_token 1\n", encoding="utf-8")
        audio_file = package_root_path / "sound" / "pickup.wav"
        audio_file.parent.mkdir(parents=True, exist_ok=True)
        audio_file.write_bytes(tiny_wav_bytes())
        wad_file = package_root_path / "maps" / "doom.wad"
        wad_file.write_bytes(b"PWAD\x00\x00\x00\x00\x0c\x00\x00\x00")
        manifest_file = package_root_path / "build" / "qbsp-manifest.json"
        stage_package_root = package_root_path / "stage-package"
        stage_package_file = stage_package_root / "textures" / "panel.txt"
        stage_package_file.parent.mkdir(parents=True, exist_ok=True)
        stage_package_file.write_text("panel", encoding="utf-8")
        stage_source_file = package_root_path / "stage-sources" / "autoexec.cfg"
        stage_source_file.parent.mkdir(parents=True, exist_ok=True)
        stage_source_file.write_text("echo staged\n", encoding="utf-8")
        stage_manifest = package_root_path / "stage.manifest.json"
        stage_pak = package_root_path / "stage-rebuilt.pak"
        stage_pak_manifest = package_root_path / "stage-rebuilt.pak.manifest.json"
        stage_dry_run_pak = package_root_path / "stage-dry-run.pak"
        stage_pk3 = package_root_path / "stage-rebuilt.pk3"
        extract_dir = package_root_path / "extracted"
        extract_dry_run_dir = package_root_path / "extract-dry-run"
        extract_json_dir = package_root_path / "extract-json"
        diagnostics_dir = package_root_path / "diagnostics"
        asset_convert_dir = package_root_path / "asset-converted"
        asset_convert_dry_run_dir = package_root_path / "asset-converted-dry-run"
        asset_wav_output = package_root_path / "pickup-copy.wav"
        asset_wav_dry_run_output = package_root_path / "pickup-copy-dry-run.wav"
        steam_root = package_root_path / "steam"
        steam_package = steam_root / "steamapps" / "common" / "Quake" / "id1" / "pak0.pak"
        steam_package.parent.mkdir(parents=True, exist_ok=True)
        steam_package.write_text("pak", encoding="utf-8")
        gog_root = package_root_path / "gog"
        gog_package = gog_root / "Quake III Arena" / "baseq3" / "pak0.pk3"
        gog_package.parent.mkdir(parents=True, exist_ok=True)
        gog_package.write_text("pk3", encoding="utf-8")
        extension_root = package_root_path / "extensions" / "sample"
        extension_root.mkdir(parents=True, exist_ok=True)
        extension_manifest = extension_root / "vibestudio.extension.json"
        extension_manifest.write_text(
            """{
  "schemaVersion": 1,
  "id": "sample-tools",
  "displayName": "Sample Tools",
  "version": "1.0.0",
  "trustLevel": "workspace",
  "sandbox": "staged-files",
  "capabilities": ["shader", "sprite"],
  "commands": [{
    "id": "make-file",
    "displayName": "Make File",
    "program": "echo",
    "arguments": ["generated"],
    "generatedFiles": [{"virtualPath": "scripts/generated.shader", "summary": "Generated shader"}]
  }]
}
""",
            encoding="utf-8",
        )

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
            (["--cli", "--json", "ui", "semantics"], '"shortcutRegistryOk": true'),
            (["--cli", "--package-formats"], "Package archive interfaces"),
            (["--cli", "--check-package-path", "textures/wall.tga"], "Safe: yes"),
            (["--cli", "--info", str(package_root_path)], "Package info"),
            (["--cli", "package", "info", str(unicode_root)], "Package info"),
            (["--cli", "package", "list", str(unicode_root)], "Müller wall Ω.txt"),
            (["--cli", "--json", "--info", str(package_root_path)], '"entryCount"'),
            (["--cli", "--list", str(package_root_path)], "textures/wall.txt"),
            (["--cli", "package", "info", str(package_root_path)], "Package info"),
            (["--cli", "package", "list", str(package_root_path)], "textures/wall.txt"),
            (["--cli", "--json", "package", "list", str(package_root_path)], '"entries"'),
            (["--cli", "--json", "package", "info", str(package_root_path)], '"packageOpenMs"'),
            (["--cli", "--preview-package", str(package_root_path), "--preview-entry", "textures/wall.txt"], "Kind: text"),
            (["--cli", "--json", "package", "preview", str(package_root_path), "textures/wall.txt"], '"kind": "text"'),
            (["--cli", "--json", "package", "preview", str(package_root_path), "textures/wall.txt"], '"previewMs"'),
            (["--cli", "asset", "inspect", str(package_root_path), "textures/wall.bmp"], "Kind: image"),
            (["--cli", "--json", "asset", "inspect", str(package_root_path), "sound/pickup.wav"], '"kind": "audio"'),
            (["--cli", "asset", "convert", str(package_root_path), "--entry", "textures/wall.bmp", "--output", str(asset_convert_dry_run_dir), "--format", "png", "--resize", "1x1", "--dry-run"], "Would write converted image"),
            (["--cli", "asset", "convert", str(package_root_path), "--entry", "textures/wall.bmp", "--output", str(asset_convert_dir), "--format", "png", "--resize", "1x1"], "Wrote converted image"),
            (["--cli", "asset", "audio-wav", str(package_root_path), "sound/pickup.wav", "--output", str(asset_wav_dry_run_output), "--dry-run"], "Would write WAV file"),
            (["--cli", "--json", "asset", "audio-wav", str(package_root_path), "sound/pickup.wav", "--output", str(asset_wav_output)], '"written": true'),
            (["--cli", "asset", "find", str(package_root_path), "--find", "milestone6_find_token"], "Matches: 1"),
            (["--cli", "asset", "find", str(unicode_root), "--find", "unicode_path_token"], "Matches: 1"),
            (["--cli", "asset", "replace", str(package_root_path), "--find", "milestone6_find_token", "--replace", "milestone6_replace_token", "--dry-run"], "Save state: modified"),
            (["--cli", "asset", "replace", str(package_root_path), "--find", "milestone6_find_token", "--replace", "milestone6_replace_token", "--write"], "Save state: saved"),
            (["--cli", "map", "inspect", str(map_file), "--select", "entity:1"], "Level map"),
            (["--cli", "--json", "map", "inspect", str(map_file)], '"format": "quake-map"'),
            (["--cli", "map", "edit", str(map_file), "--entity", "1", "--set", "targetname=lamp_cli", "--output", str(map_dry_run_file), "--dry-run"], "Level map save-as"),
            (["--cli", "map", "edit", str(map_file), "--entity", "1", "--set", "targetname=lamp_cli", "--output", str(map_edit_file)], "Written: yes"),
            (["--cli", "map", "move", str(map_edit_file), "--object", "entity:1", "--delta", "16,0,0", "--output", str(map_move_file)], "Written: yes"),
            (["--cli", "--json", "map", "compile-plan", str(map_move_file), "--profile", "ericw-qbsp"], '"commandLine"'),
            (["--cli", "shader", "inspect", str(shader_file), "--package", str(package_root_path)], "Shader script"),
            (["--cli", "--json", "shader", "inspect", str(shader_file), "--package", str(package_root_path)], '"referenceValidation"'),
            (["--cli", "shader", "set-stage", str(shader_file), "--shader", "textures/base/wall", "--stage", "1", "--directive", "blendFunc", "--value", "GL_DST_COLOR GL_ZERO", "--output", str(shader_edit_file)], "Written: yes"),
            (["--cli", "sprite", "plan", "--engine", "doom", "--name", "TROO", "--frames", "2", "--rotations", "8"], "TROOA1"),
            (["--cli", "--json", "sprite", "plan", "--engine", "quake", "--name", "torch", "--frames", "3"], '"frames"'),
            (["--cli", "code", "index", str(package_root_path), "--find", "worldspawn"], "Code workspace"),
            (["--cli", "--json", "code", "index", str(package_root_path), "--find", "textures/base/wall"], '"languageHooks"'),
            (["--cli", "extension", "discover", str(package_root_path / "extensions")], "Sample Tools"),
            (["--cli", "--json", "extension", "inspect", str(extension_manifest)], '"sandboxModel"'),
            (["--cli", "extension", "run", str(extension_manifest), "make-file", "--dry-run"], "scripts/generated.shader"),
            (["--cli", "--validate-package", str(package_root_path)], "Package validation"),
            (["--cli", "--json", "package", "validate", str(package_root_path)], '"validation"'),
            (["--cli", "--extract", str(package_root_path), "--output", str(extract_dry_run_dir), "--extract-entry", "textures/wall.txt", "--dry-run"], "would write"),
            (["--cli", "package", "extract", str(package_root_path), "--output", str(extract_dir), "--entry", "textures/wall.txt"], "wrote"),
            (["--cli", "--json", "package", "extract", str(package_root_path), "--output", str(extract_json_dir), "--entry", "maps/start.map", "--dry-run"], '"outputPath"'),
            (["--cli", "--json", "package", "stage", str(stage_package_root), "--add-file", str(stage_source_file), "--as", "scripts/autoexec.cfg"], '"afterEntries"'),
            (["--cli", "package", "manifest", str(stage_package_root), "--output", str(stage_manifest), "--add-file", str(stage_source_file), "--as", "scripts/autoexec.cfg"], "Package staging manifest exported"),
            (["--cli", "package", "save-as", str(stage_package_root), str(stage_pak), "--format", "pak", "--add-file", str(stage_source_file), "--as", "scripts/autoexec.cfg", "--manifest", str(stage_pak_manifest)], "SHA-256"),
            (["--cli", "--json", "package", "save-as", str(stage_package_root), str(stage_dry_run_pak), "--format", "pak", "--add-file", str(stage_source_file), "--as", "scripts/autoexec.cfg", "--dry-run"], '"dryRun": true'),
            (["--cli", "--json", "package", "save-as", str(stage_package_root), str(stage_pk3), "--format", "pk3", "--add-file", str(stage_source_file), "--as", "scripts/autoexec.cfg"], '"succeeded": true'),
            (["--cli", "package", "validate", str(stage_pak)], "Validation: usable"),
            (["--cli", "package", "validate", str(stage_pk3)], "Validation: usable"),
            (["--cli", "--project-init", str(package_root_path)], "Project manifest saved"),
            (["--cli", "--project-init", str(unicode_root)], "Project manifest saved"),
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
            (["--cli", "compiler", "plan", "ericw-qbsp", "--input", str(unicode_map_file)], "Command line:"),
            (["--cli", "--json", "compiler", "plan", "ericw-qbsp", "--input", str(map_file)], '"commandLine"'),
            (["--cli", "compiler", "plan", "zdbsp-nodes", "--input", str(wad_file), "--output", str(package_root_path / "maps" / "doom-nodes.wad")], "zdbsp-nodes"),
            (["--cli", "compiler", "plan", "q3map2-probe"], "-help"),
            (["--cli", "--json", "compiler", "plan", "q3map2-bsp", "--input", str(map_file)], '"q3map2-bsp"'),
            (["--cli", "compiler", "manifest", "ericw-qbsp", "--input", str(map_file), "--manifest", str(manifest_file)], "Manifest saved"),
            (["--cli", "--json", "compiler", "manifest", "q3map2-probe"], '"taskLog"'),
            (["--cli", "--setup-report"], "First-run setup"),
            (["--cli", "--preferences-report"], "Accessibility and language preferences"),
            (["--cli", "localization", "targets"], "Localization targets"),
            (["--cli", "--json", "localization", "report", "--locale", "ar"], '"untranslatedMessageCount"'),
            (["--cli", "--json", "--localization-report", "--locale", "ur"], '"expansionSmokeOk"'),
            (["--cli", "--json", "localization", "report", "--locale", "ar"], '"pluralizationSmokeOk": true'),
            (["--cli", "--json", "localization", "report", "--locale", "ar"], '"expansionLayoutSmokeOk": true'),
            (["--cli", "diagnostics", "bundle", "--output", str(diagnostics_dir)], "vibestudio-diagnostics.json"),
            (["--cli", "--json", "diagnostics", "bundle"], '"redaction"'),
            (["--cli", "--json", "credits", "validate"], '"compiler-submodule-revision-ericw-tools"'),
            (["--cli", "--editor-profiles"], "GtkRadiant 1.6.0 Style"),
            (["--cli", "--json", "editor", "profiles"], '"commandId"'),
            (["--cli", "editor", "current"], "Selected editor profile"),
            (["--cli", "--ai-status"], "AI automation"),
            (["--cli", "ai", "connectors"], "OpenAI"),
            (["--cli", "--json", "ai", "status"], '"aiFreeMode"'),
            (["--cli", "ai", "shader-scaffold", "--prompt", "glowing gothic wall"], "Shader Scaffold"),
            (["--cli", "ai", "entity-snippet", "--prompt", "trigger starts lift"], "classname"),
            (["--cli", "ai", "package-plan", "--prompt", "validate release", "--package", str(package_root_path)], "package validate"),
            (["--cli", "ai", "batch-recipe", "--prompt", "convert doom sprites"], "asset convert"),
            (["--cli", "--json", "ai", "review", "--kind", "shader", "--prompt", "glowing shader"], "AI Proposal Review"),
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
        for staged_output in (stage_manifest, stage_pak, stage_pak_manifest, stage_pk3):
            if not staged_output.exists():
                print(f"Package staging output was not written: {staged_output}", file=sys.stderr)
                return 1
        if '"schemaVersion"' not in stage_manifest.read_text(encoding="utf-8"):
            print(f"Package staging manifest did not contain a schema version: {stage_manifest}", file=sys.stderr)
            return 1
        if '"operations"' not in stage_pak_manifest.read_text(encoding="utf-8"):
            print(f"Package save-as manifest did not record staged operations: {stage_pak_manifest}", file=sys.stderr)
            return 1
        if stage_dry_run_pak.exists():
            print(f"Package save-as dry-run unexpectedly wrote output: {stage_dry_run_pak}", file=sys.stderr)
            return 1
        if map_dry_run_file.exists():
            print(f"Map edit dry-run unexpectedly wrote output: {map_dry_run_file}", file=sys.stderr)
            return 1
        if '"targetname" "lamp_cli"' not in map_edit_file.read_text(encoding="utf-8"):
            print(f"Map entity edit did not write the expected key/value: {map_edit_file}", file=sys.stderr)
            return 1
        if '"origin" "48 32 64"' not in map_move_file.read_text(encoding="utf-8"):
            print(f"Map entity move did not write the expected origin: {map_move_file}", file=sys.stderr)
            return 1
        if "GL_DST_COLOR GL_ZERO" not in shader_edit_file.read_text(encoding="utf-8"):
            print(f"Shader stage edit did not write the expected blend mode: {shader_edit_file}", file=sys.stderr)
            return 1
        extracted_wall = extract_dir / "textures" / "wall.txt"
        if extracted_wall.read_text(encoding="utf-8") != "wall":
            print(f"Extracted package entry did not match expected content: {extracted_wall}", file=sys.stderr)
            return 1
        if (extract_dry_run_dir / "textures" / "wall.txt").exists():
            print(f"Dry-run extraction unexpectedly wrote a file: {extract_dry_run_dir}", file=sys.stderr)
            return 1
        if (asset_convert_dry_run_dir / "textures" / "wall.png").exists():
            print(f"Image conversion dry-run unexpectedly wrote a file: {asset_convert_dry_run_dir}", file=sys.stderr)
            return 1
        if not (asset_convert_dir / "textures" / "wall.png").exists():
            print(f"Image conversion output was not written: {asset_convert_dir}", file=sys.stderr)
            return 1
        if asset_wav_dry_run_output.exists():
            print(f"WAV export dry-run unexpectedly wrote a file: {asset_wav_dry_run_output}", file=sys.stderr)
            return 1
        if asset_wav_output.read_bytes() != tiny_wav_bytes():
            print(f"WAV export output did not match source: {asset_wav_output}", file=sys.stderr)
            return 1
        if "milestone6_replace_token" not in script_file.read_text(encoding="utf-8"):
            print(f"Asset replace did not update the script fixture: {script_file}", file=sys.stderr)
            return 1

    print(f"Build validation passed for {binary.name} ({args.expected_version}).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
