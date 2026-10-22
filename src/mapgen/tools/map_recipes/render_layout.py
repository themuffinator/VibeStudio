"""Render a recipe's typed geometry and compiled anchors as an analytical fragment.

Usage: python -B -m tools.map_recipes.render_layout --output out/spillway
The directory must already contain layout.json and compiled-analysis.json.
Standalone browser wrapping and QA are separate from this reproducible export.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import stat


def _is_link(path: Path) -> bool:
    attributes = getattr(path.lstat(), "st_file_attributes", 0)
    return path.is_symlink() or bool(attributes & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0))


def _bounds_valid(bounds) -> bool:
    return (
        isinstance(bounds, list)
        and len(bounds) == 2
        and all(isinstance(point, list) and len(point) == 3 for point in bounds)
        and all(isinstance(value, (int, float)) and math.isfinite(value) for point in bounds for value in point)
        and all(bounds[0][axis] < bounds[1][axis] for axis in range(3))
    )


def _point_valid(point, dimensions=3) -> bool:
    return (isinstance(point, list) and len(point) == dimensions
            and all(isinstance(value, (int, float)) and not isinstance(value, bool)
                    and math.isfinite(value) for value in point))


def _footprint_valid(points, bounds) -> bool:
    """A footprint describes one convex vertical prism, never its aggregate box."""
    if not isinstance(points, list) or len(points) < 3 or any(not _point_valid(point, 2) for point in points):
        return False
    if len({tuple(point) for point in points}) != len(points):
        return False
    if any(point[axis] < bounds[0][axis] - .001 or point[axis] > bounds[1][axis] + .001
           for point in points for axis in range(2)):
        return False
    turns = []
    for index, point in enumerate(points):
        following = points[(index + 1) % len(points)]
        for other in points:
            turn = ((following[0] - point[0]) * (other[1] - point[1])
                    - (following[1] - point[1]) * (other[0] - point[0]))
            if abs(turn) > 1e-6:
                turns.append(turn)
    return bool(turns) and (all(turn > 0 for turn in turns) or all(turn < 0 for turn in turns))


def _native_drop_evidence(output: Path, layout: dict):
    """Bind optional native probes without turning the diagram into a trajectory.

    Native frame bytes remain in the evidence directory. Only a small status
    record reaches HTML; the independently checked geometric endpoints stay put.
    """
    summary_path = output / "native-routes" / "summary.json"
    if not summary_path.exists():
        return None

    def require(condition, message):
        if not condition:
            raise ValueError("Native drop display: " + message)

    def read(path):
        require(path.is_file(), "missing evidence file")
        require(not any(_is_link(parent) for parent in (path, *path.parents)
                        if parent == output or output in parent.parents), "evidence traverses a link")
        return path.read_bytes()

    def digest(path):
        return hashlib.sha256(read(path)).hexdigest()

    def record(path):
        return json.loads(read(path).decode("utf-8-sig"))

    summary = record(summary_path)
    map_name = layout["map_name"]
    require(summary.get("schema_version") == 1 and summary.get("world_only") is True
            and summary.get("gameplay_tested") is False, "scope must be world-only with gameplay untested")
    require(summary.get("layout_sha256") == layout.get("layout_sha256"), "canonical layout changed")
    for key, filename in (("layout_file_sha256", "layout.json"), ("bsp_sha256", map_name + ".bsp"),
                          ("map_sha256", map_name + ".map")):
        require(summary.get(key) == digest(output / filename), "stale " + filename)
    native = summary.get("drop", {})
    matches = [drop for drop in layout.get("drops", []) if drop["id"] == native.get("id")]
    require(len(matches) == 1, "unknown or duplicate drop identity")
    drop = matches[0]
    require(native.get("geometric_candidate_departure") == drop["departure"]
            and native.get("geometric_candidate_landing") == drop["to"], "geometric candidate endpoints changed")
    trials = native.get("trials", [])
    require(len(trials) == 2 and {trial.get("forwardmove") for trial in trials} == {200, 400}
            and len({trial.get("id") for trial in trials}) == 2, "need two distinct fixed-input probes")
    for trial in trials:
        trial_id = trial.get("id")
        require(isinstance(trial_id, str) and re.fullmatch(r"[a-z][a-z0-9_-]*", trial_id), "invalid trial ID")
        require(trial.get("success") is True and trial.get("failure_reason") is None
                and trial.get("route") == "drop" and trial.get("source_feet") == drop["from"],
                "failed probe or different departure")
        directory = summary_path.parent / trial_id
        require(digest(directory / "result.json") == trial.get("result_sha256")
                and digest(directory / "route-audit.json") == trial.get("route_audit_sha256"),
                "stale probe result or route audit")
        result, audit = record(directory / "result.json"), record(directory / "route-audit.json")
        require(result.get("id") == trial_id and result.get("success") is True
                and result.get("failure_reason") is None and result.get("bsp_sha256") == summary["bsp_sha256"]
                and result.get("stable_grounded_settle_verified") is True
                and result.get("initial_native_state", {}).get("world_only_collision") is True,
                "result lacks current world-only movement evidence")
        for filename, key in (("trial.json", "trial_sha256"), ("commands.txt", "commands_sha256"),
                              ("frames.ndjson", "frames_sha256")):
            require(digest(directory / filename) == result.get(key), "stale original " + filename)
        spec = record(directory / "trial.json")
        require(spec.get("id") == trial_id and spec.get("start", {}).get("feet") == drop["from"]
                and spec.get("start", {}).get("clearance_z") == trial.get("explicit_launch_clearance_z") == .125
                and spec.get("start", {}).get("velocity") == [0, 0, 0], "different native start")
        phases = spec.get("commands", [])
        require(len(phases) == 1 and phases[0].get("forward") == trial["forwardmove"]
                and phases[0].get("side") == phases[0].get("up") == phases[0].get("pitch") == 0,
                "probe is not the declared fixed no-jump input")
        require(audit.get("success") is True and audit.get("failure_reason") is None
                and audit.get("independent_first_goal_matches_runner") is True
                and audit.get("stable_standing_dry_start_verified") is True
                and audit.get("standing_dry_through_observation") is True, "native route audit failed")
        candidate = audit.get("static_drop_candidate", {})
        require(candidate.get("departure_feet") == drop["from"]
                and candidate.get("clearance_feet") == drop["departure"]
                and candidate.get("landing_feet") == drop["to"], "audit candidate differs from diagram")
        require(audit.get("bounded_goal_time_ms") == result.get("movement_time_ms")
                == trial.get("bounded_goal_time_ms"), "arrival time differs from accepted evidence")
        goal = audit.get("first_grounded_station_goal", {})
        observed = result.get("final_observed_frame", {})
        require(goal.get("grounded") is True and goal.get("waterlevel") == 0
                and all(goal.get(key) == observed.get(key) for key in ("frame", "origin", "feet_z", "grounded", "waterlevel"))
                and _point_valid(trial.get("actual_goal_feet"))
                and [*goal.get("origin", [])[:2], goal.get("feet_z")] == trial["actual_goal_feet"]
                and abs(goal["feet_z"] - drop["to"][2]) <= 1, "native arrival is not the declared dry lower floor")
    return {"summary_sha256": digest(summary_path), "bsp_sha256": summary["bsp_sha256"],
            "world_only": True, "gameplay_tested": False, "diagram_is_native_trajectory": False,
            "drops": [{"id": drop["id"], "probe_count": 2, "forward_inputs": [200, 400]}]}


def render(output: Path) -> Path:
    output = output.absolute()
    for path in (output, *output.parents):
        if _is_link(path):
            raise ValueError(f"Output directory cannot traverse a filesystem link: {path}")
    output = output.resolve(strict=True)
    layout = json.loads((output / "layout.json").read_text(encoding="utf-8-sig"))
    compiled = json.loads((output / "compiled-analysis.json").read_text(encoding="utf-8-sig"))
    map_name = layout["map_name"]
    if not re.fullmatch(r"[a-z][a-z0-9_-]*", map_name):
        raise ValueError("map_name must be a lowercase filename identifier")
    levels = layout["levels"]
    if not levels or any(not isinstance(value, (int, float)) or not math.isfinite(value) for value in levels):
        raise ValueError("levels must contain finite numeric elevations")
    if len(set(levels)) != len(levels):
        raise ValueError("levels cannot contain repeated elevations")
    # A sunken local room floor remains inspectable without being counted as a
    # main combat floor. Its actual elevation is retained in separated views.
    layout["local_floor_levels"] = [
        {"height": z, "label": "/".join(r["name"] for r in layout["rooms"] if r["bounds"][0][2] == z) + " bed"}
        for z in sorted({r["bounds"][0][2] for r in layout["rooms"]} - set(levels))
    ]
    owner_ids = {placement["id"] for placement in layout.get("placements", [])}
    owner_ids.update(mechanism["id"] for mechanism in layout.get("mechanisms", []))
    solid_ids = set()
    liquids = []
    for solid in layout["solids"]:
        if not _bounds_valid(solid["bounds"]):
            raise ValueError("Solids must have valid bounds")
        if solid.get("purpose") == 6:
            if solid.get("contents") not in {"water", "slime", "lava"} or solid.get("owner") is not None:
                raise ValueError("Liquid display requires explicit native contents and a world volume")
            liquids.append({"id":solid["id"], "bounds":solid["bounds"], "contents":solid["contents"]})
        elif solid.get("contents", "solid") != "solid":
            raise ValueError("Non-solid contents require the liquid purpose")
        if "footprint" in solid and not _footprint_valid(solid["footprint"], solid["bounds"]):
            raise ValueError("Solid footprints must be finite convex polygons inside their bounds")
        if solid.get("owner") is not None and solid["owner"] not in owner_ids:
            raise ValueError("Solid owners must identify a declared placement/mechanism or be null")
        if "id" in solid:
            if solid["id"] in solid_ids:
                raise ValueError("Solid IDs cannot repeat")
            solid_ids.add(solid["id"])
    ramp_ids, ramp_solids = set(), set()
    for ramp in layout.get("ramps", []):
        if (not isinstance(ramp.get("id"), (str, int)) or isinstance(ramp["id"], bool)
                or ramp["id"] in ramp_ids):
            raise ValueError("Ramps must have unique string or integer IDs")
        ramp_ids.add(ramp["id"])
        if ramp.get("solid_id") not in solid_ids or ramp["solid_id"] in ramp_solids:
            raise ValueError("Each ramp must bind a distinct declared solid")
        ramp_solids.add(ramp["solid_id"])
        if not _bounds_valid(ramp.get("bounds")):
            raise ValueError("Ramps must declare valid spatial bounds")
        solid = next(solid for solid in layout["solids"] if solid.get("id") == ramp["solid_id"])
        if any(ramp["bounds"][0][axis] < solid["bounds"][0][axis] - .001
               or ramp["bounds"][1][axis] > solid["bounds"][1][axis] + .001 for axis in range(3)):
            raise ValueError("Ramp bounds must lie within the associated solid's conservative bounds")
        surface = ramp.get("surface")
        if (not isinstance(surface, list) or len(surface) != 4
                or any(not _point_valid(point) for point in surface)
                or not _footprint_valid([point[:2] for point in surface], ramp["bounds"])
                or any(point[axis] < ramp["bounds"][0][axis] - .001
                       or point[axis] > ramp["bounds"][1][axis] + .001
                       for point in surface for axis in range(3))):
            raise ValueError("Ramp surfaces must be convex XYZ quads inside their solid bounds")
        if any(not _point_valid(ramp.get(endpoint)) for endpoint in ("from", "to")):
            raise ValueError("Ramps must declare finite low and high edge centers")
        for endpoint, edge in (("from", surface[:2]), ("to", surface[2:])):
            if (any(abs(point[2] - ramp[endpoint][2]) > .001 for point in edge)
                    or any(abs(sum(point[axis] for point in edge) / 2 - ramp[endpoint][axis]) > .001
                           for axis in range(3))):
                raise ValueError("Ramp surface order must be low-left, low-right, high-right, high-left")
        # The two horizontal edges plus equal corner heights must lie on one plane.
        u = [surface[1][i] - surface[0][i] for i in range(3)]
        v = [surface[2][i] - surface[0][i] for i in range(3)]
        normal = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
        length = math.sqrt(sum(value*value for value in normal))
        if not length or abs(sum(normal[i]*(surface[3][i]-surface[0][i]) for i in range(3))) / length > .001:
            raise ValueError("Ramp surface must be planar")
        measured = {"width":math.dist(surface[0][:2], surface[1][:2]),
                    "run":math.dist(ramp["from"][:2], ramp["to"][:2]),
                    "rise":ramp["to"][2] - ramp["from"][2]}
        if any(not isinstance(ramp.get(key), (int, float)) or isinstance(ramp[key], bool)
               or not math.isfinite(ramp[key]) or ramp[key] <= 0 or abs(ramp[key] - value) > .001
               for key, value in measured.items()):
            raise ValueError("Ramp width, run and rise must match its actual surface")
        if abs(math.dist(surface[2][:2], surface[3][:2]) - ramp["width"]) > .001:
            raise ValueError("Ramp high and low edges must have the declared width")
        if (ramp.get("base_floor") not in levels or ramp["to"][2] not in levels
                or ramp["base_floor"] >= ramp["to"][2]
                or min(levels, key=lambda z:abs(z-ramp["from"][2])) != ramp["base_floor"]):
            raise ValueError("Ramp ends must connect the declared lower floor context and an upper floor")
    mechanism_ids = set()
    for mechanism in layout.get("mechanisms", []):
        if mechanism.get("kind") not in {"lift", "teleporter", "drop"}:
            raise ValueError("Mechanisms must declare lift, teleporter or drop kind")
        if (not isinstance(mechanism.get("id"), (str, int)) or isinstance(mechanism["id"], bool)
                or mechanism["id"] in mechanism_ids):
            raise ValueError("Mechanisms must have unique string or integer IDs")
        mechanism_ids.add(mechanism["id"])
        if not _point_valid(mechanism.get("from")) or not _point_valid(mechanism.get("to")):
            raise ValueError("Mechanism stations must declare finite XYZ coordinates")
        if not _bounds_valid(mechanism.get("bounds")):
            raise ValueError("Mechanisms must declare valid spatial bounds")
        if mechanism["from"] == mechanism["to"]:
            raise ValueError("Mechanism stations must be distinct")
        if mechanism["kind"] == "lift" and mechanism["from"][:2] != mechanism["to"][:2]:
            raise ValueError("The analytical lift viewer currently requires vertical station alignment")
    for run in layout["stair_runs"]:
        bounds = run if isinstance(run, list) else run.get("bounds", run.get("steps", []))
        bounds = [step.get("bounds", step) if isinstance(step, dict) else step for step in bounds]
        if not bounds or any(not _bounds_valid(value) for value in bounds):
            raise ValueError("stair_runs must contain explicit arrays of valid AABB tread bounds")
        local_tops = {s['bounds'][1][2] for s in layout['solids'] if s.get('purpose') == 1
                      and min(levels) <= s['bounds'][1][2] <= max(levels)}
        if bounds[0][0][2] not in levels or bounds[-1][1][2] not in set(levels) | local_tops:
            raise ValueError("Each stair run must join a declared floor or explicit local platform")
        if any(before[1][2] >= after[1][2] for before, after in zip(bounds, bounds[1:])):
            raise ValueError("Stair treads must be ordered uphill")
    drops = layout.get("drops", [])
    if not isinstance(drops, list) or len(drops) > 16:
        raise ValueError("Drops must be a bounded list of explicit candidates")
    drop_ids = set()
    for drop in drops:
        if (not isinstance(drop.get("id"), (str, int)) or isinstance(drop["id"], bool)
                or drop["id"] in drop_ids):
            raise ValueError("Drop candidates must have unique string or integer IDs")
        drop_ids.add(drop["id"])
        if any(not _point_valid(drop.get(endpoint)) for endpoint in ("from", "departure", "to")):
            raise ValueError("Drop candidates must retain finite takeoff, departure and landing XYZ points")
        source, departure, target = (drop[key] for key in ("from", "departure", "to"))
        if (source[2] not in levels or target[2] not in levels or source[2] <= target[2]
                or departure[2] != source[2] or departure[:2] != target[:2]
                or sum(source[axis] != departure[axis] for axis in range(2)) != 1):
            raise ValueError("Drop display requires an axial departure followed by a vertical descent between declared floors")
    layout.pop("drop_evidence", None)
    verification_path = output / "verification.json"
    if drops and verification_path.exists():
        verification = json.loads(verification_path.read_text(encoding="utf-8-sig"))
        audit = verification.get("drop_checks")
        if audit:
            if (audit.get("ok") is not True or audit.get("runtime_verified") is not False
                    or audit.get("layout_sha256") != layout.get("layout_sha256")):
                raise ValueError("Drop display evidence does not match the current layout")
            bsp_hash = hashlib.sha256((output / f"{map_name}.bsp").read_bytes()).hexdigest()
            if verification.get("bsp_sha256") != bsp_hash:
                raise ValueError("Drop display evidence is stale for the current BSP")
            checked = []
            for drop in drops:
                matches = [item for item in audit.get("drops", []) if item.get("id") == drop["id"]]
                if len(matches) != 1:
                    raise ValueError("Each displayed drop needs one matching audit record")
                item = matches[0]
                if any(item.get(endpoint, {}).get("feet") != drop[endpoint] for endpoint in ("from", "departure", "to")):
                    raise ValueError("Drop audit endpoints differ from the displayed geometry")
                ordinary = item.get("ordinary_return", {})
                if (ordinary.get("graph_reachability_verified") is not True
                        or any(kind not in {"walk", "step", "door"} for kind in ordinary.get("edge_kinds", []))):
                    raise ValueError("Displayed ordinary return lacks accepted graph evidence")
                checked.append({"id": drop["id"], "ordinary_return_verified": True,
                                "ordinary_return_edges": len(ordinary["edge_ids"])})
            layout["drop_evidence"] = {"bsp_sha256": bsp_hash, "drops": checked, "runtime_verified": False}
    layout.pop("native_drop_evidence", None)
    native_drop_evidence = _native_drop_evidence(output, layout)
    if native_drop_evidence:
        layout["native_drop_evidence"] = native_drop_evidence
    layout.pop("liquids", None)
    if liquids:
        layout["liquids"] = liquids
    if map_name == "qmg_lockyard":
        feature_path = output / "features.json"
        features = json.loads(feature_path.read_text(encoding="utf-8"))
        verification = json.loads((output / "verification.json").read_text(encoding="utf-8"))
        if (features.get("layout_sha256") != layout["layout_sha256"] or
                verification.get("features_sha256") != hashlib.sha256(feature_path.read_bytes()).hexdigest()):
            raise ValueError("Local route display requires matching feature evidence")
        layout["sky_room_ids"] = features["sky_rooms"]
        if any(room_id not in {room["id"] for room in layout["rooms"]} for room_id in layout["sky_room_ids"]):
            raise ValueError("Sky display refers to an unknown room")
        ceiling = features["submerged_ceiling"]
        if not _bounds_valid(ceiling) or not any(s["bounds"] == ceiling for s in layout["solids"]):
            raise ValueError("Submerged ceiling display must bind its actual solid")
        layout["overhead_obstacles"] = [{"bounds":ceiling, "context":features["local_wet_bed"],
                                         "label":"Dive under roof"}]
        layout["auxiliary_stair_runs"] = []
        for index, (key, name) in enumerate((("descent_steps", "West wet exit"), ("ascent_steps", "East wet exit"))):
            steps = features["wet_trial"][key]
            if (not steps or any(not _bounds_valid(b) for b in steps) or
                    any(not any(s["bounds"] == b and s.get("contents", "solid") == "solid"
                                for s in layout["solids"]) for b in steps)):
                raise ValueError("Wet stair display must bind actual solid treads")
            steps = sorted(steps, key=lambda b: b[1][2])
            if (steps[0][0][2] != features["local_wet_bed"] or steps[-1][1][2] not in levels or
                    any(a[1][2] >= b[1][2] for a,b in zip(steps, steps[1:]))):
                raise ValueError("Wet steps need an explicit bed and uphill dry exit")
            layout["auxiliary_stair_runs"].append({"id":f"W{index+1}", "name":name, "steps":steps})
    layout["anchors"] = compiled["anchors"]
    template = Path(__file__).with_name("layout-view-v2.html").read_text(encoding="utf-8")
    if template.count("__LAYOUT_DATA__") != 1:
        raise ValueError("The layout template must contain exactly one data placeholder")
    data = json.dumps(layout, ensure_ascii=False, separators=(",", ":")).replace("<", "\\u003c")
    fragment = template.replace("__LAYOUT_DATA__", data)
    if len(fragment.encode("utf-8")) > 1_000_000:
        raise ValueError("Analytical fragment exceeds the 1 MB size limit")
    slug = map_name.removeprefix("qmg_")
    destination = output / f"{slug}-layout.html"
    if destination.is_symlink() or (destination.exists() and _is_link(destination)):
        raise ValueError(f"Cannot replace a linked output file: {destination}")
    destination.write_text(fragment, encoding="utf-8", newline="\n")
    return destination


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="Existing recipe output directory")
    args = parser.parse_args()
    print(render(args.output))


if __name__ == "__main__":
    main()
