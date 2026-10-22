"""Synthetic compiled-plane regression cases; no archive geometry or assets."""

import math
import sys
import unittest
from fractions import Fraction
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.map_recipes.geometry import (
    GeometryError, convex_polyhedron, intersects_aabb, intersects_hull,
    intersects_swept_aabb, model_brushes, support_height, supported_hull,
    translated, vertical_prism,
)
from tools.map_recipes.ramp_audit import MASK_PLAYERSOLID, audit_ramps


def box_planes(low=(0, 0, 0), high=(10, 10, 10)):
    return [(1, 0, 0, high[0]), (-1, 0, 0, -low[0]),
            (0, 1, 0, high[1]), (0, -1, 0, -low[1]),
            (0, 0, 1, high[2]), (0, 0, -1, -low[2])]


def model_fixture():
    # World model references brushes 0 and 1; lift model references only 2.
    # Repeated leaves and repeated leafbrush entries must not duplicate brushes.
    return SimpleNamespace(
        models=[(0,) * 9 + (0, 0, 0), (0,) * 9 + (1, 0, 0), (0,) * 9 + (-3, 0, 0)],
        nodes=[(0, -1, -2), (0, -3, -3)],
        leaves=[(0,) * 11 + (0, 2), (0,) * 11 + (2, 1), (0,) * 11 + (3, 1)],
        leafbrushes=[0, 0, 1, 2],
        brushes=[(0, 6, 1), (6, 6, 1), (12, 6, 1)],
    )


class ModelBrushTests(unittest.TestCase):
    def test_world_and_lift_are_separate_and_deduplicated(self):
        bsp = model_fixture()
        self.assertEqual(model_brushes(bsp), (0, 1))
        self.assertEqual(model_brushes(bsp, 1), (2,))
        self.assertEqual(model_brushes(bsp, 2), (2,))

    def test_malformed_tree_references_fail_closed(self):
        bsp = model_fixture()
        with self.assertRaises(GeometryError):
            model_brushes(bsp, 4)
        bsp.nodes[0] = (0, 0, -1)
        with self.assertRaisesRegex(GeometryError, "cyclic"):
            model_brushes(bsp)
        bsp = model_fixture()
        bsp.leafbrushes[2] = 8
        with self.assertRaisesRegex(GeometryError, "brush"):
            model_brushes(bsp)
        bsp = model_fixture()
        bsp.leaves[1] = (0,) * 11 + (3, 9)
        with self.assertRaisesRegex(GeometryError, "range"):
            model_brushes(bsp)


