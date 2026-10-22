"""Exact static route audit for the generated opposed-stair bridge motif.

This checks actual compiled AABB support and collision, not room adjacency or
native movement. Existing world/native helpers are read-only dependencies.
"""
from __future__ import annotations
import argparse
from fractions import Fraction as F
import hashlib
from itertools import combinations, product
import json
from pathlib import Path
import re

from tools.map_study.binary import Bsp, vec
from tools.mp_portfolio.inspect import validate_report
from tools.native_movement.upper_link import World, audit_transfer

PROJECT = Path(__file__).resolve().parents[2]
TAG = 'opposed_stair_bridge'


def require(value, message):
    if not value: raise ValueError(message)


def read(path): return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(path,value): Path(path).write_text(json.dumps(value,indent=2,allow_nan=False)+'\n')


def point(room, rotation, u, v, z):
    lo,hi=room['bounds']
    return [[lo[0]+u,lo[1]+v,z], [hi[0]-v,lo[1]+u,z],
            [hi[0]-u,hi[1]-v,z], [lo[0]+v,hi[1]-u,z]][rotation]


def local_box(room,rotation,u0,v0,u1,v1,z0,z1):
    a=point(room,rotation,u0,v0,z0);b=point(room,rotation,u1,v1,z1)
    return [[min(a[i],b[i]) for i in range(3)],[max(a[i],b[i]) for i in range(3)]]


def contains(bounds,p): return all(bounds[0][i]<=p[i]<=bounds[1][i] for i in range(3))


