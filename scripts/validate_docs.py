#!/usr/bin/env python3
from __future__ import annotations

import configparser
import re
import subprocess
import sys
from pathlib import Path


REQUIRED_DOCS = [
    "README.md",
    "docs/ARCHITECTURE.md",
    "docs/STACK.md",
    "docs/DEPENDENCIES.md",
    "docs/SUPPORT_MATRIX.md",
    "docs/ROADMAP.md",
    "docs/CONTRIBUTING.md",
    "docs/CREDITS.md",
    "docs/ACCESSIBILITY_LOCALIZATION.md",
    "docs/INITIAL_SETUP.md",
    "docs/COMPILER_INTEGRATION.md",
    "docs/CLI_STRATEGY.md",
]

REQUIRED_CREDIT_TOKENS = [
    "PakFu",
    "GtkRadiant",
    "NetRadiant Custom",
    "TrenchBroom",
    "QuArK",
    "ericw-tools",
    "ZDBSP",
    "ZokumBSP",
]

EXPECTED_SUBMODULES = [
    "external/compilers/ericw-tools",
    "external/compilers/q3map2-nrc",
    "external/compilers/zdbsp",
    "external/compilers/zokumbsp",
]

EXPECTED_ISSUE_TEMPLATES = [
    ".github/ISSUE_TEMPLATE/bug_report.md",
    ".github/ISSUE_TEMPLATE/feature_request.md",
    ".github/ISSUE_TEMPLATE/format_support.md",
    ".github/ISSUE_TEMPLATE/compiler_integration.md",
    ".github/ISSUE_TEMPLATE/attribution_update.md",
    ".github/pull_request_template.md",
]

LINK_RE = re.compile(r"\[[^\]]+\]\(([^)]+)\)")


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def fail(message: str) -> int:
    print(message, file=sys.stderr)
    return 1


def git_ls_files(root: Path) -> set[str]:
    result = subprocess.run(["git", "ls-files"], cwd=root, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "git ls-files failed")
    return {line.strip().replace("\\", "/") for line in result.stdout.splitlines() if line.strip()}


def parse_gitmodules(path: Path) -> list[str]:
    parser = configparser.ConfigParser()
    parser.read(path, encoding="utf-8")
    paths: list[str] = []
    for section in parser.sections():
        if parser.has_option(section, "path"):
            paths.append(parser.get(section, "path").replace("\\", "/"))
    return paths


def local_markdown_links(markdown_path: Path) -> list[Path]:
    links: list[Path] = []
    text = markdown_path.read_text(encoding="utf-8")
    for match in LINK_RE.finditer(text):
        target = match.group(1).strip()
        if not target or target.startswith(("#", "http://", "https://", "mailto:")):
            continue
        if "://" in target:
            continue
        target = target.split("#", 1)[0].strip()
        if not target:
            continue
        if target.startswith("<") and target.endswith(">"):
            target = target[1:-1]
        links.append((markdown_path.parent / target).resolve())
    return links


def main() -> int:
    root = repo_root()
    errors: list[str] = []

    for relative in REQUIRED_DOCS:
        if not (root / relative).is_file():
            errors.append(f"Required document missing: {relative}")

    readme = (root / "README.md").read_text(encoding="utf-8")
    credits = (root / "docs" / "CREDITS.md").read_text(encoding="utf-8")
    if "## Credits" not in readme:
        errors.append("README.md is missing a Credits section.")
    for token in REQUIRED_CREDIT_TOKENS:
        if token not in readme and token not in credits:
            errors.append(f"Credits token missing from README/docs credits: {token}")

    gitmodules_path = root / ".gitmodules"
    if not gitmodules_path.is_file():
        errors.append(".gitmodules is missing.")
    else:
        submodule_paths = parse_gitmodules(gitmodules_path)
        for expected in EXPECTED_SUBMODULES:
            if expected not in submodule_paths:
                errors.append(f"Expected compiler submodule missing from .gitmodules: {expected}")
            if not (root / expected).exists():
                errors.append(f"Expected compiler submodule directory missing: {expected}")

    for relative in EXPECTED_ISSUE_TEMPLATES:
        if not (root / relative).is_file():
            errors.append(f"Expected contribution template missing: {relative}")

    pr_template = (root / ".github" / "pull_request_template.md").read_text(encoding="utf-8") if (root / ".github" / "pull_request_template.md").is_file() else ""
    for token in ("README Credits", "docs/CREDITS.md", "docs/DEPENDENCIES.md", "Accessibility/localization"):
        if token not in pr_template:
            errors.append(f"Pull request checklist missing token: {token}")

    pr_ci = (root / ".github" / "workflows" / "pr-ci.yml").read_text(encoding="utf-8") if (root / ".github" / "workflows" / "pr-ci.yml").is_file() else ""
    nightly = (root / ".github" / "workflows" / "release-nightly.yml").read_text(encoding="utf-8") if (root / ".github" / "workflows" / "release-nightly.yml").is_file() else ""
    if "actions/upload-artifact" not in pr_ci or "retention-days: 14" not in pr_ci or "validate_cli_docs.py" not in pr_ci or "validate_credits.py" not in pr_ci or "extract_translations.py" not in pr_ci:
        errors.append("PR CI must validate CLI docs and upload retained app artifacts.")
    if "schedule:" not in nightly or "retention-days: 30" not in nightly or "validate_cli_docs.py" not in nightly or "validate_credits.py" not in nightly or "extract_translations.py" not in nightly:
        errors.append("Nightly release workflow must schedule, validate, and retain portable artifacts.")

    credits_validation = subprocess.run(
        [sys.executable, str(root / "scripts" / "validate_credits.py")],
        cwd=root,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        check=False,
    )
    if credits_validation.returncode != 0:
        details = (credits_validation.stderr or credits_validation.stdout).strip()
        errors.append(f"Credits validation failed: {details}")

    translation_extraction = subprocess.run(
        [sys.executable, str(root / "scripts" / "extract_translations.py"), "--check"],
        cwd=root,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        check=False,
    )
    if translation_extraction.returncode != 0:
        details = (translation_extraction.stderr or translation_extraction.stdout).strip()
        errors.append(f"Translation extraction validation failed: {details}")

    for markdown in sorted(root.glob("*.md")) + sorted((root / "docs").rglob("*.md")):
        for link in local_markdown_links(markdown):
            try:
                link.relative_to(root)
            except ValueError:
                continue
            if not link.exists():
                errors.append(f"Broken local Markdown link in {markdown.relative_to(root)}: {link.relative_to(root)}")

    i18n_files = sorted((root / "i18n").glob("vibestudio_*.ts"))
    if len(i18n_files) < 21:
        errors.append("Initial translation catalog scaffold must include 20 targets plus pseudo-localization.")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print("Documentation validation passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
