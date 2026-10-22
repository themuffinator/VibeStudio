"""Physical-band, continuous-upper-support and final-recovery negative controls."""
import unittest

from tools.native_movement.motif_turning import World, analyze, stair_band
from tools.native_movement.motif_circuits import gate_matches


def state(feet,index):
    return {'frame':index,'time_ms':index*16,'msec':16,'origin':[*feet[:2],feet[2]+24.125],
            'feet_z':feet[2]+.125,'velocity':[0,0,0],'grounded':True,'waterlevel':0,
            'hull_mins':[-16,-16,-24],'hull_maxs':[16,16,32],
            'post_startsolid':False,'post_allsolid':False}


class ClearWorld:
    def upper(self,a,b): return {'fully_supported':True,'clear':True}


def fixture():
    points=[[88,552,0],[88,960,128],[696,960,128],[696,552,0]]
    gates=[('ascent_mid',[88,718,64]),('upper_entry',[88,896,128]),
           ('first_upper_corner',points[1]),('second_upper_corner',points[2]),
           ('upper_exit',[696,896,128]),('descent_mid',[696,718,64])]
    spec={'motif':'crossing6','settle_frames':8,'start':{'feet':points[0]},
          'commands':[{'frames':6},{'frames':40}],'route_points':points,
          'ordered_checkpoints':[{'label':label,'feet':p,'xy_tolerance':[48,12],
                                  'feet_tolerance':1,'grounded':True} for label,p in gates],
          'circuit_goal':{'nominal_feet':points[-1],'center_xy_bounds':[[664,544],[728,576]],
                          'feet_z':0,'feet_tolerance':1,'maximum_horizontal_speed':10,
                          'maximum_absolute_vertical_speed':10,'consecutive_samples':9,
                          'first_to_last_dwell_ms':128}}
    sequence=[points[0]]*8+[p for _,p in gates]+[points[-1]]*40
    return spec,[state(p,i) for i,p in enumerate(sequence,1)]


class PhysicalContractTests(unittest.TestCase):
    def test_physical_exit_gate_allows48_but_rejects49(self):
        spec,_=fixture(); gate=spec['ordered_checkpoints'][4]
        self.assertTrue(gate_matches(state([744,896,128],1),gate))
        self.assertFalse(gate_matches(state([745,896,128],1),gate))

    def test_stair_band_enforced_between_gates(self):
        spec,frames=fixture(); frames[8]['origin'][0]=137
        self.assertFalse(stair_band(spec,frames[8],False))
        result=analyze(spec,frames,ClearWorld())
        self.assertFalse(result['success'])
        self.assertIn('full_hull_stair_corridor_lost',result['first_violation']['reasons'])

    def test_airborne_upper_sample_cannot_be_skipped(self):
        spec,frames=fixture(); frames[10]['grounded']=False
        result=analyze(spec,frames,ClearWorld())
        self.assertFalse(result['success'])
        self.assertIn('upper_floor_contact_lost',result['first_violation']['reasons'])

    def test_upper_exit_boundary_chord_is_required(self):
        class ExitGap(ClearWorld):
            def upper(self,a,b): return {'fully_supported':b['frame']!=13,'clear':True}
        spec,frames=fixture(); result=analyze(spec,frames,ExitGap())
        self.assertFalse(result['success'])
        self.assertIn('upper_full_footprint_support_lost',result['first_violation']['reasons'])

    def test_missed_gate_cannot_be_erased_by_late_pocket_dwell(self):
        spec,frames=fixture(); frames[8]=state(spec['start']['feet'],9)
        result=analyze(spec,frames,ClearWorld())
        self.assertFalse(result['success'])
        self.assertIsNotNone(result['final_pocket_dwell_ignoring_route_failure'])

    def test_eight_state_finish_is_too_short(self):
        spec,frames=fixture()
        for f in frames[14:-8]: f['velocity']=[200,0,0]
        self.assertFalse(analyze(spec,frames,ClearWorld())['success'])
        frames[-9]['velocity']=[0,0,0]
        result=analyze(spec,frames,ClearWorld())
        self.assertTrue(result['success'])
        self.assertEqual(result['first_physical_circuit_dwell']['end']['time_ms']-
                         result['first_physical_circuit_dwell']['start']['time_ms'],128)

    def test_velocity_norm_spike_resets_dwell(self):
        spec,frames=fixture(); baseline=analyze(spec,frames,ClearWorld())
        index=baseline['first_physical_circuit_dwell']['end']['frame']-4
        frames[index-1]['velocity']=[8,8,0]
        result=analyze(spec,frames,ClearWorld())
        self.assertGreaterEqual(result['first_physical_circuit_dwell']['end']['frame'],index+9)

    def test_lateral_goal_overhang_is_preserved(self):
        spec,frames=fixture()
        for f in frames[14:]: f['origin'][0]=729
        result=analyze(spec,frames,ClearWorld())
        self.assertFalse(result['success'])
        self.assertIsNone(result['first_violation'])
        self.assertEqual(result['failure_reason'],'no_bounded_low_speed_final_dwell')

    def test_incomplete_frames_reject(self):
        spec,frames=fixture()
        with self.assertRaisesRegex(ValueError,'Incomplete native sequence'):
            analyze(spec,frames[:-1],ClearWorld())


class UpperChordTests(unittest.TestCase):
    def test_supported_endpoints_do_not_certify_unsupported_middle(self):
        world=World.__new__(World); world.world=[]
        world.rectangles=[((-16,-16),(16,16),128),((48,-16),(80,16),128)]
        a,b=state([0,0,128],1),state([64,0,128],2)
        self.assertTrue(world.upper(a,a)['fully_supported'])
        self.assertTrue(world.upper(b,b)['fully_supported'])
        self.assertFalse(world.upper(a,b)['fully_supported'])

    def test_touching_floor_union_covers_whole_chord(self):
        world=World.__new__(World); world.world=[]
        world.rectangles=[((-16,-16),(32,16),128),((32,-16),(80,16),128)]
        self.assertTrue(world.upper(state([0,0,128],1),state([64,0,128],2))['fully_supported'])


if __name__=='__main__': unittest.main()