class PrismTests(unittest.TestCase):
    def test_chamfer_does_not_report_the_empty_corner_of_its_aabb(self):
        # Triangle x>=0, y>=0, x+y<=10, extruded from z=0 to 10.
        planes = [(-1, 0, 0, 0), (0, -1, 0, 0), (1, 1, 0, 10),
                  (0, 0, -1, 0), (0, 0, 1, 10)]
        prism = vertical_prism(planes)
        self.assertEqual(prism.bounds, ((0, 0, 0), (10, 10, 10)))
        self.assertFalse(intersects_hull(prism, (7, 7, 1), (9, 9, 9)))
        self.assertTrue(intersects_hull(prism, (4, 4, 1), (7, 7, 9)))
        self.assertFalse(intersects_hull(prism, (5, 5, 1), (7, 7, 9)))
        self.assertFalse(intersects_hull(prism, (4, 4, 10), (7, 7, 12)))

    def test_rotated_thin_prism_requires_polygon_edge_axes(self):
        # Diamond: |x|+|y|<=2. An overlapping AABB alone is insufficient.
        prism = vertical_prism([(1, 1, 0, 2), (-1, 1, 0, 2),
                                (1, -1, 0, 2), (-1, -1, 0, 2),
                                (0, 0, -1, 1), (0, 0, 1, 1)])
        self.assertFalse(intersects_hull(prism, (1.5, 1.5, -.5), (2.5, 2.5, .5)))
        self.assertTrue(intersects_hull(prism, (-.25, -.25, -.5), (.25, .25, .5)))

    def test_axial_containment_and_boundary_contact(self):
        prism = vertical_prism(box_planes())
        self.assertTrue(intersects_hull(prism, (1, 1, 1), (2, 2, 2)))
        self.assertTrue(intersects_hull(prism, (-1, -1, -1), (11, 11, 11)))
        self.assertFalse(intersects_hull(prism, (10, 1, 1), (11, 2, 2)))
        self.assertFalse(intersects_hull(prism, (10, 10, 10), (11, 11, 11)))
        self.assertFalse(intersects_hull(prism, (1, 1, -1), (2, 2, 0)))
        # No loose epsilon silently rounds a tiny real overlap to contact.
        near = math.nextafter(10.0, 0.0)
        self.assertTrue(intersects_hull(prism, (near, 1, 1), (11, 2, 2)))
        self.assertIsInstance(prism.polygon[0][0], Fraction)

    def test_reordering_redundant_and_scaled_planes_preserve_the_prism(self):
        planes = box_planes((-4, -3, -2), (8, 9, 10))
        expected = vertical_prism(planes)
        shuffled = list(reversed(planes)) + [(2, 0, 0, 16), (1, 0, 0, 100)]
        self.assertEqual(vertical_prism(shuffled), expected)
        self.assertEqual(vertical_prism([(*p, 3) for p in planes]), expected)

    def test_unsupported_and_empty_geometry_fail_closed(self):
        with self.assertRaisesRegex(GeometryError, "sloping"):
            vertical_prism(box_planes() + [(1, 0, .000001, 20)])
        with self.assertRaisesRegex(GeometryError, "unbounded"):
            vertical_prism([(-1, 0, 0, 0), (0, -1, 0, 0), (-1, -1, 0, -1),
                            (-2, -1, 0, -1.5), (0, 0, -1, 0), (0, 0, 1, 10)])
        with self.assertRaisesRegex(GeometryError, "empty|zero"):
            vertical_prism(box_planes() + [(1, 0, 0, -1)])
        with self.assertRaises(GeometryError):
            vertical_prism(box_planes()[:-1])
        with self.assertRaises(GeometryError):
            vertical_prism(box_planes() + [(1, 0, 0, float("nan"))])
        with self.assertRaises(GeometryError):
            intersects_hull(vertical_prism(box_planes()), (0, 0, 0), (0, 1, 1))
        with self.assertRaisesRegex(GeometryError, "cap"):
            vertical_prism(box_planes() * 22)


def ramp_planes():
    # RampSpec: run 480, width 160, lower thickness 8, rise 120; z=8+x/4.
    # This is one true six-plane wedge, not a stack of tread boxes.
    return [(1, 0, 0, 480), (-1, 0, 0, 0), (0, 1, 0, 160),
            (0, -1, 0, 0), (0, 0, -1, 0), (-1, 0, 4, 32)]


