#!/usr/bin/env python3
from __future__ import annotations

import argparse
import bisect
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path


SOURCE_EXTENSIONS = {".cpp", ".h"}
TRANSLATABLE_RE = re.compile(
    r"\b(?:tr|translate|QT_TR_NOOP|QT_TR_NOOP3|QT_TR_N_NOOP|QT_TRANSLATE_NOOP|QT_TRANSLATE_NOOP3|QT_TRANSLATE_N_NOOP|QT_TRANSLATE_N_NOOP3)\s*\("
)

# ---------------------------------------------------------------------------
# Literals lupdate cannot see
#
# lupdate reads the literal written inside tr(), QCoreApplication::translate()
# or a QT_*_NOOP marker. It does not follow a literal into a function, so a
# helper such as
#
#     QString mapText(const char* source)
#     {
#         return QCoreApplication::translate("VibeStudioLevelMap", source);
#     }
#
# translates at run time while every mapText("...") literal stays out of the
# catalogs. The scan below finds such helpers: functions, named lambdas and
# macros that hand one of their parameters to a translation call, directly or
# through another helper. Each literal given to one must be translated in place
# or marked QT_TRANSLATE_NOOP with the helper's context. lupdate's
# -tr-function-alias is no way out, because it files the literal under the
# wrong context.
#
# The scan also rejects translation calls lupdate would misread: a source built
# from an expression, a context that is not a literal, and %n plurals with no
# count or behind a marker that does not mark plurals.
# ---------------------------------------------------------------------------

CPP_TOKEN_RE = re.compile(
    r"""
    (?P<space>\s+)
    | (?P<comment>//[^\n]*|/\*.*?\*/)
    | (?P<directive>\#(?:\\\r?\n|\\.|[^\n\\])*)
    | (?P<string>(?:u8|u|U|L)?R"(?P<delimiter>[^()\\\s"]{0,16})\(.*?\)(?P=delimiter)"
        | (?:u8|u|U|L)?"(?:\\.|[^"\\\n])*")
    | (?P<char>(?:u8|u|U|L)?'(?:\\.|[^'\\\n])*')
    | (?P<ident>[A-Za-z_]\w*)
    | (?P<number>\.?\d(?:[eEpP][+-]|[\w.'])*)
    | (?P<punct>::|->|.)
    """,
    re.S | re.X,
)

# Where lupdate finds the context and the source of each call. A context of
# None comes from the enclosing class.
TRANSLATION_CALLS = {
    "translate": (0, 1),
    "tr": (None, 0),
    "trUtf8": (None, 0),
}
TRANSLATION_QUALIFIERS = {"", "QCoreApplication", "QApplication", "QGuiApplication", "QObject", "qApp"}
# Marker macros and the argument holding their context (None: the class).
NOOP_MARKERS = {
    "QT_TRANSLATE_NOOP": 0,
    "QT_TRANSLATE_NOOP3": 0,
    "QT_TRANSLATE_NOOP_UTF8": 0,
    "QT_TRANSLATE_N_NOOP": 0,
    "QT_TRANSLATE_N_NOOP3": 0,
    "QT_TR_NOOP": None,
    "QT_TR_NOOP_UTF8": None,
    "QT_TR_N_NOOP": None,
}
PLURAL_MARKERS = {
    "QT_TRANSLATE_NOOP": "QT_TRANSLATE_N_NOOP",
    "QT_TRANSLATE_NOOP3": "QT_TRANSLATE_N_NOOP3",
    "QT_TR_NOOP": "QT_TR_N_NOOP",
}
NOT_FUNCTION_NAMES = {
    "alignas", "alignof", "catch", "decltype", "defined", "delete", "for", "if", "new", "noexcept",
    "operator", "requires", "return", "sizeof", "static_assert", "switch", "throw", "typeid", "while",
}
# An identifier right before a name makes it a declaration ("QString mapText(")
# unless it is one of these.
KEYWORDS_BEFORE_CALLS = {"and", "case", "co_return", "co_yield", "delete", "do", "else", "new", "not", "or", "return", "throw"}
TYPE_KEYWORDS = {"auto", "bool", "char", "const", "double", "float", "int", "long", "short", "signed", "unsigned", "void", "volatile"}
TRAILING_QUALIFIERS = {"&", "&&", "const", "final", "mutable", "noexcept", "override", "volatile"}
MACRO_DEFINITION_RE = re.compile(r"#\s*define\s+(\w+)\(([^)]*)\)(.*)", re.S)
CLOSERS = {")": "(", "]": "[", "}": "{"}


