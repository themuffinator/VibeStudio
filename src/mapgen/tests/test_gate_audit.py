import unittest
from tools.map_recipes.gate_audit import audit_gates


def fixture():
    layout = {"layout_sha256": "fixture", "placements": [{"id": 1, "role": 1}, {"id": 2, "role": 4}],
              "connections": [{"id": 1, "from": 1, "to": 2}, {"id": 2, "from": 2, "to": 3}]}
    nodes = [{"id": i, "source": {"kind": "connection" if i in (3, 4) else "room",
               "id": {1: 1, 2: 2, 3: 1, 4: 2, 5: 2}[i]}} for i in range(1, 6)]
    # Nodes2/5 share a room label but are physically disconnected dry surfaces.
    edges = [{"from": a, "to": b, "kind": "walk"} for a, b in ((1,3),(3,1),(3,2),(2,3),(2,4),(4,2),(4,5),(5,4))]
    graph = {"nodes": nodes, "edges": edges, "placement_bindings": [{"placement":1,"node":1},{"placement":2,"node":5}]}
    return layout, {"analysis": {"navigation": {"player": {"layout_sha256":"fixture", "graph":graph}}}}


class GateTests(unittest.TestCase):
    def test_room_identity_does_not_merge_disconnected_surfaces(self):
        layout, report = fixture()
        result = audit_gates(layout, report)
        self.assertEqual(result["baseline_missing_pairs"], [])
        self.assertEqual([p["unreachable_pairs"] for p in result["passages"]], [1, 1])

    def test_new_independent_dry_return_removes_dependency(self):
        layout, report = fixture()
        report["analysis"]["navigation"]["player"]["graph"]["edges"].extend(
            [{"from":1,"to":5,"kind":"step"},{"from":5,"to":1,"kind":"step"}])
        self.assertEqual([p["unreachable_pairs"] for p in audit_gates(layout, report)["passages"]], [0, 0])

    def test_optional_mechanisms_are_not_dry_return(self):
        for kind in ("jump", "drop", "lift", "teleporter", "swim"):
            with self.subTest(kind=kind):
                layout, report = fixture()
                report["analysis"]["navigation"]["player"]["graph"]["edges"].append({"from":1,"to":5,"kind":kind})
                self.assertEqual(audit_gates(layout, report)["passages"][0]["unreachable_pairs"], 1)

    def test_direction_is_preserved(self):
        layout, report = fixture()
        report["analysis"]["navigation"]["player"]["graph"]["edges"].append({"from":5,"to":1,"kind":"walk"})
        self.assertEqual(audit_gates(layout, report)["passages"][0]["unreachable_pairs"], 1)

    def test_bad_identity_and_binding_rejected(self):
        layout, report = fixture()
        layout["layout_sha256"] = "different"
        with self.assertRaises(ValueError): audit_gates(layout, report)
        layout["layout_sha256"] = "fixture"
        report["analysis"]["navigation"]["player"]["graph"]["placement_bindings"][0]["node"] = 99
        with self.assertRaises(ValueError): audit_gates(layout, report)

    def test_explicit_baseline_closure_and_unknown_closure(self):
        layout, report = fixture()
        result = audit_gates(layout, report, [1])
        self.assertEqual(result["baseline_closed_connections"], [1])
        self.assertEqual(len(result["baseline_missing_pairs"]), 1)
        self.assertTrue(result["passages"][0]["already_closed_in_baseline"])
        with self.assertRaises(ValueError): audit_gates(layout, report, [99])


if __name__ == "__main__": unittest.main()
