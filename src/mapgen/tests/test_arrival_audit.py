"""Negative geometric controls for arrival discs and complete departure paths."""
import copy
from types import SimpleNamespace
import unittest

from tools.map_recipes.arrival_audit import audit_paths, box, pads_and_arrival, point, passage_stations


def fixture():
    world = {0: box((-512, -256, -24), (512, 256, 0))}
    entities = [{'classname': 'worldspawn'},
                {'classname': 'misc_teleporter', 'origin': '-256 0 24', 'target': 'exit'},
                {'classname': 'misc_teleporter_dest', 'origin': '256 0 24', 'targetname': 'exit', 'angle': '0'}]
    spec = {'source_origin': [-256, 0, 24], 'destination_origin': [256, 0, 24], 'arrival_yaw': 0}
    return SimpleNamespace(entities=entities), world, spec


def paths():
    return [{'id': str(i), 'kind': kind, 'points': [[0, -64, 0], [0, 64, 0]]}
            for i, kind in enumerate(('departure', 'departure', 'ordinary_bypass', 'resource_approach'))]


class ArrivalGeometryTests(unittest.TestCase):
    def test_crossed_passage_need_not_be_an_authored_waypoint(self):
        volumes = [[[-80, -24, 0], [80, 24, 192]]]
        self.assertEqual(passage_stations([[0, -64, 0], [0, 64, 0]], volumes), [[0, 0, 0]])
        self.assertEqual(passage_stations([[72, -64, 0], [72, 64, 0]], volumes), [])
        self.assertEqual(passage_stations([[0, -64, 128], [0, 64, 128]], volumes), [])

    def test_raised_native_pose_and_disc_are_distinct_from_floor(self):
        pads, discs, arrival = pads_and_arrival(*fixture())
        self.assertEqual(pads[0]['standing_origin'], [-256, 0, 32])
        self.assertEqual(arrival['arrival_feet'], [256, 0, 10])
        self.assertEqual(arrival['disc_settled_feet'], [256, 0, 8])
        self.assertEqual(len(discs), 2)
        self.assertFalse(arrival['native_activation_tested_by_this_audit'])

    def test_matching_destination_class_does_not_hide_duplicate_target(self):
        bsp, world, spec = fixture()
        bsp.entities.append({'classname': 'info_null', 'targetname': 'exit'})
        with self.assertRaisesRegex(ValueError, 'unique'):
            pads_and_arrival(bsp, world, spec)

    def test_yaw_and_origin_mismatch_reject(self):
        for field, value in [('angle', '90'), ('angle', 'nan'), ('origin', '240 0 24')]:
            bsp, world, spec = fixture()
            bsp.entities[2][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                pads_and_arrival(bsp, world, spec)

    def test_floor_standing_clearance_cannot_hide_actual_arrival_ceiling(self):
        bsp, world, spec = fixture()
        world[1] = box((224, -32, 64), (288, 32, 72))
        with self.assertRaisesRegex(ValueError, 'Raised native'):
            pads_and_arrival(bsp, world, spec)

    def test_player_sized_support_is_insufficient_for_disc(self):
        bsp, world, spec = fixture()
        world = {0: box((-512, -256, -24), (0, 256, 0)),
                 1: box((240, -16, -24), (272, 16, 0))}
        with self.assertRaisesRegex(ValueError, '64x64'):
            pads_and_arrival(bsp, world, spec)

    def test_complete_union_support_can_cross_a_brush_seam(self):
        world = {0: box((-64, -128, -24), (64, 0, 0)),
                 1: box((-64, 0, -24), (64, 128, 0))}
        result = audit_paths(world, paths())
        self.assertTrue(all(p['pass'] for p in result))
        self.assertEqual(result[0]['legs'][0]['support']['support_brushes'], [0, 1])

    def test_clear_endpoints_do_not_hide_a_one_unit_floor_hole(self):
        world = {0: box((-64, -128, -24), (64, 0, 0)),
                 1: box((-64, 1, -24), (64, 128, 0))}
        result = audit_paths(world, paths())
        self.assertFalse(result[0]['pass'])
        self.assertFalse(result[0]['legs'][0]['support']['pass'])
        self.assertEqual(result[0]['legs'][0]['world_blockers'], [])

    def test_clear_endpoints_do_not_hide_an_intermediate_barrier(self):
        world = {0: box((-64, -128, -24), (64, 128, 0)),
                 1: box((-8, -1, 16), (8, 1, 32))}
        result = audit_paths(world, paths())
        self.assertFalse(result[0]['pass'])
        self.assertEqual(result[0]['legs'][0]['world_blockers'], [1])
        self.assertTrue(result[0]['legs'][0]['support']['pass'])

    def test_native_disc_is_a_real_obstacle_for_floor_zero_sweep(self):
        bsp, world, spec = fixture()
        _, discs, _ = pads_and_arrival(bsp, world, spec)
        routes = paths()
        for p in routes:
            p['points'] = [[256, -64, 0], [256, 64, 0]]
        self.assertTrue(audit_paths(world, routes)[0]['pass'])
        self.assertFalse(audit_paths({**world, **discs}, routes)[0]['pass'])

    def test_non_axial_or_repeated_route_is_rejected(self):
        _, world, _ = fixture()
        for change in ('diagonal', 'height', 'zero', 'duplicate'):
            routes = copy.deepcopy(paths())
            if change == 'diagonal': routes[0]['points'][1][0] = 1
            if change == 'height': routes[0]['points'][1][2] = 1
            if change == 'zero': routes[0]['points'][1] = routes[0]['points'][0]
            if change == 'duplicate': routes[0]['id'] = routes[1]['id']
            with self.subTest(change=change), self.assertRaises(ValueError):
                audit_paths(world, routes)

    def test_nonfinite_or_boolean_coordinate_is_rejected(self):
        for value in (True, float('nan'), float('inf'), 65537):
            with self.subTest(value=value), self.assertRaises(ValueError):
                point([value, 0, 0])


if __name__ == '__main__':
    unittest.main()
