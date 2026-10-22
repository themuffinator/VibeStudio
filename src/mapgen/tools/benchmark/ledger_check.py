#!/usr/bin/env python3
"""Integrity check for the RC ledger and the tracked benchmark registries (stdlib only).

  python -B tools/benchmark/ledger_check.py [--ledger tools/benchmark/rc-ledger.json] [--json out.json]

Checks (errors fail the run, exit 1; warnings are printed):
  1. rc-ledger.json validates against rc-ledger.schema.json (a stdlib subset of JSON Schema).
  2. The ledger has the 17 paths of BENCH-v1, with the same game, mode, module and claimed tier.
  3. Every evidence pointer resolves to a repository file whose sha256 matches; 'plan record' pointers are
     allowed only in 'sources'; a status of pass, met or partial has at least one evidence pointer.
  4. Waivers: a reason code from the closed list (schema enum, also listed in README.md), the user's approval,
     at most 5% of the measured instances per gate per path, and none on a hero unless that hero's HEROES entry
     declared the instance out of scope before generation.
  5. tools/benchmark/manifests.json hashes match the files; sealed.json names cases that exist with the same seed
     and prompt; hero seeds appear in no other fixed set; baselines/index.json entries are well formed and, when
     their out directory exists locally, still match summary.json.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BENCH = REPO / "tools" / "benchmark"
PASSING = {"pass", "met", "partial"}


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


# --------------------------------------------------------------------------- minimal JSON Schema subset

def validate(value, schema, root, where="$", errors=None):
    errors = [] if errors is None else errors
    if "$ref" in schema:
        ref = schema["$ref"]
        if not ref.startswith("#/"):
            errors.append(f"{where}: unsupported $ref {ref}")
            return errors
        target = root
        for part in ref[2:].split("/"):
            target = target[part]
        return validate(value, target, root, where, errors)
    if "const" in schema and value != schema["const"]:
        errors.append(f"{where}: expected {schema['const']!r}, found {value!r}")
    if "enum" in schema and value not in schema["enum"]:
        errors.append(f"{where}: {value!r} not in {schema['enum']}")
    kind = schema.get("type")
    types = {"object": dict, "array": list, "string": str, "boolean": bool, "null": type(None)}
    if kind == "integer":
        if not isinstance(value, int) or isinstance(value, bool):
            errors.append(f"{where}: expected integer")
            return errors
    elif kind == "number":
        if not isinstance(value, (int, float)) or isinstance(value, bool):
            errors.append(f"{where}: expected number")
            return errors
    elif kind in types and not isinstance(value, types[kind]):
        errors.append(f"{where}: expected {kind}, found {type(value).__name__}")
        return errors
    if isinstance(value, str) and "pattern" in schema and not re.search(schema["pattern"], value):
        errors.append(f"{where}: {value!r} does not match {schema['pattern']}")
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if "minimum" in schema and value < schema["minimum"]:
            errors.append(f"{where}: {value} < {schema['minimum']}")
        if "maximum" in schema and value > schema["maximum"]:
            errors.append(f"{where}: {value} > {schema['maximum']}")
    if isinstance(value, list):
        if "minItems" in schema and len(value) < schema["minItems"]:
            errors.append(f"{where}: fewer than {schema['minItems']} items")
        if "items" in schema:
            for i, item in enumerate(value):
                validate(item, schema["items"], root, f"{where}[{i}]", errors)
    if isinstance(value, dict):
        for key in schema.get("required", []):
            if key not in value:
                errors.append(f"{where}: missing required key {key!r}")
        props = schema.get("properties", {})
        for key, item in value.items():
            if key in props:
                validate(item, props[key], root, f"{where}.{key}", errors)
            elif "additionalProperties" in schema:
                extra = schema["additionalProperties"]
                if extra is False:
                    errors.append(f"{where}: unexpected key {key!r}")
                elif isinstance(extra, dict):
                    validate(item, extra, root, f"{where}.{key}", errors)
    return errors


# --------------------------------------------------------------------------- checks

class Report:
    def __init__(self):
        self.errors, self.warnings, self.notes = [], [], []

    def error(self, text):
        self.errors.append(text)

    def warn(self, text):
        self.warnings.append(text)


def check_pointer(pointer: dict, where: str, rep: Report, allow_plan: bool):
    path = REPO / pointer.get("path", "")
    if pointer.get("level") == "plan record" and not allow_plan:
        rep.error(f"{where}: 'plan record' is not an evidence level (allowed only in sources)")
    if not path.is_file():
        rep.error(f"{where}: pointer does not resolve: {pointer.get('path')}")
        return
    actual = sha256_file(path)
    if actual != pointer.get("sha256"):
        rep.error(f"{where}: sha256 mismatch for {pointer.get('path')} (recorded {str(pointer.get('sha256'))[:16]}, "
                  f"now {actual[:16]}); re-review the rows that rest on it, then re-pin")


def readme_reason_codes() -> set:
    text = (BENCH / "README.md").read_text(encoding="utf-8") if (BENCH / "README.md").is_file() else ""
    section = text.split("### Waiver reason codes", 1)[1] if "### Waiver reason codes" in text else ""
    section = section.split("\n## ", 1)[0].split("\n### ", 1)[0]
    return set(re.findall(r"^\s*[-|]\s*`([a-z_]+)`", section, re.M))


def check_ledger(ledger_path: Path, rep: Report):
    ledger = json.loads(ledger_path.read_text(encoding="utf-8"))
    schema = json.loads((BENCH / "rc-ledger.schema.json").read_text(encoding="utf-8"))
    for problem in validate(ledger, schema, schema):
        rep.error(f"schema: {problem}")
    bench = json.loads((BENCH / "rc1-v1.json").read_text(encoding="utf-8"))
    expected = bench.get("paths", {})
    paths = ledger.get("paths", {})
    if set(paths) != set(expected):
        rep.error(f"ledger paths {sorted(set(paths) ^ set(expected))} differ from BENCH-v1 paths")
    for name, row in paths.items():
        want = expected.get(name)
        if want:
            for key in ("game", "mode", "module", "claimed"):
                if row.get(key) != want.get(key):
                    rep.error(f"{name}.{key}: ledger {row.get(key)!r} != BENCH-v1 {want.get(key)!r}")
    for i, pointer in enumerate(ledger.get("sources", [])):
        check_pointer(pointer, f"sources[{i}]", rep, allow_plan=True)
    enum_codes = set(schema["$defs"]["waiver"]["properties"]["reason_code"]["enum"])
    readme_codes = readme_reason_codes()
    if readme_codes != enum_codes:
        rep.error(f"README waiver reason codes {sorted(readme_codes)} != schema closed list {sorted(enum_codes)}")
    heroes = json.loads((BENCH / "heroes.json").read_text(encoding="utf-8")).get("heroes", [])
    hero_declared = {h.get("path"): {str(x.get("instance") if isinstance(x, dict) else x)
                                     for x in h.get("declared_out_of_scope", [])} for h in heroes}
    totals = {"items": 0, "with_evidence": 0, "waivers": 0}
    for name, row in paths.items():
        groups = {}
        for block in ("rc", "parity", "sp_parity"):
            for key, item in (row.get(block) or {}).items():
                totals["items"] += 1
                where = f"{name}.{block}.{key}"
                for j, pointer in enumerate(item.get("evidence") or []):
                    check_pointer(pointer, f"{where}.evidence[{j}]", rep, allow_plan=False)
                if item.get("evidence"):
                    totals["with_evidence"] += 1
                if item.get("status") in PASSING and not item.get("evidence"):
                    rep.error(f"{where}: status {item['status']} without an evidence pointer")
        if row.get("human_playtest") == "done":
            rep.error(f"{name}: human_playtest 'done' needs a protocol record with 'human play' evidence")
        for k, waiver in enumerate(row.get("waivers") or []):
            totals["waivers"] += 1
            where = f"{name}.waivers[{k}]"
            if waiver.get("reason_code") not in enum_codes:
                rep.error(f"{where}: reason code {waiver.get('reason_code')!r} not in the closed list")
            approval = waiver.get("approved_by_user") or {}
            if approval.get("by") != "user" or not approval.get("date") or not approval.get("record"):
                rep.error(f"{where}: no user approval recorded (an agent cannot approve its own waivers)")
            for j, pointer in enumerate(waiver.get("evidence") or []):
                check_pointer(pointer, f"{where}.evidence[{j}]", rep, allow_plan=False)
            if waiver.get("hero"):
                if waiver.get("reason_code") != "declared_out_of_scope" or \
                        str(waiver.get("instance")) not in hero_declared.get(name, set()):
                    rep.error(f"{where}: hero waiver not predeclared in heroes.json (heroes allow 0 otherwise)")
            elif waiver.get("reason_code") == "declared_out_of_scope":
                rep.error(f"{where}: declared_out_of_scope is for heroes only")
            groups.setdefault(waiver.get("gate"), []).append(waiver)
        for gate, items in groups.items():
            measured = {w.get("measured_instances") for w in items}
            if len(measured) != 1:
                rep.error(f"{name} {gate}: waivers disagree on measured_instances {sorted(measured)}")
                continue
            n = measured.pop() or 0
            cap = math.floor(0.05 * n)
            non_hero = [w for w in items if not w.get("hero")]
            if len(non_hero) > cap:
                rep.error(f"{name} {gate}: {len(non_hero)} waivers exceed the 5% cap ({cap} of {n} instances)")
            rep.notes.append(f"{name} {gate}: {len(non_hero)} waiver(s) of {n} measured "
                             f"({100.0 * len(non_hero) / n if n else 0:.1f}%)")
    rep.notes.append(f"ledger: {len(paths)} paths, {totals['items']} items, {totals['with_evidence']} with evidence "
                     f"pointers, {totals['waivers']} waivers")


def check_registries(rep: Report):
    registry = json.loads((BENCH / "manifests.json").read_text(encoding="utf-8"))
    rows = registry.get("manifests", []) + ([registry["draft"]] if registry.get("draft") else [])
    seeds_by_set = {}
    for row in rows:
        path = REPO / row["path"]
        if not path.is_file():
            rep.error(f"manifests.json: {row['path']} is missing")
            continue
        actual = sha256_file(path)
        if actual != row["sha256"]:
            (rep.error if row.get("frozen") else rep.warn)(
                f"manifests.json: {row['path']} sha256 {actual[:16]} != registered {row['sha256'][:16]}"
                + ("" if row.get("frozen") else " (not frozen; re-register)"))
        data = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(data.get("cases"), list):
            seeds_by_set[row["set"]] = {c.get("seed") for c in data["cases"] if c.get("seed") is not None} | \
                                       {c.get("requested_seed") for c in data["cases"] if c.get("requested_seed")}
            if "cases_sha256" in data:
                canon = json.dumps(data["cases"], sort_keys=True, separators=(",", ":"), ensure_ascii=False)
                if hashlib.sha256(canon.encode("utf-8")).hexdigest() != data["cases_sha256"]:
                    rep.error(f"{row['path']}: cases_sha256 does not match its cases")
    bench = json.loads((BENCH / "rc1-v1.json").read_text(encoding="utf-8"))
    by_id = {c["id"]: c for c in bench["cases"]}
    sealed = json.loads((BENCH / "sealed.json").read_text(encoding="utf-8"))
    for entry in sealed.get("sealed", []):
        case = by_id.get(entry["id"])
        if not case:
            rep.error(f"sealed.json: {entry['id']} is not a BENCH-v1 case")
            continue
        if case["seed"] != entry.get("seed") or \
                hashlib.sha256(case["prompt"].encode("utf-8")).hexdigest() != entry.get("prompt_sha256"):
            rep.error(f"sealed.json: {entry['id']} seed or prompt sha256 differs from BENCH-v1")
    heroes = json.loads((BENCH / "heroes.json").read_text(encoding="utf-8"))
    for hero in heroes.get("heroes", []):
        for set_name, seeds in seeds_by_set.items():
            if hero.get("seed") in seeds:
                rep.error(f"heroes.json: {hero.get('path')} seed {hero.get('seed')} appears in {set_name}")
    if not heroes.get("heroes"):
        rep.notes.append("heroes.json: no hero declared yet (D26 pending)")
    index = json.loads((BENCH / "baselines" / "index.json").read_text(encoding="utf-8"))
    hexes = re.compile(r"^[0-9a-f]{64}$")
    ids = set()
    for entry in index.get("baselines", []):
        name = entry.get("id")
        if name in ids:
            rep.error(f"baselines: duplicate id {name}")
        ids.add(name)
        for key in index.get("required", []):
            if key not in entry:
                rep.error(f"baselines {name}: missing {key}")
        for key in ("qmg_sha256", "runner_sha256", "manifest_sha256", "summary_sha256"):
            if key in entry and not hexes.match(str(entry[key])):
                rep.error(f"baselines {name}: {key} is not a sha256")
        reg = next((r for r in rows if r["path"] == entry.get("manifest")), None)
        if reg and reg["sha256"] != entry.get("manifest_sha256"):
            rep.error(f"baselines {name}: manifest_sha256 differs from manifests.json")
        summary = REPO / str(entry.get("out_dir", "")) / "summary.json"
        if summary.is_file():
            if sha256_file(summary) != entry.get("summary_sha256"):
                rep.error(f"baselines {name}: {summary} no longer has the recorded sha256")
            data = json.loads(summary.read_text(encoding="utf-8"))
            if data.get("runner_version") != entry.get("runner_sha256"):
                rep.error(f"baselines {name}: summary runner {str(data.get('runner_version'))[:16]} != entry")
            if (data.get("qmg") or {}).get("sha256") != entry.get("qmg_sha256"):
                rep.error(f"baselines {name}: summary qmg sha256 != entry")
            if data.get("manifest_sha256") != entry.get("manifest_sha256"):
                rep.error(f"baselines {name}: summary manifest sha256 != entry")
        else:
            rep.warn(f"baselines {name}: {entry.get('out_dir')} is not present locally (outputs are local)")
    rep.notes.append(f"baselines: {len(index.get('baselines', []))} recorded")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ledger", type=Path, default=BENCH / "rc-ledger.json")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args(argv)
    rep = Report()
    check_ledger(args.ledger, rep)
    check_registries(rep)
    for note in rep.notes:
        print(f"note: {note}")
    for warning in rep.warnings:
        print(f"warning: {warning}")
    for error in rep.errors:
        print(f"ERROR: {error}")
    print(f"ledger_check: {len(rep.errors)} error(s), {len(rep.warnings)} warning(s)")
    if args.json:
        args.json.write_text(json.dumps({"errors": rep.errors, "warnings": rep.warnings, "notes": rep.notes},
                                        indent=2) + "\n", encoding="utf-8")
    return 1 if rep.errors else 0


if __name__ == "__main__":
    sys.exit(main())
