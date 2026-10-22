"""Upper-link support, ordered-route and recovery interpretation controls."""
from copy import deepcopy
import unittest

from tools.map_recipes.geometry import convex_polyhedron
from tools.native_movement.upper_link import World, analyze, audit_stair, audit_transfer, support_union, validate_factorial


def state(feet, index):
    return {'frame': index, 'time_ms': index * 16, 'msec': 16,
            'origin': [*feet[:2], feet[2] + 24.125], 'feet_z': feet[2] + .125,
            'velocity': [0, 0, 0], 'grounded': True, 'waterlevel': 0,
            'hull_mins': [-16, -16, -24], 'hull_maxs': [16, 16, 32],
            'post_startsolid': False, 'post_allsolid': False}


class ClearWorld:
    def flat_chord(self, a, b, floor):
        return {'fully_supported': True, 'clear': True}

    def wall_distance(self, frame):
        return {'distance_xy': 24, 'brush': 1}


def fixture():
    def gate(x, feet):
        return {'center_xy_bounds': [[x - 4, -16], [x + 4, 16]], 'feet_z': feet,
                'feet_tolerance': 1, 'grounded': True}
    spec = {'timestep_ms': 16, 'settle_frames': 8, 'start': {'feet': [0, 0, 0]},
            'commands': [{'frames': 4, 'forward': 200}, {'frames': 128, 'forward': 0}],
            'segments': [
                {'label': 'ascent', 'kind': 'stair', 'center_xy_bounds': [[-16, -48], [68, 48]],
                 'feet_range': [-1, 129], 'gate': gate(64, 128)},
                {'label': 'upper_link', 'kind': 'upper', 'center_xy_bounds': [[60, -48], [132, 48]],
                 'feet_z': 128, 'gate': gate(128, 128)},
                {'label': 'descent', 'kind': 'stair', 'center_xy_bounds': [[124, -48], [196, 48]],
                 'feet_range': [-1, 129], 'gate': gate(192, 0)},
                {'label': 'ground_finish', 'kind': 'ground', 'center_xy_bounds': [[188, -48], [240, 48]],
                 'feet_z': 0, 'gate': gate(224, 0)}],
            'physical_goal': {**gate(224, 0), 'consecutive_samples': 9, 'first_to_last_dwell_ms': 128,
                              'maximum_horizontal_speed': 10, 'maximum_absolute_vertical_speed': 10}}
    points = [[0, 0, 0]] * 8 + [[64, 0, 128], [128, 0, 128], [192, 0, 0], [224, 0, 0]] + [[224, 0, 0]] * 128
    return spec, [state(p, i) for i, p in enumerate(points, 1)]


def top(ident, low, high, floor=128):
    return {'brush': ident, 'low': low, 'high': high, 'floor': floor}


def stair_fixture(extra=()):
    stair = {'label': 'synthetic-stair', 'axis': 0, 'ascending_direction': 1, 'ground_z': 0,
             'treads': [{'id': i+1, 'bounds': [[i*36, -64, 0], [(i+1)*36, 64, (i+1)*16]]}
                        for i in range(8)]}
    boxes = [[[-64, -128, -24], [400, 128, 0]]] + [t['bounds'] for t in stair['treads']] + list(extra)
    world = World.__new__(World)
    world.world = []
    world.tops = []
    for ident, (low, high) in enumerate(boxes):
        planes = [(1, 0, 0, high[0]), (-1, 0, 0, -low[0]), (0, 1, 0, high[1]),
                  (0, -1, 0, -low[1]), (0, 0, 1, high[2]), (0, 0, -1, -low[2])]
        world.world.append((ident, 1, convex_polyhedron(planes)))
        world.tops.append(top(ident, low[:2], high[:2], high[2]))
    return stair, world


def factorial_fixture():
    trials = []
    for group in ('rotation', 'transfer'):
        for direction in ('forward', 'reverse'):
            for speed in (200, 400):
                for pause in ((0, 24) if group == 'rotation' else (0,)):
                    spec, _ = fixture()
                    points = ([[0, 0, 0], [64, 0, 128], [64, 64, 128], [128, 64, 128], [128, 128, 0]]
                              if group == 'rotation' else [[0, 0, 128], [208, 0, 128]])
                    if direction == 'reverse':
                        points.reverse()
                    commands = []
                    for a, b in zip(points, points[1:]):
                        yaw = (0 if b[0] > a[0] else 180) if a[0] != b[0] else (90 if b[1] > a[1] else -90)
                        if commands and pause:
                            commands.append({**commands[-1], 'forward': 0, 'frames': pause})
                        commands.append({'frames': 40, 'forward': speed, 'yaw': yaw})
                    commands.append({**commands[-1], 'forward': 0, 'frames': 128})
                    spec.update({'schema_version': 1, 'id': f'{group}-{direction}-{speed}-{pause}',
                                 'group': group, 'direction': direction, 'literal_forwardmove': speed,
                                 'corner_pause_frames': pause, 'route_waypoints': points,
                                 'start': {'feet': points[0]}, 'commands': commands,
                                 'goal': {'axis': 'x', 'direction': 1 if direction == 'forward' else -1,
                                          'coordinate': points[-1][0], 'feet_z': points[-1][2],
                                          'cross_axis_range': [-1000, 1000]}})
                    x, y, z = points[-1]
                    spec['physical_goal'].update({'center_xy_bounds': [[x-8, y-8], [x+8, y+8]], 'feet_z': z})
                    if group == 'transfer':
                        spec['segments'] = [spec['segments'][1]]
                    trials.append(spec)
    return trials


