"""python -B -m tools.map_study --help"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

from . import EXTRACTOR_VERSION
from .binary import Aas, Bsp, FormatError, read_bytes, sha256
from .corpus import assign_splits, build_profile, inventory, is_link, json_bytes, provenance, write_json
from .features import analyze, navigation
from .render import atlas, dossier

PROJECT = Path(__file__).resolve().parents[2]


def load_json(path):
    return json.loads(read_bytes(path, 64 * 1024 * 1024).decode("utf-8"))


def prepare_output(output, source=None):
    output = output.absolute()
    for parent in [output] + list(output.parents):
        if parent.exists() and is_link(parent):
            raise ValueError("output cannot contain symlinks/reparse points")
    output = output.resolve()
    if source and (output.is_relative_to(source) or source.is_relative_to(output)):
        raise ValueError("output and source archive must not contain one another")
    output.mkdir(parents=True, exist_ok=True)
    return output


def ingest(args):
    root = args.archive.resolve(strict=True)
    if not root.is_dir():
        raise ValueError("archive must be an existing directory")
    curriculum = load_json(args.curriculum)
    output = prepare_output(args.output, root)
    for child in ("records", "maps"):
        target = output / child
        if target.exists() and is_link(target):
            raise ValueError("output child is a link")
        target.mkdir(exist_ok=True)
    # Never follow existing output-file links when replacing prior artifacts.
    _, output_links = inventory(output)
    if output_links:
        raise ValueError("output contains links/reparse points")
    files, skipped = inventory(root)
    bsps = [p for p in files if p.suffix.casefold() == ".bsp"]
    if not bsps:
        raise ValueError("no BSP files found")
    selected = {name for lesson in curriculum["lessons"] for name in lesson["maps"]}
    package_roots = {p.parent.parent for p in bsps}
    packages = defaultdict(list)
    for path in files:
        for parent in path.parents:
            if parent in package_roots:
                packages[parent].append(path)
                break
    records, errors, detailed_floors = [], [], {}
    for i, path in enumerate(bsps):
        try:
            bsp = Bsp(read_bytes(path))
            record = provenance(path, root, packages[path.parent.parent], bsp)
            analysis, floors = analyze(bsp)
            record["profile_eligible"] = not bool(analysis["unsupported_pickup_classes"])
            if not record["profile_eligible"]:
                record["issues"].append("unsupported pickup classes; game module/entity semantics require review before profile admission")
            for sidecar in record["sidecars"]:
                if sidecar["kind"] == "aas":
                    try:
                        analysis["navigation"] = navigation(Aas(read_bytes(root / sidecar["path"])), analysis["anchors"])
                        analysis["availability"]["navigation"] = "unverified_sidecar"
                    except (FormatError, OSError) as error:
                        analysis["navigation"] = {"status": "invalid_sidecar", "error": str(error)}
                        record["issues"].append("AAS sidecar failed validation")
                    break
            record["analysis"] = analysis
            record["artifact_id"] = record["map"] + "-" + sha256(record["id"].encode())[:10]
            if record["map"] in selected:
                detailed_floors[record["id"]] = floors
            records.append(record)
        except (FormatError, OSError) as error:
            errors.append({"path": path.relative_to(root).as_posix(), "error": str(error)})
            print(f"Quarantined {path.relative_to(root).as_posix()}: {error}", flush=True)
        if (i + 1) % 25 == 0 or i + 1 == len(bsps):
            print(f"Analyzed {i + 1}/{len(bsps)} BSPs; {len(errors)} quarantined", flush=True)
    assign_splits(records, curriculum)
    registry = [{k: v for k, v in record.items() if k != "analysis"} for record in records]
    manifest = {"schema_version": 1, "extractor_version": EXTRACTOR_VERSION,
                "archive_root": str(root), "curriculum_sha256": sha256(json_bytes(curriculum)),
                "records": registry, "quarantined_files": errors, "skipped_links": skipped}
    manifest_hash = sha256(json_bytes(manifest))
    profile = build_profile(records, manifest_hash)
    summary = {
        "schema_version": 1, "extractor_version": EXTRACTOR_VERSION, "manifest_sha256": manifest_hash,
        "files_by_extension": dict(sorted(Counter(p.suffix.lower() for p in files).items())),
        "bsp_files": len(bsps), "analyzed_maps": len(records), "quarantined_files": errors, "skipped_links": skipped,
        "partitions": dict(sorted(Counter(r["partition"] for r in records).items())),
        "split_groups": len({r["split_group"] for r in records}), "curriculum_maps": len(selected),
        "unknown_authors": sum(not r["authors"] for r in records),
        "metadata_issues": sum(any("metadata" in issue for issue in r["issues"]) for r in records),
        "entity_sidecars_differ": sum(any(s.get("matches_embedded_entities") is False for s in r["sidecars"]) for r in records),
        "unsupported_pickup_maps": sum(not r["profile_eligible"] for r in records),
        "aas_with_reachabilities": sum(r["analysis"].get("navigation", {}).get("reachabilities", 0) > 0 for r in records),
        "aas_without_reachabilities": sum(r["analysis"].get("navigation", {}).get("reachabilities") == 0 for r in records),
        "aas_invalid": sum(r["analysis"].get("navigation", {}).get("status") == "invalid_sidecar" for r in records),
        "training_eligible": 0, "trained_model": False,
    }
    sessions = []
    lookup = {r["map"]: r for r in records}
    for lesson in curriculum["lessons"]:
        for name in lesson["maps"]:
            record = lookup[name]
            sessions.append({
                "lesson": lesson["id"], "map_id": record["id"], "bsp_sha256": record["bsp_sha256"],
                "partition": record["partition"], "package_path": str((root / record["bsp_path"]).parent.parent),
                "load_command": "map " + record["map"], "question": lesson["question"], "rule_ids": lesson["rules"],
                "status": "static_analysis_complete_runtime_pending", "observed_players": None,
                "reviewer": None, "engine_sha256": None, "game_module_sha256": None, "dmflags": None,
                "entity_variant_sha256": None, "demo_path": None, "engine_screenshot_paths": [],
                "scores": {k: None for k in ("navigation", "spaces", "sightlines", "items", "verticality", "theme", "fun")},
                "observations": [], "counterexamples": [],
            })
    for record in records:
        write_json(output / "records" / (record["artifact_id"] + ".json"), record)
        if record["id"] in detailed_floors:
            (output / "maps" / (record["artifact_id"] + ".html")).write_text(dossier(record, detailed_floors[record["id"]]), encoding="utf-8")
    write_json(output / "manifest.json", manifest)
    write_json(output / "profile.json", profile)
    write_json(output / "summary.json", summary)
    write_json(output / "study-plan.template.json", {"schema_version": 1, "manifest_sha256": manifest_hash, "sessions": sessions})
    (output / "index.html").write_text(atlas(records, summary, curriculum), encoding="utf-8")
    print(json.dumps(summary, indent=2))
    print(f"Atlas: {output / 'index.html'}")
    return 1 if args.strict and (errors or skipped) else 0


def validate_profile(profile):
    if profile.get("extractor_version") != EXTRACTOR_VERSION or profile.get("game") != "q2" or profile.get("entity_policy") != "embedded_stock_dm":
        raise ValueError("reference profile uses incompatible extractor/game/entity policy")


def compare_metrics(profile, cohort, analysis):
    validate_profile(profile)
    if analysis.get("unsupported_pickup_classes"):
        raise ValueError("candidate pickup classes require game module/entity review before comparison")
    reference = profile["cohorts"].get(cohort)
    if not reference or not reference["usable_for_comparison"]:
        raise ValueError("cohort requires at least five independent author/family groups")
    comparisons = []
    for name, value in analysis["metrics"].items():
        distribution = reference["metrics"].get(name, {})
        sample = distribution.get("group_medians", [])
        if value is None or len(sample) < 5:
            continue
        percentile = 100 * (sum(v < value for v in sample) + .5 * sum(v == value for v in sample)) / len(sample)
        comparisons.append({"metric": name, "value": value, "reference_p10": distribution["p10"],
                            "reference_p50": distribution["p50"], "reference_p90": distribution["p90"],
                            "percentile_of_group_medians": percentile,
                            "observation": "below_reference_band" if value < distribution["p10"] else
                            "above_reference_band" if value > distribution["p90"] else "within_reference_band"})
    return {"cohort": cohort, "comparisons": comparisons,
            "interpretation": "Descriptive differences, not quality scores or pass/fail thresholds. Inspect intent and playtest counterexamples."}


def compare(args):
    profile = load_json(args.profile)
    bsp = Bsp(read_bytes(args.bsp))
    analysis, _ = analyze(bsp)
    result = {"schema_version": 1, "extractor_version": EXTRACTOR_VERSION,
              "bsp_sha256": bsp.sha256, "profile_sha256": sha256(read_bytes(args.profile)),
              **compare_metrics(profile, args.cohort, analysis), "limitations": analysis["limitations"]}
    output = prepare_output(args.output.parent)
    if args.output.exists() and is_link(args.output):
        raise ValueError("output cannot be a link")
    if args.output.resolve() in (args.bsp.resolve(), args.profile.resolve()):
        raise ValueError("comparison cannot overwrite an input")
    write_json(output / args.output.name, result)
    print(f"Comparison: {args.output.resolve()}")
    return 0


def benchmark(args):
    profile = load_json(args.profile)
    validate_profile(profile)
    matrix = load_json(args.matrix)
    qmg, qbsp = args.qmg.resolve(strict=True), args.qbsp.resolve(strict=True)
    qbsp_hash = sha256(read_bytes(qbsp))
    if not re.fullmatch(r"[0-9a-fA-F]{64}", args.qbsp_sha256) or qbsp_hash != args.qbsp_sha256.lower():
        raise ValueError("qbsp SHA-256 does not match the requested compiler")
    output = prepare_output(args.output)
    if any(output.iterdir()):
        raise ValueError("benchmark output must be empty to exclude stale compiler evidence")
    cases = matrix.get("cases", [])
    if not cases or sum(len(c["seeds"]) for c in cases) > 64:
        raise ValueError("benchmark requires 1–64 runs")
    run_ids = []
    for case in cases:
        if not re.fullmatch(r"[a-z0-9_]+", case["id"]) or case["mode"] not in ("dm", "duel", "ctf"):
            raise ValueError("invalid benchmark case identifier/mode")
        for seed in case["seeds"]:
            if not isinstance(seed, int) or isinstance(seed, bool) or not 0 <= seed < 2**64:
                raise ValueError("invalid benchmark seed")
            run_ids.append(f'{case["id"]}_{seed}')
    if len(run_ids) != len(set(run_ids)):
        raise ValueError("duplicate benchmark run IDs")
    evidence = {"schema_version": 1, "extractor_version": EXTRACTOR_VERSION,
                "qmg_sha256": sha256(read_bytes(qmg)), "qbsp_sha256": qbsp_hash,
                "profile_sha256": sha256(read_bytes(args.profile)), "matrix_sha256": sha256(json_bytes(matrix)),
                "runtime_playtested": False, "trained_model": False, "runs": []}
    for case in cases:
        for seed in case["seeds"]:
            identifier = f'{case["id"]}_{seed}'
            directory = output / identifier
            directory.mkdir()
            map_path, bsp_path = directory / "candidate.map", directory / "candidate.bsp"
            run = {"id": identifier, "case": case["id"], "seed": seed, "prompt": case["prompt"],
                   "mode": case["mode"], "rooms": case["rooms"], "players": case["players"],
                   "generation_passed": False, "compiler_passed": False}
            try:
                command = [str(qmg), "generate", "--prompt", case["prompt"], "--game", "q2", "--mode", case["mode"],
                           "--rooms", str(case["rooms"]), "--players", str(case["players"]), "--seed", str(seed),
                           "--output", str(map_path)]
                run["generator_command"] = command
                generated = subprocess.run(command, cwd=directory, capture_output=True, timeout=120)
                (directory / "generator.log").write_bytes(generated.stdout + b"\n" + generated.stderr)
                if generated.returncode or not map_path.is_file():
                    raise ValueError("generator failed; see generator.log")
                run["generation_passed"] = True
                run["map_sha256"] = sha256(read_bytes(map_path))
                command = [str(qbsp), "-q2bsp", "-leaktest", "-noallowupgrade", "-notex", "-lowpriority", "0",
                           "-nolog", "-nocolor", "-quiet", str(map_path), str(bsp_path)]
                run["compiler_command"] = command
                compiled = subprocess.run(command, cwd=directory, capture_output=True, timeout=120)
                compiler_log = compiled.stdout + b"\n" + compiled.stderr
                (directory / "compiler.log").write_bytes(compiler_log)
                failure_markers = (b"leaked", b"no visible sides on brush", b"outside world", b"invalid brush", b"brush with no faces")
                if compiled.returncode or any(marker in compiler_log.lower() for marker in failure_markers):
                    raise ValueError("compiler failed; see compiler.log")
                if any(map_path.with_suffix(s).exists() for s in (".pts", ".lin")):
                    raise ValueError("compiler emitted a leak pointfile")
                portal = map_path.with_suffix(".prt")
                if not portal.is_file() or not portal.stat().st_size:
                    raise ValueError("compiler did not emit nonempty portal evidence")
                bsp = Bsp(read_bytes(bsp_path))
                run["compiler_passed"] = True
                run["bsp_sha256"] = bsp.sha256
                run["portal_sha256"] = sha256(read_bytes(portal))
                analysis, _ = analyze(bsp)
                run["metrics"] = analysis["metrics"]
                reference = profile["cohorts"].get(case["cohort"], {})
                if not reference.get("usable_for_comparison"):
                    run["reference_comparison"] = {"cohort": case["cohort"], "status": "insufficient_independent_reference_groups"}
                else:
                    run["reference_comparison"] = compare_metrics(profile, case["cohort"], analysis)
                write_json(directory / "analysis.json", analysis)
            except (ValueError, OSError, subprocess.TimeoutExpired) as error:
                run["error"] = str(error)
            evidence["runs"].append(run)
            write_json(output / "benchmark.json", evidence)
            print(f'{identifier}: {run.get("error", "compiled and analyzed")}', flush=True)
    successes = sum(r["compiler_passed"] and "error" not in r for r in evidence["runs"])
    print(f"Benchmark: {successes}/{len(evidence['runs'])} completed; {output / 'benchmark.json'}")
    return 0 if successes == len(evidence["runs"]) else 1


def main():
    parser = argparse.ArgumentParser(description="Local Quake II reference studies; no weights, game launches or input control.")
    commands = parser.add_subparsers(dest="command", required=True)
    scan = commands.add_parser("ingest", help="Analyze an archive and build a local atlas, registry, curriculum and profile")
    scan.add_argument("--archive", type=Path, required=True)
    scan.add_argument("--output", type=Path, required=True)
    scan.add_argument("--curriculum", type=Path, default=PROJECT / "knowledge/q2/curriculum.json")
    scan.add_argument("--strict", action="store_true", help="Return failure for quarantined BSPs or excluded links")
    scan.set_defaults(run=ingest)
    comparison = commands.add_parser("compare", help="Compare a compiled Q2 candidate against compatible reference descriptors")
    comparison.add_argument("--bsp", type=Path, required=True)
    comparison.add_argument("--profile", type=Path, required=True)
    comparison.add_argument("--cohort", choices=("duel", "dm_small", "dm_medium", "dm_large", "ctf"), required=True)
    comparison.add_argument("--output", type=Path, required=True)
    comparison.set_defaults(run=compare)
    regression = commands.add_parser("benchmark", help="Generate, hash-pin compile, and compare a fixed prompt/seed matrix")
    regression.add_argument("--qmg", type=Path, required=True)
    regression.add_argument("--qbsp", type=Path, required=True)
    regression.add_argument("--qbsp-sha256", required=True)
    regression.add_argument("--profile", type=Path, required=True)
    regression.add_argument("--matrix", type=Path, default=PROJECT / "knowledge/q2/generator-regression.json")
    regression.add_argument("--output", type=Path, required=True)
    regression.set_defaults(run=benchmark)
    args = parser.parse_args()
    try:
        return args.run(args)
    except (ValueError, OSError) as error:
        print(f"map-study: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