class ConvexTests(unittest.TestCase):
    def test_box_compatibility_and_exact_touch(self):
        solid = convex_polyhedron(box_planes())
        prism = vertical_prism(box_planes())
        for low, high in [((1, 1, 1), (2, 2, 2)), ((10, 0, 0), (12, 2, 2)),
                          ((10, 10, 10), (12, 12, 12)), ((-1, -1, -1), (11, 11, 11))]:
            self.assertEqual(intersects_aabb(solid, low, high), intersects_hull(prism, low, high))
            self.assertEqual(intersects_aabb(prism, low, high), intersects_hull(prism, low, high))
        self.assertTrue(intersects_aabb(solid, (math.nextafter(10.0, 0.0), 1, 1), (11, 2, 2)))
        self.assertTrue(all(isinstance(c, Fraction) for v in solid.vertices for c in v))
        self.assertEqual(convex_polyhedron(list(reversed(box_planes())) + [(2, 0, 0, 20)]), solid)
        self.assertEqual(convex_polyhedron([(*p, 0) for p in box_planes()]), solid)

    def test_sloped_tetrahedron_requires_edge_cross_axes(self):
        # Vertices (0,0,0), (8,0,4), (0,8,4), (1,1,8). The target box passes
        # EVERY face-normal and coordinate-axis interval test, yet an edge-cross
        # axis separates it. This guards the tempting incomplete plane-only SAT.
        solid = convex_polyhedron([(32, 32, -64, 0), (-4, -60, 8, 0),
                                  (-60, -4, 8, 0), (32, 32, 48, 448)])
        low, high = (0, 3, 7), (1, 4, 8)
        axes = [p[:3] for p in solid.planes] + [(1, 0, 0), (0, 1, 0), (0, 0, 1)]
        for axis in axes:
            box_low = sum(n * (low[i] if n >= 0 else high[i]) for i, n in enumerate(axis))
            box_high = sum(n * (high[i] if n >= 0 else low[i]) for i, n in enumerate(axis))
            projection = [sum(n * v[i] for i, n in enumerate(axis)) for v in solid.vertices]
            self.assertLess(box_low, max(projection))
            self.assertGreater(box_high, min(projection))
        self.assertFalse(intersects_aabb(solid, low, high))
        self.assertTrue(intersects_aabb(solid, (1, 1, 4), (2, 2, 5)))

    def test_rotated_sloped_box_and_translation(self):
        # Orthogonal XZ axes (3,0,4), (-4,0,3), each limited to +/-5.
        solid = convex_polyhedron([(3, 0, 4, 5), (-3, 0, -4, 5),
                                  (-4, 0, 3, 5), (4, 0, -3, 5),
                                  (0, 1, 0, 1), (0, -1, 0, 1)])
        self.assertFalse(intersects_aabb(solid, (1, -.5, 1), (2, .5, 2)))
        self.assertTrue(intersects_aabb(solid, (-.1, -.1, -.1), (.1, .1, .1)))
        moved = translated(solid, (32, -17, 128))
        self.assertEqual(moved.bounds, tuple(tuple(v + d for v, d in zip(b, (32, -17, 128)))
                                            for b in solid.bounds))
        self.assertTrue(intersects_aabb(moved, (31.9, -17.1, 127.9), (32.1, -16.9, 128.1)))

    def test_unbounded_flat_empty_and_budget_inputs_fail_closed(self):
        for planes in [box_planes()[:-1], [(1, 0, 0, 1), (-1, 0, 0, 1)]]:
            with self.assertRaisesRegex(GeometryError, "unbounded|degenerate"):
                convex_polyhedron(planes)
        # Four side planes make a full-rank but unbounded pointed cone.
        with self.assertRaisesRegex(GeometryError, "unbounded"):
            convex_polyhedron([(1, 0, -1, 0), (-1, 0, -1, 0),
                               (0, 1, -1, 0), (0, -1, -1, 0)])
        for planes in [box_planes(high=(10, 10, 0)), box_planes() + [(1, 0, 0, -1)]]:
            with self.assertRaisesRegex(GeometryError, "empty|zero"):
                convex_polyhedron(planes)
        for planes in [box_planes() + [(0, 0, 0, 1)], box_planes() + [(1, 0, 0, math.inf)]]:
            with self.assertRaises(GeometryError):
                convex_polyhedron(planes)
        with self.assertRaisesRegex(GeometryError, "cap"):
            convex_polyhedron(box_planes() * 6)
        with self.assertRaisesRegex(GeometryError, "cell budget"):
            convex_polyhedron(box_planes(), max_cells=19)
        with self.assertRaisesRegex(GeometryError, "cap"):
            convex_polyhedron(box_planes(), max_planes=5)
        for budget in [0, True, 8193, 1.5]:
            with self.assertRaises(GeometryError):
                convex_polyhedron(box_planes(), max_cells=budget)

    def test_exact_sweep_detects_interior_obstacle_without_enclosing_box_false_alarm(self):
        obstacle = convex_polyhedron(box_planes((4, 4, 4), (6, 6, 6)))
        start, end = ((0, 0, 0), (1, 1, 1)), ((9, 9, 9), (10, 10, 10))
        self.assertFalse(intersects_aabb(obstacle, *start))
        self.assertFalse(intersects_aabb(obstacle, *end))
        self.assertTrue(intersects_swept_aabb(obstacle, *start, (9, 9, 9)))
        # Its enclosing box covers this obstacle, but the actual diagonal does not.
        off_path = convex_polyhedron(box_planes((4, 0, 4), (6, 1, 6)))
        self.assertTrue(intersects_aabb(off_path, (0, 0, 0), (10, 10, 10)))
        self.assertFalse(intersects_swept_aabb(off_path, *start, (9, 9, 9)))
        # Tangency at the endpoint and along a face is allowed.
        self.assertFalse(intersects_swept_aabb(obstacle, (0, 4, 4), (1, 5, 5), (3, 0, 0)))
        self.assertFalse(intersects_swept_aabb(obstacle, (0, 3, 4), (1, 4, 5), (9, 0, 0)))
        self.assertTrue(intersects_swept_aabb(obstacle, (5, 5, 5), (7, 7, 7), (0, 0, 0)))
        self.assertTrue(intersects_swept_aabb(obstacle, *end, (-9, -9, -9)))