@dataclass
class SourceUnit:
    """A tokenized C++ file: whitespace and comments dropped, brackets paired."""

    path: Path
    kinds: list[str]
    texts: list[str]
    offsets: list[int]
    partner: list[int]
    line_starts: list[int]
    # Name -> indexes where it is followed by "(", then every "(" and every preprocessor line.
    calls: dict[str, list[int]]
    parentheses: list[int]
    directives: list[int]

    def line(self, index: int) -> int:
        return bisect.bisect_right(self.line_starts, self.offsets[index])


@dataclass
class Definition:
    name: str
    line: int
    parameters: list[str]
    unit: SourceUnit
    # Tokens that may use the parameters: initializers, trailing return type and body.
    scope: tuple[int, int]


@dataclass
class TranslationHelper:
    name: str
    path: Path
    line: int
    # Parameter position -> (context, or None when a class decides it; the callee it reaches).
    parameters: dict[int, tuple[str | None, str]] = field(default_factory=dict)


@dataclass
class TranslationProblem:
    path: Path
    line: int
    message: str


def source_unit(path: Path, text: str) -> SourceUnit:
    kinds: list[str] = []
    texts: list[str] = []
    offsets: list[int] = []
    calls: dict[str, list[int]] = {}
    parentheses: list[int] = []
    directives: list[int] = []
    stack: list[int] = []
    pairs: list[tuple[int, int]] = []
    for match in CPP_TOKEN_RE.finditer(text):
        kind = match.lastgroup
        if kind == "space" or kind == "comment":
            continue
        value = match.group()
        index = len(texts)
        kinds.append(kind)
        texts.append(value)
        offsets.append(match.start())
        if kind == "punct":
            if value in ("(", "[", "{"):
                stack.append(index)
                if value == "(":
                    parentheses.append(index)
                    if index and kinds[index - 1] == "ident":
                        calls.setdefault(texts[index - 1], []).append(index - 1)
            elif value in CLOSERS and stack and texts[stack[-1]] == CLOSERS[value]:
                pairs.append((stack.pop(), index))
        elif kind == "directive":
            directives.append(index)
    partner = [-1] * len(texts)
    for opener, closer in pairs:
        partner[opener] = closer
        partner[closer] = opener
    line_starts = [0] + [match.end() for match in re.finditer("\n", text)]
    return SourceUnit(path, kinds, texts, offsets, partner, line_starts, calls, parentheses, directives)


def split_top_level(unit: SourceUnit, start: int, end: int, angles: bool = False) -> list[tuple[int, int]]:
    """Split tokens start..end at commas outside brackets, and outside <> when angles is set."""
    if start >= end:
        return []
    parts: list[tuple[int, int]] = []
    depth = 0
    part_start = start
    index = start
    while index < end:
        text = unit.texts[index]
        if text in ("(", "[", "{") and unit.partner[index] > index:
            index = unit.partner[index] + 1
            continue
        if angles and text == "<":
            depth += 1
        elif angles and text == ">" and depth:
            depth -= 1
        elif text == "," and not depth:
            parts.append((part_start, index))
            part_start = index + 1
        index += 1
    parts.append((part_start, end))
    return parts


