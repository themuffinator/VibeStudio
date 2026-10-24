#!/usr/bin/env python3
"""Reproduce the bounded TB/NRC command discovery catalogue.

This reads pinned, clean reference checkouts; it neither builds nor executes them.
It is a deliberately narrow C++ source scanner, not a preprocessor or a parity test.
Unknown registration expressions fail instead of silently shrinking the inventory.
See docs/plans/level-editor-commands.md for scope, provenance and review policy.
"""
from __future__ import annotations

import argparse
import ast
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CATALOGUE = ROOT / "docs/plans/level-editor-commands.json"
REVIEWS = ROOT / "docs/plans/level-editor-command-reviews.json"
REPORT = ROOT / "docs/plans/level-editor-commands.md"
BASELINES = {
    "tb": {
        "name": "TrenchBroom v2026.2",
        "revision": "4c756be1a54a28d921d18c77716a5f170bdc31d0",
        "repository": "https://github.com/TrenchBroom/TrenchBroom",
        "licence": "GPL-3.0-or-later",
    },
    "nrc": {
        "name": "NetRadiant Custom",
        "revision": "449778b3840f8f2062daab15c87cb012f2ac945c",
        "repository": "https://github.com/Garux/netradiant-custom",
        "licence": "GPL-2.0-or-later; individual plugin notices also apply",
    },
}
# Comments are tokens so they cannot expose fake calls, but newlines/offsets survive.
LEX = re.compile(
    r'(?P<comment>//[^\n]*|/\*[\s\S]*?\*/)'
    r'|(?P<raw>(?:u8|[uUL])?R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\([\s\S]*?\)(?P=delimiter)")'
    r'|(?P<string>(?:u8|[uUL])?"(?:\\[\s\S]|[^"\\])*")'
    r"|(?P<char>(?:u8|[uUL])?'(?:\\[\s\S]|[^'\\])*')"
    r'|(?P<identifier>[A-Za-z_][A-Za-z_0-9]*)|(?P<other>[^\s])'
)


def tokens(text: str) -> list[tuple[str, int, str]]:
    return [(m.group(), m.start(), m.lastgroup or "") for m in LEX.finditer(text)
            if m.lastgroup != "comment"]


def arguments(ts: list, start: int) -> tuple[list[list], int]:
    """Split a parenthesised/braced call, retaining nested lambdas and literals."""
    pairs = {"(": ")", "{": "}", "[": "]"}
    if ts[start][0] not in pairs:
        raise ValueError("Expected a call or initializer")
    stack, parts, begin = [pairs[ts[start][0]]], [], start + 1
    for i in range(start + 1, len(ts)):
        value, _, kind = ts[i]
        if kind in ("string", "raw", "char"):
            continue
        if value in pairs:
            stack.append(pairs[value])
        elif value in pairs.values():
            if stack.pop() != value:
                raise ValueError("Unbalanced C++ initializer")
            if not stack:
                parts.append(ts[begin:i])
                return parts, i
        elif value == "," and len(stack) == 1:
            parts.append(ts[begin:i])
            begin = i + 1
    raise ValueError("Unterminated C++ initializer")


def literal(ts: list, constants: dict | None = None) -> str:
    constants = constants or {}
    result = []
    for value, _, kind in ts:
        if kind == "string":
            result.append(ast.literal_eval(value[value.index('"'):]))
        elif kind == "raw":
            opening = value.index("(")
            delimiter = value[value.index('"') + 1:opening]
            result.append(value[opening + 1:-(len(delimiter) + 2)])
        elif value in constants:
            result.append(constants[value])
        else:
            raise ValueError("Non-literal expression: " + " ".join(t[0] for t in ts))
    if not ts:
        raise ValueError("Empty expression")
    return "".join(result)


