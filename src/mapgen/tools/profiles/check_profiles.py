"""Validate data/profiles (qmg.game_profile v1): schema, citations, facts fixture, generator cross-checks.

usage:
  python -B tools/profiles/check_profiles.py [--repo DIR] [--header PATH] [--self-test]
                                            [--resolve --root NAME=DIR ... --progs PATH] [--json OUT]

Checks (all stdlib, deterministic):
  schema      every profile validates against data/profiles/schema.json (a JSON Schema 2020-12 subset).
  citations   every value sits under an object with a non-empty "cite" list; prose keys (note, detail, ...)
              and file metadata are exempt. Each citation's root must be declared in the profile's sources.
  semantics   unique ids and classnames, item respawn rules exist, contract classnames are catalogued,
              digest_corrections paths resolve.
  fixture     tests/fixtures/profiles/facts.json: values from the Phase-1 facts digest and plan section 9.2.
              A profile value that disagrees with the fixture is an error.
  generator   each generator-current file covers exactly the C++ enumerators (read from include/qmg), every
              generated classname is catalogued by at least one listed module, and the discrepancies computed
              against the module profiles equal the declared "computed" known_defects (none silent, none stale),
              monster_hulls holds one spawn box (origin inside) per monster variant, and (Quake 1 and Quake 2 only)
              pickup_hulls holds one drop box (origin inside or on it) per pickup classname: every weapon variant,
              the health classname and the armor classname.
  header      (--header) the generated header carries the profile-set sha256 computed here and every compiled
              generator value, monster and pickup hulls included.
  resolve     (--resolve) every file citation's line range exists under its root and contains its token; every
              progs1996 citation names a function whose statements include the range and token.
  self-test   (--self-test) mutation controls: each must be rejected, and the unmutated set must pass.
Exit 0 when no check fails, 1 otherwise.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_REPO = HERE.parents[1]
PROSE_KEYS = {"note", "notes", "detail", "description", "origin", "title"}
TOP_METADATA = {"schema", "schema_version", "profile_kind", "game", "module", "title", "evidence_level", "sources", "notes",
                "digest_corrections", "origin", "modules"}
EXEMPT_PATHS = {("runtime", "name")}
GAME_DIR = {"quake1": "q1", "quake2": "q2", "quake3": "q3"}
FILE_CITE = re.compile(r"^(?P<root>[a-z][a-z0-9]*):(?P<path>[^:|\s][^:|]*):(?P<first>[0-9]+)(?:-(?P<last>[0-9]+))?(?:\|(?P<token>.+))?$")
PROGS_CITE = re.compile(r"^progs1996:(?:(?P<header>@header)|@global:(?P<global>[A-Za-z_][A-Za-z0-9_]*)|"
                        r"(?P<function>[A-Za-z_][A-Za-z0-9_]*):(?P<first>[0-9]+)(?:-(?P<last>[0-9]+))?)(?:\|(?P<token>.+))?$")
SAFE_VALUE = re.compile(r"^[A-Za-z0-9_*+/.-]+$")


def norm(text: str) -> str:
    return " ".join(text.split())


# --------------------------------------------------------------------------- JSON Schema subset

class SchemaValidator:
    """type, enum, const, required, properties, additionalProperties, items, min/maxItems, minLength, pattern,
    minimum, maximum, minProperties, $ref (#/$defs/...), allOf, oneOf and boolean schemas."""

    def __init__(self, root: dict):
        self.root = root

    def resolve(self, ref: str) -> dict:
        if not ref.startswith("#/"):
            raise ValueError(f"unsupported $ref {ref}")
        node = self.root
        for part in ref[2:].split("/"):
            node = node[part]
        return node

    @staticmethod
    def type_ok(value, name: str) -> bool:
        if name == "object":
            return isinstance(value, dict)
        if name == "array":
            return isinstance(value, list)
        if name == "string":
            return isinstance(value, str)
        if name == "integer":
            return isinstance(value, int) and not isinstance(value, bool)
        if name == "number":
            return isinstance(value, (int, float)) and not isinstance(value, bool)
        if name == "boolean":
            return isinstance(value, bool)
        if name == "null":
            return value is None
        raise ValueError(f"unsupported type {name}")

    def validate(self, value, schema, path: str = "$") -> list[str]:
        if schema is True:
            return []
        if schema is False:
            return [f"{path}: not allowed"]
        errors: list[str] = []
        if "$ref" in schema:
            errors += self.validate(value, self.resolve(schema["$ref"]), path)
        if "type" in schema:
            names = schema["type"] if isinstance(schema["type"], list) else [schema["type"]]
            if not any(self.type_ok(value, name) for name in names):
                return errors + [f"{path}: expected {schema['type']}, got {type(value).__name__}"]
        if "const" in schema and value != schema["const"]:
            errors.append(f"{path}: must equal {schema['const']!r}")
        if "enum" in schema and value not in schema["enum"]:
            errors.append(f"{path}: {value!r} not in {schema['enum']}")
        if isinstance(value, str):
            if "minLength" in schema and len(value) < schema["minLength"]:
                errors.append(f"{path}: shorter than {schema['minLength']}")
            if "pattern" in schema and not re.search(schema["pattern"], value):
                errors.append(f"{path}: {value!r} does not match {schema['pattern']}")
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            if "minimum" in schema and value < schema["minimum"]:
                errors.append(f"{path}: below {schema['minimum']}")
            if "maximum" in schema and value > schema["maximum"]:
                errors.append(f"{path}: above {schema['maximum']}")
        if isinstance(value, list):
            if "minItems" in schema and len(value) < schema["minItems"]:
                errors.append(f"{path}: fewer than {schema['minItems']} items")
            if "maxItems" in schema and len(value) > schema["maxItems"]:
                errors.append(f"{path}: more than {schema['maxItems']} items")
            if "items" in schema:
                for index, item in enumerate(value):
                    errors += self.validate(item, schema["items"], f"{path}[{index}]")
        if isinstance(value, dict):
            if "minProperties" in schema and len(value) < schema["minProperties"]:
                errors.append(f"{path}: fewer than {schema['minProperties']} properties")
            for key in schema.get("required", []):
                if key not in value:
                    errors.append(f"{path}: missing required '{key}'")
            properties = schema.get("properties", {})
            for key, item in value.items():
                if key in properties:
                    errors += self.validate(item, properties[key], f"{path}.{key}")
                elif "additionalProperties" in schema:
                    errors += self.validate(item, schema["additionalProperties"], f"{path}.{key}")
        for sub in schema.get("allOf", []):
            errors += self.validate(value, sub, path)
        if "oneOf" in schema:
            results = [self.validate(value, sub, path) for sub in schema["oneOf"]]
            passing = [r for r in results if not r]
            if len(passing) != 1:
                if not passing:
                    best = min(results, key=len)
                    errors += [f"{path}: matches no oneOf branch"] + best
                else:
                    errors.append(f"{path}: matches {len(passing)} oneOf branches")
        return errors


# --------------------------------------------------------------------------- loading and paths

def load_profiles(repo: Path) -> dict[str, dict]:
    root = repo / "data" / "profiles"
    profiles = {}
    for path in sorted(root.rglob("*.json")):
        relative = path.relative_to(repo).as_posix()
        if relative == "data/profiles/schema.json":
            continue
        profiles[relative] = json.loads(path.read_text(encoding="utf-8"))
    return profiles


def profile_set_sha256(repo: Path) -> str:
    """sha256 over '<relative path>\\n<sha256 of LF-normalised bytes>\\n' for every data/profiles JSON, sorted by path.
    cmake/GenerateGameProfileTables.cmake computes the same value."""
    manifest = ""
    for path in sorted((repo / "data" / "profiles").rglob("*.json"), key=lambda p: p.relative_to(repo).as_posix()):
        data = path.read_bytes().replace(b"\r\n", b"\n")
        manifest += f"{path.relative_to(repo).as_posix()}\n{hashlib.sha256(data).hexdigest()}\n"
    return hashlib.sha256(manifest.encode("ascii")).hexdigest()


PATH_SEGMENT = re.compile(r"^(?P<name>[A-Za-z_][A-Za-z0-9_]*)(?:\[(?:(?P<key>[A-Za-z_]+)=(?P<value>[^\]]*)|(?P<index>\d+))\])?$")


def select(document, path: str):
    """Resolve 'a.b[key=value].c[0]'; raise KeyError when any step fails."""
    node = document
    for segment in path.split("."):
        match = PATH_SEGMENT.match(segment)
        if not match:
            raise KeyError(f"bad path segment {segment!r}")
        if not isinstance(node, dict) or match["name"] not in node:
            raise KeyError(f"{segment}: no member {match['name']}")
        node = node[match["name"]]
        if match["key"] is not None:
            if not isinstance(node, list):
                raise KeyError(f"{segment}: not a list")
            hits = [item for item in node if isinstance(item, dict) and str(item.get(match["key"])) == match["value"]]
            if len(hits) != 1:
                raise KeyError(f"{segment}: {len(hits)} matches")
            node = hits[0]
        elif match["index"] is not None:
            index = int(match["index"])
            if not isinstance(node, list) or index >= len(node):
                raise KeyError(f"{segment}: index out of range")
            node = node[index]
    return node


# --------------------------------------------------------------------------- checks

def walk_citations(profile: dict, name: str) -> tuple[list[str], list[str], int]:
    """Return (errors, citations, cited_value_count)."""
    errors: list[str] = []
    cites: list[str] = []
    count = 0

    def visit(node, path: tuple, cited: bool):
        nonlocal count
        if isinstance(node, dict):
            own = node.get("cite")
            if own is not None:
                if not isinstance(own, list) or not own or not all(isinstance(c, str) for c in own):
                    errors.append(f"{name}: {'.'.join(map(str, path)) or '$'}: cite must be a non-empty list of strings")
                else:
                    cites.extend(own)
                cited = True
            for key, value in node.items():
                if key == "cite":
                    continue
                if key in PROSE_KEYS or (len(path) == 0 and key in TOP_METADATA) or (path + (key,)) in EXEMPT_PATHS:
                    continue
                visit(value, path + (key,), cited)
        elif isinstance(node, list):
            for index, value in enumerate(node):
                visit(value, path + (index,), cited)
        else:
            if not cited:
                errors.append(f"{name}: uncited value at {'.'.join(map(str, path))}: {node!r}")
            else:
                count += 1

    visit(profile, (), False)
    return errors, cites, count


def check_cite_roots(profile: dict, name: str, cites: list[str]) -> list[str]:
    errors = []
    declared = set(profile.get("sources", {}))
    for cite in cites:
        if cite == "generator-current" or cite.startswith("generator-current:"):
            if profile.get("profile_kind") != "generator":
                errors.append(f"{name}: generator-current citation in a module profile: {cite}")
            continue
        match = PROGS_CITE.match(cite) if cite.startswith("progs1996:") else FILE_CITE.match(cite)
        if not match:
            errors.append(f"{name}: malformed citation {cite!r}")
            continue
        root = "progs1996" if cite.startswith("progs1996:") else match["root"]
        if root not in declared:
            errors.append(f"{name}: citation root '{root}' is not declared in sources: {cite}")
        first, last = match["first"], match["last"]
        if first and last and int(last) < int(first):
            errors.append(f"{name}: inverted range in {cite}")
    return errors


def catalogue(profile: dict) -> dict[str, str]:
    """classname -> role/kind for every item and classname record."""
    out = {}
    for item in profile.get("items", []):
        out[item["classname"]] = f"item:{item['kind']}:{item['tier']}"
    for record in profile.get("classnames", []):
        out.setdefault(record["classname"], record["role"])
    return out


def check_module_semantics(profile: dict, name: str) -> list[str]:
    errors = []
    expected_dir = GAME_DIR[profile["game"]]
    if f"/{expected_dir}/" not in name:
        errors.append(f"{name}: game {profile['game']} must live under data/profiles/{expected_dir}/")
    if not profile["module"].startswith(expected_dir + "-"):
        errors.append(f"{name}: module id {profile['module']} must start with {expected_dir}-")
    rule_ids = [rule["id"] for rule in profile["respawn_rules"]]
    if len(rule_ids) != len(set(rule_ids)):
        errors.append(f"{name}: duplicate respawn rule ids")
    for rule in profile["respawn_rules"]:
        case_ids = [case["id"] for case in rule["cases"]]
        if len(case_ids) != len(set(case_ids)):
            errors.append(f"{name}: duplicate case ids in respawn rule {rule['id']}")
        for case in rule["cases"]:
            if case["behaviour"] in ("respawn", "respawn_after_rot", "auto_return", "relocated") and not (
                    "seconds" in case or "seconds_from" in case or "seconds_range" in case):
                errors.append(f"{name}: respawn rule {rule['id']}.{case['id']} has no time")
    item_names = [item["classname"] for item in profile["items"]]
    if len(item_names) != len(set(item_names)):
        errors.append(f"{name}: duplicate item classnames")
    class_names = [record["classname"] for record in profile["classnames"]]
    if len(class_names) != len(set(class_names)):
        errors.append(f"{name}: duplicate classnames")
    overlap = set(item_names) & set(class_names)
    if overlap:
        errors.append(f"{name}: classnames listed as both item and entity: {sorted(overlap)}")
    valid_rules = set(rule_ids) | {"none"}
    for item in profile["items"]:
        refs = [item["respawn"]] + [variant["respawn"] for variant in item.get("variants", [])]
        for ref in refs:
            if ref not in valid_rules:
                errors.append(f"{name}: item {item['classname']} names unknown respawn rule {ref}")
        for rule in profile["respawn_rules"]:
            if rule["id"] == item["respawn"] and any(case.get("seconds_from") == "quantity" for case in rule["cases"]) and "quantity" not in item:
                errors.append(f"{name}: item {item['classname']} uses a quantity-timed rule but has no quantity")
    known = set(item_names) | set(class_names)
    for contract in profile["mode_contracts"]:
        for entity in contract["entities"]:
            if entity["classname"] not in known:
                errors.append(f"{name}: contract {contract['mode']} names uncatalogued classname {entity['classname']}")
    for correction in profile["digest_corrections"]:
        try:
            select(profile, correction["path"])
        except KeyError as exc:
            errors.append(f"{name}: digest correction path {correction['path']} does not resolve ({exc})")
    return errors


def check_fixture(profiles: dict[str, dict], fixture: dict) -> tuple[list[str], int]:
    by_module = {p["module"]: p for p in profiles.values() if p.get("profile_kind") == "module"}
    errors = []
    passed = 0
    for check in fixture["checks"]:
        module = by_module.get(check["module"])
        label = f"fixture {check['id']} ({check['module']})"
        if module is None:
            errors.append(f"{label}: module profile missing")
            continue
        if "path" in check:
            try:
                value = select(module, check["path"])
            except KeyError as exc:
                errors.append(f"{label}: {check['path']} does not resolve ({exc})")
                continue
            if value != check["expect"]:
                errors.append(f"{label}: {check['path']} is {value!r}, the facts digest says {check['expect']!r} ({check['digest']})")
                continue
        elif "absent_classname" in check:
            if check["absent_classname"] in catalogue(module):
                errors.append(f"{label}: {check['absent_classname']} must not be catalogued ({check['digest']})")
                continue
        elif "present_classname" in check:
            if check["present_classname"] not in catalogue(module):
                errors.append(f"{label}: {check['present_classname']} must be catalogued ({check['digest']})")
                continue
        elif "respawn_when_contains" in check:
            bad = [f"{rule['id']}.{case['id']}" for rule in module["respawn_rules"] for case in rule["cases"]
                   if case["behaviour"] in ("respawn", "respawn_after_rot") and check["respawn_when_contains"] not in case["when"]]
            if bad:
                errors.append(f"{label}: respawn cases without '{check['respawn_when_contains']}': {bad} ({check['digest']})")
                continue
        else:
            errors.append(f"{label}: unknown check form")
            continue
        passed += 1
    return errors, passed


def read_enums(repo: Path) -> dict[str, list[str]]:
    enums = {}
    for header in ["include/qmg/layout.hpp", "include/qmg/semantic_plan.hpp", "include/qmg/types.hpp"]:
        text = (repo / header).read_text(encoding="utf-8")
        for match in re.finditer(r"enum class (\w+)\s*(?::\s*[\w:]+)?\s*\{([^}]*)\}", text):
            body = re.sub(r"//[^\n]*", "", match.group(2))
            names = [part.split("=")[0].strip() for part in body.split(",")]
            enums[match.group(1)] = [n for n in names if n]
    return enums


def generator_values(profile: dict) -> list[str | int]:
    values: list = []
    for table in ("solid_materials", "liquid_materials", "classnames"):
        values += [fact["value"] for fact in profile[table].values() if fact["value"] is not None]
    for pair in profile["team_classnames"].values():
        values += [pair["red"]["value"], pair["blue"]["value"]]
    for fact in profile["variants"].values():
        values += list(fact["value"])
        if "unsupported_reason" in fact:
            values.append(fact["unsupported_reason"])
    for preset in profile["lighting"].values():
        values.append(preset["brightness"]["value"])
    return values


def compute_defects(profile: dict, modules: dict[str, dict]) -> set:
    found = set()
    weapons = list(profile["variants"]["weapon"]["value"])
    missing, starting = set(), set()
    for module in modules.values():
        pickups = {i["classname"] for i in module["items"] if i["kind"] == "weapon" and i["tier"] == "pickup"}
        missing |= pickups - set(weapons)
        starting |= {i["classname"] for i in module["items"] if i["kind"] == "weapon" and i["tier"] == "starting"} & set(weapons)
    if missing:
        found.add(("generator_omits_catalogue_weapon", tuple(sorted(missing)), ()))
    if starting:
        found.add(("generator_places_starting_weapon", tuple(sorted(starting)), ()))
    names = set(weapons) | set(profile["variants"]["monster"]["value"])
    names |= {fact["value"] for fact in profile["classnames"].values()}
    names |= {pair[t]["value"] for pair in profile["team_classnames"].values() for t in ("red", "blue")}
    groups: dict[tuple, set] = {}
    removed: dict[tuple, set] = {}
    for classname in names:
        present = tuple(m for m in profile["modules"] if classname in catalogue(modules[m]))
        if not present:
            found.add(("classname_not_in_any_module", (classname,), ()))
        elif len(present) != len(profile["modules"]):
            groups.setdefault(present, set()).add(classname)
        gone = tuple(m for m in profile["modules"] if catalogue(modules[m]).get(classname) == "removed")
        if gone:
            removed.setdefault(gone, set()).add(classname)
    for present, members in groups.items():
        found.add(("module_specific_classname", tuple(sorted(members)), present))
    for gone, members in removed.items():
        found.add(("classname_removed_in_module", tuple(sorted(members)), gone))
    eye = profile.get("uncompiled", {}).get("analysis_spawn_eye_offset_z", {}).get("value")
    if eye is not None and any(m["movement"]["eye_height"]["value"] != eye for m in modules.values()):
        found.add(("analysis_eye_offset_differs", (), ()))
    return found


def check_generators(profiles: dict[str, dict], enums: dict[str, list[str]]) -> list[str]:
    errors = []
    modules = {p["module"]: p for p in profiles.values() if p.get("profile_kind") == "module"}
    generators = {name: p for name, p in profiles.items() if p.get("profile_kind") == "generator"}
    games = sorted({p["game"] for p in generators.values()})
    if games != ["quake1", "quake2", "quake3"]:
        errors.append(f"generator-current files must exist for all three games, found {games}")
    for name, profile in generators.items():
        if f"/{GAME_DIR[profile['game']]}/" not in name:
            errors.append(f"{name}: game {profile['game']} in the wrong directory")
        expect = {
            "solid_materials": set(enums["MaterialRole"]),
            "liquid_materials": set(enums["BrushContents"]),
            "lighting": set(enums["LightingStyle"]),
        }
        for table, keys in expect.items():
            if set(profile[table]) != keys:
                errors.append(f"{name}: {table} keys {sorted(profile[table])} != C++ enumerators {sorted(keys)}")
        roles = set(enums["PlacementRole"])
        parts = [set(profile["classnames"]), set(profile["team_classnames"]), set(profile["variants"])]
        if parts[0] | parts[1] | parts[2] != roles or sum(len(p) for p in parts) != len(roles):
            errors.append(f"{name}: classnames, team_classnames and variants must partition PlacementRole {sorted(roles)}")
        for role, pair in profile["team_classnames"].items():
            if set(pair) != set(enums["Team"]):
                errors.append(f"{name}: team_classnames.{role} keys must be the Team enumerators")
        for value in generator_values(profile):
            if isinstance(value, str) and not SAFE_VALUE.match(value) and value != profile["variants"]["monster"].get("unsupported_reason"):
                errors.append(f"{name}: value {value!r} is not safe to compile into a C++ literal")
        # Change r1s-monster-hull-support: one spawn box per monster variant, keyed by classname, and none for a game
        # without monster variants.
        monsters = list(profile["variants"]["monster"]["value"])
        hulls = profile.get("monster_hulls", {})
        if sorted(hulls) != sorted(monsters):
            errors.append(f"{name}: monster_hulls keys {sorted(hulls)} != the monster variants {sorted(monsters)}")
        for classname, fact in hulls.items():
            mins, maxs = fact["value"]["mins"], fact["value"]["maxs"]
            if not all(low < 0 < high for low, high in zip(mins, maxs)):
                errors.append(f"{name}: monster_hulls.{classname} must hold its origin strictly inside (mins < 0 < maxs)")
        # Change r1s-sp-pickup-hull-support: one drop box per pickup classname the generator emits (every weapon
        # variant, the health and the armor classname) in the Quake 1 and Quake 2 profiles, keyed by classname, and none
        # for Quake 3 (the cause is Quake 1 and Quake 2 single player only). A Quake 1 item's origin may be the box's
        # minimum corner (item_health '0 0 0' '32 32 56'), so the origin must lie inside the closed box.
        if profile["game"] in ("quake1", "quake2"):
            pickups = sorted(set(profile["variants"]["weapon"]["value"]) |
                             {profile["classnames"][role]["value"] for role in ("health", "armor")
                              if role in profile["classnames"]})
            pickup_hulls = profile.get("pickup_hulls", {})
            if sorted(pickup_hulls) != pickups:
                errors.append(f"{name}: pickup_hulls keys {sorted(pickup_hulls)} != the pickup classnames {pickups}")
            for classname, fact in pickup_hulls.items():
                mins, maxs = fact["value"]["mins"], fact["value"]["maxs"]
                if not all(low <= 0 <= high and low < high for low, high in zip(mins, maxs)):
                    errors.append(f"{name}: pickup_hulls.{classname} must hold its origin inside the closed box "
                                  "(mins <= 0 <= maxs, mins < maxs)")
        elif "pickup_hulls" in profile:
            errors.append(f"{name}: pickup_hulls is for the Quake 1 and Quake 2 profiles only")
        listed = profile["modules"]
        own = {m: modules[m] for m in listed if m in modules}
        for module in listed:
            if module not in modules:
                errors.append(f"{name}: lists unknown module {module}")
            elif modules[module]["game"] != profile["game"]:
                errors.append(f"{name}: module {module} belongs to another game")
        expected_modules = sorted(m for m, p in modules.items() if p["game"] == profile["game"])
        if sorted(listed) != expected_modules:
            errors.append(f"{name}: modules {sorted(listed)} != every {profile['game']} module profile {expected_modules}")
        if len(own) != len(listed):
            continue
        computed = compute_defects(profile, own)
        for kind, classnames, _modules in sorted(computed):
            if kind == "classname_not_in_any_module":
                errors.append(f"{name}: generator classname {classnames[0]} is catalogued by no listed module")
        computed = {c for c in computed if c[0] != "classname_not_in_any_module"}
        declared = set()
        for defect in profile["known_defects"]:
            if defect["check"] != "computed":
                continue
            declared.add((defect["kind"], tuple(sorted(defect.get("classnames", []))), tuple(defect.get("modules_with", []))))
        for item in sorted(computed - declared):
            errors.append(f"{name}: undeclared generator/profile disagreement {item}")
        for item in sorted(declared - computed):
            errors.append(f"{name}: declared computed defect no longer holds (stale) {item}")
        ids = [d["id"] for d in profile["known_defects"]]
        if len(ids) != len(set(ids)):
            errors.append(f"{name}: duplicate known_defect ids")
    return errors


def check_header(profiles: dict[str, dict], header: Path, repo: Path) -> list[str]:
    errors = []
    text = header.read_text(encoding="utf-8")
    expected = profile_set_sha256(repo)
    match = re.search(r'kProfileSetSha256\s*=\s*"([0-9a-f]{64})"', text)
    if not match:
        errors.append(f"{header}: no kProfileSetSha256")
    elif match.group(1) != expected:
        errors.append(f"{header}: profile-set sha256 {match.group(1)} != {expected} (stale header)")
    literals = set(re.findall(r'"([^"\\]*)"', text))
    numbers = set(re.findall(r"\b\d+\b", text))
    for name, profile in profiles.items():
        if profile.get("profile_kind") != "generator":
            continue
        for value in generator_values(profile):
            if isinstance(value, str) and value not in literals:
                errors.append(f"{header}: missing compiled value {value!r} from {name}")
            if isinstance(value, int) and str(value) not in numbers:
                errors.append(f"{header}: missing compiled number {value} from {name}")
        # Change r1s-monster-hull-support: each spawn box compiles to exactly its values.
        for classname, fact in profile.get("monster_hulls", {}).items():
            mins, maxs = fact["value"]["mins"], fact["value"]["maxs"]
            expected = (f'if (classname == "{classname}")\n            return MonsterHull{{{{{mins[0]}, {mins[1]}, {mins[2]}}}, '
                        f'{{{maxs[0]}, {maxs[1]}, {maxs[2]}}}}};')
            if expected not in text:
                errors.append(f"{header}: missing compiled monster hull {classname} {mins} {maxs} from {name}")
        # Change r1s-sp-pickup-hull-support: each pickup drop box compiles to exactly its values.
        for classname, fact in profile.get("pickup_hulls", {}).items():
            mins, maxs = fact["value"]["mins"], fact["value"]["maxs"]
            expected = (f'if (classname == "{classname}")\n            return PickupHull{{{{{mins[0]}, {mins[1]}, {mins[2]}}}, '
                        f'{{{maxs[0]}, {maxs[1]}, {maxs[2]}}}}};')
            if expected not in text:
                errors.append(f"{header}: missing compiled pickup hull {classname} {mins} {maxs} from {name}")
    return errors


# --------------------------------------------------------------------------- citation resolution

class Resolver:
    def __init__(self, roots: dict[str, Path], progs: Path | None):
        self.roots = roots
        self.progs_path = progs
        self._progs = None
        self._files: dict[Path, list[str]] = {}

    def progs(self):
        if self._progs is None:
            sys.path.insert(0, str(HERE))
            import progs_disasm
            self._progs = progs_disasm.load(str(self.progs_path))
        return self._progs

    def lines(self, path: Path) -> list[str]:
        if path not in self._files:
            self._files[path] = path.read_text(encoding="latin-1").splitlines()
        return self._files[path]

    def check(self, cite: str) -> str | None:
        """None when the citation resolves; otherwise the reason."""
        if cite.startswith("generator-current"):
            return None
        if cite.startswith("progs1996:"):
            if self.progs_path is None:
                return "no --progs given"
            match = PROGS_CITE.match(cite)
            progs = self.progs()
            token = norm(match["token"] or "")
            if match["header"]:
                return None if token in progs.header_text() else "header token mismatch"
            if match["global"]:
                value = progs.globals.get(match["global"])
                if value is None:
                    return "global not found"
                return None if token in norm(value) else f"global value {value} lacks token"
            function = progs.functions.get(match["function"])
            if function is None:
                return "function not found"
            first = int(match["first"])
            last = int(match["last"] or first)
            if not match["last"] and not match["token"]:
                return None if function.first_statement == first else f"first statement is {function.first_statement}"
            if any(i not in function.statements for i in range(first, last + 1)):
                return "statement range outside the function"
            text = norm(" ".join(function.statements[i] for i in range(first, last + 1)))
            return None if token in text else "token not in the statements"
        match = FILE_CITE.match(cite)
        root = self.roots.get(match["root"])
        if root is None:
            return f"root {match['root']} not given"
        path = root / match["path"]
        if not path.is_file():
            return "file not found"
        body = self.lines(path)
        first = int(match["first"])
        last = int(match["last"] or first)
        if last > len(body):
            return f"line {last} beyond end ({len(body)})"
        if not match["token"]:
            return None
        text = norm(" ".join(body[first - 1:last]))
        return None if norm(match["token"]) in text else "token not in the cited lines"


# --------------------------------------------------------------------------- driver

def run_checks(repo: Path, profiles: dict[str, dict], schema: dict, fixture: dict, enums: dict,
               header: Path | None) -> tuple[list[str], dict]:
    errors: list[str] = []
    stats = {"profiles": len(profiles), "cited_values": 0, "citations": 0, "fixture_checks": 0}
    validator = SchemaValidator(schema)
    for name, profile in profiles.items():
        schema_errors = validator.validate(profile, schema)
        errors += [f"{name}: schema: {e}" for e in schema_errors]
        cite_errors, cites, count = walk_citations(profile, name)
        errors += cite_errors
        stats["cited_values"] += count
        stats["citations"] += len(cites)
        errors += check_cite_roots(profile, name, cites)
        if not schema_errors and profile.get("profile_kind") == "module":
            errors += check_module_semantics(profile, name)
    modules = [p["module"] for p in profiles.values() if p.get("profile_kind") == "module"]
    if len(modules) != len(set(modules)):
        errors.append("duplicate module ids")
    if not any(e for e in errors if ": schema:" in e):
        fixture_errors, passed = check_fixture(profiles, fixture)
        errors += fixture_errors
        stats["fixture_checks"] = passed
        errors += check_generators(profiles, enums)
    if header is not None:
        errors += check_header(profiles, header, repo)
    return errors, stats


def mutations():
    """(name, profile file, mutate(profiles) -> None). Each must make run_checks fail."""

    def drop_cite(p):
        del p["data/profiles/q2/baseq2-3.19.json"]["movement"]["eye_height"]["cite"]

    def uncited_value(p):
        p["data/profiles/q3/baseq3-1.32b.json"]["movement"]["air_accelerate"] = {"value": 1}

    def uncited_leaf(p):
        p["data/profiles/q1/id1-1996.json"]["runtime"]["extra"] = 3

    def q3_weapon_30(p):
        rule = select(p["data/profiles/q3/baseq3-1.32b.json"], "respawn_rules[id=weapon].cases[id=other_gametypes]")
        rule["seconds"] = 30

    def q2_not_coop_live(p):
        select(p["data/profiles/q2/baseq2-3.19.json"], "spawnflags[name=NOT_COOP]")["tested_when"] = "coop != 0"

    def q2_trigger_teleport(p):
        p["data/profiles/q2/baseq2-3.19.json"]["classnames"].append(
            {"classname": "trigger_teleport", "role": "teleporter", "cite": ["q2src:game/g_spawn.c:1|x"]})

    def q1_mega_rerelease_20(p):
        select(p["data/profiles/q1/rerelease-2021.json"], "respawn_rules[id=mega_health].cases[id=deathmatch]")["seconds"] = 20

    def q1_ssg_added(p):
        p["data/profiles/q1/generator-current.json"]["variants"]["weapon"]["value"].append("weapon_supershotgun")

    def missing_role(p):
        del p["data/profiles/q2/generator-current.json"]["classnames"]["armor"]

    def bad_cite(p):
        p["data/profiles/q2/baseq2-3.19.json"]["movement"]["gravity"]["cite"] = ["q2src:game/g_save.c"]

    def undeclared_root(p):
        p["data/profiles/q3/baseq3-1.32b.json"]["movement"]["gravity"]["cite"] = ["q4src:code/x.c:1"]

    def uncatalogued_generator_class(p):
        p["data/profiles/q3/generator-current.json"]["classnames"]["light"]["value"] = "light_spot"

    def unsafe_literal(p):
        p["data/profiles/q2/generator-current.json"]["solid_materials"]["floor"]["value"] = 'e1u1/"floor'

    def stale_declared(p):
        p["data/profiles/q3/generator-current.json"]["known_defects"].append(
            {"id": "q3-stale", "check": "computed", "kind": "generator_omits_catalogue_weapon", "classnames": ["weapon_gauntlet"],
             "detail": "control", "cite": ["generator-current"]})

    def eye_offset_fixed(p):
        p["data/profiles/q3/generator-current.json"]["uncompiled"]["analysis_spawn_eye_offset_z"]["value"] = 26

    def generator_cite_in_module(p):
        p["data/profiles/q2/ctf-3.19.json"]["movement"]["gravity"]["cite"] = ["generator-current"]

    def bad_correction_path(p):
        p["data/profiles/q2/ctf-3.19.json"]["digest_corrections"][0]["path"] = "respawn_rules[id=nothing].seconds"

    def monster_hull_missing(p):
        del p["data/profiles/q1/generator-current.json"]["monster_hulls"]["monster_ogre"]

    def monster_hull_without_origin(p):
        p["data/profiles/q2/generator-current.json"]["monster_hulls"]["monster_gladiator"]["value"]["mins"][2] = 0

    def pickup_hull_missing(p):
        del p["data/profiles/q1/generator-current.json"]["pickup_hulls"]["item_health"]

    def pickup_hull_without_origin(p):
        p["data/profiles/q2/generator-current.json"]["pickup_hulls"]["weapon_railgun"]["value"]["mins"][0] = 1

    return [
        ("removed citation", drop_cite, "citations"), ("fact without cite", uncited_value, "citations"),
        ("uncited leaf", uncited_leaf, "citations"),
        ("Q3 weapons 30 s outside GT_TEAM", q3_weapon_30, "fixture"), ("Q2 NOT_COOP live", q2_not_coop_live, "fixture"),
        ("Q2 baseq2 trigger_teleport", q2_trigger_teleport, "fixture"), ("Q1 re-release MH 20 s", q1_mega_rerelease_20, "fixture"),
        ("generator SSG added without updating defects", q1_ssg_added, "generator"), ("generator role missing", missing_role, "generator"),
        ("citation without line", bad_cite, "cite_roots"), ("undeclared citation root", undeclared_root, "cite_roots"),
        ("generator classname not catalogued", uncatalogued_generator_class, "generator"), ("unsafe C++ literal", unsafe_literal, "schema"),
        ("stale declared defect", stale_declared, "generator"), ("eye offset fixed without updating defects", eye_offset_fixed, "generator"),
        ("generator-current citation in a module profile", generator_cite_in_module, "cite_roots"),
        ("digest correction path broken", bad_correction_path, "semantics"),
        ("monster variant without a hull", monster_hull_missing, "generator"),
        ("monster hull without its origin inside", monster_hull_without_origin, "generator"),
        ("pickup classname without a hull", pickup_hull_missing, "generator"),
        ("pickup hull without its origin inside", pickup_hull_without_origin, "generator"),
    ]


def categories(errors: list[str]) -> set[str]:
    tags = set()
    for error in errors:
        if ": schema:" in error:
            tags.add("schema")
        elif "uncited value" in error or "cite must be" in error:
            tags.add("citations")
        elif "citation root" in error or "malformed citation" in error or "generator-current citation" in error or "inverted range" in error:
            tags.add("cite_roots")
        elif error.startswith("fixture "):
            tags.add("fixture")
        elif "generator-current.json" in error or error.startswith("generator-current files"):
            tags.add("generator")
        elif error.startswith("resolve:"):
            tags.add("resolve")
        else:
            tags.add("semantics")
    return tags


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", type=Path, default=DEFAULT_REPO)
    parser.add_argument("--header", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--resolve", action="store_true")
    parser.add_argument("--root", action="append", default=[], help="NAME=DIR for --resolve (repo defaults to --repo)")
    parser.add_argument("--progs", type=Path, help="1996 id1 progs.dat for --resolve")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args(argv)
    repo = args.repo.resolve()
    schema = json.loads((repo / "data/profiles/schema.json").read_text(encoding="utf-8"))
    fixture = json.loads((repo / "tests/fixtures/profiles/facts.json").read_text(encoding="utf-8"))
    enums = read_enums(repo)
    profiles = load_profiles(repo)
    errors, stats = run_checks(repo, profiles, schema, fixture, enums, args.header)
    report: dict = {"stats": stats, "errors": errors}
    stats["profile_set_sha256"] = profile_set_sha256(repo)

    if args.self_test:
        controls = []
        for label, mutate, expected in mutations():
            mutated = copy.deepcopy(profiles)
            mutate(mutated)
            control_errors, _ = run_checks(repo, mutated, schema, fixture, enums, None)
            tags = categories(control_errors)
            hits = [e for e in control_errors if expected in categories([e])]
            controls.append({"control": label, "expected_check": expected, "rejected": bool(hits), "checks_fired": sorted(tags),
                             "error": hits[0] if hits else (control_errors[0] if control_errors else None)})
            if not hits:
                errors.append(f"self-test: control '{label}' was not rejected by the {expected} check (fired: {sorted(tags)})")
        report["controls"] = controls
        stats["controls_rejected"] = sum(1 for c in controls if c["rejected"])
        stats["controls_total"] = len(controls)

    if args.resolve:
        roots = {"repo": repo}
        for item in args.root:
            name, _, value = item.partition("=")
            roots[name] = Path(value)
        resolver = Resolver(roots, args.progs)
        counts = {"file": 0, "progs1996": 0, "generator_current_unresolvable": 0}
        failures = []
        for name, profile in profiles.items():
            _, cites, _ = walk_citations(profile, name)
            for cite in sorted(set(cites)):
                reason = resolver.check(cite)
                if reason is not None:
                    failures.append(f"{name}: {cite}: {reason}")
                elif cite.startswith("generator-current"):
                    counts["generator_current_unresolvable"] += 1
                elif cite.startswith("progs1996:"):
                    counts["progs1996"] += 1
                else:
                    counts["file"] += 1
        errors += [f"resolve: {f}" for f in failures]
        stats["citations_resolved_file"] = counts["file"]
        stats["citations_resolved_progs1996"] = counts["progs1996"]
        stats["citations_generator_current"] = counts["generator_current_unresolvable"]
        stats["citations_unresolved"] = len(failures)

    report["errors"] = errors
    for line in errors:
        print("ERROR", line)
    summary = ", ".join(f"{k}={v}" for k, v in stats.items())
    print(("FAIL " if errors else "PASS ") + summary)
    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