def is_call(unit: SourceUnit, index: int) -> bool:
    following = index + 1
    return following < len(unit.texts) and unit.texts[following] == "(" and unit.partner[following] > following


def call_arguments(unit: SourceUnit, open_index: int) -> list[tuple[int, int]]:
    close = unit.partner[open_index]
    return split_top_level(unit, open_index + 1, close) if close > open_index else []


def call_qualifier(unit: SourceUnit, index: int) -> str | None:
    """'' for a plain call, 'Class' for Class::name(), None for a member call or a declaration."""
    if index == 0:
        return ""
    before = unit.texts[index - 1]
    if before in (".", "->"):
        return "qApp" if before == "->" and index >= 2 and unit.texts[index - 2] == "qApp" else None
    if before == "::":
        return unit.texts[index - 2] if index >= 2 and unit.kinds[index - 2] == "ident" else ""
    if unit.kinds[index - 1] == "ident" and before not in KEYWORDS_BEFORE_CALLS:
        return None
    return ""


def is_literal(unit: SourceUnit, argument: tuple[int, int]) -> bool:
    start, end = argument
    return start < end and all(unit.kinds[index] == "string" for index in range(start, end))


def literal_value(unit: SourceUnit, argument: tuple[int, int]) -> str:
    """The text of a literal, joined when it is split into several, with escapes as written."""
    parts: list[str] = []
    for index in range(*argument):
        text = unit.texts[index]
        quote = text.index('"')
        if text[quote - 1:quote] == "R":
            parts.append(text[text.index("(", quote) + 1:text.rindex(")")])
        else:
            parts.append(text[quote + 1:-1])
    return "".join(parts)


def argument_surface(unit: SourceUnit, argument: tuple[int, int]) -> tuple[list[int], list[int]]:
    """Literals and marker macros written in the argument itself rather than inside a nested
    call, so both branches of cond ? "a" : "b" count."""
    literals: list[int] = []
    markers: list[int] = []
    index, end = argument
    while index < end:
        text = unit.texts[index]
        if text in ("(", "[", "{") and unit.partner[index] > index:
            grouping = text == "(" and unit.kinds[index - 1] != "ident" and unit.texts[index - 1] not in (">", "]", ")")
            index = index + 1 if grouping else unit.partner[index] + 1
            continue
        if unit.kinds[index] == "string":
            literals.append(index)
        elif text in NOOP_MARKERS and is_call(unit, index):
            markers.append(index)
            index = unit.partner[index + 1] + 1
            continue
        index += 1
    return literals, markers


def marker_context(unit: SourceUnit, index: int) -> str | None:
    """The context a QT_*_NOOP marker files its literal under; None for the enclosing class."""
    position = NOOP_MARKERS[unit.texts[index]]
    arguments = call_arguments(unit, index + 1)
    if position is None or position >= len(arguments) or not is_literal(unit, arguments[position]):
        return None
    return literal_value(unit, arguments[position])


def parameter_name(unit: SourceUnit, part: tuple[int, int]) -> str:
    start, end = part
    for index in range(start, end):
        if unit.texts[index] == "=":
            end = index
            break
    for index in range(end - 1, start, -1):
        if unit.kinds[index] == "ident":
            return "" if unit.texts[index] in TYPE_KEYWORDS else unit.texts[index]
        if unit.texts[index] not in ("[", "]") and unit.kinds[index] != "number":
            return ""
    return ""


