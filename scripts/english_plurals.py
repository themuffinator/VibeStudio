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

Derivation rules, applied only to the words between %n (or %Ln, the count in
the user's number format) and the next %1..%9 or %L1..%L9 placeholder, so
"%n class(es) loaded from %1 source(s)" changes "class(es)" and leaves
"source(s)" to its own number:

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
PLACEHOLDER = re.compile(r"%L?[1-9]")
COUNT_MARKER = re.compile(r"%L?n")

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
    "%n off-centre joint(s) keep their names on the mirrored half.": (
        "%n off-centre joint keeps its name on the mirrored half.",
        "%n off-centre joints keep their names on the mirrored half.",
    ),
    "%n off-centre joint(s) have no mirror joint. Detect Pairs, enter them, or keep joint names.": (
        "%n off-centre joint has no mirror joint. Detect Pairs, enter it, or keep its name.",
        "%n off-centre joints have no mirror joint. Detect Pairs, enter them, or keep joint names.",
    ),
    "Surface %1 follows the skeleton, but %n new vertices have no vertex to take weights from. Bind them to a joint or to the "
    "nearest bones, or add them as a new surface (--bind-joint, --bind nearest-bones or --new-surface on the command line). "
    "No change was made.": (
        "Surface %1 follows the skeleton, but %n new vertex has no vertex to take weights from. Bind it to a joint or to the "
        "nearest bones, or add it as a new surface (--bind-joint, --bind nearest-bones or --new-surface on the command line). "
        "No change was made.",
        "Surface %1 follows the skeleton, but %n new vertices have no vertex to take weights from. Bind them to a joint or to the "
        "nearest bones, or add them as a new surface (--bind-joint, --bind nearest-bones or --new-surface on the command line). "
        "No change was made.",
    ),
}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


# Counts the modeller writes into sentences. "%1 vertices" reads "1 vertices"
# for one and cannot be translated into languages with more plural forms, so
# these must be tr("%n vert(ex)(ices)", nullptr, count) or %Ln instead.
COUNT_LINT_GLOBS = ("src/app/model_*.cpp", "src/app/model_*.h")
COUNT_NOUNS = (
    "vertices", "faces", "edges", "triangles", "frames", "surfaces", "joints", "keys", "clips", "tags", "weights",
    "influences", "problems", "islands", "materials", "boxes", "components", "samples", "channels", "parts", "files",
    "bytes", "segments", "cuts", "steps", "poses", "members", "skins", "entries", "blockers", "limits", "errors",
    "warnings", "points", "loops", "rings", "shells", "groups", "slots", "images", "textures", "models", "items",
    "issues", "notes", "pixels", "sides", "corners", "bones", "targets", "levels", "brushes", "copies", "previews",
)
# A bare plural noun, or plural markup such as "face(s)" that only %n resolves,
# straight after the placeholder or after one word ("%1 selected faces").
COUNT_LITERAL = re.compile(
    r"%L?[1-9]\s+(?:[A-Za-z-]+\s+)?(?:(?:" + "|".join(COUNT_NOUNS) + r")\b|\w+\((?:s|es)\)|\w+\(\w+\)\(\w+\))", re.IGNORECASE
)
# Any other word ending in "s" in the same places ("%1 brushes", "%1 recovery
# copies"), so a noun missing from COUNT_NOUNS is caught too. Words that end in
# "s" without being a counted plural are listed: verbs after a name ("%1 uses
# %2"), units after a measure ("%1 degrees") and a few others. A new one is
# added here once it is checked not to follow a count.
ANY_PLURAL = re.compile(r"%L?[1-9]\s+(?:([A-Za-z-]+)\s+)?([A-Za-z]+s)\b")
NOT_COUNTED = frozenset((
    # Verbs and other words after a name or a key ("%1 has no mirror joint").
    "is", "has", "was", "does", "uses", "needs", "turns", "adjusts", "loads", "searches", "follows", "exists", "mirrors",
    "moves", "means", "keeps", "takes", "makes", "shows", "stays", "starts", "stops", "opens", "reads", "writes", "as",
    "its", "this", "his", "focus", "axis", "roundness", "always", "controls", "tangents", "less", "yes", "plus", "across",
    "towards", "afterwards",
    # Units after a measure, which read in the plural even for 1.0.
    "fps", "ms", "degrees", "units", "pixels", "seconds", "minutes", "hours", "bytes",
))


def count_literal_problem(literal: str) -> str | None:
    """The part of a literal that puts a count before a fixed plural, if any."""
    found = COUNT_LITERAL.search(literal)
    if found:
        return found.group(0)
    for match in ANY_PLURAL.finditer(literal):
        between, word = match.group(1), match.group(2).lower()
        if between and between.lower() in NOT_COUNTED:
            continue
        if word not in NOT_COUNTED:
            return match.group(0)
    return None


STRING_LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


def joined_literals(text: str) -> list[tuple[int, str]]:
    """(line, text) of every string literal, adjacent literals joined as C++ joins them."""
    result: list[tuple[int, str]] = []
    position = 0
    while True:
        match = STRING_LITERAL.search(text, position)
        if match is None:
            return result
        line = text.count("\n", 0, match.start()) + 1
        parts = [match.group(1)]
        position = match.end()
        while True:
            gap = re.match(r"\s*", text[position:])
            following = STRING_LITERAL.match(text, position + gap.end())
            if following is None:
                break
            parts.append(following.group(1))
            position = following.end()
        result.append((line, "".join(parts)))


def count_literal_problems(root: Path) -> list[str]:
    problems: list[str] = []
    for pattern in COUNT_LINT_GLOBS:
        for path in sorted(root.glob(pattern)):
            text = path.read_text(encoding="utf-8")
            for line, literal in joined_literals(text):
                found = count_literal_problem(literal)
                if found:
                    problems.append(f"{path.relative_to(root).as_posix()}:{line}: \"{found}\" in \"{literal[:90]}\"")
    return problems


def english_forms(source: str) -> tuple[str, str]:
    if source in OVERRIDES:
        return OVERRIDES[source]
    # %Ln is the same count written the user's way ("1,024" or "1.024").
    count = COUNT_MARKER.search(source)
    if count is None:
        return source, source
    head = source[: count.end()]
    rest = source[count.end() :]
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
    if not args.write:
        counts = count_literal_problems(root)
        if counts:
            print("These modeller messages put a count before a fixed plural; use %n or %Ln with a count argument:", file=sys.stderr)
            for line in counts[:60]:
                print(f"  {line}", file=sys.stderr)
            return 1
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
