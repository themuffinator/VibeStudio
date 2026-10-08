#!/usr/bin/env python3
"""Keep CHANGELOG.md tidy: add entries, validate it, and cut releases from it.

The changelog follows Keep a Changelog 1.1.0. Changes collect under
``## [Unreleased]`` as they land; cutting a release turns them into a dated
version section and keeps the compare links at the bottom current.

    python scripts/changelog.py add fixed "**Levels:** Undo restores hidden brushes."
    python scripts/changelog.py show                 # the Unreleased entries
    python scripts/changelog.py show --version 0.1.0-alpha.1
    python scripts/changelog.py check                # structure, order, links
    python scripts/changelog.py release              # Unreleased -> [VERSION] - today
    python scripts/changelog.py notes --version X    # one version's entries, for release notes

Entry style: start with the studio area in bold ("**Packages:**"), describe what
changed for the user in one sentence, and link docs when it helps.
See docs/RELEASING.md.
"""
from __future__ import annotations

import argparse
import datetime as dt
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from release_meta import CHANGELOG, TAG_PREFIX, Version, read_version, repository_url

TYPES = ["Added", "Changed", "Deprecated", "Removed", "Fixed", "Security"]
SECTION_RE = re.compile(r"^## \[(?P<label>[^\]]+)\](?: - (?P<date>\d{4}-\d{2}-\d{2}))?\s*$")
TYPE_RE = re.compile(r"^### (?P<type>.+?)\s*$")
LINK_RE = re.compile(r"^\[(?P<label>[^\]]+)\]: (?P<url>\S+)\s*$")
UNRELEASED = "Unreleased"


@dataclass
class Section:
    label: str
    date: str | None = None
    entries: dict[str, list[str]] = field(default_factory=dict)  # type -> bullet blocks
    stray: list[str] = field(default_factory=list)                # unexpected lines, for check

    @property
    def is_empty(self) -> bool:
        return not any(self.entries.values())


@dataclass
class Changelog:
    header: list[str]
    sections: list[Section]
    links: dict[str, str]
    problems: list[str]

    def section(self, label: str) -> Section | None:
        for section in self.sections:
            if section.label.lower() == label.lower():
                return section
        return None

    def releases(self) -> list[Section]:
        return [s for s in self.sections if s.label != UNRELEASED]


def parse(text: str) -> Changelog:
    lines = text.splitlines()
    header: list[str] = []
    sections: list[Section] = []
    links: dict[str, str] = {}
    problems: list[str] = []
    current: Section | None = None
    current_type: str | None = None
    for number, line in enumerate(lines, 1):
        link = LINK_RE.match(line)
        if link:
            links[link["label"]] = link["url"]
            continue
        match = SECTION_RE.match(line)
        if match:
            current = Section(match["label"], match["date"])
            sections.append(current)
            current_type = None
            continue
        if current is None:
            header.append(line)
            continue
        kind = TYPE_RE.match(line)
        if kind:
            current_type = kind["type"]
            if current_type not in TYPES:
                problems.append(f"line {number}: '### {current_type}' is not one of: {', '.join(TYPES)}")
            current.entries.setdefault(current_type, [])
            continue
        if not line.strip():
            continue
        if line.startswith("- "):
            if current_type is None:
                problems.append(f"line {number}: entry outside a '### Type' heading in [{current.label}]")
                current.stray.append(line)
                continue
            current.entries[current_type].append(line[2:].rstrip())
        elif line.startswith("  ") and current_type is not None and current.entries.get(current_type):
            current.entries[current_type][-1] += "\n" + line.rstrip()
        else:
            problems.append(f"line {number}: unexpected text in [{current.label}]: {line.strip()[:60]}")
            current.stray.append(line)
    while header and not header[-1].strip():
        header.pop()
    return Changelog(header, sections, links, problems)


