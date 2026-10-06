#!/usr/bin/env python3
"""Generate or check the English plural forms in i18n/vibestudio_en.ts.

VibeStudio's source strings mark plurals the Qt way, for example
tr("%n item(s)", nullptr, count). Qt only resolves %n into a proper
singular or plural through a translator, so without an English catalog the
studio shows "1 item(s)". This script asks lupdate for every plural message
(-pluralonly), derives the English singular and plural from the markup in the
source text, and writes them into the English catalog, keeping any other
messages the catalog already holds.

The forms are filled in where lupdate put each message, so its message order
and relative source locations survive, and lconvert from the same Qt bin
directory writes the file back in lupdate's own format. Running
scripts/extract_translations.py --write afterwards then leaves the file alone.

Derivation rules, applied only to the words between %n and the next %1..%9
placeholder, so "%n class(es) loaded from %1 source(s)" changes "class(es)"
and leaves "source(s)" to its own number:

  word(a)(b)  -> word + a  /  word + b      entr(y)(ies), vert(ex)(ices)
  word(s)     -> word      /  word + s
  word(es)    -> word      /  word + es
  bare plural -> singular from a small word list ("%n entries" -> "%n entry")

A handful of sentences also need their verb changed; those are listed in
OVERRIDES by exact source text.
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from extract_translations import find_lupdate  # noqa: E402

PAIR_MARKER = re.compile(r"\((\w+)\)\((\w+)\)")
SINGLE_MARKER = re.compile(r"\((s|es)\)")
PLACEHOLDER = re.compile(r"%[1-9]")

# Bare plurals that follow %n in a source string, and their singular.
SINGULAR_WORDS = {
    "entries": "entry",
    "files": "file",
    "warnings": "warning",
    "profiles": "profile",
    "presets": "preset",
    "paths": "path",
    "buckets": "bucket",
    "blockers": "blocker",
    "brushes": "brush",
    "entities": "entity",
    "linedefs": "linedef",
    "things": "thing",
    "candidates": "candidate",
    "primitives": "primitive",
    "sectors": "sector",
    "textures": "texture",
    "stages": "stage",
    "classes": "class",
    "definitions": "definition",
    "issues": "issue",
    "items": "item",
    "images": "image",
    "models": "model",
    "sounds": "sound",
    "shaders": "shader",
    "surfaces": "surface",
    "triangles": "triangle",
    "vertices": "vertex",
    "changes": "change",
}

# Sentences whose singular needs more than the noun changed.
OVERRIDES = {
    "%n staged package change(s) have not been written to an archive. Close anyway?": (
        "%n staged package change has not been written to an archive. Close anyway?",
        "%n staged package changes have not been written to an archive. Close anyway?",
    ),
    "%n staged package change(s) have not been written. Close the package and discard them?": (
        "%n staged package change has not been written. Close the package and discard it?",
        "%n staged package changes have not been written. Close the package and discard them?",
    ),
    "MISSING TEXTURES [%1]\n%n name(s) referenced by this map are not in %2.": (
        "MISSING TEXTURES [%1]\n%n name referenced by this map is not in %2.",
        "MISSING TEXTURES [%1]\n%n names referenced by this map are not in %2.",
    ),
    "TEXTURES [%1]\nAll %n referenced name(s) resolve against %2.": (
        "TEXTURES [%1]\nThe one referenced name resolves against %2.",
        "TEXTURES [%1]\nAll %n referenced names resolve against %2.",
    ),
    "ENTITIES [%1]\nAll %n entit(y)(ies) match the loaded definitions.": (
        "ENTITIES [%1]\nThe one entity matches the loaded definitions.",
        "ENTITIES [%1]\nAll %n entities match the loaded definitions.",
    ),
}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def english_forms(source: str) -> tuple[str, str]:
    if source in OVERRIDES:
        return OVERRIDES[source]
    position = source.find("%n")
    if position < 0:
        return source, source
    head = source[: position + 2]
    rest = source[position + 2 :]
    boundary = PLACEHOLDER.search(rest)
    segment = rest[: boundary.start()] if boundary else rest
    tail = rest[boundary.start() :] if boundary else ""

    singular = PAIR_MARKER.sub(lambda match: match.group(1), segment)
    plural = PAIR_MARKER.sub(lambda match: match.group(2), segment)
    singular = SINGLE_MARKER.sub("", singular)
    plural = SINGLE_MARKER.sub(lambda match: match.group(1), plural)

    if singular == plural:
        words = re.split(r"(\W+)", segment)
        for index, word in enumerate(words):
            replacement = SINGULAR_WORDS.get(word.lower())
            if replacement:
                if word[:1].isupper():
                    replacement = replacement[:1].upper() + replacement[1:]
                words[index] = replacement
                singular = "".join(words)
                break
    return head + singular + tail, head + plural + tail


def plural_messages(lupdate: Path, root: Path) -> list[tuple[str, str]]:
    """(context, source) for every plural message lupdate finds under src/."""
    with tempfile.TemporaryDirectory(prefix="vibestudio-plurals-") as temp_dir:
        target = Path(temp_dir) / "plurals.ts"
        command = [
            str(lupdate),
            str(root / "src"),
            "-extensions",
            "cpp,h",
            "-locations",
            "none",
            "-pluralonly",
            "-target-language",
            "en",
            "-ts",
            str(target),
        ]
        result = subprocess.run(command, cwd=root, text=True, encoding="utf-8", errors="replace", capture_output=True, check=False)
        if result.returncode != 0:
            raise RuntimeError(f"lupdate failed with {result.returncode}\n{result.stdout}\n{result.stderr}")
        tree = ET.parse(target)
    messages: list[tuple[str, str]] = []
    for context in tree.getroot().findall("context"):
        name = context.findtext("name") or ""
        for message in context.findall("message"):
            if message.get("numerus") == "yes":
                messages.append((name, message.findtext("source") or ""))
    return messages


def catalog_forms(catalog: Path) -> dict[tuple[str, str], list[str]]:
    forms: dict[tuple[str, str], list[str]] = {}
    if not catalog.exists():
        return forms
    tree = ET.parse(catalog)
    for context in tree.getroot().findall("context"):
        name = context.findtext("name") or ""
        for message in context.findall("message"):
            if message.get("numerus") != "yes":
                continue
            translation = message.find("translation")
            if translation is None or translation.get("type") == "unfinished":
                continue
            forms[(name, message.findtext("source") or "")] = [form.text or "" for form in translation.findall("numerusform")]
    return forms


def find_lconvert(lupdate: Path) -> Path | None:
    for name in ("lconvert", "lconvert6", "lconvert.exe", "lconvert6.exe"):
        tool = lupdate.parent / name
        if tool.is_file():
            return tool
    found = shutil.which("lconvert") or shutil.which("lconvert6")
    return Path(found) if found else None


def make_locations_absolute(root: ET.Element) -> None:
    # A relative <location> names its file only when that differs from the
    # previous location's, and counts its line from the previous line in that
    # file, so it is only right in lupdate's exact message order. Absolute ones
    # stay right while plural messages are dropped or added around them.
    current_file = ""
    last_line: dict[str, int] = {}
    for location in root.iter("location"):
        current_file = location.get("filename") or current_file
        location.set("filename", current_file)
        line = location.get("line") or ""
        if line[:1] in ("+", "-"):
            last_line[current_file] = last_line.get(current_file, 0) + int(line)
            location.set("line", str(last_line[current_file]))


def fill_plural_forms(message: ET.Element, source: str) -> None:
    translation = message.find("translation")
    if translation is None:
        translation = ET.SubElement(message, "translation")
    translation.clear()
    singular, plural = english_forms(source)
    ET.SubElement(translation, "numerusform").text = singular
    ET.SubElement(translation, "numerusform").text = plural


def write_catalog(catalog: Path, messages: list[tuple[str, str]], lconvert: Path | None) -> None:
    # Plural entries are regenerated from source so they never go stale; every
    # other message is kept as it is, where lupdate put it.
    wanted: dict[str, list[str]] = {}
    for name, source in messages:
        wanted.setdefault(name, []).append(source)

    root = ET.parse(catalog).getroot() if catalog.exists() else ET.Element("TS", {"version": "2.1", "language": "en"})
    make_locations_absolute(root)
    contexts = {context.findtext("name") or "": context for context in root.findall("context")}
    for name, context in contexts.items():
        pending = wanted.get(name, [])
        for message in context.findall("message"):
            if message.get("numerus") != "yes":
                continue
            source = message.findtext("source") or ""
            if source in pending:
                fill_plural_forms(message, source)
                pending.remove(source)
            else:
                context.remove(message)
    # Plurals lupdate has not put in the catalog yet go at the end of their
    # context; the next lupdate run moves them into source order.
    for name, sources in wanted.items():
        for source in dict.fromkeys(sources):
            context = contexts.get(name)
            if context is None:
                context = contexts[name] = ET.SubElement(root, "context")
                ET.SubElement(context, "name").text = name
            message = ET.SubElement(context, "message", {"numerus": "yes"})
            ET.SubElement(message, "source").text = source
            fill_plural_forms(message, source)
    for context in contexts.values():
        if context.find("message") is None:
            root.remove(context)

    ET.indent(root, space="    ")
    text = '<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n' + ET.tostring(root, encoding="unicode") + "\n"
    if lconvert is None:
        catalog.write_text(text, encoding="utf-8")
        return
    # lconvert writes TS files exactly as lupdate does. The staging file sits
    # next to the catalog so the relative source paths resolve the same way.
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", suffix=".ts", dir=catalog.parent, delete=False) as staging:
        staging.write(text)
    try:
        command = [str(lconvert), "-i", staging.name, "-o", str(catalog), "-locations", "relative", "-sort-contexts"]
        result = subprocess.run(command, text=True, encoding="utf-8", errors="replace", capture_output=True, check=False)
        if result.returncode != 0:
            raise RuntimeError(f"lconvert failed with {result.returncode}\n{result.stdout}\n{result.stderr}")
    finally:
        Path(staging.name).unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate or check English plural forms in i18n/vibestudio_en.ts.")
    parser.add_argument("--write", action="store_true", help="Rewrite the plural entries of the English catalog.")
    parser.add_argument("--require-tool", action="store_true", help="Fail when lupdate cannot be found.")
    args = parser.parse_args()

    root = repo_root()
    catalog = root / "i18n" / "vibestudio_en.ts"
    lupdate = find_lupdate()
    if lupdate is None:
        if args.require_tool:
            print("Qt lupdate was not found; English plural forms cannot be checked.", file=sys.stderr)
            return 1
        print("Qt lupdate was not found; skipping the English plural forms check.")
        return 0

    messages = plural_messages(lupdate, root)
    if args.write:
        write_catalog(catalog, messages, find_lconvert(lupdate))
        print(f"Wrote {len(messages)} English plural message(s) to {catalog.relative_to(root)}.")
        return 0

    existing = catalog_forms(catalog)
    missing = [f"{name}: {source}" for name, source in messages if len(existing.get((name, source), [])) != 2]
    if missing:
        print("English plural forms are missing for these messages; run python scripts/english_plurals.py --write:", file=sys.stderr)
        for line in missing[:40]:
            print(f"  {line}", file=sys.stderr)
        return 1
    print(f"English plural forms cover {len(messages)} plural message(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