def definition_body(unit: SourceUnit, close: int) -> int:
    """Index of the '{' opening the body after a parameter list, or -1 when there is none."""
    texts = unit.texts
    index = close + 1
    while index < len(texts):
        text = texts[index]
        if text in TRAILING_QUALIFIERS:
            index += 1
        elif text == "(" and texts[index - 1] == "noexcept" and unit.partner[index] > index:
            index = unit.partner[index] + 1
        elif text == "->":
            index += 1
            while index < len(texts) and (unit.kinds[index] == "ident" or texts[index] in ("::", "<", ">", "*", "&", ",")):
                index += 1
        elif text == ":":
            # Constructor initializers: name(...) or name{...}, separated by commas.
            index += 1
            while index < len(texts):
                if unit.kinds[index] == "ident" or texts[index] in ("::", "<", ">", ","):
                    index += 1
                elif texts[index] in ("(", "{") and unit.partner[index] > index and texts[index - 1] not in (")", "}", ","):
                    index = unit.partner[index] + 1
                else:
                    break
            break
        else:
            break
    if index < len(texts) and texts[index] == "{" and unit.partner[index] > index:
        return index
    return -1


def function_definitions(unit: SourceUnit) -> list[Definition]:
    """Functions, constructors and named lambdas that have a body in this unit."""
    definitions: list[Definition] = []
    texts = unit.texts
    for open_index in unit.parentheses:
        close = unit.partner[open_index]
        if open_index == 0 or close < open_index:
            continue
        body = definition_body(unit, close)
        if body < 0:
            continue
        name_index = open_index - 1
        if unit.kinds[name_index] == "ident":
            if texts[name_index] in NOT_FUNCTION_NAMES or (name_index and texts[name_index - 1] in (".", "->")):
                continue
        elif texts[name_index] == "]" and unit.partner[name_index] >= 2 and texts[unit.partner[name_index] - 1] == "=":
            name_index = unit.partner[name_index] - 2
            if unit.kinds[name_index] != "ident":
                continue
        else:
            continue
        parameters = [parameter_name(unit, part) for part in split_top_level(unit, open_index + 1, close, angles=True)]
        definitions.append(Definition(texts[name_index], unit.line(name_index), parameters, unit, (close + 1, unit.partner[body])))
    return definitions


def macro_definitions(unit: SourceUnit) -> list[Definition]:
    macros: list[Definition] = []
    for index in unit.directives:
        match = MACRO_DEFINITION_RE.match(unit.texts[index])
        if not match:
            continue
        body = source_unit(unit.path, re.sub(r"\\\r?\n", " ", match.group(3)))
        parameters = [part.strip() for part in match.group(2).split(",")]
        macros.append(Definition(match.group(1), unit.line(index), parameters, body, (0, len(body.texts))))
    return macros


def translated_arguments(unit: SourceUnit, index: int, helpers: dict[str, TranslationHelper]) -> list[tuple[tuple[int, int], str | None]]:
    """(argument, context) for each argument that the call at index translates."""
    name = unit.texts[index]
    qualifier = call_qualifier(unit, index)
    arguments = call_arguments(unit, index + 1)
    if name in TRANSLATION_CALLS:
        context_position, source_position = TRANSLATION_CALLS[name]
        if qualifier not in TRANSLATION_QUALIFIERS or source_position >= len(arguments):
            return []
        context = None
        if context_position is not None and is_literal(unit, arguments[context_position]):
            context = literal_value(unit, arguments[context_position])
        return [(arguments[source_position], context)]
    helper = helpers.get(name)
    if helper is None or qualifier is None:
        return []
    return [(arguments[position], context) for position, (context, _) in helper.parameters.items() if position < len(arguments)]


def callee_indexes(unit: SourceUnit, helpers: dict[str, TranslationHelper]) -> list[int]:
    return sorted(index for name in TRANSLATION_CALLS.keys() | helpers.keys() for index in unit.calls.get(name, ()))


def translated_parameters(definition: Definition, calls: list[int], helpers: dict[str, TranslationHelper]) -> dict[int, tuple[str | None, str]]:
    """Parameters of a definition that reach the source of a translation call in its scope."""
    unit = definition.unit
    positions = {name: position for position, name in enumerate(definition.parameters) if name}
    found: dict[int, tuple[str | None, str]] = {}
    start, end = definition.scope
    for index in calls[bisect.bisect_left(calls, start):bisect.bisect_left(calls, end)]:
        for argument, context in translated_arguments(unit, index, helpers):
            for position in range(*argument):
                word = unit.texts[position]
                if word in positions and unit.kinds[position] == "ident" and unit.texts[position - 1] not in (".", "->", "::"):
                    found.setdefault(positions[word], (context, unit.texts[index]))
    return found


