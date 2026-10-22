"""Synthetic Fold material, native-entity, support and ordinary-route controls."""

import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.map_recipes.fold_audit import audit_fold
from tools.map_recipes.geometry import GeometryError, convex_polyhedron
from tools.map_recipes.inspect import ordinary_navigation


WATER = 32 | (1 << 27) | (1 << 28)  # ericw WATER | DETAIL | TRANSLUCENT


def box_planes(low, high):
    return [(1, 0, 0, high[0]), (-1, 0, 0, -low[0]),
            (0, 1, 0, high[1]), (0, -1, 0, -low[1]),
            (0, 0, 1, high[2]), (0, 0, -1, -low[2])]


def fixture(extra_brushes=(), *, water_contents=WATER, target_support=True):
    entries = [(box_planes((-1024, -896, -24), (1024, 896, 0)), 1, 0)]
    if target_support:
        entries.append((box_planes((736, 0, 104), (896, 192, 128)), 1, 0))
    entries.append((box_planes((512, -64, 0), (752, 160, 24)), water_contents, 1))
    entries.extend(extra_brushes)
    planes, sides, brushes = [], [], []
    for rows, contents, tex in entries:
        first = len(sides)
        for plane in rows:
            sides.append((len(planes), tex))
            planes.append(plane)
        brushes.append((first, len(rows), contents))
    bsp = SimpleNamespace(models=[(0,) * 9 + (-1, 0, 0)], nodes=[],
                          leaves=[(0,) * 11 + (0, len(brushes))], leafbrushes=list(range(len(brushes))),
                          brushes=brushes, planes=planes, brushsides=sides,
                          texinfo=[(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, b'e1u1/flat1_1', -1),
                                   (1, 0, 0, 0, 0, 1, 0, 0, 24, 0, b'e3u2/water2', -1)],
                          entities=[{'classname': 'worldspawn'},
                                    {'classname': 'misc_teleporter', 'origin': '-800 608 24', 'target': 'fold_exit'},
                                    {'classname': 'misc_teleporter_dest', 'origin': '816 96 152',
                                     'targetname': 'fold_exit', 'angle': '270'}])

    def trace(start, end, mask=1):
        # The audit asks point-contents questions here, not movement traces.
        assert start == end
        for first, count, contents in bsp.brushes:
            if contents & mask and all(sum(bsp.planes[index][i] * start[i] for i in range(3))
                                       <= bsp.planes[index][3] for index, _ in bsp.brushsides[first:first + count]):
                return 0
        return 1

    bsp.trace = trace
    identity = 'a' * 64
    layout = {'layout_sha256': identity,
              'solids': [{'id': 1, 'purpose': 6, 'contents': 'water', 'bounds': [[512, -64, 0], [752, 160, 24]]}],
              'mechanisms': [{'kind': 'teleporter', 'from': [-800, 608, 0], 'to': [816, 96, 128]}],
              'navigation_edge_counts': {'teleporter': 1}, 'ordinary_path_checks': 264,
              'ordinary_route_edge_kinds': ['walk', 'step', 'door']}
    physics = {'e1u1/flat1_1': {'flags': 0, 'contents': 0, 'value': 0},
               'e3u2/water2': {'flags': 25, 'contents': 32, 'value': 100}}
    features = {'schema_version': 1, 'map': 'qmg_fold', 'layout_sha256': identity,
                'flat_segments': [{'label': 'synthetic lower floor', 'from': [32, 240, 0], 'to': [144, 240, 0]},
                                  {'label': 'synthetic upper floor', 'from': [768, 64, 128], 'to': [848, 64, 128]}],
                'teleporter': {'source_origin': [-800, 608, 24], 'destination_origin': [816, 96, 152],
                               'arrival_yaw': 270, 'optional': True}}
    return bsp, layout, physics, features


def ordinary_fixture(kind='walk'):
    placements = [{'id': i, 'role': 1 if i <= 8 else (4, 5, 6)[i % 3]} for i in range(1, 34)]
    # Mechanism endpoints deliberately have no ordinary binding. They must not
    # inflate the gameplay-pair count or become mandatory pickup destinations.
    placements += [{'id': 34, 'role': 17}, {'id': 35, 'role': 18}]
    layout = {'placements': placements, 'ramps': []}
    graph = {'nodes': [{'id': 1}, {'id': 2}],
             'edges': [{'id': 1, 'from': 1, 'to': 2, 'kind': kind}],
             'placement_bindings': [{'placement': i, 'node': 1 if i <= 8 else 2} for i in range(1, 34)]}
    report = {'analysis': {'navigation': {'player': {'graph': graph}}}}
    return layout, report