def wrapped_literal(ts: list) -> str:
    values = [t[0] for t in ts]
    for prefix in (["std", ":", ":", "filesystem", ":", ":", "path"],
                   ["QObject", ":", ":", "tr"]):
        if values[:len(prefix)] == prefix:
            args, end = arguments(ts, len(prefix))
            if len(args) != 1 or end != len(ts) - 1:
                raise ValueError("Unexpected literal wrapper")
            return literal(args[0])
    return literal(ts)


def calls(ts: list, names: set[str]):
    for i, (value, _, _) in enumerate(ts[:-1]):
        if value in names and ts[i + 1][0] == "(":
            args, end = arguments(ts, i + 1)
            yield value, args, i, end


def conditions(text: str, offset: int) -> list[str]:
    # Preserve rather than evaluate platform/debug guards. Runtime predicates remain
    # inspectable at the source location and are not inferred to be build guards.
    stack: list[list[str]] = []
    clean = LEX.sub(lambda m: "".join("\n" if ch == "\n" else " " for ch in m.group())
                    if m.lastgroup in {"comment", "string", "raw", "char"}
                    else m.group(), text[:offset])
    for line in clean.splitlines():
        match = re.match(r"\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", line)
        if not match:
            continue
        directive = "#" + match[1] + (" " + match[2].strip() if match[2].strip() else "")
        if match[1] in ("if", "ifdef", "ifndef"):
            stack.append([directive])
        elif match[1] == "endif":
            if not stack:
                raise ValueError("Unmatched #endif")
            stack.pop()
        else:
            if not stack:
                raise ValueError("Unmatched conditional branch")
            stack[-1].append(directive)
    return ["; ".join(branches) for branches in stack]


def location(path: str, text: str, begin: int, end: int) -> dict:
    return {"file": path, "line": text.count("\n", 0, begin) + 1,
            "endLine": text.count("\n", 0, end) + 1,
            "sha256": hashlib.sha256(text[begin:end].encode()).hexdigest(),
            "guards": conditions(text, begin)}


def entry(project: str, command: str, kind: str, loc: dict, **extra) -> dict:
    return {"id": project + ":" + command, "project": project,
            "command": command, "kind": kind, "locations": [loc], **extra}


def scan_tb(path: str, text: str) -> list[dict]:
    ts, rows = tokens(text), []
    for _, args, i, end in calls(ts, {"addAction"}):
        # The declaration itself takes an Action parameter, not an Action{...}.
        if not args[0] or [t[0] for t in args[0][:2]] != ["Action", "{"]:
            if [t[0] for t in args[0]] == ["Action", "action"]:
                continue
            raise ValueError("Unknown TB action registration")
        fields, _ = arguments(args[0], 1)
        rows.append(entry("tb", wrapped_literal(fields[0]), "action",
                          location(path, text, ts[i][1], ts[end][1] + 1),
                          label=wrapped_literal(fields[1])))
    # Each runtime family is a template, not an invented count of game definitions.
    for name, args, i, end in calls(ts, {"emplace_back"}):
        if [t[0] for t in ts[max(0, i - 2):i]] != ["result", "."]:
            continue  # m_mainMenu.emplace_back constructs a menu, not an action.
        if not args[0] or args[0][0][0] != "std":
            raise ValueError("Unknown TB generated action")
        expression = " ".join(t[0] for t in args[0])
        strings = [literal([t]) for t in args[0] if t[2] in ("string", "raw")]
        if "tag . name ( )" in expression:
            placeholder = "{tag}"
        elif "definition . name" in expression:
            placeholder = "{entity-definition}"
        else:
            raise ValueError("Unknown TB generated action expression: " + expression)
        if len(strings) != 2:
            raise ValueError("Unknown TB generated action path")
        rows.append(entry("tb", strings[0] + placeholder + strings[1], "family",
                          location(path, text, ts[i][1], ts[end][1] + 1)))
    return rows


