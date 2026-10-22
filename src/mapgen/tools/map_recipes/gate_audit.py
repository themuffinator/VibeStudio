"""Measure dependence on one passage using the serialized ordinary route graph.

Closing a passage removes its navigation nodes, not its room. This is a graph
intervention; it does not compile a new wall or simulate movement/combat.
"""
import argparse
import hashlib
import json
from pathlib import Path

ORDINARY = frozenset(("walk", "step", "door"))
GAMEPLAY = frozenset((1, 4, 5, 6))


def audit_gates(layout, report, closed_connections=()):
    player = report["analysis"]["navigation"]["player"]
    if player["layout_sha256"] != layout["layout_sha256"]:
        raise ValueError("Layout and navigation identities differ")
    graph = player["graph"]
    nodes = {n["id"]: n for n in graph["nodes"]}
    if len(nodes) != len(graph["nodes"]):
        raise ValueError("Duplicate navigation node")
    binding_rows = graph["placement_bindings"]
    bindings = {b["placement"]: b["node"] for b in binding_rows}
    if len(bindings) != len(binding_rows):
        raise ValueError("Duplicate placement binding")
    anchors = [p for p in layout["placements"] if p["role"] in GAMEPLAY]
    spawns = [p for p in anchors if p["role"] == 1]
    if not spawns or not anchors or len({p["id"] for p in anchors}) != len(anchors):
        raise ValueError("Need distinct gameplay anchors and spawns")
    if any(bindings.get(p["id"]) not in nodes for p in anchors):
        raise ValueError("Gameplay anchor lacks an exact node binding")
    adjacent = {node: set() for node in nodes}
    for edge in graph["edges"]:
        if edge["from"] not in nodes or edge["to"] not in nodes:
            raise ValueError("Edge references an unknown node")
        if edge["kind"] in ORDINARY:
            adjacent[edge["from"]].add(edge["to"])

    def reachable_pairs(removed):
        missing = []
        for spawn in spawns:
            start = bindings[spawn["id"]]
            reached = set() if start in removed else {start}
            pending = list(reached)
            while pending:
                new = adjacent[pending.pop()] - removed - reached
                reached.update(new)
                pending.extend(new)
            missing.extend({"spawn": spawn["id"], "target": p["id"]}
                           for p in anchors if bindings[p["id"]] not in reached)
        return missing

    rows = []
    connection_ids = [c["id"] for c in layout["connections"]]
    if len(set(connection_ids)) != len(connection_ids):
        raise ValueError("Duplicate connection ID")
    if any(type(value) is not int or value not in connection_ids for value in closed_connections):
        raise ValueError("Unknown baseline passage closure")
    closed_connections = set(closed_connections)
    baseline_removed = {n["id"] for n in nodes.values() if n["source"]["kind"] == "connection"
                        and n["source"]["id"] in closed_connections}
    baseline_missing = reachable_pairs(baseline_removed)
    for connection in layout["connections"]:
        removed = {n["id"] for n in nodes.values()
                   if n["source"]["kind"] == "connection" and n["source"]["id"] == connection["id"]}
        if not removed:
            raise ValueError("Passage lacks serialized navigation nodes")
        missing = reachable_pairs(removed | baseline_removed)
        rows.append({"connection": connection["id"], "from_room": connection["from"],
                     "to_room": connection["to"], "removed_nodes": sorted(removed),
                     "already_closed_in_baseline": connection["id"] in closed_connections,
                     "unreachable_pairs": len(missing), "missing_pairs": missing})
    return {"method": "remove one passage node set from directed serialized ordinary graph",
            "layout_sha256": layout["layout_sha256"], "edge_kinds": sorted(ORDINARY),
            "spawn_count": len(spawns), "gameplay_anchor_count": len(anchors),
            "baseline_closed_connections": sorted(closed_connections),
            "baseline_pairs": len(spawns) * len(anchors), "baseline_missing_pairs": baseline_missing,
            "passages": rows, "runtime_verified": False,
            "limits": ["The same room may contain disconnected surfaces; room adjacency alone does not prove a dry return.",
                       "Node removal is an analytical route-dependency experiment, not a compiled doorway closure.",
                       "One-way edges remain one-way. Jumps, drops, lifts, teleports and liquid travel are excluded.",
                       "An optional pickup recess may intentionally depend on one exit. No universal quality threshold is assigned."]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="Existing recipe output directory")
    parser.add_argument("--close", type=int, action="append", default=[], help="Baseline passage node closure")
    args = parser.parse_args()
    output = args.output.resolve(strict=True)
    layout_path = output / "layout.json"
    layout = json.loads(layout_path.read_text(encoding="utf-8"))
    report_path = output / (layout["map_name"] + ".qmg.json")
    report = json.loads(report_path.read_text(encoding="utf-8"))
    result = audit_gates(layout, report, args.close)
    result["input_hashes"] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (layout_path, report_path)}
    suffix = "-closed-" + "-".join(map(str, sorted(set(args.close)))) if args.close else ""
    target = output / ("ordinary-gate-dependencies" + suffix + ".json")
    target.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(target)


if __name__ == "__main__":
    main()