class RampSupportTests(unittest.TestCase):
    def setUp(self):
        self.ramp = convex_polyhedron(ramp_planes())

    def test_full_footprint_support_above_center_height(self):
        low, high = supported_hull(self.ramp, (32, 80))
        self.assertEqual((low, high), ((16, 64, 20), (48, 96, 76)))
        self.assertEqual(support_height(self.ramp, (16, 64), (48, 96)), 20)
        self.assertFalse(intersects_aabb(self.ramp, low, high))
        # Center-only z=16 places the leading part of the standing hull inside.
        self.assertTrue(intersects_aabb(self.ramp, (16, 64, 16), (48, 96, 72)))
        self.assertIsNone(support_height(self.ramp, (-32, 64), (0, 96)))
        with self.assertRaisesRegex(GeometryError, "support"):
            supported_hull(self.ramp, (-16, 80))

    def test_real_slope_ascent_and_top_support_break_need_two_segments(self):
        start = supported_hull(self.ramp, (32, 80))
        mid = supported_hull(self.ramp, (450, 80))
        lip = supported_hull(self.ramp, (464, 80))
        top = supported_hull(self.ramp, (480, 80))
        self.assertFalse(intersects_swept_aabb(self.ramp, *start, (418, 0, Fraction(209, 2))))
        self.assertEqual(mid[0][2], Fraction(249, 2))
        self.assertEqual(lip[0][2], 128)
        self.assertEqual(top[0][2], 128)
        # Clear endpoint boxes do not certify the connecting movement segment.
        self.assertTrue(intersects_swept_aabb(self.ramp, *mid, (30, 0, Fraction(7, 2))))
        self.assertFalse(intersects_swept_aabb(self.ramp, *mid, (14, 0, Fraction(7, 2))))
        self.assertFalse(intersects_swept_aabb(self.ramp, *lip, (16, 0, 0)))
        deck = convex_polyhedron(box_planes((480, 0, 104), (640, 160, 128)))
        self.assertFalse(intersects_swept_aabb(deck, *mid, (14, 0, Fraction(7, 2))))
        self.assertFalse(intersects_swept_aabb(deck, *lip, (32, 0, 0)))
        ceiling = convex_polyhedron(box_planes((200, 0, 100), (210, 160, 108)))
        self.assertTrue(intersects_swept_aabb(ceiling, *start, (418, 0, Fraction(209, 2))))

    def test_eight_unit_entry_is_a_separate_bounded_step_then_slope(self):
        # At x=-16 the player is still fully outside the wedge. Raise by 8.25
        # and move one unit: a geometric step-up contract, not a runtime pmove test.
        approach = ((-32, 64, 0), (0, 96, 56))
        self.assertFalse(intersects_swept_aabb(self.ramp, *approach, (0, 0, Fraction(33, 4))))
        raised = ((-32, 64, Fraction(33, 4)), (0, 96, Fraction(257, 4)))
        entry = supported_hull(self.ramp, (-15, 80))
        self.assertEqual(entry[0][2], Fraction(33, 4))
        self.assertFalse(intersects_swept_aabb(self.ramp, *raised, (1, 0, 0)))
        self.assertTrue(intersects_swept_aabb(self.ramp, *approach, (1, 0, 0)))

    def test_slopes_in_both_xy_axes_and_translated_height(self):
        # z <= 8+x/4+y/8: support is at the high/high footprint corner.
        solid = convex_polyhedron(box_planes((0, 0, 0), (480, 160, 500))[:-2]
                                  + [(0, 0, -1, 0), (-2, -1, 8, 64)])
        self.assertEqual(supported_hull(solid, (32, 80))[0][2], 32)
        upper = translated(solid, (0, 0, 128))
        self.assertEqual(supported_hull(upper, (32, 80))[0][2], 160)
        negative = convex_polyhedron([(1, 0, 0, 0), (-1, 0, 0, 480),
                                     (0, 1, 0, 160), (0, -1, 0, 0),
                                     (0, 0, -1, 0), (1, 0, 4, 32)])
        self.assertEqual(supported_hull(negative, (-32, 80))[0][2], 20)

    def test_support_budget_and_invalid_hulls(self):
        with self.assertRaisesRegex(GeometryError, "cell budget"):
            support_height(self.ramp, (16, 64), (48, 96), max_cells=20)
        with self.assertRaises(GeometryError):
            support_height(self.ramp, (1, 1), (1, 2))
        with self.assertRaises(GeometryError):
            supported_hull(self.ramp, (32, 80), half_width=0)
        with self.assertRaises(GeometryError):
            intersects_swept_aabb(self.ramp, (0, 0, 0), (1, 1, 1), (1, 2))
        # Legacy two-dimensional prism axes must never be mistaken for 3D SAT.
        with self.assertRaisesRegex(GeometryError, "ConvexPolyhedron"):
            intersects_swept_aabb(vertical_prism(box_planes()), (0, 0, 11), (1, 1, 12), (0, 0, -4))