class UpperLinkControls(unittest.TestCase):
    def test_full_width_stair_caps_and_all_fixed_step_translations(self):
        stair, world = stair_fixture()
        proof = audit_stair(world, stair)
        self.assertTrue(proof['success'])
        self.assertEqual(len(proof['full_tread_caps']), 8)
        self.assertEqual(len(proof['ascending_step_constructions']), 8)
        self.assertTrue(all(len(x['legs']) == 3 for x in proof['ascending_step_constructions']))

    def test_standing_clearance_does_not_imply_raise_before_advance_clearance(self):
        stair, world = stair_fixture([[[72, -64, 104], [108, 64, 128]]])
        proof = audit_stair(world, stair)
        self.assertTrue(proof['full_tread_caps'][2]['clear'])
        self.assertFalse(proof['ascending_step_constructions'][3]['legs'][0]['clear'])
        self.assertFalse(proof['success'])

    def test_stair_raised_edge_obstacle_and_false_source_dimensions_are_rejected(self):
        stair, world = stair_fixture([[[72, 48, 104], [108, 64, 128]]])
        self.assertFalse(audit_stair(world, stair)['success'])
        stair['treads'][3]['bounds'][0][0] += 1
        with self.assertRaises(ValueError):
            audit_stair(world, stair)

    def test_fixed_factorial_cannot_change_active_timing_or_reverse_geometry(self):
        trials = factorial_fixture()
        validate_factorial(trials)
        trials[1]['commands'][0]['frames'] += 1
        with self.assertRaises(ValueError):
            validate_factorial(trials)
        trials = factorial_fixture()
        trials[4]['route_waypoints'][1][0] += 1
        with self.assertRaises(ValueError):
            validate_factorial(trials)

    def test_factorial_rejects_missing_profile_and_wrong_yaw(self):
        trials = factorial_fixture()
        trials[-1] = deepcopy(trials[-2])
        with self.assertRaises(ValueError):
            validate_factorial(trials)
        trials = factorial_fixture()
        trials[-1]['commands'][0]['yaw'] = 90
        with self.assertRaises(ValueError):
            validate_factorial(trials)

    def test_exact_top_union_covers_seam_without_one_brush_covering_it(self):
        result = support_union([top(1, [0, 0], [16, 32]), top(2, [16, 0], [32, 32])], [0, 0], [32, 32], 128)
        self.assertTrue(result['fully_supported'])
        self.assertEqual(result['supporting_brushes'], [1, 2])

    def test_corner_support_and_lower_floor_do_not_cover_missing_upper_slab(self):
        tops = [top(1, [0, 0], [8, 32]), top(2, [24, 0], [32, 32]),
                top(3, [0, 0], [32, 32], 0)]
        result = support_union(tops, [0, 0], [32, 32], 128)
        self.assertFalse(result['fully_supported'])
        self.assertEqual(result['covered_area'], 512)
        self.assertEqual(result['uncovered_rectangles'], [[[8.0, 0.0], [24.0, 32.0]]])

    def test_overlap_is_not_double_counted_and_eighth_unit_hole_is_retained(self):
        tops = [top(1, [0, 0], [16, 32]), top(2, [8, 0], [31.875, 32])]
        result = support_union(tops, [0, 0], [32, 32], 128)
        self.assertFalse(result['fully_supported'])
        self.assertEqual(result['covered_area'], 1020)

    def test_missing_floor_at_portal_side_cannot_hide_behind_clear_center(self):
        class PartialPortal:
            tops = [top(1, [-16, -80], [64, 80]), top(2, [64, -80], [112, 40]),
                    top(3, [112, -80], [224, 80]), top(4, [64, 40], [112, 80], 0)]

            def flat_region(self, bounds, floor):
                low = [x - 16 for x in bounds[0]]
                high = [x + 16 for x in bounds[1]]
                return {**support_union(self.tops, low, high, floor), 'clear': True}

            def flat_chord(self, a, b, floor):
                low = [min(a[i], b[i]) - 16 for i in range(2)]
                high = [max(a[i], b[i]) + 16 for i in range(2)]
                return {**support_union(self.tops, low, high, floor), 'clear': True}

        result = audit_transfer(PartialPortal(), [0, 0, 128], [208, 0, 128])
        self.assertFalse(result['success'])
        self.assertTrue(result['lanes'][1]['fully_supported'])
        self.assertFalse(result['lanes'][2]['fully_supported'])
        self.assertEqual(result['lanes'][2]['cross_offset'], 64)
        self.assertEqual(result['full_strip']['required_area'] - result['full_strip']['covered_area'], 1920)

    def test_transfer_rejects_diagonal_or_partial_width_claim(self):
        with self.assertRaises(ValueError):
            audit_transfer(ClearWorld(), [0, 0, 128], [208, 16, 128])
        with self.assertRaises(ValueError):
            audit_transfer(ClearWorld(), [0, 0, 128], [208, 0, 128], 32)

    def test_complete_ordered_route_and_nine_state_finish(self):
        spec, frames = fixture()
        result = analyze(spec, frames, ClearWorld())
        self.assertTrue(result['success'])
        self.assertEqual([x['label'] for x in result['gate_events']],
                         ['ascent', 'upper_link', 'descent', 'ground_finish'])
        self.assertEqual(result['first_route_dwell']['end']['time_ms'] -
                         result['first_route_dwell']['start']['time_ms'], 128)

    def test_missing_gate_stays_failed_despite_later_ground_dwell(self):
        spec, frames = fixture()
        frames[9] = state([112, 0, 128], 10)
        result = analyze(spec, frames, ClearWorld())
        self.assertFalse(result['success'])
        self.assertIsNotNone(result['late_pocket_dwell_ignoring_route_failure'])
        self.assertEqual(result['next_missing_gate'], 'upper_link')

    def test_one_state_cannot_satisfy_two_ordered_gates(self):
        spec, frames = fixture()
        spec['segments'][1]['gate'] = deepcopy(spec['segments'][0]['gate'])
        result = analyze(spec, frames, ClearWorld())
        self.assertFalse(result['success'])
        self.assertEqual(result['next_missing_gate'], 'upper_link')

    def test_airborne_upper_frame_is_irreversible_failure(self):
        spec, frames = fixture()
        frames[9]['grounded'] = False
        result = analyze(spec, frames, ClearWorld())
        self.assertFalse(result['success'])
        self.assertIn('flat_floor_contact_lost', result['first_violation']['reasons'])

    def test_upper_exit_chord_must_have_full_support(self):
        class MissingSlab(ClearWorld):
            def flat_chord(self, a, b, floor):
                return {'fully_supported': not (a[0] == 64 and b[0] == 128), 'clear': True}
        spec, frames = fixture()
        result = analyze(spec, frames, MissingSlab())
        self.assertFalse(result['success'])
        self.assertIn('full_footprint_support_lost', result['first_violation']['reasons'])

    def test_stair_band_and_final_ground_clearance_remain_required(self):
        spec, frames = fixture()
        frames[8]['origin'][1] = 49
        result = analyze(spec, frames, ClearWorld())
        self.assertFalse(result['success'])
        self.assertIn('declared_physical_phase_corridor_lost', result['first_violation']['reasons'])
        spec, frames = fixture()
        frames[12]['post_startsolid'] = True
        self.assertFalse(analyze(spec, frames, ClearWorld())['success'])

    def test_eight_qualifying_states_are_insufficient(self):
        spec, frames = fixture()
        for frame in frames[12:-8]:
            frame['velocity'] = [200, 0, 0]
        self.assertFalse(analyze(spec, frames, ClearWorld())['success'])
        frames[-9]['velocity'] = [0, 0, 0]
        self.assertTrue(analyze(spec, frames, ClearWorld())['success'])

    def test_speed_norm_spike_resets_dwell(self):
        spec, frames = fixture()
        frames[16]['velocity'] = [8, 8, 0]
        result = analyze(spec, frames, ClearWorld())
        self.assertEqual(result['first_route_dwell']['start']['frame'], 18)
        self.assertEqual(result['first_route_dwell']['end']['frame'], 26)

    def test_supported_final_precision_miss_is_not_hidden(self):
        spec, frames = fixture()
        for frame in frames[12:]:
            frame['origin'][0] = 229
        result = analyze(spec, frames, ClearWorld())
        self.assertFalse(result['success'])
        self.assertIsNone(result['first_violation'])
        self.assertEqual(result['failure_reason'], 'no_bounded_low_speed_dwell')

    def test_frame_gaps_feet_mismatch_and_weakened_contract_are_rejected(self):
        spec, frames = fixture()
        frames[9]['time_ms'] += 1
        with self.assertRaises(ValueError):
            analyze(spec, frames, ClearWorld())
        spec, frames = fixture()
        frames[9]['feet_z'] = 0
        with self.assertRaises(ValueError):
            analyze(spec, frames, ClearWorld())
        spec, frames = fixture()
        spec['physical_goal']['consecutive_samples'] = 8
        with self.assertRaises(ValueError):
            analyze(spec, frames, ClearWorld())


if __name__ == '__main__':
    unittest.main()
