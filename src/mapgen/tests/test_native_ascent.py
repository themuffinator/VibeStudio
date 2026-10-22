"""Controls for the new frame-end analysis and isolated evidence root.

The 32 retained native journeys are the integration experiment. These tests guard
interpretation errors rather than replaying the same implementation as fixtures.
"""
from copy import deepcopy
import unittest

from tools.native_movement import run
from tools.native_movement.ascent import OUTPUT, profile, runner, trial


def frame(index, y, feet=0.125, grounded=True, velocity_z=0):
    return {'frame': index, 'time_ms': index*16, 'msec': 16,
            'origin': [-656, y, feet+24], 'feet_z': feet,
            'velocity': [0, -300, velocity_z], 'waterlevel': 0, 'grounded': grounded,
            'hull_mins': [-16, -16, -24], 'hull_maxs': [16, 16, 32]}


class NativeAscentAnalysisTests(unittest.TestCase):
    def spec(self):
        spec = trial('stairs', 400, 'down', -656)
        spec['settle_frames'] = 2
        return spec

    def test_airborne_planar_crossing_is_separate_from_grounded_completion(self):
        frames = [frame(1, -200, 16.125), frame(2, -200, 16.125),
                  frame(3, -209, 11.5, False, -76.5), frame(4, -214, 0.125),
                  frame(5, -220, 0.125)]
        result = profile(self.spec(), {'success': True, 'goal_frame': 4, 'movement_time_ms': 32}, frames)
        self.assertEqual(result['first_planar_station_crossing']['movement_time_ms'], 16)
        self.assertFalse(result['first_planar_station_crossing']['grounded'])
        self.assertEqual(result['planar_crossing_to_accepted_goal_ms'], 16)
        self.assertEqual(result['airborne_frame_end_duration_ms'], 16)
        self.assertEqual(len(result['vertical_response']), 2)

    def test_post_goal_fall_cannot_contaminate_response(self):
        frames = [frame(1, -200), frame(2, -200), frame(3, -209),
                  frame(4, -250, -300, False, -700)]
        result = profile(self.spec(), {'success': True, 'goal_frame': 3, 'movement_time_ms': 16}, frames)
        self.assertEqual(result['airborne_frame_end_count'], 0)
        self.assertEqual(result['largest_downward_feet_delta'], 0)

    def test_native_step_displacement_is_not_inferred_from_velocity(self):
        spec = trial('stairs', 400, 'up', -656); spec['settle_frames'] = 2
        frames = [frame(1, 355, 112.125), frame(2, 355, 112.125), frame(3, 369, 128.125)]
        result = profile(spec, {'success': True, 'goal_frame': 3, 'movement_time_ms': 16}, frames)
        self.assertEqual(result['largest_upward_feet_delta'], 16)
        self.assertEqual(result['native_velocity_z_range'], [0, 0])

    def test_far_later_landing_and_missing_frame_are_rejected(self):
        frames = [frame(1, -200), frame(2, -200), frame(3, -241)]
        with self.assertRaisesRegex(ValueError, '32-unit landing gate'):
            profile(self.spec(), {'success': True, 'goal_frame': 3, 'movement_time_ms': 16}, frames)
        missing = deepcopy(frames); missing[2]['frame'] = 4
        with self.assertRaisesRegex(ValueError, 'complete ordered'):
            profile(self.spec(), {'success': True, 'goal_frame': 3, 'movement_time_ms': 16}, missing)

    def test_separate_output_namespace_does_not_mutate_original_runner(self):
        old_root = run.EVIDENCE
        isolated = runner()
        self.assertEqual(isolated.EVIDENCE, OUTPUT)
        self.assertEqual(run.EVIDENCE, old_root)
        self.assertIsNot(isolated.run_trial, run.run_trial)
        with self.assertRaisesRegex(ValueError, 'beneath'):
            isolated.run_trial(trial('ramp', 400, 'up', -656), OUTPUT.parent)


if __name__ == '__main__':
    unittest.main()
