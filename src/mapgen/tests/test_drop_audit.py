"""Independent synthetic compiled geometry and serialized-graph drop cases."""

from copy import deepcopy
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.map_recipes.drop_audit import audit_drops
from tools.map_recipes.geometry import GeometryError


def box_planes(low, high):
    return [(1, 0, 0, high[0]), (-1, 0, 0, -low[0]),
            (0, 1, 0, high[1]), (0, -1, 0, -low[1]),
            (0, 0, 1, high[2]), (0, 0, -1, -low[2])]


def fixture(extra_brushes=(), *, ledge=True, ground=True):
    # Deliberate 128-unit drop to the right of a 24-thick upper ledge.
    entries = []
    if ledge:
        entries.append((box_planes((-128, -64, 104), (0, 64, 128)), 1))
    if ground:
        entries.append((box_planes((-256, -128, -24), (256, 256, 0)), 1))
    entries.extend(extra_brushes)
    planes, brushsides, brushes = [], [], []
    for rows, contents in entries:
        first = len(brushsides)
        for row in rows:
            brushsides.append((len(planes), 0))
            planes.append(row)
        brushes.append((first, len(rows), contents))
    bsp = SimpleNamespace(models=[(0,) * 9 + (-1, 0, 0)], nodes=[],
                          leaves=[(0,) * 11 + (0, len(brushes))], leafbrushes=list(range(len(brushes))),
                          brushes=brushes, planes=planes, brushsides=brushsides)
    identity = 'a' * 64
    layout = {'layout_sha256': identity, 'drops': [
        {'id': 1, 'label': 'synthetic ledge', 'from': [-32, 0, 128],
         'departure': [32, 0, 128], 'to': [32, 0, 0]}]}
    def node(key, low_x, high_x, floor):
        return {'id': key, 'area': {'min_x': low_x, 'min_y': -48, 'max_x': high_x,
                                   'max_y': 48, 'floor_z': floor, 'ceiling_z': floor + 96, 'slope': None},
                'origin': [(low_x + high_x) // 2, 0, floor + 24]}
    # The return's intermediate source graph is intentionally supplied evidence,
    # not a claimed compiled geometry proof. Each step changes source floor by16;
    # the audit separately states that this whole return is not compiled-swept.
    nodes = [node(1, 16, 240, 0), node(2, -112, -16, 128)]
    nodes.extend(node(i + 2, 64, 192, i * 16) for i in range(1, 8))
    route = [1] + list(range(3, 10)) + [2]
    edges = [{'id': i + 1, 'from': a, 'to': b, 'kind': 'step'}
             for i, (a, b) in enumerate(zip(route, route[1:]))]
    edges.append({'id': 99, 'from': 2, 'to': 1, 'kind': 'drop'})
    report = {'layout_sha256': identity, 'analysis': {'navigation': {'player': {
        'ok': True, 'layout_sha256': identity,
        'profile': {'game': 'quake2', 'hull_mins': [-16, -16, -24], 'hull_maxs': [16, 16, 32]},
        'graph': {'nodes': nodes, 'edges': edges}}}}}
    return bsp, layout, report


class DropAuditTests(unittest.TestCase):
    def test_clear_drop_has_independent_ordinary_return_and_explicit_limits(self):
        result = audit_drops(*fixture())
        json.dumps(result)
        self.assertTrue(result['ok'])
        self.assertEqual(result['swept_route_segments'], 2)
        self.assertFalse(result['runtime_verified'])
        self.assertFalse(result['ballistic_trajectory_tested'])
        self.assertFalse(result['fall_damage_tested'])
        self.assertEqual(result['excluded_return_graph_edges'], 1)
        drop = result['drops'][0]
        self.assertEqual(drop['geometric_drop_height'], 128)
        self.assertEqual(drop['from']['standing_origin'], [-32, 0, 152])
        self.assertTrue(drop['supporting_ledge_included_in_collision_checks'])
        path = drop['ordinary_return']
        self.assertEqual(path['bound_landing_node'], 1)
        self.assertEqual(path['bound_takeoff_node'], 2)
        self.assertEqual(path['edge_ids'], list(range(1, 9)))
        self.assertTrue(all(kind == 'step' for kind in path['edge_kinds']))
        self.assertFalse(path['compiled_entire_return_swept'])

    def test_supporting_ledge_is_not_excluded_from_descent(self):
        bsp, layout, report = fixture()
        # Fifteen units beyond the ledge leaves one unit of the hull overlapping.
        layout['drops'][0]['departure'][0] = layout['drops'][0]['to'][0] = 15
        with self.assertRaisesRegex(GeometryError, 'drop descent.*player-blocking brush'):
            audit_drops(bsp, layout, report)
        # Exact side contact is allowed once the complete hull clears the edge.
        layout['drops'][0]['departure'][0] = layout['drops'][0]['to'][0] = 16
        self.assertTrue(audit_drops(bsp, layout, report)['ok'])

    def test_all_horizontal_departure_directions_at_upper_floor(self):
        from itertools import product
        for axis, sign in ((0, 1), (0, -1), (1, 1), (1, -1)):
            with self.subTest(axis=axis, sign=sign):
                bsp, layout, report = fixture()
                def point(p):
                    return [sign * p[0], p[1], p[2] + 128] if axis == 0 else [p[1], sign * p[0], p[2] + 128]
                bsp.planes = [(sign * a, b, c, d + c * 128) if axis == 0 else (b, sign * a, c, d + c * 128)
                              for a, b, c, d in bsp.planes]
                for name in ('from', 'departure', 'to'):
                    layout['drops'][0][name] = point(layout['drops'][0][name])
                for node in report['analysis']['navigation']['player']['graph']['nodes']:
                    node['origin'] = point(node['origin'])
                    a = node['area']
                    corners = [point((x, y, a['floor_z'])) for x, y in product((a['min_x'], a['max_x']),
                                                                                  (a['min_y'], a['max_y']))]
                    a.update(min_x=min(p[0] for p in corners), max_x=max(p[0] for p in corners),
                             min_y=min(p[1] for p in corners), max_y=max(p[1] for p in corners),
                             floor_z=a['floor_z'] + 128, ceiling_z=a['ceiling_z'] + 128)
                result = audit_drops(bsp, layout, report)
                self.assertEqual(result['drops'][0]['from']['feet'][2], 256)
                self.assertEqual(result['drops'][0]['to']['feet'][2], 128)

    def test_obstruction_between_clear_endpoints_is_rejected(self):
        blocker = box_planes((20, -8, 64), (24, 8, 72))
        with self.assertRaisesRegex(GeometryError, 'drop descent.*continuous standing-hull sweep'):
            audit_drops(*fixture([(blocker, 1)]))

    def test_horizontal_departure_obstruction_is_rejected(self):
        blocker = box_planes((-1, -8, 136), (1, 8, 144))
        with self.assertRaisesRegex(GeometryError, 'drop departure.*continuous standing-hull sweep'):
            audit_drops(*fixture([(blocker, 1)]))

    def test_player_mask_is_complete_and_water_is_not_a_blocker(self):
        blocker = box_planes((20, -8, 64), (24, 8, 72))
        for contents in (2, 1 << 16, 1 << 25):
            with self.subTest(contents=contents), self.assertRaisesRegex(GeometryError, 'player-blocking brush'):
                audit_drops(*fixture([(blocker, contents)]))
        result = audit_drops(*fixture([(blocker, 32)]))
        self.assertEqual(result['world_player_blocking_brushes'], 2)

    def test_each_endpoint_requires_full_flat_support_and_headroom(self):
        with self.assertRaisesRegex(GeometryError, 'takeoff lacks compiled flat support'):
            audit_drops(*fixture(ledge=False))
        with self.assertRaisesRegex(GeometryError, 'landing lacks compiled flat support'):
            audit_drops(*fixture(ground=False))
        with self.assertRaisesRegex(GeometryError, 'landing standing hull overlaps'):
            audit_drops(*fixture([(box_planes((16, -16, 48), (48, 16, 64)), 1)]))
        # Support under only the center is insufficient for the landing hull.
        with self.assertRaisesRegex(GeometryError, 'landing lacks compiled flat support'):
            audit_drops(*fixture([(box_planes((24, -8, -24), (40, 8, 0)), 1)], ground=False))

    def test_a_clear_descent_fails_without_an_ordinary_return(self):
        for kind in ('jump', 'drop', 'lift', 'teleporter'):
            bsp, layout, report = fixture()
            report['analysis']['navigation']['player']['graph']['edges'] = [
                {'id': 1, 'from': 1, 'to': 2, 'kind': kind}]
            with self.subTest(kind=kind), self.assertRaisesRegex(GeometryError, 'lacks an ordinary'):
                audit_drops(bsp, layout, report)
        bsp, layout, report = fixture()
        graph = report['analysis']['navigation']['player']['graph']
        for edge in graph['edges']:
            edge['from'], edge['to'] = edge['to'], edge['from']
        with self.assertRaisesRegex(GeometryError, 'lacks an ordinary'):
            audit_drops(bsp, layout, report)

    def test_endpoint_binding_uses_local_slope_height_and_no_snap(self):
        bsp, layout, report = fixture()
        node = report['analysis']['navigation']['player']['graph']['nodes'][1]
        node['area'].update(min_x=-112, max_x=-16, floor_z=108, ceiling_z=224,
                            slope={'along_x': True, 'start': -128, 'end': 0,
                                   'low_z': 100, 'rise': 32, 'support_offset': 16})
        node['origin'] = [-64, 0, 144]  # Local foot120; drop endpoint at x=-32 has foot128.
        result = audit_drops(bsp, layout, report)
        self.assertEqual(result['drops'][0]['ordinary_return']['bound_takeoff_node'], 2)
        node['area']['slope']['low_z'] = 101
        node['origin'][2] = 145
        with self.assertRaisesRegex(GeometryError, 'no exact local-height graph binding'):
            audit_drops(bsp, layout, report)

    def test_bad_identity_or_graph_structure_does_not_create_evidence(self):
        bsp, layout, report = fixture()
        report['layout_sha256'] = 'b' * 64
        with self.assertRaisesRegex(GeometryError, 'identity mismatch'):
            audit_drops(bsp, layout, report)
        bsp, layout, report = fixture()
        graph = report['analysis']['navigation']['player']['graph']
        graph['edges'][0]['to'] = 10000
        with self.assertRaisesRegex(GeometryError, 'missing node'):
            audit_drops(bsp, layout, report)
        bsp, layout, report = fixture()
        graph = report['analysis']['navigation']['player']['graph']
        graph['nodes'].append(deepcopy(graph['nodes'][0]))
        with self.assertRaisesRegex(GeometryError, 'duplicate return node'):
            audit_drops(bsp, layout, report)

    def test_explicit_budgets_and_malformed_descent_fail_closed(self):
        for budget in ('MAX_WORLD_BRUSHES', 'MAX_NODES', 'MAX_EDGES'):
            with self.subTest(budget=budget), patch('tools.map_recipes.drop_audit.' + budget, 1):
                with self.assertRaisesRegex(GeometryError, 'budget exceeded'):
                    audit_drops(*fixture())
        bsp, layout, report = fixture()
        layout['drops'][0]['departure'][1] = 1
        with self.assertRaisesRegex(GeometryError, 'axial horizontal leg'):
            audit_drops(bsp, layout, report)
        bsp, layout, report = fixture()
        layout['drops'][0]['to'][2] = -1
        with self.assertRaisesRegex(GeometryError, '128-unit geometric bound'):
            audit_drops(bsp, layout, report)


if __name__ == '__main__':
    unittest.main()