def ramp_audit_fixture(extra_brushes=(), *, ramp=None, landing=True):
    """Minimal model-zero BSP tables with independent synthetic convex brushes."""
    entries = [(box_planes((-128, -64, -16), (640, 224, 0)), 1),
               (ramp if ramp is not None else ramp_planes(), 1)]
    if landing:
        entries.append((box_planes((480, -64, 104), (640, 224, 128)), 1))
    entries.extend(extra_brushes)
    planes, brushsides, brushes = [], [], []
    for rows, contents in entries:
        first = len(brushsides)
        for row in rows:
            brushsides.append((len(planes), 0))
            planes.append(tuple(row))
        brushes.append((first, len(rows), contents))
    bsp = SimpleNamespace(models=[(0,) * 9 + (-1, 0, 0)], nodes=[],
                          leaves=[(0,) * 11 + (0, len(brushes))],
                          leafbrushes=list(range(len(brushes))), brushes=brushes,
                          planes=planes, brushsides=brushsides)
    layout = {'ramps': [{'id': 1, 'solid_id': 2, 'bounds': [[0, 0, 0], [480, 160, 128]],
                         'surface': [[0, 0, 8], [0, 160, 8], [480, 160, 128], [480, 0, 128]],
                         'from': [0, 80, 8], 'to': [480, 80, 128], 'width': 160,
                         'run': 480, 'rise': 120, 'base_floor': 0}]}
    return bsp, layout


