#!/usr/bin/env python3
"""PR-time BENCH-v1 subset: the opt-in, hash-gated rc-benchmark CTest (stdlib only).

  python -B tools/benchmark/rc_benchmark.py --self-test [--subset-sha256 <pin>]
  python -B tools/benchmark/rc_benchmark.py --subset-sha256 <pin> --out out/ctest/<build>/rc-benchmark \
      -- <run.py tool arguments: --qmg, --ericw, --q3tools, --q3-runtime, --q1-assets, ...>
  python -B tools/benchmark/rc_benchmark.py --freeze-subset [--baseline baseline-0]

Plan docs/RC_PHASE1_AUDIT_AND_PLAN.md 9.1 and D11(1): "an opt-in, hash-gated rc-benchmark CTest label, plus a
PR-time subset". CMake registers it only with -DQMG_RC_BENCHMARK=ON.

Hash gate. Before anything is generated:
  * the sha256 of tools/benchmark/rc1-v1.json must equal its frozen BENCH-v1 row in tools/benchmark/manifests.json;
  * the subset's base_manifest must name that file with that sha256;
  * the subset file's own sha256 must equal the pin CMake passes (--subset-sha256), so its ids and recorded
    statuses cannot be loosened without changing the pin under review;
  * no subset id may be sealed (tools/benchmark/sealed.json, by id or normalized-prompt sha256; D26).
Any failure exits 1 and nothing runs. run.py applies its own registry check on top (it refuses a frozen
manifest whose bytes differ, and this script never passes --allow-modified-manifest).

Subset (tools/benchmark/pr-subset-v1.json, written once by --freeze-subset): for each of the 17 paths, in the
base manifest's `paths` order, the first case in manifest order of each core category (vague, specific,
contradictory) among the draft-v0 core cases (no `added_in`; the two q2-showcase probes are excluded); sealed
cases are never selected. 51 cases. The rule is applied to the manifest, not to any output.

Run. run.py --manifest <subset> --policy v1 --repeat 2 --jobs 8 into a fresh --out under out/ (8 jobs x 2 tool
threads, the light-budget concurrency of plan 8.3).

Verdict (regression against the recorded baseline). For every case, each gate G1-G8 recorded as `pass` in the
subset must still be `pass`; every case must have a result written by this run.py (sha256), from this subset
file (sha256), with the tool pins matched. A gate recorded as fail, n/a or not run that now passes is an
improvement and is reported, never failed. G9-G15 are reported columns and are not compared.
Exit codes: 0 pass, 1 regression or hash-gate failure, 2 usage error.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import shutil
import subprocess
import sys
from pathlib import Path

BENCH = Path(__file__).resolve().parent
REPO = BENCH.parents[1]
RUN_PY = BENCH / "run.py"
REGISTRY = BENCH / "manifests.json"
BASELINES = BENCH / "baselines" / "index.json"
DEFAULT_SUBSET = BENCH / "pr-subset-v1.json"
BASE_SET = "BENCH-v1"
CORE_CATEGORIES = ("vague", "specific", "contradictory")
EXCLUDED_PREFIXES = ("q2-showcase-",)
COMPARED_GATES = ("G1_generated", "G2_intent_match", "G3_leak_free_compile", "G4_full_vis", "G5_final_light",
                  "G6_aas_valid", "G7_mode_entities", "G8_determinism")
STATUSES = {"pass", "fail", "n/a", "not run", "not configured"}
MARKER = ".rc-benchmark"
SELECTION_RULE = (
    "For each path in the base manifest's `paths` order, the first case in manifest order of each core category "
    "(vague, specific, contradictory) among the draft-v0 core cases (no `added_in`; ids starting with "
    "`q2-showcase-` excluded). Sealed cases (sealed.json, by id or normalized-prompt sha256) are never selected.")
VERDICT_RULE = (
    "Regression check against the recorded baseline: for every case, each gate G1-G8 recorded as `pass` must "
    "still be `pass`. A recorded fail, n/a or not-run gate that now passes is an improvement (reported, not "
    "failed). G9-G15 are reported columns and are not compared.")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def rel(path: Path) -> str:
    try:
        return str(Path(path).resolve().relative_to(REPO)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def load_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def dump_json(value) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def runner_module():
    """run.py's own seal semantics (load_seal, is_sealed), so this script cannot drift from the runner."""
    spec = importlib.util.spec_from_file_location("qmg_benchmark_run", RUN_PY)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# --------------------------------------------------------------------------- hash gate

