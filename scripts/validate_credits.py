#!/usr/bin/env python3
from __future__ import annotations

import configparser
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


CREDIT_TOKENS = [
    "PakFu",
    "ericw-tools",
    "NetRadiant Custom",
    "ZDBSP",
    "ZokumBSP",
    "GtkRadiant",
    "TrenchBroom",
    "QuArK",
    "OpenAI",
    "Claude",
    "Gemini",
    "ElevenLabs",
    "Meshy",
]


@dataclass(frozen=True)
class CompilerIntegration:
    id: str
    display_name: str
    engines: str
    role: str
    source_path: str
    upstream_url: str
    pinned_revision: str
    license: str


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def markdown_section(markdown: str, heading: str) -> str:
    start = markdown.find(heading)
    if start < 0:
        return ""
    next_heading = re.search(r"\n##\s+", markdown[start + len(heading) :])
    if not next_heading:
        return markdown[start:]
    end = start + len(heading) + next_heading.start()
    return markdown[start:end]


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise ValueError(f"Could not find function signature: {signature}")
    open_brace = source.find("{", start)
    if open_brace < 0:
        raise ValueError(f"Could not find function body for: {signature}")

    depth = 0
    for index in range(open_brace, len(source)):
        character = source[index]
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1 : index]
    raise ValueError(f"Unclosed function body for: {signature}")


def compiler_integrations(root: Path) -> list[CompilerIntegration]:
    source = read_text(root / "src" / "core" / "studio_manifest.cpp")
    body = function_body(source, "QVector<CompilerIntegration> compilerIntegrations()")
    strings = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
    field_count = len(CompilerIntegration.__dataclass_fields__)
    if len(strings) % field_count != 0:
        raise ValueError(
            "compilerIntegrations() string literal count is not divisible by "
            f"{field_count}: {len(strings)}"
        )
    integrations: list[CompilerIntegration] = []
    for index in range(0, len(strings), field_count):
        fields = strings[index : index + field_count]
        integrations.append(CompilerIntegration(*fields))
    return integrations


def parse_gitmodules(path: Path) -> dict[str, str]:
    parser = configparser.ConfigParser()
    parser.read(path, encoding="utf-8")
    paths: dict[str, str] = {}
    for section in parser.sections():
        if parser.has_option(section, "path") and parser.has_option(section, "url"):
            paths[parser.get(section, "path").replace("\\", "/")] = parser.get(section, "url")
    return paths


def normalize_url(url: str) -> str:
    normalized = url.strip().rstrip("/")
    if normalized.endswith(".git"):
        normalized = normalized[:-4]
    return normalized.lower()


def submodule_status(root: Path) -> dict[str, tuple[str, str]]:
    result = subprocess.run(
        ["git", "submodule", "status", "--recursive"],
        cwd=root,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "git submodule status failed")

    statuses: dict[str, tuple[str, str]] = {}
    for line in result.stdout.splitlines():
        if len(line) < 43:
            continue
        status = line[0]
        revision = line[1:41]
        path = line[42:].split(" ", 1)[0].replace("\\", "/")
        statuses[path] = (status, revision)
    return statuses


def main() -> int:
    root = repo_root()
    errors: list[str] = []

    readme_path = root / "README.md"
    credits_path = root / "docs" / "CREDITS.md"
    manifest_path = root / "src" / "core" / "studio_manifest.cpp"
    gitmodules_path = root / ".gitmodules"

    for path in (readme_path, credits_path, manifest_path, gitmodules_path):
        if not path.is_file():
            errors.append(f"Required credits validation input missing: {path.relative_to(root)}")
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    readme = read_text(readme_path)
    credits = read_text(credits_path)
    readme_credits = markdown_section(readme, "## Credits")
    if not readme_credits:
        errors.append("README.md is missing a Credits section.")

    for token in CREDIT_TOKENS:
        if token not in readme_credits:
            errors.append(f"README Credits section is missing token: {token}")
        if token not in credits:
            errors.append(f"docs/CREDITS.md is missing token: {token}")

    try:
        integrations = compiler_integrations(root)
    except ValueError as error:
        errors.append(str(error))
        integrations = []

    gitmodules = parse_gitmodules(gitmodules_path)
    try:
        statuses = submodule_status(root)
    except RuntimeError as error:
        errors.append(str(error))
        statuses = {}

    for integration in integrations:
        if integration.pinned_revision not in readme:
            errors.append(
                "README.md is missing pinned compiler revision "
                f"{integration.pinned_revision} for {integration.id}."
            )
        if integration.pinned_revision not in credits:
            errors.append(
                "docs/CREDITS.md is missing pinned compiler revision "
                f"{integration.pinned_revision} for {integration.id}."
            )

        gitmodule_url = gitmodules.get(integration.source_path)
        if gitmodule_url is None:
            errors.append(f".gitmodules is missing compiler source path: {integration.source_path}")
        elif normalize_url(gitmodule_url) != normalize_url(integration.upstream_url):
            errors.append(
                ".gitmodules URL mismatch for "
                f"{integration.id}: expected {integration.upstream_url}, found {gitmodule_url}"
            )

        status = statuses.get(integration.source_path)
        if status is None:
            errors.append(f"git submodule status is missing compiler path: {integration.source_path}")
        else:
            state, revision = status
            if state == "-":
                errors.append(f"Compiler submodule is not initialized: {integration.source_path}")
            elif state == "+":
                errors.append(f"Compiler submodule is checked out at a non-index revision: {integration.source_path}")
            if revision.lower() != integration.pinned_revision.lower():
                errors.append(
                    "Compiler submodule revision mismatch for "
                    f"{integration.id}: expected {integration.pinned_revision}, found {revision}"
                )

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print(f"Credits validation passed for {len(integrations)} compiler pins and {len(CREDIT_TOKENS)} credit tokens.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