def scan_nrc(path: str, text: str) -> list[dict]:
    ts, rows = tokens(text), []
    names = {"GlobalCommands_insert", "GlobalToggles_insert", "GlobalShortcuts_insert",
             "add_filter_command"}
    for name, args, i, end in calls(ts, names):
        first = args[1] if name == "add_filter_command" else args[0]
        try:
            command = literal(first)
        except ValueError:
            expression = " ".join(t[0] for t in first)
            # These are the reviewed forwarding/definition sites, not arbitrary
            # exceptions for a variable name appearing somewhere else.
            known = {
                "radiant/commands.cpp": {"const char * name", "name"},
                "radiant/commands.h": {"const char * name"},
                "radiant/filters.cpp": {"const char * command", "command"},
                "radiant/pluginmanager.cpp": {"str . c_str ( )"},
            }
            if expression not in known.get(path, set()):
                raise ValueError(f"{path}: unknown {name}: {expression}")
            continue
        kind = "shortcut" if name == "GlobalShortcuts_insert" else "command"
        if name in {"GlobalToggles_insert", "add_filter_command"}:
            kind = "toggle"
        rows.append(entry("nrc", command, kind,
                          location(path, text, ts[i][1], ts[end][1] + 1)))
    return rows


def string_constants(text: str) -> dict[str, str]:
    """Resolve simple literal macros/variables used in NRC's plugin menu tables."""
    result: dict[str, str] = {}
    ts = tokens(text)
    pending = []
    for i, (value, _, kind) in enumerate(ts[:-1]):
        if kind != "identifier":
            continue
        j = i + 1
        if ts[j][0] == "[":
            _, j = arguments(ts, j)
            j += 1
        if j < len(ts) and ts[j][0] == "=":
            end = j + 1
            while end < len(ts) and ts[end][0] != ";":
                end += 1
            pending.append((value, ts[j + 1:end]))
        if i >= 2 and ts[i - 2][0] == "#" and ts[i - 1][0] == "define":
            end = i + 1
            line_end = text.find("\n", ts[i][1])
            if line_end < 0:
                line_end = len(text)
            while end < len(ts) and ts[end][1] < line_end:
                end += 1
            pending.append((value, ts[i + 1:end]))
    while pending:
        unresolved = []
        for key, value in pending:
            try:
                result[key] = literal(value, result)
            except ValueError:
                unresolved.append((key, value))
        if len(unresolved) == len(pending):
            break
        pending = unresolved
    return result


def plugin_rows(path: str, text: str, default_sources: set[str]) -> list[dict]:
    ts, rows = tokens(text), []
    constants = string_constants(text)
    providers = list(calls(ts, {"getCommandList", "QERPlug_GetCommandList",
                               "QERPluginGetCommandList"}))
    for _, _, start, end in providers:
        if end + 1 >= len(ts) or ts[end + 1][0] != "{":
            continue  # Declaration or use, not a provider implementation.
        _, body_end = arguments(ts, end + 1)
        body = ts[end + 2:body_end]
        values = []
        if path == "contrib/sunplug/sunplug.cpp":
            for _, args, _, _ in calls(body, {"strcat"}):
                if [t[0] for t in args[0]] != ["MenuList"]:
                    raise ValueError("Unknown SunPlug command buffer")
                values.append(literal(args[1], constants))
        elif path == "contrib/meshtex/PluginModule.cpp":
            # Expanded from MainMenu.cpp below; retain the provider as a family.
            values = []
        else:
            for i, token in enumerate(body):
                if token[0] == "return":
                    j = i + 1
                    while j < len(body) and body[j][0] != ";":
                        j += 1
                    value = body[i + 1:j]
                    if value and value[0][0] == "(":
                        _, cast_end = arguments(value, 0)
                        value = value[cast_end + 1:]
                    values.append(literal(value, constants))
        loc = location(path, text, ts[start][1], ts[body_end][1] + 1)
        rows.append(entry("nrc", f"plugin/{path}/{{command}}", "family", loc,
                          build="listed-plugin-target" if path in default_sources else "source-only"))
        for command in re.split("[,;]", "".join(values)):
            command = command.strip()
            if command and command not in {"-", ">", "<"}:
                rows.append(entry("nrc", f"plugin/{path}/{command}", "plugin-command", loc,
                                  label=command,
                                  build="listed-plugin-target" if path in default_sources else "source-only"))
    if path == "contrib/meshtex/MainMenu.cpp":
        for _, args, i, end in calls(ts, {"AddDialogShowEntry", "AddMeshVisitorEntry", "AddEntry"}):
            if args[0] and args[0][0][0] == "const":
                continue
            # Forwarding in AddMeshVisitorEntry, not another menu entry.
            if [t[0] for t in args[0]] == ["commandLabel"]:
                continue
            label, command = literal(args[0]), literal(args[1])
            rows.append(entry("nrc", f"plugin/contrib/meshtex/PluginModule.cpp/MeshTex.{command or label}",
                              "plugin-command", location(path, text, ts[i][1], ts[end][1] + 1),
                              label=label, build="listed-plugin-target" if path in default_sources else "source-only"))
    return rows


