"""A fixed, bounded geometric resource-circuit attempt in archived The Rage.

Exact collision/support witnesses are distinct from native movement and pickup
contact. Exploratory source inspection precedes the hash-frozen protocol.
"""
from __future__ import annotations

from fractions import Fraction as F
import hashlib
import json
import math
from itertools import product
from pathlib import Path

from tools.map_study.binary import Bsp, read_bytes
from tools.map_recipes.geometry import convex_polyhedron, model_brushes, intersects_swept_aabb
from tools.map_study.interlocking_topology import check_segment, hull, numeric

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'out/raised-resource-circuit'
PROTOCOL_SHA = '0c7f78bd452360cb7ea4cdcdf5c0fc6ccc5e819c32fd60adb6a12c6449062bb2'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(protocol):
    bsp = Bsp(read_bytes(Path(protocol['bsp_path'])))
    if bsp.sha256 != protocol['bsp_sha256']:
        raise ValueError('Archive BSP differs from pinned source')
    world, liquids = {}, {}
    for bid in model_brushes(bsp):
        first,count,contents = bsp.brushes[bid]
        if contents & (protocol['collision_mask'] | 56):
            shape = convex_polyhedron([bsp.planes[bsp.brushsides[i][0]] for i in range(first,first+count)])
            if contents & protocol['collision_mask']:
                world[bid] = shape
            if contents & 56:
                liquids[bid] = shape
    envelopes = []
    for eid,entity in enumerate(bsp.entities):
        model = entity.get('model','')
        if not model.startswith('*'):
            continue
        mid = int(model[1:]); row = bsp.models[mid]
        low,high = list(row[:3]),list(row[3:6])
        if entity.get('classname') == 'func_plat':
            extent = max(abs(float(entity['height'])), high[2]-low[2])
            low[2] -= extent; high[2] += extent
        envelopes.append({'entity':eid,'class':entity['classname'],'model':mid,'bounds':[low,high],
            'contract':'Authored submodel bounds; plats conservatively expanded both vertical directions by max(authored height, model height). No mover executed.'})
    return bsp,world,liquids,envelopes


def swept(world,a,b):
    delta = tuple(F(b[i])-F(a[i]) for i in range(3))
    return [bid for bid,shape in world.items() if intersects_swept_aabb(shape,*hull(a),delta)]


def envelope_checks(envelopes,a,b,band=0):
    low=[min(a[i],b[i])+(-16,-16,-band)[i] for i in range(3)]
    high=[max(a[i],b[i])+(16,16,56)[i] for i in range(3)]
    return [{'entity':e['entity'],'strictly_disjoint':any(high[i] < e['bounds'][0][i] or e['bounds'][1][i] < low[i] for i in range(3))} for e in envelopes]


def static_leg(world,liquids,envelopes,a,b,band=0):
    if band:
        support=step_ground_band(world,a,b,band)
        blockers=swept(world,a,b)
        low,high=hull(a)
        low=(*low[:2],low[2]-band)
        delta=tuple(F(b[i])-F(a[i]) for i in range(3))
        wet=[bid for bid,shape in liquids.items() if intersects_swept_aabb(shape,low,high,delta)]
        result={'from_feet':a,'to_feet':b,'world_blockers':blockers,'liquid_brushes':wet,
                'support':support,'pass':not blockers and not wet and support['pass']}
    else:
        result=check_segment(world,liquids,a,b)
    result['mechanism_exclusions']=envelope_checks(envelopes,a,b,band)
    result['pass'] = result['pass'] and all(x['strictly_disjoint'] for x in result['mechanism_exclusions'])
    return result


def step_ground_band(world,a,b,band):
    """Exact cap-rectangle union for the frozen sixteen-unit stair construction.

    The older interlocking study deliberately rejects bands this deep. Keep its
    shallow-ground certificate unchanged: this separate contract does not claim
    continuous foot contact or any native step trajectory.
    """
    if band != 16 or a[2] != b[2] or (a[0] != b[0] and a[1] != b[1]):
        raise ValueError('Only flat axial advances in the declared sixteen-unit construction')
    low=tuple(F(min(a[i],b[i])-16) for i in range(2))
    high=tuple(F(max(a[i],b[i])+16) for i in range(2))
    cuts=[{low[i],high[i]} for i in range(2)];caps=[]
    for bid,solid in world.items():
        lo=tuple(max(low[i],solid.bounds[0][i]) for i in range(2))
        hi=tuple(min(high[i],solid.bounds[1][i]) for i in range(2))
        if any(lo[i]>=hi[i] for i in range(2)):continue
        for plane in solid.planes:
            if plane[2]<=0:continue
            vertices=[(*xy,(plane[3]-plane[0]*xy[0]-plane[1]*xy[1])/plane[2]) for xy in product(*zip(lo,hi))]
            if all(a[2]-band<=v[2]<=a[2] and all(sum(p[i]*v[i] for i in range(3))<=p[3] for p in solid.planes) for v in vertices):
                caps.append((bid,lo,hi))
                for i in range(2):cuts[i].update((lo[i],hi[i]))
                break
    axes=[sorted(c) for c in cuts];cells=[];used=set()
    for x0,x1 in zip(axes[0],axes[0][1:]):
        for y0,y1 in zip(axes[1],axes[1][1:]):
            found=[bid for bid,lo,hi in caps if lo[0]<=x0 and x1<=hi[0] and lo[1]<=y0 and y1<=hi[1]]
            if not found:return {'pass':False,'uncovered_cell':[x0,y0,x1,y1]}
            used.update(found);cells.append({'bounds':[[x0,y0],[x1,y1]],'brushes':found})
    return {'pass':True,'support_brushes':sorted(used),'cells':cells,'floor_height_band':[a[2]-band,a[2]],
            'contract':'Complete projected actual ground within sixteen units below the raised clear hull envelope; native step and continuous contact untested.'}


