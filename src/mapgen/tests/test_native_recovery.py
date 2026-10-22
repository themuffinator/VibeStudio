"""Controls for residual-speed, bounded-pocket and dwell recovery semantics."""
import json
import unittest

from tools.native_movement.recovery import GOAL, OUTPUT, analyze, qualifies


def state():
    return {'origin':[0,16,152.125],'feet_z':128.125,'velocity':[0,0,0],
            'grounded':True,'waterlevel':0,'hull_mins':[-16,-16,-24],
            'hull_maxs':[16,16,32],'post_startsolid':False,'post_allsolid':False}


class RecoveryGoalTests(unittest.TestCase):
    def test_vector_speed_and_vertical_velocity_requirements(self):
        f=state(); self.assertTrue(qualifies(f,GOAL))
        for velocity in ([8,8,0],[0,0,10.125],[-200,0,0]):
            f['velocity']=velocity
            with self.subTest(velocity=velocity): self.assertFalse(qualifies(f,GOAL))

    def test_stationary_outside_pocket_is_not_recovery(self):
        f=state(); f['origin'][1]=63.375
        self.assertFalse(qualifies(f,GOAL))
        f=state(); f['grounded']=False
        self.assertFalse(qualifies(f,GOAL))


@unittest.skipUnless((OUTPUT/'ravel-recovery-f400-release/frames.ndjson').exists(),
                     'Optional retained Ravel recovery fixture is unavailable')
class RecoveryFrameTests(unittest.TestCase):
    def evidence(self):
        p=OUTPUT/'ravel-recovery-f400-release'
        spec=json.loads((p/'trial.json').read_text())
        frames=[json.loads(s) for s in (p/'frames.ndjson').read_text().splitlines()][1:]
        return spec,frames

    def test_nine_sample_dwell_spans128ms(self):
        spec,frames=self.evidence(); result=analyze(spec,frames)
        self.assertEqual(result['first_recovery_goal']['frame']-result['dwell_start']['frame'],8)
        self.assertEqual(result['first_recovery_goal']['movement_time_ms']-result['dwell_start']['movement_time_ms'],128)

    def test_speed_spike_resets_consecutive_dwell(self):
        spec,frames=self.evidence(); first=analyze(spec,frames)['first_recovery_goal']['frame']
        interrupted=first-4; frames[interrupted-1]['velocity']=[100,0,0]
        result=analyze(spec,frames)
        self.assertTrue(result['success'])
        self.assertGreaterEqual(result['first_recovery_goal']['frame'],interrupted+9)

    def test_envelope_overshoot_cannot_be_erased_by_later_return(self):
        spec,frames=self.evidence()
        frames[8+spec['approach_frames']]['origin'][0]=-145
        result=analyze(spec,frames)
        self.assertFalse(result['success'])
        self.assertIn('left_declared_recovery_envelope',result['first_violation']['reasons'])

    def test_losing_middle_floor_cannot_be_erased_by_later_landing(self):
        spec,frames=self.evidence()
        frames[8+spec['approach_frames']]['grounded']=False
        result=analyze(spec,frames)
        self.assertFalse(result['success'])
        self.assertIn('lost_required_middle_floor_state',result['first_violation']['reasons'])


if __name__=='__main__': unittest.main()
