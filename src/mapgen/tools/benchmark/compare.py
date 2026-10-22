#!/usr/bin/env python3
"""Scoped diff of two benchmark run directories (stdlib only).

  python -B tools/benchmark/compare.py <run-a> <run-b> [--scope ID[,ID...] | --scope @ids.txt | --scope manifest.json]
                                       [--only IDS] [--generate-only] [--json out.json]
  python -B tools/benchmark/compare.py --self-test

A run directory holds one directory per case id with the generated <name>.map and <name>.qmg.json (and, for a
compiled run, result.json and the BSP). For every case present in either run it compares:

  raw bytes        .map and .qmg.json byte equality
  report fields    JSON paths whose values differ in the .qmg.json report
  brush planes     sorted canonical brush planes (normal, distance, texture) per entity
  entities         the multiset of entities (classname, key/values, brush count)
  textures         the face texture histogram
  gates/requests   (compiled runs, not with --generate-only) gate and request statuses from result.json

Cases inside --scope may differ. Any difference in a case outside the scope (including a case present in only
one run) is an out-of-scope difference, and the exit code is 1. With no --scope, every difference is out of
scope. BSP byte inequality is listed for information only (light is multithreaded) and never counted.
Exit codes: 0 no out-of-scope difference, 1 out-of-scope differences, 2 usage error.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import re
import sys
import tempfile
from pathlib import Path

TOKEN = re.compile(r'"((?:[^"\\]|\\.)*)"|(\S+)')
TRIPLE = re.compile(r"\(\s*([-+0-9.eE]+)\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)\s*\)")
LIST_LIMIT = 40


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


# --------------------------------------------------------------------------- map canonicalisation

def plane_key(line: str):
    points = TRIPLE.findall(line)
    if len(points) < 3:
        return None
    p0, p1, p2 = [tuple(float(v) for v in p) for p in points[:3]]
    a = (p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2])
    b = (p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2])
    n = (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
    length = (n[0] ** 2 + n[1] ** 2 + n[2] ** 2) ** 0.5
    if length == 0:
        return ("degenerate", line.strip())
    n = tuple(round(c / length, 6) + 0.0 for c in n)
    dist = round(n[0] * p0[0] + n[1] * p0[1] + n[2] * p0[2], 4) + 0.0
    # Everything after the last point triple (or brush-primitive matrix) is texture + parameters.
    rest = line[line.rfind(")") + 1:].split()
    texture = rest[0] if rest else ""
    return (n, dist, texture, tuple(rest[1:]))


def canonical_map(path: Path) -> dict:
    entities = []
    depth = 0
    current = None
    brush = None
    in_patch = False
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            line = raw.strip()
            if not line or line.startswith("//"):
                continue
            if line == "{":
                depth += 1
                if depth == 1:
                    current = {"keys": [], "brushes": [], "patches": 0}
                elif depth == 2:
                    brush = []
                continue
            if line == "}":
                if depth == 2 and brush is not None:
                    if in_patch:
                        current["patches"] += 1
                        in_patch = False
                    else:
                        current["brushes"].append(tuple(sorted(brush, key=repr)))
                    brush = None
                if depth == 1 and current is not None:
                    entities.append(current)
                    current = None
                depth -= 1
                continue
            if depth == 1 and line.startswith('"'):
                tokens = [m.group(1) if m.group(1) is not None else m.group(2) for m in TOKEN.finditer(line)]
                if len(tokens) >= 2:
                    current["keys"].append((tokens[0], tokens[1]))
                continue
            if depth == 2 and line in ("patchDef2", "patchDef3"):
                in_patch = True
                continue
            if depth >= 2 and not in_patch and line.startswith("("):
                key = plane_key(line)
                if key is not None and brush is not None:
                    brush.append(key)
    planes = collections.Counter()
    ents = collections.Counter()
    textures = collections.Counter()
    for index, entity in enumerate(entities):
        keys = dict(entity["keys"])
        cls = keys.get("classname", "<missing>")
        owner = "worldspawn" if cls == "worldspawn" else f"{cls}"
        for b in entity["brushes"]:
            planes[(owner, b)] += 1
            for face in b:
                if face[0] != "degenerate":
                    textures[face[2]] += 1
        ents[(cls, tuple(sorted(entity["keys"])), len(entity["brushes"]), entity["patches"])] += 1
    return {"brushes": planes, "entities": ents, "textures": textures}


def counter_delta(a: collections.Counter, b: collections.Counter, render) -> dict:
    only_a = a - b
    only_b = b - a
    return {"only_in_a": sum(only_a.values()), "only_in_b": sum(only_b.values()),
            "examples_a": [render(k) for k in list(only_a)[:5]], "examples_b": [render(k) for k in list(only_b)[:5]]}


def json_diff(a, b, path="$", out=None):
    out = [] if out is None else out
    if len(out) >= LIST_LIMIT:
        return out
    if type(a) is not type(b):
        out.append(path)
    elif isinstance(a, dict):
        for key in sorted(set(a) | set(b)):
            if key not in a or key not in b:
                out.append(f"{path}.{key}")
            else:
                json_diff(a[key], b[key], f"{path}.{key}", out)
    elif isinstance(a, list):
        if len(a) != len(b):
            out.append(f"{path}[len {len(a)} != {len(b)}]")
        for i, (x, y) in enumerate(zip(a, b)):
            json_diff(x, y, f"{path}[{i}]", out)
    elif a != b:
        out.append(path)
    return out


# --------------------------------------------------------------------------- run directories

def case_files(case_dir: Path) -> dict:
    maps = sorted(p for p in case_dir.glob("*.map") if p.is_file())
    preferred = [p for p in maps if p.stem == case_dir.name] or \
                [p for p in maps if (case_dir / (p.stem + ".qmg.json")).is_file()] or maps
    files = {}
    if preferred:
        stem = preferred[0].stem
        files["map"] = preferred[0]
        report = case_dir / f"{stem}.qmg.json"
        if report.is_file():
            files["report"] = report
        bsp = case_dir / f"{stem}.bsp"
        if bsp.is_file():
            files["bsp"] = bsp
    if (case_dir / "result.json").is_file():
        files["result"] = case_dir / "result.json"
    return files


def discover(run: Path) -> dict:
    return {d.name: d for d in sorted(run.iterdir()) if d.is_dir() and case_files(d).get("map")}


def compare_case(a: Path, b: Path, generate_only: bool) -> dict:
    fa, fb = case_files(a), case_files(b)
    diff = {}
    for kind in ("map", "report"):
        pa, pb = fa.get(kind), fb.get(kind)
        if (pa is None) != (pb is None):
            diff[f"{kind}_presence"] = {"a": pa is not None, "b": pb is not None}
        elif pa is not None and pa.read_bytes() != pb.read_bytes():
            diff[f"{kind}_bytes"] = {"a": sha256_file(pa), "b": sha256_file(pb)}
    if "map_bytes" in diff:
        ca, cb = canonical_map(fa["map"]), canonical_map(fb["map"])
        brushes = counter_delta(ca["brushes"], cb["brushes"], lambda k: f"{k[0]}: {len(k[1])} planes")
        if brushes["only_in_a"] or brushes["only_in_b"]:
            diff["brush_planes"] = brushes
        entities = counter_delta(ca["entities"], cb["entities"], lambda k: f"{k[0]} {dict(k[1])}")
        if entities["only_in_a"] or entities["only_in_b"]:
            diff["entities"] = entities
        if ca["textures"] != cb["textures"]:
            keys = sorted(set(ca["textures"]) | set(cb["textures"]))
            diff["textures"] = {k: [ca["textures"].get(k, 0), cb["textures"].get(k, 0)] for k in keys
                                if ca["textures"].get(k, 0) != cb["textures"].get(k, 0)}
        if not any(k in diff for k in ("brush_planes", "entities", "textures")):
            diff["map_text_only"] = "bytes differ but brush planes, entities and textures are equal"
    if "report_bytes" in diff:
        try:
            ra = json.loads(fa["report"].read_text(encoding="utf-8"))
            rb = json.loads(fb["report"].read_text(encoding="utf-8"))
            diff["report_fields"] = json_diff(ra, rb)
        except ValueError as error:
            diff["report_fields"] = [f"unparseable report: {error}"]
    info = {}
    if not generate_only:
        if fa.get("result") and fb.get("result"):
            ra = json.loads(fa["result"].read_text(encoding="utf-8"))
            rb = json.loads(fb["result"].read_text(encoding="utf-8"))
            ga = {k: v.get("status") for k, v in (ra.get("gates") or {}).items()}
            gb = {k: v.get("status") for k, v in (rb.get("gates") or {}).items()}
            gates = {k: [ga[k], gb[k]] for k in sorted(set(ga) & set(gb)) if ga[k] != gb[k]}
            if gates:
                diff["gates"] = gates
            if set(ga) ^ set(gb):
                info["gate_keys_in_one_run_only"] = sorted(set(ga) ^ set(gb))
            qa = [(r.get("request"), r.get("status")) for r in ra.get("requests") or []]
            qb = [(r.get("request"), r.get("status")) for r in rb.get("requests") or []]
            if qa != qb:
                diff["requests"] = [[x, y] for x, y in zip(qa, qb) if x != y] or {"lengths": [len(qa), len(qb)]}
        elif bool(fa.get("result")) != bool(fb.get("result")):
            diff["result_presence"] = {"a": bool(fa.get("result")), "b": bool(fb.get("result"))}
        if fa.get("bsp") and fb.get("bsp") and fa["bsp"].read_bytes() != fb["bsp"].read_bytes():
            info["bsp_bytes_differ"] = True
    return {"differences": diff, "info": info}


def load_scope(values: list) -> set | None:
    if not values:
        return None
    scope = set()
    for value in values:
        if value.startswith("@"):
            for line in Path(value[1:]).read_text(encoding="utf-8").splitlines():
                line = line.split("#")[0].strip()
                if line:
                    scope.add(line)
        elif value.endswith(".json") and Path(value).is_file():
            data = json.loads(Path(value).read_text(encoding="utf-8"))
            if isinstance(data, list):
                scope.update(str(x) for x in data)
            elif isinstance(data, dict) and "cases" in data:
                scope.update(c["id"] for c in data["cases"])
            elif isinstance(data, dict) and "ids" in data:
                scope.update(data["ids"])
            elif isinstance(data, dict) and "declared_scope" in data:
                scope.update(data["declared_scope"])
            else:
                raise SystemExit(f"cannot read a scope from {value}")
        else:
            scope.update(x.strip() for x in value.split(",") if x.strip())
    return scope


def compare_runs(run_a: Path, run_b: Path, scope: set | None, generate_only: bool, only: set | None = None) -> dict:
    cases_a, cases_b = discover(run_a), discover(run_b)
    if only is not None:
        cases_a = {k: v for k, v in cases_a.items() if k in only}
        cases_b = {k: v for k, v in cases_b.items() if k in only}
    rows = {}
    for cid in sorted(set(cases_a) | set(cases_b)):
        if cid not in cases_a or cid not in cases_b:
            rows[cid] = {"differences": {"presence": {"a": cid in cases_a, "b": cid in cases_b}}, "info": {}}
        else:
            rows[cid] = compare_case(cases_a[cid], cases_b[cid], generate_only)
    differing = [c for c, r in rows.items() if r["differences"]]
    in_scope = [c for c in differing if scope is not None and c in scope]
    out_of_scope = [c for c in differing if scope is None or c not in scope]
    categories = collections.Counter(k for c in differing for k in rows[c]["differences"])
    return {"schema": "qmg.benchmark.compare", "schema_version": 1, "run_a": str(run_a), "run_b": str(run_b),
            "generate_only": generate_only, "scope": sorted(scope) if scope is not None else None,
            "only": sorted(only) if only is not None else None,
            "cases_compared": len(rows), "cases_identical": len(rows) - len(differing),
            "cases_differing": len(differing), "in_scope_differences": in_scope,
            "out_of_scope_differences": out_of_scope, "difference_categories": dict(categories),
            "scope_ids_without_case": sorted(scope - set(rows)) if scope else [],
            "cases": {c: rows[c] for c in differing},
            "info": {c: r["info"] for c, r in rows.items() if r["info"]}}


def print_report(result: dict) -> None:
    print(f"compared {result['cases_compared']} case(s): identical {result['cases_identical']}, "
          f"differing {result['cases_differing']} (in scope {len(result['in_scope_differences'])}, "
          f"out of scope {len(result['out_of_scope_differences'])})")
    if result["difference_categories"]:
        print("categories: " + ", ".join(f"{k} {v}" for k, v in sorted(result["difference_categories"].items())))
    for cid in result["out_of_scope_differences"][:LIST_LIMIT]:
        print(f"  OUT OF SCOPE {cid}: {sorted(result['cases'][cid]['differences'])}")
    for cid in result["in_scope_differences"][:LIST_LIMIT]:
        print(f"  in scope     {cid}: {sorted(result['cases'][cid]['differences'])}")
    if result["scope_ids_without_case"]:
        print(f"scope ids with no case in either run: {len(result['scope_ids_without_case'])}")


# --------------------------------------------------------------------------- self-test

def _brush(x0, y0, z0, x1, y1, z1, tex):
    faces = [f"( {x0} {y0} {z0} ) ( {x0} {y1} {z0} ) ( {x0} {y0} {z1} ) {tex} 0 0 0 1 1",
             f"( {x1} {y0} {z0} ) ( {x1} {y0} {z1} ) ( {x1} {y1} {z0} ) {tex} 0 0 0 1 1",
             f"( {x0} {y0} {z0} ) ( {x0} {y0} {z1} ) ( {x1} {y0} {z0} ) {tex} 0 0 0 1 1",
             f"( {x0} {y1} {z0} ) ( {x1} {y1} {z0} ) ( {x0} {y1} {z1} ) {tex} 0 0 0 1 1",
             f"( {x0} {y0} {z0} ) ( {x1} {y0} {z0} ) ( {x0} {y1} {z0} ) {tex} 0 0 0 1 1",
             f"( {x0} {y0} {z1} ) ( {x0} {y1} {z1} ) ( {x1} {y0} {z1} ) {tex} 0 0 0 1 1"]
    return "{\n" + "\n".join(faces) + "\n}\n"


def _fixture_map(tex="WALL", width=256, spawn="64 64 24", reorder=False):
    brushes = [_brush(0, 0, -16, width, 256, 0, tex), _brush(0, 0, 128, width, 256, 144, "CEIL")]
    if reorder:
        brushes.reverse()
    return ('{\n"classname" "worldspawn"\n' + "".join(brushes) + "}\n"
            '{\n"classname" "info_player_start"\n"origin" "' + spawn + '"\n}\n')


def _write_case(run: Path, cid: str, map_text: str, report: dict):
    d = run / cid
    d.mkdir(parents=True)
    (d / f"{cid}.map").write_text(map_text, encoding="utf-8", newline="\n")
    (d / f"{cid}.qmg.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")


def self_test() -> int:
    """Project-authored fixtures: two runs with known differences; check classification and exit codes."""
    checks = []
    with tempfile.TemporaryDirectory(prefix="qmg-compare-selftest-") as tmp:
        a, b = Path(tmp) / "a", Path(tmp) / "b"
        base = {"game": "quake1", "mode": "deathmatch", "counts": {"rooms": 2}}
        _write_case(a, "same", _fixture_map(), base)
        _write_case(b, "same", _fixture_map(), base)
        _write_case(a, "texture", _fixture_map(), base)
        _write_case(b, "texture", _fixture_map(tex="METAL"), base)
        _write_case(a, "entity", _fixture_map(), base)
        _write_case(b, "entity", _fixture_map(spawn="96 64 24"), base)
        _write_case(a, "plane", _fixture_map(), base)
        _write_case(b, "plane", _fixture_map(width=320), base)
        _write_case(a, "report", _fixture_map(), base)
        _write_case(b, "report", _fixture_map(), dict(base, counts={"rooms": 3}))
        _write_case(a, "reordered", _fixture_map(), base)
        _write_case(b, "reordered", _fixture_map(reorder=True), base)
        _write_case(a, "only-a", _fixture_map(), base)

        def expect(name, cond):
            checks.append((name, bool(cond)))
        r = compare_runs(a, b, None, True)
        cases = r["cases"]
        expect("identical case not listed", "same" not in cases)
        expect("texture change -> textures + brush planes", "textures" in cases.get("texture", {}).get("differences", {})
               and "brush_planes" in cases["texture"]["differences"])
        expect("entity change -> entities only", set(cases.get("entity", {}).get("differences", {})) ==
               {"map_bytes", "entities"})
        expect("plane change -> brush planes, not textures", "brush_planes" in cases.get("plane", {})
               .get("differences", {}) and "textures" not in cases["plane"]["differences"])
        expect("report change -> report field $.counts.rooms", cases.get("report", {}).get("differences", {})
               .get("report_fields") == ["$.counts.rooms"])
        expect("brush reorder -> text-only difference", set(cases.get("reordered", {}).get("differences", {})) ==
               {"map_bytes", "map_text_only"})
        expect("case in one run only -> presence difference", "presence" in cases.get("only-a", {})
               .get("differences", {}))
        expect("no scope: every difference is out of scope", len(r["out_of_scope_differences"]) == 6)
        scoped = compare_runs(a, b, {"texture", "entity", "plane", "report", "reordered", "only-a"}, True)
        expect("full scope: 0 out of scope", scoped["out_of_scope_differences"] == [])
        partial = compare_runs(a, b, {"texture"}, True)
        expect("partial scope: entity change is out of scope", "entity" in partial["out_of_scope_differences"]
               and "texture" in partial["in_scope_differences"])
        expect("exit code 1 with an out-of-scope difference", main([str(a), str(b), "--scope", "texture",
                                                                   "--generate-only", "--quiet"]) == 1)
        expect("exit code 0 when every difference is in scope",
               main([str(a), str(b), "--scope", "texture,entity,plane,report,reordered,only-a", "--generate-only",
                     "--quiet"]) == 0)
        scope_file = Path(tmp) / "scope.txt"
        scope_file.write_text("texture\nentity  # comment\nplane\nreport\nreordered\nonly-a\n", encoding="utf-8")
        expect("@file scope", main([str(a), str(b), "--scope", "@" + str(scope_file), "--generate-only",
                                    "--quiet"]) == 0)
        for cid in ("same",):
            (a / cid / "result.json").write_text(json.dumps({"gates": {"G3": {"status": "pass"}},
                                                             "requests": []}), encoding="utf-8")
            (b / cid / "result.json").write_text(json.dumps({"gates": {"G3": {"status": "fail"}},
                                                             "requests": []}), encoding="utf-8")
        full = compare_runs(a, b, None, False)
        expect("compiled mode: gate status change counted", "gates" in full["cases"].get("same", {})
               .get("differences", {}))
        gen = compare_runs(a, b, None, True)
        expect("--generate-only ignores result.json", "same" not in gen["cases"])
        only = compare_runs(a, b, None, True, {"same", "entity"})
        expect("--only restricts the compared cases", only["cases_compared"] == 2
               and only["out_of_scope_differences"] == ["entity"])
    failed = [n for n, ok in checks if not ok]
    for name, ok in checks:
        print(f"[{'ok' if ok else 'FAIL'}] {name}")
    print(f"self-test: {len(checks) - len(failed)}/{len(checks)} checks passed")
    return 0 if not failed else 1


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run_a", nargs="?", type=Path)
    parser.add_argument("run_b", nargs="?", type=Path)
    parser.add_argument("--scope", action="append", default=[],
                        help="case ids (comma-separated), @file with one id per line, or a JSON file (id list, "
                             "manifest, id set or experiment record with declared_scope); repeatable")
    parser.add_argument("--only", action="append", default=[],
                        help="restrict the comparison to these case ids (same forms as --scope)")
    parser.add_argument("--generate-only", action="store_true", help="compare generated outputs only")
    parser.add_argument("--json", type=Path, help="write the full comparison here")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if not args.run_a or not args.run_b or not args.run_a.is_dir() or not args.run_b.is_dir():
        parser.print_usage(sys.stderr)
        return 2
    result = compare_runs(args.run_a, args.run_b, load_scope(args.scope), args.generate_only,
                          load_scope(args.only))
    if args.json:
        args.json.write_text(json.dumps(result, indent=2, ensure_ascii=False, default=str) + "\n", encoding="utf-8")
    if not args.quiet:
        print_report(result)
    return 1 if result["out_of_scope_differences"] else 0


if __name__ == "__main__":
    sys.exit(main())
