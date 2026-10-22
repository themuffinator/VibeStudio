"""Physical negative controls for opposed-stair bridge route certificates."""
from copy import deepcopy
from itertools import product
from types import SimpleNamespace
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

from tools.map_recipes.geometry import convex_polyhedron
from tools.mp_portfolio.atrium_audit import (World, audit_room, audit_stair, point_ray,
    room_contract, source_contracts, bind_native_station)

native_path=Path(__file__).resolve().parents[1]/'out/span-study/native_routes.py'
native_spec=importlib.util.spec_from_file_location('_span_native_protocol_controls',native_path)
native=importlib.util.module_from_spec(native_spec);native_spec.loader.exec_module(native)
observer_spec=importlib.util.spec_from_file_location('_span_native_first_goal_controls',native_path.parent/'verify_native.py')
observer=importlib.util.module_from_spec(observer_spec);observer_spec.loader.exec_module(observer)


def timing_basis():
    return {'by_input':{
        '200':{'away_prefix_distance':19.375,'toward_prefix_distance':20.625,'away_estimated_stop_distance':21.375,
               'toward_estimated_stop_distance':23.5,'away_steady_frame_distance':3.125,'toward_steady_frame_distance':3.25},
        '400':{'away_prefix_distance':30.5,'toward_prefix_distance':31.75,'away_estimated_stop_distance':36.125,
               'toward_estimated_stop_distance':38.75,'away_steady_frame_distance':4.75,'toward_steady_frame_distance':4.875}}}


def fixture(rotation=0,height=128,depth=None):
    width=640;depth=depth or (768 if height==192 else 640)
    x,y=(width,depth) if rotation%2==0 else (depth,width)
    room={'id':1,'name':'Bridge 1','bounds':[[0,0,0],[x,y,384]],
          'tags':['compact_mp','opposed_stair_bridge','motif_rotation_'+str(rotation)]}
    contract=room_contract(room,rotation,height)
    bounds=contract['decks']+[t['bounds'] for s in contract['stairs'] for t in s['treads']]
    layout={'layout_sha256':'a'*64,'rooms':[room],'solids':[], 'mechanisms':[],'ramps':[],'placements':[]}
    for i,b in enumerate(bounds,1):
        layout['solids'].append({'id':i,'bounds':b,'purpose':1 if i<=3 else 2,
                                'contents':'solid','ownership':0,'owner':None})
    report={'layout_sha256':'a'*64,'game':'quake2','mode':'deathmatch','validation':{'ok':True},'analysis':{'navigation':{
        'player':{'available':True,'ok':True,'layout_sha256':'a'*64,
                  'profile':{'game':'quake2','hull_mins':[-16,-16,-24],'hull_maxs':[16,16,32]}}}}}
    boxes=[[[-32,-32,-24],[x+32,y+32,0]],*deepcopy(bounds)]
    return room,contract,layout,report,boxes


def world_from(boxes):
    world=World.__new__(World);world.world=[];world.tops=[];world.point_entities=[]
    for i,(lo,hi) in enumerate(boxes):
        planes=[(1,0,0,hi[0]),(-1,0,0,-lo[0]),(0,1,0,hi[1]),(0,-1,0,-lo[1]),(0,0,1,hi[2]),(0,0,-1,-lo[2])]
        shape=convex_polyhedron(planes);world.world.append((i,1,shape))
        world.tops.append({'brush':i,'low':lo[:2],'high':hi[:2],'floor':hi[2]})
    return world