class FoldAuditTests(unittest.TestCase):
    def test_actual_translucent_water_modifiers_and_limited_evidence(self):
        result = audit_fold(*fixture())
        json.dumps(result)
        self.assertTrue(result['ok'])
        self.assertFalse(result['runtime_verified'])
        self.assertEqual(result['water_sample_inside'], [True, True, False])
        self.assertEqual(result['interpreted_standing_waterlevel_at_probe'], 2)
        self.assertEqual(result['destination_yaw'], 270)
        self.assertEqual(result['teleporter_native_clearance_poses'], [[-800, 608, 32], [816, 96, 162]])
        self.assertEqual(len(result['selected_flat_segments']), 2)
        self.assertTrue(all(row['continuous_hull_clear'] for row in result['selected_flat_segments']))

    def test_physical_contents_cannot_be_hidden_by_render_modifiers(self):
        for contents in (WATER | 1, WATER | 8, WATER | (1 << 16)):
            with self.subTest(contents=contents), self.assertRaises((AssertionError, GeometryError)):
                audit_fold(*fixture(water_contents=contents))

    def test_water_flags_value_and_material_are_checked_after_compilation(self):
        for index, value in ((8, 0), (8, 25), (9, 100), (10, b'e1u1/flat1_1')):
            args = fixture()
            tex = list(args[0].texinfo[1]); tex[index] = value; args[0].texinfo[1] = tuple(tex)
            with self.subTest(field=index, value=value), self.assertRaises(AssertionError):
                audit_fold(*args)

    def test_water_sample_telemetry_is_recomputed(self):
        args = fixture()
        args[0].trace = lambda start, end, mask=1: 1
        with self.assertRaises(AssertionError):
            audit_fold(*args)

    def test_same_bounds_and_water_samples_do_not_hide_a_chamfer(self):
        args = fixture(); bsp = args[0]
        first, count, contents = bsp.brushes[-1]
        bsp.brushsides.append((len(bsp.planes), 1))
        bsp.planes.append((1, 1, 0, 880))  # Cut 32 units off the northeast corner.
        bsp.brushes[-1] = (first, count + 1, contents)
        shape = convex_polyhedron(bsp.planes[i] for i, _ in bsp.brushsides[first:first + count + 1])
        self.assertEqual(shape.bounds, ((512, -64, 0), (752, 160, 24)))
        self.assertEqual([bsp.trace((632, 48, z), (632, 48, z), mask=32) == 0 for z in (1, 23, 46)],
                         [True, True, False])
        with self.assertRaisesRegex(AssertionError, 'complete authored box'):
            audit_fold(*args)

    def test_feature_hash_and_segment_geometry_are_fail_closed(self):
        for change in ('hash', 'diagonal', 'height', 'empty'):
            args = fixture(); features = args[3]
            if change == 'hash': features['layout_sha256'] = 'b' * 64
            elif change == 'diagonal': features['flat_segments'][0]['to'][1] += 1
            elif change == 'height': features['flat_segments'][0]['to'][2] += 1
            else: features['flat_segments'] = []
            with self.subTest(change=change), self.assertRaises((AssertionError, GeometryError)):
                audit_fold(*args)

    def test_clear_endpoints_do_not_hide_an_intermediate_obstruction(self):
        obstacle = (box_planes((80, 232, 24), (84, 248, 40)), 1, 0)
        with self.assertRaisesRegex(GeometryError, 'continuous standing-hull sweep'):
            audit_fold(*fixture([obstacle]))

    def test_teleporter_full_disc_support_and_raised_headroom(self):
        with self.assertRaisesRegex(AssertionError, 'full 64x64 compiled support'):
            audit_fold(*fixture(target_support=False))
        player_sized_support = (box_planes((800, 80, 104), (832, 112, 128)), 1, 0)
        with self.assertRaisesRegex(AssertionError, 'full 64x64 compiled support'):
            audit_fold(*fixture([player_sized_support], target_support=False))
        # Feet on floor128 could clear this, but the actual +10 arrival cannot.
        roof = (box_planes((784, 64, 190), (848, 128, 198)), 1, 0)
        with self.assertRaises(AssertionError):
            audit_fold(*fixture([roof]))

    def test_compiled_yaw_and_origin_must_match_declared_contract(self):
        for field, value in (('angle', '90'), ('angle', 'nan'), ('origin', '832 96 152')):
            args = fixture(); args[0].entities[2][field] = value
            with self.subTest(field=field, value=value), self.assertRaises((AssertionError, GeometryError, ValueError)):
                audit_fold(*args)

    def test_targetname_collision_cannot_redirect_native_g_find(self):
        args = fixture()
        args[0].entities.insert(2, {'classname': 'info_null', 'targetname': 'fold_exit', 'origin': '0 0 24'})
        with self.assertRaises(AssertionError):
            audit_fold(*args)

    def test_ordinary_roles_ignore_mechanisms_and_accept_only_ordinary_edges(self):
        for kind in ('walk', 'step', 'door'):
            with self.subTest(kind=kind):
                result = ordinary_navigation(*ordinary_fixture(kind))
                self.assertEqual(result['path_checks'], 264)
                self.assertFalse(result['runtime_verified'])
        for kind in ('jump', 'drop', 'lift', 'teleporter'):
            with self.subTest(kind=kind), self.assertRaisesRegex(AssertionError, 'Missing walk/step-only'):
                ordinary_navigation(*ordinary_fixture(kind))

    def test_ordinary_proof_keeps_edge_direction(self):
        layout, report = ordinary_fixture()
        graph = report['analysis']['navigation']['player']['graph']
        graph['placement_bindings'][0]['node'] = 2
        with self.assertRaisesRegex(AssertionError, 'Missing walk/step-only'):
            ordinary_navigation(layout, report)
        graph['edges'].append({'id': 2, 'from': 2, 'to': 1, 'kind': 'walk'})
        self.assertEqual(ordinary_navigation(layout, report)['path_checks'], 264)


if __name__ == '__main__':
    unittest.main()
