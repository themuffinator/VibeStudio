"""Synthetic protocol controls. No compiler, game, client, network or native runs."""
import copy
import unittest

from tools.native_teleport import verify as v


def actor():
    return {'id':1,'mins':[-16,-16,-24],'maxs':[16,16,32],'pm_flags':0,
            'health':100,'waterlevel':0,'post_startsolid':0,'post_allsolid':0,
            'groundentity':0,'velocity':[0,0,0],'origin':[-336,672,24.25]}


def goal_fixture(count=9):
    return ([{'type':'after_clientthink','frame':i+10,'time_ms':16*(i+10),'actor':actor()} for i in range(count)],
            {'sign':1,'release_start_frame':10})


def activation_fixture(expected='clear_teleport'):
    spec={'start_feet':[464,-96,0],'occupancy':0,'missing_target':0,'expected':expected,'destination_yaw':90,
          'commands':[{'frames':120,'forward':200,'yaw':0}]}
    a=actor();a.update({'origin':[464,-96,24.125],'old_origin':[464,-96,24.125],
         'angles':[0,0,0],'viewangles':[0,0,0],'v_angle':[0,0,0],'pm_origin':[0,0,0],
         'pm_velocity':[1600,0,0],'pm_time':0,'event':0})
    records=[{'type':'fixture','game_api':3,'occupancy_mode':0,'missing_target':0,
              'destination_origin':[-832,192,152],'destination_angles':[0,90,0]},
             {'type':'initial','actor':copy.deepcopy(a)}]
    arrived=copy.deepcopy(a);arrived.update({'origin':[-832,192,162],'old_origin':[-832,192,152],
         'pm_time':20,'pm_flags':32,'event':6})
    for frame in range(1,121):
        state=copy.deepcopy(a if frame<30 else arrived)
        if frame>30:
            state['pm_time']=max(0,20-2*(frame-30));state['pm_flags']=32 if frame<40 else 0
            state['viewangles']=[0,90,0]
        row={'type':'after_clientthink','frame':frame,'time_ms':frame*16,'actor':state}
        if frame==30:
            records.append({'type':'link','changed_entity':1,'frame':frame,'time_ms':frame*16,
                 'actor':copy.deepcopy(arrived),'victim':None,'source_event':6,'means_of_death':0})
        records.append(row)
    for i in range(1,20):
        records.append({'type':'after_gameframe','game_frame':i+2,'time_ms':((i*100+15)//16)*16})
    return records,spec


def arrival(records): return next(r for r in records if r['type']=='link')


class GoalControls(unittest.TestCase):
    def test_nine_state128ms_goal(self):
        records,spec=goal_fixture();self.assertEqual(v.first_goal(records,spec)['end_time_ms'],288)

    def test_eight_samples_not128ms(self):
        records,spec=goal_fixture(8);self.assertIsNone(v.first_goal(records,spec))

    def test_high_speed_crossing_is_not_arrival(self):
        records,spec=goal_fixture();records[4]['actor']['velocity']=[100,0,0]
        self.assertIsNone(v.first_goal(records,spec))

    def test_airborne_is_not_goal(self):
        records,spec=goal_fixture();records[4]['actor']['groundentity']=-1
        self.assertIsNone(v.first_goal(records,spec))

    def test_crouched_or_wrong_hull_is_not_standing(self):
        for edit in ({'pm_flags':1},{'maxs':[16,16,4]}):
            records,spec=goal_fixture();records[4]['actor'].update(edit)
            self.assertIsNone(v.first_goal(records,spec))

    def test_wet_or_blocked_is_not_goal(self):
        for edit in ({'waterlevel':1},{'post_startsolid':1},{'post_allsolid':1},{'health':0}):
            records,spec=goal_fixture();records[4]['actor'].update(edit)
            self.assertIsNone(v.first_goal(records,spec))

    def test_goal_caps_are_not_widened(self):
        for position in ([-319.875,672,24.25],[-352.125,672,24.25],[-336,673.125,24.25],[-336,672,24.375]):
            records,spec=goal_fixture();records[4]['actor']['origin']=position
            self.assertIsNone(v.first_goal(records,spec))

    def test_wrong_direction_is_not_goal(self):
        records,spec=goal_fixture();spec['sign']=-1
        self.assertIsNone(v.first_goal(records,spec))

    def test_pre_release_station_is_not_goal(self):
        records,spec=goal_fixture();spec['release_start_frame']=11
        self.assertIsNone(v.first_goal(records,spec))

    def test_missing_frame_or_wrong_elapsed_time_breaks_dwell(self):
        for field in ('frame','time_ms'):
            records,spec=goal_fixture();records[4][field]+=1
            self.assertIsNone(v.first_goal(records,spec))

    def test_first_interval_is_reported(self):
        records,spec=goal_fixture(20);records[0]['actor']['velocity']=[200,0,0]
        self.assertEqual(v.first_goal(records,spec)['start_frame'],11)


class ActivationControls(unittest.TestCase):
    def test_full_stock_sequence(self):
        records,spec=activation_fixture();self.assertEqual(v.inspect_activation(records,spec)['activation_frame'],30)

    def test_override_and_facing_cannot_be_consistently_wrong(self):
        records,spec=activation_fixture();spec['destination_yaw']=0
        with self.assertRaises(ValueError):v.inspect_activation(records,spec)

    def test_postgameframe_is_not_relink_evidence(self):
        records,spec=activation_fixture();arrival(records)['type']='after_gameframe'
        with self.assertRaises(ValueError):v.inspect_activation(records,spec)

    def test_event_alone_does_not_prove_arrival(self):
        records,spec=activation_fixture();arrival(records)['actor']['origin'][0]+=1
        with self.assertRaises(ValueError):v.inspect_activation(records,spec)

    def test_immediate_angles_are_not_next_command_angles(self):
        records,spec=activation_fixture();arrival(records)['actor']['viewangles']=[0,90,0]
        with self.assertRaises(ValueError):v.inspect_activation(records,spec)

    def test_stale_pmove_origin_remains_distinct(self):
        records,spec=activation_fixture();a=arrival(records)['actor'];a['pm_origin']=[int(x*8) for x in a['origin']]
        with self.assertRaises(ValueError):v.inspect_activation(records,spec)

    def test_hold_cannot_move(self):
        records,spec=activation_fixture()
        next(r for r in records if r['type']=='after_clientthink' and r['frame']==34)['actor']['origin'][0]+=.125
        with self.assertRaises(ValueError):v.inspect_activation(records,spec)

    def test_wrong_timer_rejected(self):
        records,spec=activation_fixture()
        next(r for r in records if r['type']=='after_clientthink' and r['frame']==34)['actor']['pm_time']=11
        with self.assertRaises(ValueError):v.inspect_activation(records,spec)

    def test_missing_clientthink_rejected(self):
        records,spec=activation_fixture();records=[r for r in records if not(r['type']=='after_clientthink' and r['frame']==17)]
        with self.assertRaises(ValueError):v.inspect_activation(records,spec)

    def test_telefrag_requires_real_mod_and_non_solid_victim(self):
        records,spec=activation_fixture('telefrag_clear');at=arrival(records)
        at['victim']={'health':-999,'solid':0};at['means_of_death']=21
        self.assertTrue(v.inspect_activation(records,spec)['arrival_clear'])
        at['means_of_death']=0
        with self.assertRaises(ValueError):v.inspect_activation(records,spec)

    def test_unkillable_overlap_is_expected_failure_of_safe_arrival(self):
        records,spec=activation_fixture('transported_but_blocked');at=arrival(records)
        at['victim']={'health':100,'solid':2};at['actor']['post_startsolid']=1
        self.assertFalse(v.inspect_activation(records,spec)['arrival_clear'])

    def test_no_teleport_control_rejects_transition(self):
        records,spec=activation_fixture('no_teleport')
        with self.assertRaises(ValueError):v.inspect_activation(records,spec)


if __name__=='__main__':unittest.main()
