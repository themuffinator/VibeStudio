"""Negative controls for ordered circuit traversal and bounded recovery dwell."""
import unittest
import importlib
from pathlib import Path

from tools.native_movement.motif_circuits import analyze, qualifies


def state(feet,frame):
    return {'frame':frame,'time_ms':frame*16,'msec':16,'origin':[*feet[:2],feet[2]+24.125],
            'feet_z':feet[2]+.125,'velocity':[0,0,0],'grounded':True,'waterlevel':0,
            'hull_mins':[-16,-16,-24],'hull_maxs':[16,16,32],
            'post_startsolid':False,'post_allsolid':False}


def fixture():
    # Synthetic complete frame stream: tests interpretation without requiring
    # the optional native executable or retained generated map artifacts.
    gates=[('ascent_mid',[88,718,64]),('upper_entry',[88,896,128]),
           ('first_upper_corner',[88,960,128]),('second_upper_corner',[696,960,128]),
           ('upper_exit',[696,896,128]),('descent_mid',[696,718,64])]
    spec={'settle_frames':8,'start':{'feet':[88,552,0]},'commands':[{'frames':64}],
          'route_points':[[88,552,0],[88,960,128],[696,960,128],[696,552,0]],
          'route_center_half_width':48,
          'ordered_checkpoints':[{'label':label,'feet':p,'xy_tolerance':24,
                                  'feet_tolerance':1,'grounded':True} for label,p in gates],
          'circuit_goal':{'nominal_feet':[696,552,0],'center_xy_bounds':[[664,544],[728,576]],
                          'feet_z':0,'feet_tolerance':1,'maximum_horizontal_speed':10,
                          'maximum_absolute_vertical_speed':10,'consecutive_samples':9,
                          'first_to_last_dwell_ms':128}}
    count = 8+sum(c['frames'] for c in spec['commands'])
    feet = [spec['start']['feet']]*8
    feet += [g['feet'] for g in spec['ordered_checkpoints']]
    feet += [spec['circuit_goal']['nominal_feet']]*(count-len(feet))
    return spec,[state(p,i) for i,p in enumerate(feet,1)]


class CircuitGoalTests(unittest.TestCase):
    def test_planar_crossing_with_residual_speed_is_not_recovery(self):
        spec,frames = fixture(); f = frames[-1]
        for velocity in ([200,0,0],[8,8,0],[0,0,11]):
            f['velocity'] = velocity
            with self.subTest(velocity=velocity): self.assertFalse(qualifies(f,spec['circuit_goal']))

    def test_eight_frame_ends_do_not_span128ms(self):
        spec,frames = fixture()
        for f in frames[14:-8]: f['velocity']=[200,0,0]
        self.assertFalse(analyze(spec,frames)['success'])
        frames[-9]['velocity']=[0,0,0]
        result = analyze(spec,frames)
        self.assertTrue(result['success'])
        self.assertEqual(result['first_circuit_goal']['time_ms']-result['dwell_start']['time_ms'],128)

    def test_late_endpoint_pose_cannot_replace_missing_ascent(self):
        spec,frames = fixture()
        frames[8] = state(spec['start']['feet'],9)
        result = analyze(spec,frames)
        self.assertFalse(result['success'])
        self.assertEqual(result['next_missing_checkpoint'],'ascent_mid')

    def test_missed_second_corner_cannot_be_replaced_by_descent(self):
        spec,frames = fixture()
        frames[11] = state(spec['ordered_checkpoints'][2]['feet'],12)
        result = analyze(spec,frames)
        self.assertFalse(result['success'])
        self.assertEqual(result['next_missing_checkpoint'],'second_upper_corner')

    def test_upper_floor_loss_is_not_erased_by_later_recovery(self):
        spec,frames = fixture(); frames[10]['grounded']=False
        result = analyze(spec,frames)
        self.assertFalse(result['success'])
        self.assertIn('lost_upper_gallery_floor_before_exit',result['first_violation']['reasons'])

    def test_route_excursion_is_not_erased_by_later_return(self):
        spec,frames = fixture(); frames[14]['origin'][:2]=[400,700]
        result = analyze(spec,frames)
        self.assertFalse(result['success'])
        self.assertIn('left_declared_route_center_envelope',result['first_violation']['reasons'])

    def test_speed_spike_resets_dwell(self):
        spec,frames = fixture(); baseline=analyze(spec,frames)
        interrupted=baseline['first_circuit_goal']['frame']-4
        frames[interrupted-1]['velocity']=[100,0,0]
        result=analyze(spec,frames)
        self.assertTrue(result['success'])
        self.assertGreaterEqual(result['first_circuit_goal']['frame'],interrupted+9)

    def test_disordered_timestamps_reject(self):
        spec,frames=fixture(); frames[20]['time_ms']+=1
        with self.assertRaisesRegex(ValueError,'Incomplete native sequence'): analyze(spec,frames)


@unittest.skipUnless((Path(__file__).resolve().parents[1]/'out/native-motif-circuits/inspect_observations.py').exists(),
                     'Optional post-execution support inspector is unavailable')
class ObservedFootprintTests(unittest.TestCase):
    def test_support_union_accepts_seam_but_rejects_gap(self):
        support=importlib.import_module('out.native-motif-circuits.inspect_observations').support
        low,high=[0,0],[32,32]
        joined=[((0,0),(16,32),128),((16,0),(32,32),128)]
        self.assertTrue(support(joined,low,high,128)['fully_supported'])
        joined[1]=((17,0),(32,32),128)
        self.assertEqual(support(joined,low,high,128)['covered_area'],992)
        self.assertFalse(support(joined,low,high,128)['fully_supported'])

    def test_partial_edge_contact_is_not_full_footprint_support(self):
        support=importlib.import_module('out.native-motif-circuits.inspect_observations').support
        r=support([((0,0),(128,288),128)],[120,100],[152,132],128)
        self.assertEqual(r['covered_area'],256)
        self.assertFalse(r['fully_supported'])


if __name__=='__main__': unittest.main()