def render(log: Changelog) -> str:
    out: list[str] = list(log.header)
    for section in log.sections:
        out += ["", f"## [{section.label}]" + (f" - {section.date}" if section.date else "")]
        for kind in TYPES + [k for k in section.entries if k not in TYPES]:
            bullets = section.entries.get(kind)
            if not bullets:
                continue
            out += ["", f"### {kind}", ""]
            out += [f"- {bullet}" for bullet in bullets]
    if log.links:
        out.append("")
        out += [f"[{label}]: {url}" for label, url in log.links.items()]
    return "\n".join(out).rstrip() + "\n"


def compare_links(log: Changelog) -> dict[str, str]:
    """Unreleased compares against the newest release; each release against the one before."""
    links: dict[str, str] = {}
    releases = log.releases()
    if releases:
        links[UNRELEASED] = repository_url(f"/compare/{TAG_PREFIX}{releases[0].label}...HEAD")
    else:
        links[UNRELEASED] = repository_url("/commits/main")
    for index, section in enumerate(releases):
        tag = f"{TAG_PREFIX}{section.label}"
        if index + 1 < len(releases):
            links[section.label] = repository_url(f"/compare/{TAG_PREFIX}{releases[index + 1].label}...{tag}")
        else:
            links[section.label] = repository_url(f"/releases/tag/{tag}")
    return links


def load(path: Path) -> Changelog:
    return parse(path.read_text(encoding="utf-8"))


def save(path: Path, log: Changelog) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(render(log))


def check(log: Changelog, release: str | None = None) -> list[str]:
    problems = list(log.problems)
    if not log.header or log.header[0].strip() != "# Changelog":
        problems.append("The file must start with '# Changelog'.")
    labels = [s.label for s in log.sections]
    if not labels or labels[0] != UNRELEASED:
        problems.append("The first section must be '## [Unreleased]'.")
    if labels.count(UNRELEASED) > 1:
        problems.append("There must be exactly one Unreleased section.")
    seen: set[str] = set()
    previous: Version | None = None
    for section in log.releases():
        try:
            version = Version.parse(section.label)
        except ValueError:
            problems.append(f"[{section.label}] is not a Semantic Versioning version.")
            continue
        if version.build:
            problems.append(f"[{section.label}] must not carry build metadata.")
        if section.label in seen:
            problems.append(f"[{section.label}] appears twice.")
        seen.add(section.label)
        if not section.date:
            problems.append(f"[{section.label}] needs a release date: '## [{section.label}] - YYYY-MM-DD'.")
        else:
            try:
                dt.date.fromisoformat(section.date)
            except ValueError:
                problems.append(f"[{section.label}] has an invalid date: {section.date}")
        if previous is not None and version.sort_key() >= previous.sort_key():
            problems.append(f"[{section.label}] is out of order: versions run newest first.")
        previous = version
        if section.is_empty:
            problems.append(f"[{section.label}] has no entries.")
    for section in log.sections:
        for kind, bullets in section.entries.items():
            if not bullets:
                problems.append(f"[{section.label}] has an empty '### {kind}' heading.")
            for bullet in bullets:
                if not bullet.strip():
                    problems.append(f"[{section.label}] has an empty entry under {kind}.")
    expected = compare_links(log)
    for label, url in expected.items():
        if log.links.get(label) != url:
            problems.append(f"Link for [{label}] should be: [{label}]: {url}  (python scripts/changelog.py links)")
    for label in log.links:
        if label not in expected:
            problems.append(f"Link [{label}] has no matching section.")
    try:
        current = read_version()
        newest = log.releases()[0].label if log.releases() else None
        if newest and Version.parse(newest).sort_key() > current.sort_key():
            problems.append(f"VERSION ({current}) is older than the newest changelog release ({newest}).")
    except ValueError as error:
        problems.append(str(error))
    if release:
        section = log.section(release)
        if section is None:
            problems.append(f"No '## [{release}]' section: run 'python scripts/changelog.py release' before publishing.")
        elif section.is_empty:
            problems.append(f"[{release}] has no entries to publish.")
    return problems