class RampAuditTests(unittest.TestCase):
    def test_three_complete_lanes_are_swept_both_ways_and_json_serializable(self):
        import json
        result = audit_ramps(*ramp_audit_fixture())
        json.dumps(result)
        self.assertTrue(result['ok'])
        self.assertEqual(result['player_collision_mask'], MASK_PLAYERSOLID)
        self.assertEqual(result['ramps_checked'], 1)
        self.assertFalse(result['runtime_verified'])
        self.assertEqual(result['ramps'][0]['source_floor_band'], 128)
        self.assertTrue(result['supporting_ramp_included_in_sweeps'])
        self.assertGreater(result['exact_swept_checks'], 0)
        self.assertEqual(len(result['ramps'][0]['landing_turns']), 2)
        self.assertTrue(all(t['both_directions_clear'] for t in result['ramps'][0]['landing_turns']))
        for lane in result['ramps'][0]['lanes']:
            self.assertEqual(lane['entry_step_height'], 8.25)
            self.assertEqual(lane['ascent_segments'], lane['descent_segments'])
            self.assertGreaterEqual(lane['ascent_segments'], 6)
            self.assertEqual(lane['poses'][0]['standing_origin'][0], -48)
            self.assertEqual(lane['poses'][-1]['standing_origin'][0], 528)
            self.assertEqual(lane['poses'][-1]['foot_z'], 128)

    def test_all_horizontal_directions_at_an_upper_base_floor(self):
        from itertools import product
        for axis, sign in ((0, 1), (0, -1), (1, 1), (1, -1)):
            with self.subTest(axis=axis, sign=sign):
                bsp, layout = ramp_audit_fixture()
                def point(p):
                    return [sign * p[0], p[1], p[2] + 128] if axis == 0 else [p[1], sign * p[0], p[2] + 128]
                bsp.planes = [(sign * a, b, c, d + c * 128) if axis == 0 else (b, sign * a, c, d + c * 128)
                              for a, b, c, d in bsp.planes]
                r = layout['ramps'][0]
                corners = [point(v) for v in product(*zip(*r['bounds']))]
                r['bounds'] = [[min(v[i] for v in corners) for i in range(3)],
                               [max(v[i] for v in corners) for i in range(3)]]
                r['surface'] = [point(v) for v in r['surface']]
                r['from'], r['to'], r['base_floor'] = point(r['from']), point(r['to']), 128
                result = audit_ramps(bsp, layout)
                self.assertEqual(result['ramps'][0]['lanes'][1]['poses'][-1]['foot_z'], 256)

    def test_mid_incline_obstruction_cannot_be_hidden_by_clear_endpoints(self):
        # Every chosen incline endpoint is clear. The complete segment must fail.
        blocker = box_planes((220, -32, 72), (224, 192, 80))
        with self.assertRaisesRegex(GeometryError, "continuous hull sweep"):
            audit_ramps(*ramp_audit_fixture([(blocker, 1)]))

    def test_landing_turn_between_clear_individual_lanes_is_also_swept(self):
        # The three route lanes occupy y=8..40, 64..96 and 120..152. A small
        # obstacle in the gap misses every lane but blocks their lateral turn.
        blocker = box_planes((-50, 48, 16), (-46, 56, 24))
        with self.assertRaisesRegex(GeometryError, "lower landing lateral turn.*continuous hull sweep"):
            audit_ramps(*ramp_audit_fixture([(blocker, 1)]))

    def test_window_playerclip_and_monster_contents_also_block(self):
        blocker = box_planes((220, -32, 72), (224, 192, 80))
        for contents in (2, 1 << 16, 1 << 25):
            with self.subTest(contents=contents), self.assertRaisesRegex(GeometryError, "player-blocking"):
                audit_ramps(*ramp_audit_fixture([(blocker, contents)]))
        # A water-only brush does not block the player collision mask.
        result = audit_ramps(*ramp_audit_fixture([(blocker, 32)]))
        self.assertEqual(result['world_player_blocking_brushes'], 3)

    def test_requires_actual_slope_metadata_and_full_landing_support(self):
        with self.assertRaisesRegex(GeometryError, "matching compiled sloped ramp"):
            audit_ramps(*ramp_audit_fixture(ramp=box_planes((0, 0, 0), (480, 160, 128))))
        with self.assertRaisesRegex(GeometryError, "upper landing.*support"):
            audit_ramps(*ramp_audit_fixture(landing=False))
        bsp, layout = ramp_audit_fixture()
        layout['ramps'][0]['surface'][0][2] = 9
        with self.assertRaisesRegex(GeometryError, "vertices disagree"):
            audit_ramps(bsp, layout)
        bsp, layout = ramp_audit_fixture()
        layout['ramps'].append(dict(layout['ramps'][0]))
        with self.assertRaisesRegex(GeometryError, "same compiled brush"):
            audit_ramps(bsp, layout)

    def test_float32_matching_preserves_exact_compiled_support(self):
        import struct
        def f32(value):
            return struct.unpack('<f', struct.pack('<f', value))[0]
        planes = ramp_planes()
        norm = math.sqrt(17)
        planes[-1] = (f32(-1 / norm), 0, f32(4 / norm), f32(32 / norm))
        # One stored normal ULP produces a real support/landing height mismatch.
        bits = struct.unpack('<I', struct.pack('<f', planes[-1][0]))[0]
        shifted = struct.unpack('<f', struct.pack('<I', bits + 1))[0]
        planes[-1] = (shifted, *planes[-1][1:])
        result = audit_ramps(*ramp_audit_fixture(ramp=planes))
        ramp = result['ramps'][0]
        self.assertLessEqual(ramp['metadata_surface_max_residual'], ramp['metadata_surface_error_bound'])
        exact = supported_hull(convex_polyhedron(planes), (-15, 80))[0][2]
        self.assertNotEqual(exact, Fraction(33, 4))
        self.assertEqual(ramp['lanes'][1]['entry_step_height_exact'], str(exact))
        self.assertNotEqual(ramp['lanes'][1]['deck_rounding_transition_exact'], '0')
        self.assertEqual(result['collision_epsilon'], 0)
        capped = audit_ramps(*ramp_audit_fixture(ramp=planes + [(0, 0, 1, 128)]))
        knots = capped['ramps'][0]['lanes'][1]['incline_support_knots_exact']
        self.assertEqual(len(knots), 2)
        self.assertLess(Fraction(knots[0]), 464)
        self.assertEqual(Fraction(knots[1]), 464)
        self.assertEqual(capped['ramps'][0]['lanes'][1]['deck_rounding_transition_exact'], '0')
        # The opposite rounding direction requires a crest step instead of a
        # trailing-edge settle; both are explicit exact-clearance segments.
        shifted = struct.unpack('<f', struct.pack('<I', bits - 1))[0]
        planes[-1] = (shifted, *planes[-1][1:])
        opposite = audit_ramps(*ramp_audit_fixture(ramp=planes))
        self.assertGreater(Fraction(opposite['ramps'][0]['lanes'][1]['deck_rounding_transition_exact']), 0)
        # A real source mismatch cannot be hidden by the coefficient match bound.
        planes[-1] = (*planes[-1][:3], f32(33 / norm))
        with self.assertRaisesRegex(GeometryError, "matching compiled sloped ramp"):
            audit_ramps(*ramp_audit_fixture(ramp=planes))


if __name__ == "__main__":
    unittest.main()
