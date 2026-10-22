"""Pure contract controls plus optional real CM/Pmove integration against Fold.

Native tests explicitly skip when the private external-reference build is absent.
Their retained outputs belong to out/native-movement; no game is launched.
"""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

from tools.native_movement.experiment import trial
from tools.native_movement.run import (BUILD_RECORD, EVIDENCE, NATIVE, PROJECT, goal_frame, prepare,
                                       require, run_trial, save, sha, verify_build)


def fold_spec():
    return trial('fold', 400, PROJECT/'out/fold/qmg_fold.bsp')


class MovementContractTests(unittest.TestCase):
    def test_goal_rejects_wrong_height_lane_water_and_air(self):
        goal = fold_spec()['goal']
        frame = {'origin': [632, 241, 24.125], 'feet_z': 0.125, 'waterlevel': 0, 'grounded': True,
                 'hull_mins': [-16, -16, -24], 'hull_maxs': [16, 16, 32],
                 'post_startsolid': False, 'post_allsolid': False}
        self.assertTrue(goal_frame(frame, goal))
        for patch in ({'feet_z': 128.125}, {'origin': [700, 241, 24.125]}, {'waterlevel': 1},
                      {'grounded': False}, {'post_startsolid': True}, {'post_allsolid': True},
                      {'hull_maxs': [16, 16, 4]},
                      {'origin': [632, 239, 24.125]}):
            with self.subTest(patch=patch):
                self.assertFalse(goal_frame({**frame, **patch}, goal))

    def test_timestep_and_frame_caps(self):
        for timestep in (0, 7, 51, 256, 16.5, float('nan'), True):
            spec = fold_spec(); spec['timestep_ms'] = timestep
            with self.subTest(timestep=timestep), self.assertRaises(ValueError):
                prepare(spec)
        spec = fold_spec(); spec['commands'][0]['frames'] = 4096
        with self.assertRaisesRegex(ValueError, 'frame/time cap'):
            prepare(spec)

    def test_start_clearance_is_explicit_and_eighth_grid(self):
        spec = fold_spec()
        self.assertEqual(prepare(spec)[0], [632, -144, 0.125])
        del spec['start']['clearance_z']
        self.assertEqual(prepare(spec)[0], [632, -144, 0])
        spec['start']['clearance_z'] = 0.1
        with self.assertRaisesRegex(ValueError, 'eighth-unit'):
            prepare(spec)

    def test_goal_must_be_ahead_and_stationary_cannot_move(self):
        spec = fold_spec(); spec['goal']['coordinate'] = -145
        with self.assertRaisesRegex(ValueError, 'ahead'):
            prepare(spec)
        del spec['goal']; spec['expected_stationary_waterlevel'] = 2
        with self.assertRaisesRegex(ValueError, 'cannot command movement'):
            prepare(spec)


@unittest.skipUnless(NATIVE.exists() and BUILD_RECORD.exists() and (PROJECT/'out/fold/qmg_fold.bsp').exists(),
                     'Optional private native 3.19 build or original Fold fixture is unavailable')
class NativeMovementIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        verify_build(NATIVE)
        cls.controls = {'schema_version': 1, 'native_executable_sha256': sha(NATIVE),
                        'bsp_sha256': sha(PROJECT/'out/fold/qmg_fold.bsp'), 'controls': {}}

    @classmethod
    def tearDownClass(cls):
        EVIDENCE.mkdir(parents=True, exist_ok=True)
        save(EVIDENCE/'negative-controls.json', cls.controls)

    def execute(self, spec, label):
        spec['id'] = label
        return run_trial(spec, EVIDENCE/'controls'/label)

    def test_native_stationary_waterlevel_two(self):
        spec = fold_spec()
        spec['start']['feet'] = [632, 48, 0]
        spec['commands'] = [{'frames': 32}]
        del spec['goal']; spec['expected_stationary_waterlevel'] = 2
        result = self.execute(spec, 'stationary-water2')
        self.assertTrue(result['success'])
        self.assertEqual(result['waterlevel_sequence'], [2])
        self.controls['controls']['native_stationary_waterlevel_two'] = {'passed': True, 'result': result}

    @unittest.skipUnless((PROJECT/'out/lockyard/qmg_lockyard.bsp').exists(), 'Optional Lockyard fixture is unavailable')
    def test_native_stationary_waterlevel_three_below_lockyard_beam(self):
        spec = trial('lockyard', 400, PROJECT/'out/lockyard/qmg_lockyard.bsp')
        spec['start']['feet'] = [304, -560, -128]
        spec['commands'] = [{'frames': 32}]
        del spec['goal']; spec['expected_stationary_waterlevel'] = 3
        result = self.execute(spec, 'stationary-water3')
        self.assertTrue(result['success'])
        self.assertEqual(result['waterlevel_sequence'], [3])
        self.controls['controls']['native_stationary_waterlevel_three'] = {'passed': True, 'result': result}

    def test_native_solid_wall_stalls_and_goal_is_not_reached(self):
        spec = fold_spec(); spec['goal']['coordinate'] = 500
        result = self.execute(spec, 'wall-obstruction')
        self.assertFalse(result['success'])
        self.assertEqual(result['failure_reason'], 'stalled_against_collision_or_movement_constraint')
        self.assertLess(result['final_observed_frame']['origin'][1], 336)
        self.controls['controls']['solid_obstruction'] = {'passed': True, 'result': result}

    def test_native_startsolid_and_exact_floor_contact_rejected(self):
        rejections = []
        for feet in (-16, 0):
            args = [str(NATIVE), str(PROJECT/'out/fold/qmg_fold.bsp'), '632', '-144', str(feet), '0', '0', '0', '800']
            result = subprocess.run(args, input='16 0 0 0 0 0 0\n', text=True, capture_output=True, cwd=PROJECT, timeout=10)
            self.assertEqual(result.returncode, 2)
            self.assertIn('initial standing hull is startsolid', result.stderr)
            self.assertEqual(result.stdout, '')
            rejections.append({'feet_z': feet, 'returncode': result.returncode, 'stderr': result.stderr})
        self.controls['controls']['startsolid_rejected'] = {'passed': True, 'attempts': rejections}

    def test_native_timestep_overflow_and_nonfinite_rejected(self):
        args = [str(NATIVE), str(PROJECT/'out/fold/qmg_fold.bsp'), '632', '-144', '0.125', '0', '0', '0', '800']
        rejections = []
        for first in ('0', '256', '4294967312', '16.5', 'nan'):
            result = subprocess.run(args, input=first+' 0 0 0 0 0 0\n', text=True, capture_output=True, cwd=PROJECT, timeout=10)
            self.assertEqual(result.returncode, 2)
            self.assertNotIn('"type":"frame"', result.stdout)
            rejections.append({'msec_field': first, 'returncode': result.returncode, 'stderr': result.stderr})
        self.controls['controls']['native_timestep_bounds'] = {'passed': True, 'attempts': rejections}

    def test_native_deterministic_repeat_and_correct_event_time(self):
        a = self.execute(fold_spec(), 'determinism-a')
        b = self.execute(fold_spec(), 'determinism-b')
        self.assertTrue(a['success'])
        self.assertEqual(a['frames_sha256'], b['frames_sha256'])
        self.assertEqual(a['movement_time_ms'], (a['goal_frame']-8)*16)
        self.assertLess(a['goal_frame'], a['simulated_frames'])
        self.controls['controls']['deterministic_repeat'] = {'passed': True, 'frames_sha256': a['frames_sha256'],
                                                           'movement_time_ms': a['movement_time_ms']}

    def test_timeout_retains_inputs_partial_output_and_replaces_old_success(self):
        output = EVIDENCE/'controls'/'timeout-negative'
        output.mkdir(parents=True, exist_ok=True)
        save(output/'result.json', {'success': True})
        with patch('tools.native_movement.run.subprocess.run', side_effect=subprocess.TimeoutExpired(
                'native', 30, output=b'partial native stdout\n', stderr=b'timeout fixture\n')):
            with self.assertRaisesRegex(ValueError, 'timed out'):
                run_trial(fold_spec(), output)
        record = json.loads((output/'result.json').read_text(encoding='utf-8'))
        self.assertFalse(record['success'])
        self.assertEqual(record['failure_reason'], 'native_process_timeout')
        self.assertTrue((output/'commands.txt').read_text(encoding='utf-8'))
        self.assertEqual((output/'frames.ndjson').read_text(encoding='utf-8'), 'partial native stdout\n')
        self.controls['controls']['timeout_failure_retention'] = {'passed': True, 'simulated_timeout': True}

    def test_unsettled_start_rejected_and_stale_success_replaced(self):
        spec = fold_spec(); spec['start']['clearance_z'] = 1; spec['settle_frames'] = 2
        output = EVIDENCE/'controls'/'unsettled-start'
        output.mkdir(parents=True, exist_ok=True)
        save(output/'result.json', {'success': True})
        with self.assertRaisesRegex(ValueError, 'stable standing grounded'):
            run_trial(spec, output)
        record = json.loads((output/'result.json').read_text(encoding='utf-8'))
        self.assertFalse(record['success'])
        self.controls['controls']['unsettled_start_rejected'] = {'passed': True, 'error': record['error']}


if __name__ == '__main__':
    unittest.main()