def git(root: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(root), *args], check=True,
                          capture_output=True, text=True, encoding="utf-8").stdout.strip()


def scan(tb: Path, nrc: Path) -> dict:
    rows = []
    for project, root in (("tb", tb), ("nrc", nrc)):
        if git(root, "rev-parse", "HEAD") != BASELINES[project]["revision"]:
            raise ValueError(f"{project}: checkout does not match the pinned baseline")
        if git(root, "status", "--porcelain", "--untracked-files=no"):
            raise ValueError(f"{project}: reference has tracked modifications")
    path = "lib/TbUiLib/src/ActionManager.cpp"
    rows.extend(scan_tb(path, (tb / path).read_text(encoding="utf-8")))
    makefile = (nrc / "Makefile").read_text(encoding="utf-8")
    # Targets in the PLUGINS list, not all implementation rules in the Makefile.
    default_plugins = set(re.findall(r"^\s*\$\(INSTALLDIR\)/plugins/([^.$]+)\.\$\(DLL\)\s*\\?\s*$",
                                    makefile, re.M))
    default_sources = set()
    for match in re.finditer(r"^\$\(INSTALLDIR\)/plugins/([^.$]+)\.\$\(DLL\):\s*\\\n((?:[^\n]*\\\n)+)",
                             makefile, re.M):
        if match[1] in default_plugins:
            default_sources.update(p + ".cpp" for p in re.findall(r"\b((?:contrib|plugins)/[^\s]+)\.o\b", match[2]))
    for path in git(nrc, "ls-files", "radiant", "plugins", "contrib").splitlines():
        if not path.endswith((".cpp", ".h")):
            continue
        text = (nrc / path).read_text(encoding="utf-8")
        if path.startswith("radiant/"):
            rows.extend(scan_nrc(path, text))
        elif any(name in text for name in ("getCommandList", "QERPlug_GetCommandList",
                                          "QERPluginGetCommandList")) or path == "contrib/meshtex/MainMenu.cpp":
            rows.extend(plugin_rows(path, text, default_sources))
    merged = {}
    for row in rows:
        for loc in row["locations"]:
            loc["registrationKind"] = row["kind"]
        if row["id"] in merged:
            previous = merged[row["id"]]
            if previous["kind"] != row["kind"]:
                if "shortcut" not in {previous["kind"], row["kind"]}:
                    raise ValueError("Conflicting registration kind: " + row["id"])
                if previous["kind"] == "shortcut":
                    previous["kind"] = row["kind"]
            previous["locations"].extend(row["locations"])
        else:
            merged[row["id"]] = row
    return {"schemaVersion": 1, "baselines": BASELINES,
            "scope": "TB central actions and generated families; NRC core registrations and in-tree plugin menus. Not complete workflow parity.",
            "commands": sorted(merged.values(), key=lambda row: row["id"])}


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