def registry_row(registry: dict, set_name: str):
    rows = [r for r in registry.get("manifests", []) if r.get("set") == set_name]
    return rows[0] if len(rows) == 1 else None


def hash_gate(manifest_bytes: bytes, manifest_rel: str, registry: dict, subset: dict, subset_bytes: bytes,
              subset_pin) -> list:
    """Problems (empty when the gate passes). Pure: works on bytes, so the self-test can mutate them."""
    problems = []
    row = registry_row(registry, BASE_SET)
    if row is None:
        return [f"manifests.json has no single {BASE_SET} row"]
    if row.get("path") != manifest_rel:
        problems.append(f"manifests.json {BASE_SET} path is {row.get('path')!r}, expected {manifest_rel!r}")
    if row.get("frozen") is not True:
        problems.append(f"manifests.json {BASE_SET} row is not frozen")
    digest = sha256_bytes(manifest_bytes)
    if digest != row.get("sha256"):
        problems.append(f"{manifest_rel} sha256 {digest} != manifests.json {BASE_SET} {row.get('sha256')}")
    base = subset.get("base_manifest") or {}
    if base.get("path") != manifest_rel:
        problems.append(f"subset base_manifest path {base.get('path')!r} != {manifest_rel!r}")
    if base.get("sha256") != row.get("sha256"):
        problems.append(f"subset base_manifest sha256 {base.get('sha256')} != manifests.json {row.get('sha256')}")
    if subset_pin is not None:
        got = sha256_bytes(subset_bytes)
        if got != subset_pin.lower():
            problems.append(f"subset file sha256 {got} != pinned {subset_pin.lower()}")
    return problems


def select_ids(manifest: dict, sealed) -> list:
    """The PR-subset rule, applied to the manifest only."""
    order = list(manifest["paths"])
    chosen = []
    for path in order:
        for category in CORE_CATEGORIES:
            for case in manifest["cases"]:
                if case.get("added_in") or case["path"] != path or case["category"] != category:
                    continue
                if case["id"].startswith(EXCLUDED_PREFIXES) or sealed(case):
                    continue
                chosen.append(case["id"])
                break
    return chosen


def subset_problems(subset: dict, manifest: dict, sealed) -> list:
    problems = []
    if subset.get("schema") != "qmg.benchmark.id_set" or subset.get("schema_version") != 1:
        problems.append("subset schema must be qmg.benchmark.id_set version 1")
    ids = list(subset.get("ids") or [])
    if len(set(ids)) != len(ids):
        problems.append("subset ids are not unique")
    expected_ids = select_ids(manifest, sealed)
    if ids != expected_ids:
        problems.append(f"subset ids differ from the selection rule: extra {sorted(set(ids) - set(expected_ids))}, "
                        f"missing {sorted(set(expected_ids) - set(ids))}, or a different order")
    by_id = {c["id"]: c for c in manifest["cases"]}
    for cid in ids:
        if cid not in by_id:
            problems.append(f"subset id {cid} is not in the base manifest")
        elif sealed(by_id[cid]):
            problems.append(f"subset id {cid} is sealed (D26)")
    recorded = ((subset.get("expected") or {}).get("cases")) or {}
    if sorted(recorded) != sorted(ids):
        problems.append("expected.cases does not cover exactly the subset ids")
    for cid, row in recorded.items():
        gates = row.get("gates") or {}
        if sorted(gates) != sorted(COMPARED_GATES):
            problems.append(f"expected.cases.{cid}.gates must list exactly {', '.join(COMPARED_GATES)}")
        bad = {k: v for k, v in gates.items() if v not in STATUSES}
        if bad:
            problems.append(f"expected.cases.{cid}: unknown statuses {bad}")
    return problems