def find_translation_helpers(units: list[SourceUnit]) -> dict[Path, dict[str, TranslationHelper]]:
    """The translation helpers visible in each unit: its own, plus any defined in a header."""
    definitions = {unit.path: function_definitions(unit) + macro_definitions(unit) for unit in units}
    local: dict[Path, dict[str, TranslationHelper]] = {unit.path: {} for unit in units}
    shared: dict[str, TranslationHelper] = {}
    changed = True
    while changed:
        changed = False
        for unit in units:
            known = {**shared, **local[unit.path]}
            calls = callee_indexes(unit, known)
            for definition in definitions[unit.path]:
                if definition.name in TRANSLATION_CALLS or not any(definition.parameters):
                    continue
                scope_calls = calls if definition.unit is unit else callee_indexes(definition.unit, known)
                found = translated_parameters(definition, scope_calls, known)
                helper = local[unit.path].get(definition.name)
                if not found or (helper is not None and found.keys() <= helper.parameters.keys()):
                    continue
                if helper is None:
                    helper = TranslationHelper(definition.name, unit.path, definition.line)
                    local[unit.path][definition.name] = helper
                    if unit.path.suffix == ".h":
                        shared[definition.name] = helper
                for position, value in found.items():
                    helper.parameters.setdefault(position, value)
                changed = True
    return {unit.path: {**shared, **local[unit.path]} for unit in units}


def check_helper_call(unit: SourceUnit, index: int, helper: TranslationHelper, problems: list[TranslationProblem]) -> None:
    arguments = call_arguments(unit, index + 1)
    for position, (context, callee) in sorted(helper.parameters.items()):
        if position >= len(arguments):
            continue
        literals, markers = argument_surface(unit, arguments[position])
        for literal in literals:
            if context is None:
                fix = "translate it where it is written instead"
            else:
                fix = f'write QCoreApplication::translate("{context}", ...) here, or mark it QT_TRANSLATE_NOOP("{context}", ...)'
            problems.append(TranslationProblem(unit.path, unit.line(literal),
                f"{helper.name}() hands this literal to {callee}(), so lupdate never extracts it; {fix}"))
        for marker in markers:
            marked = marker_context(unit, marker)
            if context is not None and marked != context:
                where = f'context "{marked}"' if marked is not None else "the enclosing class"
                problems.append(TranslationProblem(unit.path, unit.line(marker),
                    f'{helper.name}() translates with context "{context}", but {unit.texts[marker]} files this literal under {where}, '
                    "so the catalog entry is never used"))


def check_translation_call(unit: SourceUnit, index: int, problems: list[TranslationProblem]) -> None:
    name = unit.texts[index]
    if call_qualifier(unit, index) not in TRANSLATION_QUALIFIERS:
        return
    context_position, source_position = TRANSLATION_CALLS[name]
    arguments = call_arguments(unit, index + 1)
    if source_position >= len(arguments):
        return
    source = arguments[source_position]
    literals, _ = argument_surface(unit, source)
    if not literals:
        # A variable: its literal is written, and has to be marked, somewhere else.
        return
    line = unit.line(literals[0])
    if not is_literal(unit, source):
        problems.append(TranslationProblem(unit.path, line,
            f"{name}() needs a plain literal as its source; lupdate cannot extract one from this expression"))
        return
    if context_position is not None and (context_position >= len(arguments) or not is_literal(unit, arguments[context_position])):
        problems.append(TranslationProblem(unit.path, line,
            f"{name}() needs a literal context; lupdate cannot tell which context this string belongs to"))
    if "%n" in literal_value(unit, source) and len(arguments) < source_position + 3:
        problems.append(TranslationProblem(unit.path, line, f"{name}() has a %n plural but no count, so %n is never replaced"))