# Parity statuses follow docs/BRANDING.md. "Not applicable" records an audited
# command with no meaning for VibeStudio's supported games or architecture; its
# limitation states why, so no row is silently excluded.
STATUSES = ("Available", "Partial", "Planned", "Not applicable")


def validate(catalogue: dict, reviews: dict, root: Path = ROOT,
             ui: dict | None = None, cli: dict | None = None) -> dict:
    if catalogue.get("schemaVersion") != 1 or catalogue.get("baselines") != BASELINES:
        raise ValueError("Unexpected catalogue schema or baseline")
    ids = set()
    counts: dict[str, int] = {}
    for row in catalogue["commands"]:
        key = row["id"]
        if key in ids or key != row["project"] + ":" + row["command"]:
            raise ValueError("Duplicate or malformed command ID: " + key)
        ids.add(key)
        if row["project"] not in BASELINES or row["kind"] not in {
                "action", "command", "toggle", "shortcut", "family", "plugin-command"}:
            raise ValueError("Invalid command category: " + key)
        if not row["locations"]:
            raise ValueError("Missing command source: " + key)
        for loc in row["locations"]:
            if loc["line"] < 1 or loc["endLine"] < loc["line"] or not re.fullmatch(r"[0-9a-f]{64}", loc["sha256"]):
                raise ValueError("Invalid source location: " + key)
            if not re.fullmatch(r"[A-Za-z0-9_./-]+", loc["file"]) or ".." in Path(loc["file"]).parts or loc["file"].startswith("/"):
                raise ValueError("Unsafe source location: " + key)
        count = row["project"] + "." + row["kind"]
        counts[count] = counts.get(count, 0) + 1
    if reviews.get("schemaVersion") != 1:
        raise ValueError("Unexpected review schema")
    seen = set()
    # Many reviews cite the same files; resolve each path once.
    resolved_root, checked = root.resolve(), {}
    if (ui is not None and ui.get("ok") is not True
            or cli is not None and cli.get("ok") is not True):
        raise ValueError("Command registry capture reports failure")
    ui_ids = {r["commandId"] for key in ("commandPalette", "shortcuts")
              for r in ui["uiSemantics"][key]} if ui else set()
    cli_ids = {r["name"] for r in cli["commands"]} if cli else set()
    for review in reviews["reviews"]:
        key = review["id"]
        if key not in ids or key in seen:
            raise ValueError("Unknown or duplicate reviewed command: " + key)
        seen.add(key)
        if review["status"] not in STATUSES:
            raise ValueError("Invalid parity status: " + key)
        if not review["workflow"] or not review["limitation"] or not review["evidence"]:
            raise ValueError("Missing review rationale/evidence: " + key)
        if review["status"] in {"Available", "Partial"} and not (review["gui"] or review["cli"]):
            raise ValueError("Implemented status needs a route: " + key)
        for path in review["evidence"]:
            if path not in checked:
                resolved = (root / path).resolve()
                checked[path] = resolved.is_relative_to(resolved_root) and resolved.is_file()
            if not checked[path]:
                raise ValueError("Missing or external review evidence: " + path)
        for route in review["gui"]:
            if ui is not None and route not in ui_ids:
                raise ValueError("Unknown palette route: " + route)
        for route in review["cli"]:
            if cli is not None and route not in cli_ids:
                raise ValueError("Unknown CLI route: " + route)
    return {"commands": len(ids), "categories": counts, "reviewed": len(seen),
            "unreviewed": len(ids - seen),
            "statuses": {status: sum(r["status"] == status for r in reviews["reviews"])
                         for status in STATUSES},
            "parityComplete": False}