def clearance_leg(world,liquids,envelopes,a,b):
    checks=envelope_checks(envelopes,a,b)
    blockers=swept(world,a,b); wet=swept(liquids,a,b)
    return {'from_feet':a,'to_feet':b,'world_blockers':blockers,'liquid_brushes':wet,
            'mechanism_exclusions':checks,'clear':not blockers and not wet and all(x['strictly_disjoint'] for x in checks),
            'continuous_floor_support_certified':False}


def audit_path(world,liquids,envelopes,spec):
    kind=spec['kind']
    if kind=='flat':
        points=spec['points']
        legs=[static_leg(world,liquids,envelopes,a,b) for a,b in zip(points,points[1:])]
        result={'start':points[0],'end':points[-1],'legs':legs,'pass':all(x['pass'] for x in legs)}
    elif kind=='step_construction':
        points=spec['stations']; transitions=[]
        for a,b in zip(points,points[1:]):
            rise=b[2]-a[2]
            if not 0 < rise <= spec['max_rise'] or sum(a[i]!=b[i] for i in range(2)) != 1:
                raise ValueError('Step outside frozen geometric contract')
            raised=[a[0],a[1],b[2]]
            source=static_leg(world,liquids,envelopes,a,a)
            target=static_leg(world,liquids,envelopes,b,b)
            up=clearance_leg(world,liquids,envelopes,a,raised)
            advance=static_leg(world,liquids,envelopes,raised,b,rise)
            transitions.append({'source_station':source,'target_station':target,'raise':up,'advance':advance,
                                'rise':rise,'pass':source['pass'] and target['pass'] and up['clear'] and advance['pass']})
        result={'start':points[0],'end':points[-1],'transitions':transitions,'pass':all(x['pass'] for x in transitions),
                'native_or_ordinary_movement_certified':False}
    elif kind=='drop_candidate':
        start,takeoff,corner,landing=[spec[k] for k in ['start','takeoff','clearance_corner','landing']]
        approach=static_leg(world,liquids,envelopes,start,takeoff)
        landing_check=static_leg(world,liquids,envelopes,landing,landing)
        lateral=clearance_leg(world,liquids,envelopes,takeoff,corner)
        descent=clearance_leg(world,liquids,envelopes,corner,landing)
        result={'start':start,'end':landing,'approach':approach,'landing':landing_check,
                'unsupported_lateral_envelope':lateral,'vertical_envelope':descent,
                'pass':approach['pass'] and landing_check['pass'] and lateral['clear'] and descent['clear'],
                'native_or_ordinary_movement_certified':False}
        points=[start,takeoff,corner,landing]
    elif kind=='unsupported_ascent':
        points=spec['points'];a,b=points
        result={'start':a,'end':b,'clearance':clearance_leg(world,liquids,envelopes,a,b),
                'source_station':static_leg(world,liquids,envelopes,a,a),
                'target_station':static_leg(world,liquids,envelopes,b,b),
                'pass':False,'reason':'No supported flat, stepped or ramp path is declared for this straight segment.'}
    else:
        raise ValueError('Unknown route contract')
    result.update({'kind':kind,'xy_polyline_length':sum(math.hypot(b[0]-a[0],b[1]-a[1]) for a,b in zip(points,points[1:])),
                   'xy_manhattan_sum':sum(sum(abs(b[i]-a[i]) for i in range(2)) for a,b in zip(points,points[1:])),
                   'vertical_absolute_sum':sum(abs(b[2]-a[2]) for a,b in zip(points,points[1:]))})
    return result


