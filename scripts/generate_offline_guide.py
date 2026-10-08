#!/usr/bin/env python3
"""Generate docs/OFFLINE_USER_GUIDE.md: the user manual as one Markdown file.

The manual's pages (docs/manual, in the order of docs/manual/manual.json) are
joined under one title with their headings moved down a level, and their links
rewritten so they still resolve from docs/. Edit the manual pages, never the
generated guide; ``--check`` fails when the committed guide is stale (it runs
in CI and in scripts/validate_release_assets.py).

Every release also ships the manual as HTML (scripts/build_docs_site.py).
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def read_version(root: Path) -> str:
    return (root / "VERSION").read_text(encoding="utf-8").strip()


LINK_RE = re.compile(r"\]\((?!https?://|mailto:)([^)\s]+)\)")
FENCE_RE = re.compile(r"^\s*(```|~~~)")


def github_slug(title: str) -> str:
    value = re.sub(r"[^\w\- ]", "", title.strip().lower())
    return value.replace(" ", "-")


def rewrite_link(target: str, page: str) -> str:
    """Manual-relative links become docs/-relative ones."""
    if target.startswith("#"):
        return f"manual/{page}.md{target}"
    if target.startswith("../"):
        return target[3:]
    return f"manual/{target}"


def page_text(text: str, page: str) -> tuple[str, str]:
    """Shift headings down a level and rewrite links; returns (title, body)."""
    lines = text.rstrip("\n").split("\n")
    out: list[str] = []
    title = page
    in_fence = False
    for line in lines:
        if FENCE_RE.match(line):
            in_fence = not in_fence
            out.append(line)
            continue
        if not in_fence:
            heading = re.match(r"^(#{1,5}) (.+)$", line)
            if heading:
                if heading.group(1) == "#" and title == page:
                    title = heading.group(2).strip()
                line = "#" + line
            line = LINK_RE.sub(lambda m: "](" + rewrite_link(m.group(1), page) + ")", line)
        out.append(line)
    return title, "\n".join(out)


def guide_text(root: Path) -> str:
    version = read_version(root)
    manual = root / "docs" / "manual"
    config = json.loads((manual / "manual.json").read_text(encoding="utf-8"))
    sections = []
    contents = []
    for section in config["sections"]:
        pages = [p for p in section["pages"] if p != "changelog"]
        if not pages:
            continue
        contents.append(f"- **{section['title']}**")
        for page in pages:
            title, body = page_text((manual / f"{page}.md").read_text(encoding="utf-8"), page)
            contents.append(f"  - [{title}](#{github_slug(title)})")
            sections.append(body)
    header = [
        f"# VibeStudio Offline User Guide ({version})",
        "",
        "The VibeStudio user manual in a single file, for reading offline. It is",
        "generated from the pages in [docs/manual](manual/index.md) by",
        "`scripts/generate_offline_guide.py`; edit those pages, not this file.",
        "Release packages also include the manual as HTML: open",
        "`docs/html/index.html` in a browser.",
        "",
        "> [!WARNING]",
        "> VibeStudio is pre-alpha and highly untested. Work on copies of your files",
        "> and keep backups. [Project status](manual/status.md) explains what is and",
        "> is not tested.",
        "",
        "## Contents",
        "",
        *contents,
        "",
    ]
    return "\n".join(header) + "\n" + "\n\n".join(sections) + "\n"


def main() -> int:
    root = repo_root()
    parser = argparse.ArgumentParser(description="Generate the single-file offline user guide from docs/manual.")
    parser.add_argument("--output", default=str(root / "docs" / "OFFLINE_USER_GUIDE.md"), help="Guide output path.")
    parser.add_argument("--check", action="store_true", help="Fail if the checked-in guide is stale.")
    args = parser.parse_args()

    output_path = Path(args.output)
    content = guide_text(root)
    if args.check:
        if not output_path.exists():
            print(f"Missing offline guide: {output_path}", file=sys.stderr)
            return 1
        existing = output_path.read_text(encoding="utf-8")
        if existing != content:
            print(f"Offline guide is stale: {output_path}; run python scripts/generate_offline_guide.py", file=sys.stderr)
            return 1
        print(f"Offline guide is current: {output_path}")
        return 0

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(content)
    print(f"Generated offline guide: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