# --------------------------------------------------------------------------- verdict

def compare_case(cid: str, recorded: dict, result) -> tuple:
    """(regressions, improvements) for one case. result is the parsed result.json or None."""
    if result is None:
        return [f"{cid}: no result.json"], []
    regressions, improvements = [], []
    for gate in COMPARED_GATES:
        before = recorded["gates"][gate]
        after = ((result.get("gates") or {}).get(gate) or {}).get("status")
        if before == "pass" and after != "pass":
            detail = ((result.get("gates") or {}).get(gate) or {}).get("detail")
            regressions.append(f"{cid} {gate}: pass -> {after}" + (f" ({detail})" if detail else ""))
        elif before != "pass" and after == "pass":
            improvements.append(f"{cid} {gate}: {before} -> pass")
    return regressions, improvements


def identity_problems(cid: str, result: dict, subset_sha: str, runner_sha: str) -> list:
    ident = result.get("identity") or {}
    problems = []
    if ident.get("manifest_sha256") != subset_sha:
        problems.append(f"{cid}: result manifest sha256 {ident.get('manifest_sha256')} != subset {subset_sha}")
    if ident.get("runner_sha256") != runner_sha:
        problems.append(f"{cid}: result runner sha256 {ident.get('runner_sha256')} != run.py {runner_sha}")
    if ident.get("tool_pins_ok") is not True:
        problems.append(f"{cid}: tool pins not matched")
    if result.get("policy") != "v1":
        problems.append(f"{cid}: policy {result.get('policy')!r}, expected 'v1'")
    return problems


# --------------------------------------------------------------------------- modes

def load_inputs(subset_path: Path):
    registry = load_json(REGISTRY)
    row = registry_row(registry, BASE_SET)
    manifest_path = REPO / (row or {}).get("path", "tools/benchmark/rc1-v1.json")
    manifest_bytes = manifest_path.read_bytes()
    subset_bytes = subset_path.read_bytes()
    return registry, manifest_path, manifest_bytes, json.loads(manifest_bytes), subset_bytes, json.loads(subset_bytes)


def sealed_predicate(run_mod):
    seal = run_mod.load_seal()
    return lambda case: run_mod.is_sealed(case, seal)