def build_audit():
    protocol_path=OUT/'protocol.json'
    if sha(protocol_path) != PROTOCOL_SHA:
        raise ValueError('Protocol changed; retain a separate declared study revision instead of overwriting its evidence')
    protocol=json.loads(protocol_path.read_text(encoding='utf-8'))
    bsp,world,liquids,envelopes=load(protocol)
    paths={name:audit_path(world,liquids,envelopes,spec) for name,spec in protocol['paths'].items()}
    stations={name:static_leg(world,liquids,envelopes,p,p) for name,p in protocol['named_stations'].items()}
    circuit=[]
    for entry in protocol['circuit']:
        name,_,reverse=entry.partition(':')
        path=paths[name]
        start,end=path['start'],path['end']
        if reverse:
            if reverse!='reverse' or path['kind']!='flat':raise ValueError('Unsupported reversal')
            start,end=end,start
        circuit.append({'path':entry,'start':start,'end':end,'pass':path['pass'],
                        'xy_polyline_length':path['xy_polyline_length'],'vertical_absolute_sum':path['vertical_absolute_sum']})
    joins=[a['end']==b['start'] for a,b in zip(circuit,circuit[1:]+circuit[:1])]
    if not all(joins):raise ValueError('Declared circuit endpoints do not connect exactly')
    graph={}
    for path_name,spec in protocol['paths'].items():
        if not paths[path_name]['pass']:continue
        points=spec.get('points',spec.get('stations',[]))
        for i,(a,b) in enumerate(zip(points,points[1:])):
            graph.setdefault(tuple(a),[]).append((tuple(b),path_name,i))
            if spec['kind']=='flat':graph.setdefault(tuple(b),[]).append((tuple(a),path_name,i))
    def resource_connection(point):
        queue=[(tuple(point),[])];seen=set();target=tuple(protocol['resource']['station_feet'])
        while queue:
            p,edges=queue.pop(0)
            if p==target:return edges
            if p in seen:continue
            seen.add(p)
            queue.extend((q,edges+[{'path':name,'edge':i}]) for q,name,i in graph.get(p,[]))
        return None
    bindings={}
    for name,point in protocol['named_stations'].items():
        matches=[]
        for path_name,spec in protocol['paths'].items():
            if not paths[path_name]['pass']:continue
            points=spec.get('points',spec.get('stations',[]))
            matches.extend({'path':path_name,'waypoint':i} for i,p in enumerate(points) if p==point)
        connection=resource_connection(point)
        bindings[name]={'station_pass':stations[name]['pass'],'passed_path_waypoint_matches':matches,
                        'connected_to_armor_by_measured_subroutes':connection is not None,
                        'directed_geometric_witness':connection}
    rays=[]
    for source_name in protocol['sightline_stations']:
        for target_name in protocol['sightline_target_stations']:
            if source_name==target_name:continue
            source=list(protocol['named_stations'][source_name]);target=list(protocol['named_stations'][target_name])
            source[2]+=protocol['eye_height_above_feet'];target[2]+=protocol['eye_height_above_feet']
            fraction=bsp.trace(source,target,mask=3)
            ray_envelopes=[{'entity':e['entity'],'strictly_disjoint':any(max(source[i],target[i]) < e['bounds'][0][i] or e['bounds'][1][i] < min(source[i],target[i]) for i in range(3))} for e in envelopes]
            rays.append({'source':source_name,'target':target_name,'source_eye':source,'target_eye':target,
                         'source_supported':stations[source_name]['pass'],'target_supported':stations[target_name]['pass'],
                         'world_fraction':fraction,'world_visible':fraction==1,
                         'mechanism_exclusions':ray_envelopes,
                         'scope':'Static world point ray; no facing, sound, splash, exposure time or game behavior.'})
    resource=protocol['resource'];entity=bsp.entities[resource['entity']]
    assert entity['classname']==resource['class'] and [float(x) for x in entity['origin'].split()]==resource['authored_origin']
    result={'schema_version':1,'bsp_path':protocol['bsp_path'],'bsp_sha256':bsp.sha256,'protocol_sha256':PROTOCOL_SHA,
            'world_blocking_brushes':len(world),'liquid_brushes':len(liquids),'mechanism_envelopes':envelopes,
            'resource':{**resource,'embedded_entity':entity,'authored_xy_station_check':stations['armor'],'settled_pickup_or_contact_tested':False},
            'paths':paths,'stations':stations,'circuit':{'segments':circuit,'exact_endpoint_joins':joins,
                'geometric_candidate_pass':all(p['pass'] for p in circuit),
                'xy_polyline_length':sum(p['xy_polyline_length'] for p in circuit),
                'vertical_absolute_sum':sum(p['vertical_absolute_sum'] for p in circuit),
                'ordinary_or_native_certified':False},'sightline_rays':rays,'station_path_bindings':bindings,
            'ray_contract':{'mask':3,'plane_comparison_tolerance':1e-5,'method':'Existing floating-point BSP-tree point segment clipping; not the exact standing-hull SAT contract.'},
            'native_tested':False,'gameplay_tested':False,'model_weights_trained':False}
    return numeric(result)


def main():
    result=build_audit()
    (OUT/'audit.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'blocking_brushes':result['world_blocking_brushes'],'liquids':result['liquid_brushes'],
                      'paths':{k:v['pass'] for k,v in result['paths'].items()},
                      'stations':{k:v['pass'] for k,v in result['stations'].items()},'circuit':result['circuit']['geometric_candidate_pass'],
                      'rays':[{'source':r['source'],'target':r['target'],'visible':r['world_visible']} for r in result['sightline_rays']]},indent=2))


if __name__=='__main__':
    main()
