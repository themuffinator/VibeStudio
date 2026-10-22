"""Retained-frame controls for bounded drop completion and landing semantics."""
from copy import deepcopy
import json
import unittest

from tools.native_movement.ravel import OUTPUT, audit


@unittest.skipUnless((OUTPUT/'ravel-drop-forward400/frames.ndjson').exists() and
                     (OUTPUT/'ravel-drop-forward200/frames.ndjson').exists(),
                     'Optional retained Ravel native trajectory fixtures are unavailable')
class RavelNativeRouteTests(unittest.TestCase):
    def evidence(self, speed):
        folder = OUTPUT/f'ravel-drop-forward{speed}'
        spec, result = (json.loads((folder/name).read_text()) for name in ('trial.json', 'result.json'))
        frames = [json.loads(s) for s in (folder/'frames.ndjson').read_text().splitlines()][1:]
        return spec, result, frames

    def test_unbounded_grounded_goal_does_not_override_declared_window(self):
        spec, result, frames = self.evidence(400)
        # This plane still crosses before the same landing, but that landing is
        # now33.375 beyond it. The runner's broad station goal is insufficient.
        spec['goal']['coordinate'] = 3
        checked = audit(spec, result, frames)
        self.assertTrue(result['success'])
        self.assertFalse(checked['success'])
        self.assertEqual(checked['failure_reason'], 'grounded_goal_beyond_32_unit_gate')

    def test_first_landing_can_precede_station_and_differs_from_static_candidate(self):
        spec, result, frames = self.evidence(200)
        checked = audit(spec, result, frames)
        landing = checked['first_target_floor_landing_after_airborne']
        goal = checked['first_grounded_station_goal']
        self.assertLess(landing['frame'], goal['frame'])
        self.assertGreater(landing['origin'][0], 0)
        self.assertNotEqual(landing['origin'][0], checked['static_drop_candidate']['landing_feet'][0])

    def test_forged_earlier_grounded_goal_is_rejected(self):
        spec, result, frames = self.evidence(400)
        result = deepcopy(result)
        result['goal_frame'] -= 1
        result['movement_time_ms'] -= 16
        with self.assertRaisesRegex(ValueError, 'Independent first grounded goal disagrees'):
            audit(spec, result, frames)


if __name__ == '__main__':
    unittest.main()