def markdown(catalogue: dict, reviews: dict) -> str:
    indexed = {r["id"]: r for r in reviews["reviews"]}
    counts = validate(catalogue, reviews)
    lines = [
        "# Audit level editor commands", "",
        "<!-- Generated by scripts/level_command_inventory.py; edit the review JSON, then regenerate. -->", "",
        "**Status: Partial.** This is a bounded source-discovery catalogue, not a claim of",
        "complete authoring parity. VibeStudio remains pre-alpha and largely untested in real projects.", "",
        f"The catalogue has **{counts['commands']} entries**: **{counts['reviewed']} reviewed** and",
        f"**{counts['unreviewed']} unreviewed**. A family represents a generator/provider, not another",
        "implemented command. Registration counts do not measure usability or production readiness.", "",
        "Reviewed statuses: " + ", ".join(f"**{counts['statuses'][s]} {s}**" for s in STATUSES) + ".", "",
        "## Scope and provenance", "",
        "- TrenchBroom's central `ActionManager.cpp`: 179 literal actions and five generated tag/entity families.",
        "- NRC's `radiant` command, toggle and shortcut registrations, including generated filters: 301 distinct names.",
        "- NRC's in-tree plugin providers: 96 command tokens plus 15 provider families. Eight providers occur",
        "  in the Makefile's `binaries-radiant-plugins` dependency graph; seven are source-only at this pin.",
        "  `listed-plugin-target` records source inclusion, not successful builds or applicability to every game.",
        "- Comments are ignored; adjacent literals, generated filters, plugin constants and MeshTex menu entries",
        "  are resolved. Platform/debug branches and multiple registration sites remain recorded in the JSON.",
        "  The scanner fails on unknown expressions at the supported registration sites; it does not evaluate C++.", "",
    ]
    for key, baseline in BASELINES.items():
        lines.append(f"- [{baseline['name']}]({baseline['repository']}/tree/{baseline['revision']}), "
                     f"`{baseline['revision']}`; {baseline['licence']}.")
    lines += ["", "Reviewed 2026-10-19 to 2026-10-21. Command names and source locations are factual metadata derived",
              "from the [credited upstream sources](../CREDITS.md#level-editor-command-catalogue).",
              "Plugin paths namespace tokens so unrelated plugins with identical names are not conflated.",
              "Upstream global shortcut spelling and translated menu labels may differ from these tokens.", "",
              "## Interpret the reviews", "",
              "**Unreviewed** is a discovery state: no Available/Partial/Planned parity status has been assigned.",
              "**Not applicable** marks a reviewed command that has no meaning for the games or architecture",
              "VibeStudio supports (for example a plugin for another game); the row states the reason.",
              "Reviewed rows use the project's [status definitions](../BRANDING.md). An equivalent route does",
              "not prove matching semantics, native gestures or a passing test. Linked test files describe the",
              "acceptance cases; execution results and limitations are in the [production record](level-editor-production.md).",
              "GUI identifiers listed in reviews come from the runtime shortcut/palette registry. Other GUI",
              "controls are described in the workflow and cited implementation. CLI names identify command",
              "families; their required options remain in the CLI help and manual.", "",
              "## Remaining inventory layers", "",
              "The first production gate stays open. Audit pointer/keyboard gestures and modifier transitions,",
              "context menus, inspectors (entity properties, layers and direct UV), browser/search workflows,",
              "tool state machines, preferences, external map/build/run processes, game definitions, formats,",
              "plugin sub-dialogues and error/recovery paths. Also review every unreviewed row, including",
              "source-only plugins and debug/application commands, before deciding applicability. No row is",
              "silently excluded from the user's full TB/NRC scope. New upstream versions need a delta review.", "",
              "## Reproduce and validate", "", "```sh",
              "python scripts/level_command_inventory.py",
              "python src/tests/level_command_inventory_test.py",
              "python scripts/level_command_inventory.py --trenchbroom <clean-TB-checkout> --nrc <clean-NRC-checkout>",
              "```", "",
              "The last command compares both pinned clean source scans with the stored catalogue. Add",
              "`--write` to regenerate it and this report after editing the review JSON. Checkout paths are",
              "read-only inputs; scanning never executes upstream code. Without checkouts, `--write` updates",
              "only this report from the existing catalogue and reviews. Validation also runs as Meson tests.", "",
              "To check route references against a particular executable, capture its `--cli ui commands --json`",
              "and `--cli cli commands --json` outputs with a task-local `--settings-file`, then pass them with",
              "`--ui-registry` and `--cli-registry`. This verifies registration, not behavioural parity.", ""]
    groups = [("TrenchBroom central actions and families", lambda r: r["project"] == "tb"),
              ("NRC core commands, toggles and shortcuts", lambda r: r["project"] == "nrc" and not r["command"].startswith("plugin/")),
              ("NRC plugin commands and providers", lambda r: r["project"] == "nrc" and r["command"].startswith("plugin/"))]

    def cell(value: str) -> str:
        return value.replace("|", "\\|").replace("\n", " ")

    for title, predicate in groups:
        lines += ["## " + title, "", "| Upstream command / source | Kind | Review | VibeStudio equivalent and limitation |",
                  "| --- | --- | --- | --- |"]
        for row in catalogue["commands"]:
            if not predicate(row):
                continue
            loc, base = row["locations"][0], BASELINES[row["project"]]
            source = f"{base['repository']}/blob/{base['revision']}/{loc['file']}#L{loc['line']}"
            label = row["command"]
            if row["kind"] in {"family", "plugin-command"} and row["project"] == "nrc":
                label = row["command"].removeprefix("plugin/")
            review = indexed.get(row["id"])
            status, detail = "Unreviewed", "Behaviour and applicability review pending."
            if review:
                status = review["status"]
                routes = ["GUI `" + r + "`" for r in review["gui"]] + ["CLI `" + r + "`" for r in review["cli"]]
                evidence = [f"[{Path(p).stem}](../../{p})" for p in review["evidence"]]
                detail = review["workflow"] + (" " + "; ".join(routes) + "." if routes else "")
                detail += " " + review["limitation"] + " Evidence: " + ", ".join(evidence) + "."
            kind = row["kind"] + ("; " + row["build"] if "build" in row else "")
            lines.append(f"| [{cell(label)}]({source}) | {cell(kind)} | {status} | {cell(detail)} |")
        lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trenchbroom", type=Path)
    parser.add_argument("--nrc", type=Path)
    parser.add_argument("--catalogue", type=Path, default=CATALOGUE)
    parser.add_argument("--reviews", type=Path, default=REVIEWS)
    parser.add_argument("--report", type=Path, default=REPORT)
    parser.add_argument("--write", action="store_true", help="Regenerate the report, and catalogue when both checkouts are supplied")
    parser.add_argument("--ui-registry", type=Path, help="JSON from --cli ui commands --json")
    parser.add_argument("--cli-registry", type=Path, help="JSON from --cli cli commands --json")
    args = parser.parse_args()
    try:
        if bool(args.trenchbroom) != bool(args.nrc):
            raise ValueError("Scanning requires both reference checkout paths")
        catalogue = scan(args.trenchbroom, args.nrc) if args.nrc else read_json(args.catalogue)
        reviews = read_json(args.reviews)
        result = validate(catalogue, reviews, ui=read_json(args.ui_registry) if args.ui_registry else None,
                          cli=read_json(args.cli_registry) if args.cli_registry else None)
        if args.write:
            if args.nrc:
                args.catalogue.write_text(json.dumps(catalogue, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
            args.report.write_text(markdown(catalogue, reviews), encoding="utf-8")
        elif args.nrc and catalogue != read_json(args.catalogue):
            raise ValueError("Discovery catalogue differs from the pinned source scan")
        elif args.report.read_text(encoding="utf-8") != markdown(catalogue, reviews):
            raise ValueError("Command report is stale; regenerate with --write")
        print(json.dumps(result, indent=2))
        return 0
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as error:
        print(f"Command inventory: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