def room_contract(room,rotation,height):
    """Declare geometry from the named contract, then test the compiled world independently."""
    require(rotation in range(4) and type(rotation) is int and height in (128,192),'Unsupported motif rotation/height')
    lo,hi=room['bounds']; w=hi[rotation%2]-lo[rotation%2];d=hi[1-rotation%2]-lo[1-rotation%2]
    require(lo[2]==0 and w>=640 and d>=(768 if height==192 else 640) and hi[2]>=height+56,'Motif room outside common-floor/headroom scope')
    end=64+height//16*36; middle=w//2
    require(w%2==0 and end+160<=d-64,'Deck/stair construction leaves insufficient perimeter')
    to_world=lambda u,v,z:point(room,rotation,u,v,z)
    decks=[local_box(room,rotation,64,end,middle-80,end+160,height-24,height),
           local_box(room,rotation,middle-80,min(end,d-end-160),middle+80,max(end+160,d-end),height-24,height),
           local_box(room,rotation,middle+80,d-end-160,w-64,d-end,height-24,height)]
    stairs=[]
    for side in ('southwest','northeast'):
        boxes=[]
        for i in range(height//16):
            coords=(64,64+36*i,192,64+36*(i+1)) if side=='southwest' else (w-192,d-64-36*(i+1),w-64,d-64-36*i)
            boxes.append(local_box(room,rotation,*coords,0,16*(i+1)))
        start=to_world(128,48,0) if side=='southwest' else to_world(w-128,d-48,0)
        finish=to_world(128,end-18,height) if side=='southwest' else to_world(w-128,d-end+18,height)
        axis=next(i for i in range(2) if start[i]!=finish[i]);direction=1 if finish[axis]>start[axis] else -1
        stairs.append({'label':side,'axis':axis,'ascending_direction':direction,'ground_z':0,
                       'height':height,'entry':start,'top':finish,'treads':[{'id':i+1,'bounds':b} for i,b in enumerate(boxes)]})
    upper=[to_world(128,end+80,height),to_world(middle,end+80,height),
           to_world(middle,d-end-80,height),to_world(w-128,d-end-80,height)]
    floor=[to_world(128,32,0),to_world(middle,32,0),to_world(middle,d-32,0),to_world(w-128,d-32,0)]
    return {'room':room['id'],'name':room['name'],'room_bounds':room['bounds'],'rotation':rotation,
            'local_width':w,'local_depth':d,'height':height,'stair_end':end,'decks':decks,'stairs':stairs,
            'upper_centerline':upper,'lower_centerline':floor,
            'complete_upper_route_waypoints':[floor[0],upper[0],*upper[1:],floor[-1]],
            'route_scope':'Opposite ordinary floor approaches through both stairs and the joined upper deck; lower aisle separately closes a static geometric cycle. No native path or continuous foot-contact claim.'}


def source_contracts(layout,report):
    validate_report(layout,report)
    player=report['analysis']['navigation']['player'];profile=player.get('profile',{})
    require(report.get('game')=='quake2' and report.get('mode') in ('deathmatch','duel') and
            profile.get('game')=='quake2' and profile.get('hull_mins')==[-16,-16,-24] and profile.get('hull_maxs')==[16,16,32],
            'Typed report is outside stock Q2 MP standing-hull scope')
    require(re.fullmatch('[0-9a-f]{64}',layout.get('layout_sha256','')),'Canonical identity is not a SHA-256')
    require(not layout.get('mechanisms') and not layout.get('ramps'),'AABB motif audit excludes mechanisms and ramps')
    require(0<len(layout['rooms'])<=32 and len(layout['solids'])<=4096,'Source geometry budget exceeded')
    require(len({s['id'] for s in layout['solids']})==len(layout['solids']),'Duplicate source solid IDs')
    for s in layout['solids']:
        require(s.get('contents')=='solid' and s.get('ownership') in (0,1) and not s.get('owner') and
                len(s['bounds'])==2 and all(type(v) is int for b in s['bounds'] for v in b) and
                all(-65536<=s['bounds'][0][i]<s['bounds'][1][i]<=65536 for i in range(3)),
                'Source architecture is not bounded unowned static SOLID AABB geometry')
    contracts=[]
    for room in layout['rooms']:
        if TAG not in room['tags']:continue
        rotations=[int(tag[-1]) for tag in room['tags'] if re.fullmatch('motif_rotation_[0-3]',tag)]
        require(len(rotations)==1,'Tagged room needs exactly one rotation')
        own=[s for s in layout['solids'] if all(contains(room['bounds'],corner) for corner in product(*zip(*s['bounds'])))]
        treads=[s for s in own if s['purpose']==2]
        require(treads,'Tagged bridge has no actual source stair treads')
        height=max(s['bounds'][1][2] for s in treads)
        contract=room_contract(room,rotations[0],height)
        used=[]
        for stair in contract['stairs']:
            for tread in stair['treads']:
                found=[s for s in treads if s['bounds']==tread['bounds']]
                require(len(found)==1,'Actual opposed stair is missing, duplicated or changed')
                tread['id']=found[0]['id'];used.append(found[0]['id'])
        require(set(used)=={s['id'] for s in treads},'Unexpected additional stairs in the bridge room')
        deck_ids=[]
        for bounds in contract['decks']:
            found=[s for s in own if s['purpose']==1 and s['bounds']==bounds]
            require(len(found)==1,'Actual Z deck is missing, duplicated or disconnected')
            deck_ids.append(found[0]['id'])
        contract['deck_solid_ids']=deck_ids;contracts.append(contract)
    require(contracts,'No opposed-stair bridge is present; absence is not a passed motif audit')
    return contracts


def audit_stair(world,stair):
    axis=stair['axis'];cross=1-axis; treads=stair['treads']
    require(len(treads)==stair['height']//16 and len(treads) in (8,12),'Expected complete128/192 stair')
    limits=[treads[0]['bounds'][0][cross],treads[0]['bounds'][1][cross]]
    require(limits[1]-limits[0]==128,'Stair width differs')
    caps=[];motions=[];source=list(stair['entry'])
    start_bounds=[source[:2],source[:2]];start_bounds[0]=start_bounds[0].copy();start_bounds[1]=start_bounds[1].copy()
    start_bounds[0][cross]=limits[0]+16;start_bounds[1][cross]=limits[1]-16
    entry=world.flat_region(start_bounds,0)
    for i,t in enumerate(treads):
        lo,hi=t['bounds']
        require(hi[axis]-lo[axis]==36 and [lo[cross],hi[cross]]==limits and lo[2]==0 and hi[2]==16*(i+1),'Stair cap dimensions differ')
        if i:
            previous=treads[i-1]['bounds'];direction=stair['ascending_direction']
            require(lo[axis]==previous[1][axis] if direction==1 else hi[axis]==previous[0][axis],'Non-adjacent stair treads')
        target=[(lo[j]+hi[j])/2 for j in range(2)]+[hi[2]]
        center_bounds=[[lo[j]+16 for j in range(2)],[hi[j]-16 for j in range(2)]]
        caps.append({'solid':t['id'],'feet':target,**world.flat_region(center_bounds,hi[2])})
        raised=[*source[:2],source[2]+18];advanced=[*target[:2],raised[2]]
        legs=[world.strip_sweep(a,b,axis,limits) for a,b in ((source,raised),(raised,advanced),(advanced,target))]
        motions.append({'from_feet':source,'to_feet':target,'legs':legs,'clear':all(x['clear'] for x in legs)})
        source=target
    return {'label':stair['label'],'success':entry['fully_supported'] and entry['clear'] and all(c['fully_supported'] and c['clear'] for c in caps) and all(m['clear'] for m in motions),
            'ground_entry':entry,'full_tread_caps':caps,'fixed18_raise_advance_lower':motions,
            'scope':'Full128-wide source caps and reversible continuous collision-free box translations. The raised advances are constructions, not native substeps or continuous ground contact.'}


def point_ray(world,a,b):
    """Exact point-segment intersection with open interiors of compiled AABBs."""
    hits=[]
    for ident,_,shape in world.world:
        lo,hi=shape.bounds;enter=F(0);leave=F(1)
        for axis in range(3):
            x=F(a[axis]);delta=F(b[axis])-x
            if not delta:
                if not lo[axis]<x<hi[axis]:enter,leave=F(1),F(0);break
            else:
                first=(lo[axis]-x)/delta;last=(hi[axis]-x)/delta
                if first>last:first,last=last,first
                enter=max(enter,first);leave=min(leave,last)
        if enter<leave:hits.append(ident)
    return {'visible':not hits,'blocking_brushes':hits,'from_eye':a,'to_eye':b,
            'scope':'Fixed exact standing-eye point ray against static SOLID AABBs; not movement, safety or exposure over time.'}


def audit_room(world,contract):
    upper=contract['upper_centerline'];floor=contract['lower_centerline'];height=contract['height']
    stairs=[audit_stair(world,s) for s in contract['stairs']]
    strips=[]
    for i,(a,b) in enumerate(zip(upper,upper[1:])):
        if a==b:
            # Coincident turns still need a full160 square; no zero-length path claim.
            bounds=[[a[0]-64,a[1]-64],[a[0]+64,a[1]+64]];r=world.flat_region(bounds,height)
            strips.append({'segment':i,'success':r['clear'] and r['fully_supported'],'coincident_turn_region':r})
        else:strips.append({'segment':i,**audit_transfer(world,a,b)})
    turns=[]
    for p in upper[1:3]:
        r=world.flat_region([[p[0]-64,p[1]-64],[p[0]+64,p[1]+64]],height)
        turns.append({'feet':p,'success':r['clear'] and r['fully_supported'],**r})
    joins=[]
    for s,p in zip(contract['stairs'],(upper[0],upper[-1])):
        axis=s['axis'];cross=1-axis;top=s['top'];limits=[s['treads'][-1]['bounds'][0][cross],s['treads'][-1]['bounds'][1][cross]]
        lo=[min(top[i],p[i]) for i in range(2)];hi=[max(top[i],p[i]) for i in range(2)]
        lo[cross],hi[cross]=limits[0]+16,limits[1]-16
        r=world.flat_region([lo,hi],height)
        joins.append({'from_feet':top,'to_feet':p,'full_width':128,'success':r['clear'] and r['fully_supported'],**r})
    lower_aisle=audit_transfer(world,floor[1],floor[2])
    ground_chords=[]
    for a,b in ((floor[0],floor[1]),(floor[2],floor[3]),(floor[0],contract['stairs'][0]['entry']),(floor[3],contract['stairs'][1]['entry'])):
        r=world.flat_chord(a,b,0);ground_chords.append({'from_feet':a,'to_feet':b,'success':r['clear'] and r['fully_supported'],**r})
    route_stations=[{'id':f'lower-{i}','feet':p} for i,p in enumerate(floor)]+[{'id':f'upper-{i}','feet':p} for i,p in enumerate(upper)]
    rays=[{'a':a['id'],'b':b['id'],**point_ray(world,[*a['feet'][:2],a['feet'][2]+46],[*b['feet'][:2],b['feet'][2]+46])} for a,b in combinations(route_stations,2)]
    checks=stairs+strips+turns+joins+[lower_aisle]+ground_chords
    return {'room':contract['room'],'name':contract['name'],'rotation':contract['rotation'],'height':height,
            'success':all(c['success'] for c in checks),'stairs':stairs,'upper160_strips':strips,'upper160_turn_squares':turns,
            'stair128_to_deck_joins':joins,'lower160_aisle':lower_aisle,'ordinary_ground_connectors':ground_chords,
            'route_eye_stations':route_stations,'fixed_route_eye_rays':rays,'native_traversal_tested':False}


def native_station_class(placement):
    role=placement['role'];variant=placement.get('variant',0)
    require(type(variant) is int and variant>=0,'Invalid typed station variant')
    if role==4:
        weapons=('weapon_shotgun','weapon_machinegun','weapon_chaingun','weapon_grenadelauncher',
                 'weapon_rocketlauncher','weapon_hyperblaster','weapon_railgun')
        require(variant<len(weapons),'Unsupported Q2 weapon station variant')
        return weapons[variant]
    require(role in (1,5,6),'Unsupported local gameplay station role')
    return {1:'info_player_deathmatch',5:'item_health',6:'item_armor_combat'}[role]


def bind_native_station(native,placement):
    expected=native_station_class(placement)
    matches=[e for e in native if 'origin' in e and list(vec(e['origin']))==placement['origin'] and e['classname']!='light']
    require(len(matches)==1 and matches[0]['classname']==expected,'Expected one exact stock class at the authored station')
    return matches[0]['classname']


def entity_stations(world,layout,report,contract):
    graph=report['analysis']['navigation']['player']['graph'];nodes={n['id']:n for n in graph['nodes']}
    bindings={b['placement']:b['node'] for b in graph['placement_bindings']}
    require(len(nodes)==len(graph['nodes']) and len(bindings)==len(graph['placement_bindings']),'Duplicate navigation bindings')
    native=world.point_entities;rows=[]
    for p in layout['placements']:
        if p['role'] not in (1,4,5,6) or not contains(contract['room_bounds'],p['origin']):continue
        feet=[*p['origin'][:2],p['origin'][2]-24]
        require(feet[2] in (0,contract['height']),'Same-room entity uses an undeclared floor')
        lo,hi=contract['room_bounds']
        require(all(lo[i]<=feet[i]-16 and feet[i]+16<=hi[i] for i in range(2)) and feet[2]+56<=hi[2], 'Same-room entity full hull crosses its room')
        physical=world.flat_chord(feet,feet,feet[2])
        node=nodes.get(bindings.get(p['id']));require(node is not None,'Entity lacks navigation binding')
        a=node['area'];bound=(a['slope'] is None and a['floor_z']==feet[2] and a['ceiling_z']>=feet[2]+56 and
                             a['min_x']<=feet[0]<=a['max_x'] and a['min_y']<=feet[1]<=a['max_y'])
        classname=bind_native_station(native,p)
        rows.append({'placement':p['id'],'role':p['role'],'authored_origin':p['origin'],'feet':feet,
                     'floor':'ground' if feet[2]==0 else 'raised','native_classname':classname,
                     'bound_navigation_node':node['id'],'node_contains_actual_station':bound,
                     'success':physical['clear'] and physical['fully_supported'] and bound,'physical':physical})
    rays=[{'a':a['placement'],'b':b['placement'],**point_ray(world,[*a['feet'][:2],a['feet'][2]+46],[*b['feet'][:2],b['feet'][2]+46])} for a,b in combinations(rows,2)]
    return {'stations':rows,'ground_stations':sum(r['floor']=='ground' for r in rows),'raised_stations':sum(r['floor']=='raised' for r in rows),
            'success':all(r['success'] for r in rows),'fixed_entity_eye_rays':rays,
            'scope':'Actual authored room/floor stations and local typed-node binding, compiled full hull/support and separate point rays. No native pickup/spawn or playable route to every station is inferred.'}


def audit(layout_path,report_path,map_path,bsp_path):
    paths=[Path(p).resolve(strict=True) for p in (layout_path,report_path,map_path,bsp_path)]
    layout,report=read(paths[0]),read(paths[1]);contracts=source_contracts(layout,report)
    bsp=Bsp(paths[3].read_bytes());require(not bsp.file_issues and not bsp.entity_issues,'BSP parsing failed')
    require(all(e.get('classname') in ('worldspawn','light','info_player_deathmatch') or
                e.get('classname','').startswith(('weapon_','item_','ammo_')) for e in bsp.entities),
            'Compiled dynamic/unknown entities are outside world-only motif scope')
    world=World(paths[3]);rows=[]
    for c in contracts:
        r=audit_room(world,c);r['entity_stations']=entity_stations(world,layout,report,c)
        r['success'] &= r['entity_stations']['success'];rows.append(r)
    return {'schema_version':1,'kind':'opposed-stair-bridge-static-audit','layout_sha256':layout['layout_sha256'],
            'inputs':[{'path':p.relative_to(PROJECT).as_posix(),'sha256':sha(p)} for p in paths],
            'auditor_sha256':sha(Path(__file__)),'raw_model0_all_solid':True,'compiled_AABB_world_brushes':len(world.world),
            'contracts':contracts,'rooms':rows,'motif_count':len(rows),'success':all(r['success'] for r in rows),
            'limits':['Full-width exact compiled support and standing clearances are static constructions.',
                      'The eighteen-unit raised advances are not native movement or continuous foot contact.',
                      'Same-room station binding and fixed eye rays do not certify every resource journey or multiplayer balance.']}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('layout','report','map','bsp','output'):p.add_argument('--'+key,type=Path,required=True)
    args=p.parse_args();result=audit(args.layout,args.report,args.map,args.bsp)
    require(args.output.resolve().is_relative_to(PROJECT),'Keep study output inside project')
    require(not args.output.exists(),'Never overwrite a retained audit result')
    args.output.parent.mkdir(parents=True,exist_ok=True);save(args.output,result)
    print(json.dumps({'success':result['success'],'motif_count':result['motif_count'],'output':str(args.output)}))


if __name__=='__main__':main()
