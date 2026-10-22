"""Synthetic controls for exact wet profile, sky and independent dry bindings."""

import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.map_recipes.lockyard_audit import audit_lockyard
from tools.map_recipes.geometry import GeometryError, convex_polyhedron


WATER = 32 | (1 << 27) | (1 << 28)


def box(low,high):
    return [(1,0,0,high[0]),(-1,0,0,-low[0]),(0,1,0,high[1]),(0,-1,0,-low[1]),
            (0,0,1,high[2]),(0,0,-1,-low[2])]


def fixture(extra=()):
    identity = 'a'*64
    down = [[[-224+i*36,-656,-128],[-188+i*36,-464,-16*i]] for i in range(8)]
    up = [[[448+i*36,-656,-128],[484+i*36,-464,-112+16*i]] for i in range(8)]
    water = [[-224,-736,-128],[736,-304,-16]]
    roof = [[-384,-224,512],[384,512,536]]
    ceiling = [[256,-768,-40],[352,-272,192]]
    entries = [(box((-384,-768,-152),(1008,-272,-128)),1,0),
               (box((-384,-768,-24),(-224,-272,0)),1,0),
               (box((736,-768,-24),(1008,-272,0)),1,0),
               (box((-384,-224,-24),(384,512,0)),1,0),
               (box((-384,352,104),(384,512,128)),1,0)]
    entries += [(box(*bounds),1,0) for bounds in down+up]
    entries += [(box(*ceiling),1,0),(box(*roof),1,[0,0,0,0,0,2]),(box(*water),WATER,1)]
    entries += list(extra)
    planes,sides,brushes = [],[],[]
    for rows,contents,tex in entries:
        first = len(sides)
        for i,p in enumerate(rows):
            sides.append((len(planes),tex[i] if isinstance(tex,list) else tex)); planes.append(p)
        brushes.append((first,len(rows),contents))
    texinfo = [(1,0,0,0,0,1,0,0,0,0,b'e1u1/flat1_1',-1),
               (1,0,0,0,0,1,0,0,24,0,b'e3u2/water2',-1),
               (1,0,0,0,0,1,0,0,4,0,b'e1u1/sky1',-1)]
    bsp = SimpleNamespace(models=[(0,)*9+(-1,0,0)],nodes=[],leaves=[(0,)*11+(0,len(brushes))],
                          leafbrushes=list(range(len(brushes))),brushes=brushes,planes=planes,
                          brushsides=sides,texinfo=texinfo,entities=[{'classname':'worldspawn','sky':'unit1_'}])
    def trace(start,end,mask=1):
        result = 1
        for first,count,contents in bsp.brushes:
            if not contents & mask: continue
            enter,leave = 0,1
            for pi,_ in bsp.brushsides[first:first+count]:
                p = bsp.planes[pi]
                a = sum(p[i]*start[i] for i in range(3))-p[3]
                b = sum(p[i]*end[i] for i in range(3))-p[3]
                if a > 0 and b > 0: break
                if a > 0: enter=max(enter,a/(a-b))
                elif b > 0: leave=min(leave,a/(a-b))
            else:
                if enter <= leave: result=min(result,enter)
        return result
    bsp.trace=trace
    layout={'layout_sha256':identity,'rooms':[{'id':1,'bounds':[[-384,-224,0],[384,512,512]]}],
            'solids':[{'id':1,'purpose':6,'contents':'water','bounds':water}],
            'ordinary_path_checks':264,'ordinary_route_edge_kinds':['walk','step','door']}
    physics={'e1u1/flat1_1':{'flags':0,'contents':0,'value':0},
             'e3u2/water2':{'flags':25,'contents':32,'value':100},
             'e1u1/sky1':{'flags':132,'contents':0,'value':0}}
    features={'map':'qmg_lockyard','layout_sha256':identity,'main_floors':[0,128],'local_wet_bed':-128,
              'water':water,'sky_rooms':[1],'sky_roof':roof,'sky':'unit1_','submerged_ceiling':ceiling,
              'wet_trial':{'start_feet':[-320,-560,0],'goal_feet':[832,-560,0],
                           'descent_steps':down,'ascent_steps':up,
                           'bottom_segment':[[64,-560,-128],[448,-560,-128]]},
              'flat_segments':[{'label':'lower crossing','from':[-144,432,0],'to':[160,432,0]},
                               {'label':'upper crossing','from':[-144,432,128],'to':[160,432,128]}]}
    def node(i,lo,hi):
        return {'id':i,'area':{'min_x':lo,'max_x':hi,'min_y':-752,'max_y':-288,'floor_z':0,'ceiling_z':192,'slope':None},
                'origin':[(lo+hi)//2,-520,24]}
    graph={'nodes':[node(1,-368,-240),node(2,752,992)],
           'edges':[{'id':1,'from':1,'to':2,'kind':'walk'},{'id':2,'from':2,'to':1,'kind':'walk'}]}
    report={'layout_sha256':identity,'analysis':{'navigation':{'player':{'layout_sha256':identity,'ok':True,
            'profile':{'game':'quake2','hull_mins':[-16,-16,-24],'hull_maxs':[16,16,32]},'graph':graph}}}}
    return bsp,layout,physics,features,report


class LockyardAuditTests(unittest.TestCase):
    def test_complete_supported_profile_and_dry_bindings_are_json_safe(self):
        result=audit_lockyard(*fixture())
        json.dumps(result)
        self.assertTrue(result['ok']); self.assertFalse(result['runtime_verified'])
        self.assertEqual(len(result['wet_supported_treads']),16)
        self.assertEqual(len(result['wet_geometric_intervals']),19)
        self.assertEqual(result['interpreted_standing_waterlevel_at_bottom'],3)
        self.assertEqual(result['dry_station_forward']['endpoint_snap_distance'],0)
        self.assertFalse(result['dry_station_forward']['compiled_entire_return_swept'])
        self.assertTrue(result['blocked_surfacing_brushes'])

    def test_waterbox_cannot_hide_a_same_bounds_chamfer(self):
        args=fixture(); bsp=args[0]; first,count,contents=bsp.brushes[-1]
        bsp.brushsides.append((len(bsp.planes),1)); bsp.planes.append((1,1,0,400))
        bsp.brushes[-1]=(first,count+1,contents)
        shape=convex_polyhedron(bsp.planes[p] for p,_ in bsp.brushsides[first:first+count+1])
        self.assertEqual(shape.bounds,((-224,-736,-128),(736,-304,-16)))
        with self.assertRaisesRegex(GeometryError,'complete compiled authored box'): audit_lockyard(*args)

    def test_compiler_liquid_modifiers_cannot_hide_blocking_or_hazard_bits(self):
        for bit in (1,8,16,1<<16):
            args=fixture(); first,count,_=args[0].brushes[-1]
            args[0].brushes[-1]=(first,count,WATER|bit)
            with self.subTest(bit=bit),self.assertRaises(GeometryError): audit_lockyard(*args)

    def test_sky_on_wrong_side_or_wrong_material_fails(self):
        for field,value in ((8,132),(9,100),(10,b'e1u1/flat1_1')):
            args=fixture(); tex=list(args[0].texinfo[2]);tex[field]=value;args[0].texinfo[2]=tuple(tex)
            with self.subTest(field=field),self.assertRaises(GeometryError): audit_lockyard(*args)
        args=fixture(); bsp=args[0]; first,_,_=bsp.brushes[-2]
        p,_=bsp.brushsides[first];bsp.brushsides[first]=(p,2)
        with self.assertRaisesRegex(GeometryError,'only on the chosen'):audit_lockyard(*args)

    def test_selected_vertical_sky_ray_detects_an_interior_cover(self):
        obstacle=(box((-16,-16,200),(16,16,224)),1,0)
        with self.assertRaisesRegex(GeometryError,'sky ray is occluded'):audit_lockyard(*fixture([obstacle]))

    def test_breathing_column_detects_head_clearance_obstruction(self):
        obstacle=(box((140,-580,-48),(180,-540,-32)),1,0)
        with self.assertRaisesRegex(GeometryError,'emergence column'):audit_lockyard(*fixture([obstacle]))

    def test_clear_wet_stations_do_not_hide_intermediate_geometry(self):
        obstacle=(box((210,-568,-110),(214,-552,-96)),1,0)
        with self.assertRaisesRegex(GeometryError,'continuous standing-hull sweep'):audit_lockyard(*fixture([obstacle]))

    def test_equal_height_endpoints_do_not_hide_a_missing_floor_patch(self):
        right=(box((124,-768,-152),(1008,-272,-128)),1,0)
        args=fixture([right]);args[0].planes[0]=(1,0,0,120)
        with self.assertRaisesRegex(GeometryError,'continuous full-footprint floor support'):audit_lockyard(*args)

    def test_coplanar_support_partition_has_a_budget(self):
        with patch('tools.map_recipes.lockyard_audit.MAX_FLOOR_CELLS',0):
            with self.assertRaisesRegex(GeometryError,'cell budget'):audit_lockyard(*fixture())

    def test_tread_center_requires_complete_footprint_support(self):
        args=fixture(); bsp=args[0]
        # Narrow only the first descending brush to thirty units. Its center
        # remains supported as a point; the required 32-wide footprint does not.
        first,count,contents=bsp.brushes[5]
        for i,p in enumerate(box((-224,-575,-128),(-188,-545,0))): bsp.planes[first+i]=p
        args[3]['wet_trial']['descent_steps'][0]=[[-224,-575,-128],[-188,-545,0]]
        with self.assertRaisesRegex(GeometryError,'width or 36-unit run'):audit_lockyard(*args)

    def test_profile_budget_actual_box_and_bottom_metadata_are_checked(self):
        for change in ('count','step','bottom','start'):
            args=fixture(); trial=args[3]['wet_trial']
            if change=='count': trial['descent_steps']*=5;trial['ascent_steps']*=5
            if change=='step':trial['ascent_steps'][1][1][2]+=1
            if change=='bottom':trial['bottom_segment'][0][0]+=1
            if change=='start':trial['start_feet'][0]=-250
            with self.subTest(change=change),self.assertRaises(GeometryError):audit_lockyard(*args)

    def test_required_physics_and_current_hashes_are_fail_closed(self):
        for change in ('physics','hash','graphhash','report'):
            args=list(fixture())
            if change=='physics':args[2].pop('e1u1/sky1')
            if change=='hash':args[3]['layout_sha256']='b'*64
            if change=='graphhash':args[4]['layout_sha256']='b'*64
            if change=='report':args[4]=None
            with self.subTest(change=change),self.assertRaises(GeometryError):audit_lockyard(*args)

    def test_dry_route_excludes_optional_edges_and_preserves_direction(self):
        for kind in ('jump','drop','lift','teleporter',None):
            args=fixture();graph=args[4]['analysis']['navigation']['player']['graph']
            if kind is None:graph['edges'].pop()
            else:graph['edges'][0]['kind']=kind
            with self.subTest(kind=kind),self.assertRaisesRegex(GeometryError,'ordinary walk/step/door return'):audit_lockyard(*args)

    def test_exact_station_binding_does_not_snap_to_wrong_height(self):
        args=fixture();node=args[4]['analysis']['navigation']['player']['graph']['nodes'][1]
        node['area']['floor_z']=16;node['origin'][2]=40
        with self.assertRaisesRegex(GeometryError,'exact local-height graph binding'):audit_lockyard(*args)


if __name__=='__main__':unittest.main()