def freeze_subset(args) -> int:
    if args.subset.exists():
        print(f"refusing to overwrite {rel(args.subset)}: a frozen subset gets a new file and a new pin")
        return 2
    run_mod = runner_module()
    registry = load_json(REGISTRY)
    row = registry_row(registry, BASE_SET)
    manifest_path = REPO / row["path"]
    manifest = load_json(manifest_path)
    if sha256_file(manifest_path) != row["sha256"]:
        print(f"{row['path']} does not match manifests.json; refusing to freeze")
        return 1
    baselines = load_json(BASELINES)
    entry = next((b for b in baselines["baselines"] if b["id"] == args.baseline), None)
    if entry is None or entry["manifest_sha256"] != row["sha256"] or entry["policy"] != "v1":
        print(f"baseline {args.baseline} is missing, not on {BASE_SET} {row['sha256'][:16]}, or not v1 policy")
        return 1
    out_dir = REPO / entry["out_dir"]
    summary_sha = sha256_file(out_dir / "summary.json")
    if summary_sha != entry["summary_sha256"]:
        print(f"{entry['out_dir']}/summary.json sha256 {summary_sha} != baselines index {entry['summary_sha256']}")
        return 1
    ids = select_ids(manifest, sealed_predicate(run_mod))
    cases = {}
    for cid in ids:
        result = load_json(out_dir / cid / "result.json")
        ident = result.get("identity") or {}
        if ident.get("qmg_sha256") != entry["qmg_sha256"] or ident.get("runner_sha256") != entry["runner_sha256"]:
            print(f"{cid}: result identity does not match baseline {args.baseline}")
            return 1
        cases[cid] = {"all_hard_gates": bool(result["all_hard_gates"]),
                      "gates": {g: result["gates"][g]["status"] for g in COMPARED_GATES}}
    subset = {
        "schema": "qmg.benchmark.id_set",
        "schema_version": 1,
        "set": "BENCH-v1-PR",
        "created": args.created,
        "frozen": True,
        "change_id": "x1-ctest-integration",
        "description": "PR-time subset of BENCH-v1 for the opt-in, hash-gated rc-benchmark CTest "
                       "(docs/RC_PHASE1_AUDIT_AND_PLAN.md 9.1, D11(1)). Run by tools/benchmark/rc_benchmark.py; "
                       "the file's sha256 is pinned in cmake/QmgBenchmarkTests.cmake. A changed subset gets a new "
                       "file, a new set name and a new pin; this file is not edited.",
        "base_manifest": {"path": row["path"], "sha256": row["sha256"]},
        "selection_rule": SELECTION_RULE,
        "ids": ids,
        "expected": {
            "rule": VERDICT_RULE,
            "baseline": {"id": entry["id"], "summary_sha256": entry["summary_sha256"],
                         "qmg_sha256": entry["qmg_sha256"], "runner_sha256": entry["runner_sha256"],
                         "policy": entry["policy"], "source": "result.json gate statuses of the baseline run "
                         "(local, git-ignored); only these statuses are tracked here"},
            "gates": list(COMPARED_GATES),
            "cases": cases,
        },
    }
    data = dump_json(subset)
    args.subset.write_bytes(data)
    passing = sum(1 for c in cases.values() if c["all_hard_gates"])
    print(f"wrote {rel(args.subset)}: {len(ids)} ids, {passing} pass every hard gate at {entry['id']}; "
          f"sha256 {sha256_bytes(data)}")
    return 0