class AtriumPhysicalControls(unittest.TestCase):
    def test_all_four_rotations_and_both_heights_complete(self):
        for rotation,height in product(range(4),(128,192)):
            with self.subTest(rotation=rotation,height=height):
                _,contract,_,_,boxes=fixture(rotation,height)
                result=audit_room(world_from(boxes),contract)
                self.assertTrue(result['success'])
                self.assertEqual(len(result['stairs'][0]['full_tread_caps']),height//16)

    def test_long_depth960_and1536_keep_full_turn_joins(self):
        for depth in (960,1536):
            _,contract,_,_,boxes=fixture(depth=depth)
            result=audit_room(world_from(boxes),contract)
            self.assertTrue(result['success'])
            self.assertTrue(all(t['fully_supported'] for t in result['upper160_turn_squares']))

    def test_coincident_turns_still_need_full_square(self):
        _,contract,_,_,boxes=fixture(depth=864)
        result=audit_room(world_from(boxes),contract)
        self.assertEqual(contract['upper_centerline'][1],contract['upper_centerline'][2])
        self.assertTrue(result['success'])

    def test_old_narrow_long_room_overlap_is_not_a_complete_turn(self):
        _,contract,_,_,boxes=fixture(depth=960)
        # Original suggested central slab left only64 shared units. Its center
        # may remain usable, but the complete160-wide turns are not covered.
        boxes[2]=[[240,448,104],[400,512,128]]
        result=audit_room(world_from(boxes),contract)
        self.assertFalse(result['success'])
        self.assertFalse(all(t['fully_supported'] for t in result['upper160_turn_squares']))

    def test_missing_bridge_cannot_be_replaced_by_lower_floor(self):
        _,contract,_,_,boxes=fixture();del boxes[2]
        result=audit_room(world_from(boxes),contract)
        self.assertFalse(result['success'])
        self.assertTrue(result['lower160_aisle']['success'])

    def test_eighth_unit_disconnected_join_is_retained(self):
        _,contract,_,_,boxes=fixture();boxes[2][0][0]+=.125
        result=audit_room(world_from(boxes),contract)
        self.assertFalse(result['success'])
        self.assertFalse(result['upper160_strips'][0]['success'])

    def test_missing_tread_rejects_complete_stair(self):
        _,contract,_,_,boxes=fixture();del boxes[6]
        result=audit_room(world_from(boxes),contract)
        self.assertFalse(result['stairs'][0]['success'])
        self.assertFalse(result['success'])

    def test_clear_pose_does_not_hide_raise_before_advance_collision(self):
        _,contract,_,_,boxes=fixture();boxes.append([[64,136,104],[192,172,128]])
        result=audit_stair(world_from(boxes),contract['stairs'][0])
        self.assertTrue(result['full_tread_caps'][2]['clear'])
        self.assertFalse(result['fixed18_raise_advance_lower'][3]['legs'][0]['clear'])
        self.assertFalse(result['success'])

    def test_partial_width_tread_overhang_is_not_center_support(self):
        _,contract,_,_,boxes=fixture();boxes[6][1][0]-=1
        result=audit_stair(world_from(boxes),contract['stairs'][0])
        self.assertFalse(result['success'])
        self.assertFalse(result['full_tread_caps'][2]['fully_supported'])

    def test_fifty_five_units_headroom_fails_full_standing_hull(self):
        _,contract,_,_,boxes=fixture();boxes.append([[0,0,183],[640,640,208]])
        result=audit_room(world_from(boxes),contract)
        self.assertFalse(result['success'])
        self.assertTrue(result['lower160_aisle']['success'])

    def test_upper_route_does_not_hide_blocked_lower_aisle(self):
        _,contract,_,_,boxes=fixture();boxes.append([[272,280,0],[368,360,64]])
        result=audit_room(world_from(boxes),contract)
        self.assertTrue(all(s['success'] for s in result['upper160_strips']))
        self.assertFalse(result['lower160_aisle']['success'])
        self.assertFalse(result['success'])

    def test_los_is_a_separate_point_measure(self):
        world=world_from([[[0,-32,0],[32,32,64]]])
        self.assertFalse(point_ray(world,[-32,0,46],[64,0,46])['visible'])
        self.assertTrue(point_ray(world,[-32,0,110],[64,0,110])['visible'])


class AtriumBindingControls(unittest.TestCase):
    def test192_requires_actual_constructor_minimum_depth(self):
        with self.assertRaises(ValueError):fixture(height=192,depth=736)

    def test_wrong_native_class_or_coincident_entity_rejected(self):
        p={'role':4,'variant':2,'origin':[0,0,24]}
        native=[{'classname':'weapon_chaingun','origin':'0 0 24'}]
        self.assertEqual(bind_native_station(native,p),'weapon_chaingun')
        with self.assertRaises(ValueError):bind_native_station(native+native,p)
        native[0]['classname']='weapon_machinegun'
        with self.assertRaises(ValueError):bind_native_station(native,p)

    def test_actual_source_ids_and_rotations_are_bound(self):
        _,_,layout,report,_=fixture(rotation=3,height=192)
        result=source_contracts(layout,report)[0]
        self.assertEqual(result['deck_solid_ids'],[1,2,3])
        self.assertEqual(len(result['stairs'][1]['treads']),12)

    def test_absent_motif_is_not_a_pass(self):
        _,_,layout,report,_=fixture();layout['rooms'][0]['tags']=['raised_crosswalk']
        with self.assertRaisesRegex(ValueError,'No opposed-stair'):source_contracts(layout,report)

    def test_source_missing_stair_or_deck_rejected(self):
        for index in (1,5):
            _,_,layout,report,_=fixture();del layout['solids'][index]
            with self.assertRaises(ValueError):source_contracts(layout,report)

    def test_unavailable_or_differently_bound_navigation_rejected(self):
        for available,identity in ((False,'a'*64),(True,'b'*64)):
            _,_,layout,report,_=fixture();player=report['analysis']['navigation']['player']
            player.update({'available':available,'layout_sha256':identity})
            with self.assertRaises(ValueError):source_contracts(layout,report)

    def test_liquid_or_mechanism_cannot_enter_world_scope(self):
        _,_,layout,report,_=fixture();layout['solids'][0]['contents']='water'
        with self.assertRaises(ValueError):source_contracts(layout,report)
        _,_,layout,report,_=fixture();layout['mechanisms']=[{'kind':'teleporter'}]
        with self.assertRaises(ValueError):source_contracts(layout,report)

    def test_raw_nonblocking_contents_rejected_before_world_filter(self):
        bsp=SimpleNamespace(models=[None],brushes=[(0,6,1),(6,6,32)])
        with patch('tools.native_movement.upper_link.preflight',return_value=bsp), \
             patch('tools.native_movement.upper_link.model_brushes',return_value=[0,1]), \
             patch('tools.native_movement.upper_link._world') as filtered:
            with self.assertRaisesRegex(ValueError,'every raw model0'):World('unused.bsp')
            filtered.assert_not_called()


class AtriumNativeDeclarationControls(unittest.TestCase):
    def test_piecewise_zero_crossing_preserves_signed_timing_classes(self):
        result=native.timing([-128,0,0],[128,0,0],200,timing_basis())
        self.assertEqual([p['coordinate_class'] for p in result['steady_pieces']],['toward','away'])
        self.assertEqual(result['steady_pieces'][0]['to'],0)
        self.assertEqual(result['steady_pieces'][1]['from'],0)

    def test_both_directions_all_rotations_bind_actual_opposite_floor_gates(self):
        for rotation,direction in product(range(4),('forward','reverse')):
            _,contract,_,_,_=fixture(rotation)
            spec=native.trial(contract,direction,200,timing_basis())
            route=contract['complete_upper_route_waypoints']
            self.assertEqual(spec['route_waypoints'],route if direction=='forward' else list(reversed(route)))
            self.assertEqual(len([c for c in spec['commands'] if c['forward']==0 and c['frames']==24]),4)
            self.assertEqual(spec['physical_goal']['feet_z'],0)

    def test_short_or_coincident_leg_is_not_silently_timed(self):
        with self.assertRaises(ValueError):native.timing([0,0,0],[16,0,0],400,timing_basis())
        _,contract,_,_,_=fixture(depth=864)
        with self.assertRaises(ValueError):native.trial(contract,'forward',200,timing_basis())

    def goal_fixture(self):
        gates=[{'gate':{'center_xy_bounds':[[x-.1,-1],[x+.1,1]],'feet_z':0,'feet_tolerance':1},'label':str(x)} for x in range(1,7)]
        spec={'segments':gates,'physical_goal':gates[-1]['gate'],'commands':[{'frames':6},{'frames':128}]}
        points=[0]*8+list(range(1,7))+[6]*9
        frames=[{'frame':i,'time_ms':16*i,'origin':[x,0,24.125],'feet_z':.125,'grounded':True,'waterlevel':0,
                 'hull_mins':[-16,-16,-24],'hull_maxs':[16,16,32],'post_startsolid':False,'post_allsolid':False,
                 'velocity':[0,0,0]} for i,x in enumerate(points,1)]
        return spec,frames

    def test_independent_goal_needs_all_gates_and_nine_states(self):
        spec,frames=self.goal_fixture()
        self.assertEqual(observer.independently_first_goal(spec,frames)[0]['end_frame'],23)
        self.assertIsNone(observer.independently_first_goal(spec,frames[:-1])[0])
        spec['segments'][2]['gate']['center_xy_bounds']=[[30,-1],[31,1]]
        self.assertIsNone(observer.independently_first_goal(spec,frames)[0])

    def test_independent_goal_rejects_velocity_and_missing_time(self):
        for kind in ('speed','time'):
            spec,frames=self.goal_fixture()
            if kind=='speed':frames[-5]['velocity']=[200,0,0]
            else:frames[-5]['time_ms']+=1
            self.assertIsNone(observer.independently_first_goal(spec,frames)[0])


if __name__=='__main__':unittest.main()