def check_marker(unit: SourceUnit, index: int, problems: list[TranslationProblem]) -> None:
    name = unit.texts[index]
    plural = PLURAL_MARKERS.get(name)
    arguments = call_arguments(unit, index + 1)
    source_position = 0 if NOOP_MARKERS[name] is None else 1
    if plural and source_position < len(arguments) and is_literal(unit, arguments[source_position]) \
            and "%n" in literal_value(unit, arguments[source_position]):
        problems.append(TranslationProblem(unit.path, unit.line(index),
            f"{name}() marks a %n plural as a plain string; use {plural}() so translators get its plural forms"))


def scan_translation_sources(files: list[Path]) -> tuple[list[TranslationHelper], list[TranslationProblem]]:
    units = [source_unit(path, path.read_text(encoding="utf-8", errors="replace")) for path in files]
    visible = find_translation_helpers(units)
    helpers: dict[tuple[Path, str], TranslationHelper] = {}
    problems: list[TranslationProblem] = []
    for unit in units:
        known = visible[unit.path]
        for name in NOOP_MARKERS:
            for index in unit.calls.get(name, ()):
                check_marker(unit, index, problems)
        for name in TRANSLATION_CALLS:
            for index in unit.calls.get(name, ()):
                check_translation_call(unit, index, problems)
        for name, helper in known.items():
            helpers[(helper.path, name)] = helper
            for index in unit.calls.get(name, ()):
                if call_qualifier(unit, index) is not None:
                    check_helper_call(unit, index, helper, problems)
    problems.sort(key=lambda problem: (str(problem.path), problem.line))
    return sorted(helpers.values(), key=lambda helper: (str(helper.path), helper.line)), problems


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
        # lupdate exits 0 when it refuses a catalog, for example one whose
        # language it has no plural rules for.
        skipped = [line.strip() for line in f"{result.stdout}\n{result.stderr}".splitlines() if "won't be updated" in line]
        return {
            "command": command,
            "stdout": result.stdout.strip(),
            "stderr": result.stderr.strip(),
            "messageCounts": counts,
            "minimumMessageCount": min(counts.values()) if counts else 0,
            "skippedCatalogs": skipped,
        }
    finally:
        if temp_dir is not None:
            temp_dir.cleanup()


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate or run the Qt Linguist translation extraction workflow.")
    parser.add_argument("--check", action="store_true", help="Validate source/catalog coverage, including literals that lupdate cannot see. This is the default when --write is not used.")
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

    # Checked before lupdate runs, so --write never fills the catalogs from a
    # tree whose strings it cannot all see.
    helpers, problems = scan_translation_sources(sources)
    for problem in problems:
        errors.append(f"{problem.path.relative_to(root).as_posix()}:{problem.line}: {problem.message}")

    lupdate_report: dict | None = None
    should_run_lupdate = args.write or args.dry_run
    if should_run_lupdate and lupdate is not None and not errors:
        try:
            lupdate_report = run_lupdate(lupdate, root, catalogs, args.write)
            errors.extend(lupdate_report["skippedCatalogs"])
            if lupdate_report["minimumMessageCount"] < 20 and not lupdate_report["skippedCatalogs"]:
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
        "translationHelpers": [
            {
                "file": helper.path.relative_to(root).as_posix(),
                "line": helper.line,
                "name": helper.name,
                "translatedParameters": sorted(helper.parameters),
            }
            for helper in helpers
        ],
        "unextractableLiteralCount": len(problems),
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
            f"({len(catalogs)} catalogs, {len(sources)} source files, {call_count} translation call sites, "
            f"{len(helpers)} translation helpers with marked call sites)."
        )
        if lupdate_report:
            print(f"lupdate dry-run minimum messages: {lupdate_report['minimumMessageCount']}")

    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
