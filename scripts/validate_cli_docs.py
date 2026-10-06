#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def run_cli_commands(binary: Path, settings_file: Path) -> list[dict]:
    process = subprocess.run(
        [str(binary), "--cli", "--settings-file", str(settings_file), "--json", "cli", "commands"],
        cwd=repo_root(),
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        env=os.environ.copy(),
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(f"cli commands failed with {process.returncode}\n{process.stdout}\n{process.stderr}")
    try:
        payload = json.loads(process.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"cli commands did not emit JSON\n{process.stdout}\n{process.stderr}") from exc
    commands = payload.get("commands", [])
    if not isinstance(commands, list):
        raise RuntimeError("cli commands JSON did not contain a commands array")
    return commands


def command_name(command: dict) -> str:
    return str(command.get("name") or f"{command.get('family', '')} {command.get('command', '')}").strip()


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate CLI command registry coverage in user-facing docs.")
    parser.add_argument("--binary", required=True, help="Path to the built vibestudio executable.")
    args = parser.parse_args()

    root = repo_root()
    binary = Path(args.binary).resolve()
    if not binary.exists():
        print(f"Binary not found: {binary}", file=sys.stderr)
        return 1

    cli_strategy = (root / "docs" / "CLI_STRATEGY.md").read_text(encoding="utf-8")
    readme = (root / "README.md").read_text(encoding="utf-8")

    errors: list[str] = []
    with tempfile.TemporaryDirectory(prefix="vibestudio-cli-registry-") as isolated:
        commands = run_cli_commands(binary, Path(isolated) / "settings.ini")
    seen: set[str] = set()
    for command in commands:
        name = command_name(command)
        if not name:
            errors.append(f"Registered command missing name: {command!r}")
            continue
        if name in seen:
            errors.append(f"Duplicate registered CLI command: {name}")
        seen.add(name)
        examples = command.get("examples", [])
        if not examples:
            errors.append(f"Registered command has no example: {name}")
        for example in examples:
            if "vibestudio --cli" not in str(example):
                errors.append(f"Registered command example is not a vibestudio CLI invocation for {name}: {example}")
        if name not in cli_strategy:
            errors.append(f"docs/CLI_STRATEGY.md is missing registered command: {name}")

    # Every registered command must actually be routed. Running it with no
    # arguments should produce that command's own usage error, never the
    # router's "Unknown VibeStudio CLI subcommand" fallthrough. This is the
    # check that stops a command from being documented but unreachable.
    with tempfile.TemporaryDirectory(prefix="vibestudio-cli-docs-") as isolated:
        settings_file = Path(isolated) / "settings.ini"
        for command in commands:
            family = str(command.get("family") or "").strip()
            action = str(command.get("command") or "").strip()
            if not family or not action:
                continue
            process = subprocess.run(
                [str(binary), "--cli", "--settings-file", str(settings_file), family, action],
                cwd=root,
                text=True,
                encoding="utf-8",
                errors="replace",
                capture_output=True,
                env=os.environ.copy(),
                check=False,
            )
            merged = process.stdout + "\n" + process.stderr
            if "Unknown VibeStudio CLI subcommand" in merged:
                errors.append(f"Registered command is not routed: {family} {action}")

    required_readme_tokens = [
        "CLI Quick Reference",
        "localization targets",
        "diagnostics bundle",
        "ui semantics",
    ]
    for token in required_readme_tokens:
        if token not in readme:
            errors.append(f"README.md is missing CLI coverage token: {token}")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print(f"CLI documentation validation passed for {len(commands)} registered commands.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