def self_test(args) -> int:
    run_mod = runner_module()
    sealed = sealed_predicate(run_mod)
    registry, manifest_path, manifest_bytes, manifest, subset_bytes, subset = load_inputs(args.subset)
    manifest_rel = rel(manifest_path)
    checks = []

    def check(name, ok, detail=""):
        checks.append(ok)
        print(f"[{'ok' if ok else 'FAIL'}] {name}" + (f": {detail}" if detail and not ok else ""))

    problems = hash_gate(manifest_bytes, manifest_rel, registry, subset, subset_bytes, args.subset_sha256)
    check("hash gate accepts the registered manifest and the pinned subset", not problems, "; ".join(problems))
    mutated = bytearray(manifest_bytes)
    mutated[len(mutated) // 2] ^= 0x01
    check("hash gate rejects a one-byte-mutated manifest",
          bool(hash_gate(bytes(mutated), manifest_rel, registry, subset, subset_bytes, None)))
    unfrozen = json.loads(json.dumps(registry))
    registry_row(unfrozen, BASE_SET)["frozen"] = False
    check("hash gate rejects an unfrozen registry row",
          bool(hash_gate(manifest_bytes, manifest_rel, unfrozen, subset, subset_bytes, None)))
    rebased = dict(subset, base_manifest={"path": manifest_rel, "sha256": "0" * 64})
    check("hash gate rejects a subset naming another base sha256",
          bool(hash_gate(manifest_bytes, manifest_rel, registry, rebased, subset_bytes, None)))
    check("hash gate rejects a subset whose bytes differ from the pin",
          bool(hash_gate(manifest_bytes, manifest_rel, registry, subset, subset_bytes + b" ", sha256_bytes(subset_bytes))))
    problems = subset_problems(subset, manifest, sealed)
    check("subset follows its selection rule and records every compared gate", not problems, "; ".join(problems))
    ids = subset["ids"]
    paths = {c["id"]: c["path"] for c in manifest["cases"]}
    check("subset covers all 17 paths x 3 core categories",
          len(ids) == 51 and len({paths[i] for i in ids}) == 17, f"{len(ids)} ids")
    hero = next((c for c in manifest["cases"] if c["id"] == "q2-showcase-hero"), None)
    check("the sealed hero is sealed and not selected", hero is not None and sealed(hero) and hero["id"] not in ids)
    renamed = dict(hero, id="pr-subset-selftest-alias")
    check("a case with the hero's prompt under another id counts as sealed", sealed(renamed))
    widened = dict(subset, ids=ids + ["q1-sp-v2"])
    check("subset check rejects an id outside the rule", bool(subset_problems(widened, manifest, sealed)))
    recorded = {"gates": {g: "pass" for g in COMPARED_GATES}}
    recorded["gates"]["G2_intent_match"] = "fail"
    result = {"gates": {g: {"status": "pass"} for g in COMPARED_GATES}}
    reg, imp = compare_case("x", recorded, result)
    check("verdict: fail -> pass is an improvement, not a regression", not reg and len(imp) == 1)
    result["gates"]["G4_full_vis"] = {"status": "fail", "detail": "c_chains 0"}
    reg, _ = compare_case("x", recorded, result)
    check("verdict: pass -> fail is a regression", len(reg) == 1 and "G4_full_vis" in reg[0])
    result["gates"]["G4_full_vis"] = {"status": "not run"}
    reg, _ = compare_case("x", recorded, result)
    check("verdict: pass -> not run is a regression", len(reg) == 1)
    reg, _ = compare_case("x", recorded, None)
    check("verdict: a missing result is a regression", len(reg) == 1)
    na = {"gates": dict(recorded["gates"], G7_mode_entities="n/a")}
    reg, _ = compare_case("x", na, {"gates": {g: {"status": "fail" if g == "G7_mode_entities" else "pass"}
                                              for g in COMPARED_GATES}})
    check("verdict: n/a -> fail is reported, not failed (only recorded passes are compared)",
          all("G7_mode_entities" not in r for r in reg))
    good = {"identity": {"manifest_sha256": "a", "runner_sha256": "b", "tool_pins_ok": True}, "policy": "v1"}
    check("identity: matching result accepted", not identity_problems("x", good, "a", "b"))
    check("identity: another runner rejected", bool(identity_problems("x", good, "a", "c")))
    check("identity: unpinned tools rejected",
          bool(identity_problems("x", dict(good, identity=dict(good["identity"], tool_pins_ok=False)), "a", "b")))
    passed = sum(checks)
    print(f"self-test: {passed}/{len(checks)} checks passed")
    return 0 if passed == len(checks) else 1


def run_subset(args, passthrough) -> int:
    if args.subset_sha256 is None:
        print("--subset-sha256 is required for a run (CMake passes the pin)")
        return 2
    run_mod = runner_module()
    sealed = sealed_predicate(run_mod)
    registry, manifest_path, manifest_bytes, manifest, subset_bytes, subset = load_inputs(args.subset)
    problems = hash_gate(manifest_bytes, rel(manifest_path), registry, subset, subset_bytes, args.subset_sha256)
    problems += subset_problems(subset, manifest, sealed)
    if problems:
        for problem in problems:
            print(f"hash gate: {problem}")
        print("rc-benchmark: hash gate FAILED; nothing was generated")
        return 1
    print(f"hash gate: {rel(manifest_path)} sha256 {sha256_bytes(manifest_bytes)} matches manifests.json; "
          f"subset {rel(args.subset)} sha256 {sha256_bytes(subset_bytes)} matches the pin; no sealed ids")
    forbidden = {"--allow-modified-manifest", "--hero-run", "--manifest", "--cases-file", "--out", "--output",
                 "--filter", "--policy", "--repeat", "--import", "--controls", "--summary-only", "--generate-only",
                 "--no-compile", "--allow-unpinned", "--jobs", "--light-budget", "--light-timeout"}
    bad = [a for a in passthrough if a.split("=", 1)[0] in forbidden]
    if bad:
        print(f"refusing run.py arguments that would change the gated run: {bad}")
        return 2
    out_root = (REPO / "out").resolve()
    out = args.out.resolve()
    if out == out_root or out_root not in out.parents:
        print(f"--out must be inside {out_root}")
        return 2
    if out.exists():
        if not (out / MARKER).is_file():
            print(f"refusing to clear {out}: it has no {MARKER} marker from an earlier rc-benchmark run")
            return 2
        shutil.rmtree(out)
    out.mkdir(parents=True)
    (out / MARKER).write_bytes(b"rc-benchmark run directory; cleared at the start of every run\n")
    command = [sys.executable, "-B", str(RUN_PY), "--manifest", str(args.subset), "--out", str(out),
               "--policy", "v1", "--repeat", "2", "--jobs", "8"] + passthrough
    print("run:", " ".join(command), flush=True)
    completed = subprocess.run(command, cwd=str(REPO))
    verdict = {"schema": "qmg.benchmark.rc_benchmark_verdict", "schema_version": 1,
               "subset": rel(args.subset), "subset_sha256": sha256_bytes(subset_bytes),
               "base_manifest_sha256": sha256_bytes(manifest_bytes), "runner_sha256": sha256_file(RUN_PY),
               "run_py_exit": completed.returncode, "baseline": subset["expected"]["baseline"]["id"],
               "rule": VERDICT_RULE}
    regressions, improvements, identity = [], [], []
    passing_now = passing_before = 0
    if completed.returncode != 0:
        regressions.append(f"run.py exited {completed.returncode}")
    runner_sha = sha256_file(RUN_PY)
    for cid in subset["ids"]:
        recorded = subset["expected"]["cases"][cid]
        passing_before += bool(recorded["all_hard_gates"])
        path = out / cid / "result.json"
        result = load_json(path) if path.is_file() else None
        reg, imp = compare_case(cid, recorded, result)
        regressions += reg
        improvements += imp
        if result is not None:
            identity += identity_problems(cid, result, verdict["subset_sha256"], runner_sha)
            passing_now += bool(result.get("all_hard_gates"))
    verdict.update({"cases": len(subset["ids"]), "all_hard_gates": passing_now,
                    "all_hard_gates_at_baseline": passing_before, "regressions": regressions,
                    "improvements": improvements, "identity_problems": identity,
                    "result": "pass" if not regressions and not identity else "fail"})
    (out / "rc-benchmark-verdict.json").write_bytes(dump_json(verdict))
    for line in regressions:
        print(f"REGRESSION {line}")
    for line in identity:
        print(f"IDENTITY {line}")
    for line in improvements:
        print(f"improvement {line}")
    print(f"rc-benchmark: {len(subset['ids'])} cases; all hard gates {passing_now} "
          f"(baseline {passing_before}); regressions {len(regressions)}; improvements {len(improvements)}; "
          f"identity problems {len(identity)} -> {rel(out / 'rc-benchmark-verdict.json')}")
    return 0 if verdict["result"] == "pass" else 1


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    passthrough = []
    if "--" in argv:
        split = argv.index("--")
        argv, passthrough = argv[:split], argv[split + 1:]
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--subset", type=Path, default=DEFAULT_SUBSET)
    parser.add_argument("--subset-sha256", default=None)
    parser.add_argument("--out", type=Path, default=REPO / "out" / "ctest" / "rc-benchmark")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--freeze-subset", action="store_true")
    parser.add_argument("--baseline", default="baseline-0")
    parser.add_argument("--created", default="2026-10-15")
    args = parser.parse_args(argv)
    args.subset = args.subset.resolve()
    if args.freeze_subset:
        return freeze_subset(args)
    if args.self_test:
        return self_test(args)
    return run_subset(args, passthrough)


if __name__ == "__main__":
    sys.exit(main())