def notes_text(section: Section) -> str:
    out: list[str] = []
    for kind in TYPES:
        bullets = section.entries.get(kind)
        if bullets:
            out += [f"### {kind}", ""] + [f"- {bullet}" for bullet in bullets] + [""]
    return "\n".join(out).rstrip() + "\n" if out else ""


def main() -> int:
    parser = argparse.ArgumentParser(description="Maintain CHANGELOG.md.")
    parser.add_argument("--file", type=Path, default=CHANGELOG, help=argparse.SUPPRESS)
    commands = parser.add_subparsers(dest="command", required=True)
    add = commands.add_parser("add", help="Add an entry under Unreleased.")
    add.add_argument("type", type=str.capitalize, choices=TYPES)
    add.add_argument("text", nargs="+")
    show = commands.add_parser("show", help="Print one section's entries.")
    show.add_argument("--version", default=UNRELEASED)
    chk = commands.add_parser("check", help="Validate structure, ordering and links.")
    chk.add_argument("--release", help="Also require a non-empty section for this version.")
    rel = commands.add_parser("release", help="Turn Unreleased into a dated version section.")
    rel.add_argument("--version", help="Defaults to VERSION.")
    rel.add_argument("--date", default=dt.date.today().isoformat())
    commands.add_parser("links", help="Rewrite the compare links at the bottom.")
    notes = commands.add_parser("notes", help="Print a version's entries for release notes.")
    notes.add_argument("--version", required=True)
    args = parser.parse_args()

    log = load(args.file)
    if args.command == "add":
        section = log.section(UNRELEASED)
        if section is None:
            print("changelog: no Unreleased section.", file=sys.stderr)
            return 1
        section.entries.setdefault(args.type, []).append(" ".join(args.text).strip())
        save(args.file, log)
        print(f"Added to Unreleased > {args.type}.")
        return 0
    if args.command == "show":
        section = log.section(args.version)
        if section is None:
            print(f"changelog: no [{args.version}] section.", file=sys.stderr)
            return 1
        sys.stdout.write(notes_text(section) or "(no entries)\n")
        return 0
    if args.command == "check":
        problems = check(log, args.release)
        if problems:
            print("CHANGELOG.md needs attention:", file=sys.stderr)
            for problem in problems:
                print(f"  - {problem}", file=sys.stderr)
            return 1
        print(f"CHANGELOG.md is valid ({len(log.releases())} releases).")
        return 0
    if args.command == "links":
        log.links = compare_links(log)
        save(args.file, log)
        return 0
    if args.command == "release":
        version = Version.parse(args.version) if args.version else read_version()
        if version.build:
            print("changelog: release versions carry no build metadata.", file=sys.stderr)
            return 1
        label = str(version)
        if log.section(label):
            print(f"changelog: [{label}] already exists.", file=sys.stderr)
            return 1
        unreleased = log.section(UNRELEASED)
        if unreleased is None or unreleased.is_empty:
            print("changelog: Unreleased has no entries; add some before cutting a release.", file=sys.stderr)
            return 1
        try:
            dt.date.fromisoformat(args.date)
        except ValueError:
            print(f"changelog: invalid date {args.date}", file=sys.stderr)
            return 1
        released = Section(label, args.date, {k: v for k, v in unreleased.entries.items() if v})
        unreleased.entries = {}
        log.sections.insert(log.sections.index(unreleased) + 1, released)
        log.links = compare_links(log)
        save(args.file, log)
        print(f"Moved Unreleased into [{label}] - {args.date}.")
        return 0
    if args.command == "notes":
        section = log.section(args.version)
        if section is None:
            print(f"changelog: no [{args.version}] section.", file=sys.stderr)
            return 1
        sys.stdout.write(notes_text(section))
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
