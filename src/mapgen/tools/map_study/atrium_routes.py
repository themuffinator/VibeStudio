"""Frozen three-tier atrium queries in The Killing Machine; no native run."""
from pathlib import Path
from fractions import Fraction as F
import argparse, hashlib, json

from tools.map_study.raised_resource_circuit import load, static_leg, clearance_leg
from tools.map_study.teleporter_arrivals import box, origin, point_ray
from tools.map_study.interlocking_topology import numeric

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'out/atrium-route-study'
PROTOCOL_SHA='b98d35c12fe4e4df237ad1e3edab6680dd3edeb4c17d766517cf8de7f9058de3'


def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def step_pair(world,liquids,envelopes,a,b):
    """Bounded up/retreat/down construction, distinct from engine stepping.

    All endpoints require the whole32x32 footprint. Intermediate raised travel
    admits actual ground within16 below it, never an empty unsupported gap.
    """
    if b[2]-a[2]!=16 or (a[0]!=b[0] and a[1]!=b[1]):
        raise ValueError('Only frozen axial sixteen-unit stair changes')
    raised=[a[0],a[1],b[2]]
    first=static_leg(world,liquids,envelopes,a,a)
    last=static_leg(world,liquids,envelopes,b,b)
    rise=clearance_leg(world,liquids,envelopes,a,raised)
    advance=static_leg(world,liquids,envelopes,raised,b,16)
    retreat=static_leg(world,liquids,envelopes,b,raised,16)
    descent=clearance_leg(world,liquids,envelopes,raised,a)
    up=first['pass'] and last['pass'] and rise['clear'] and advance['pass']
    down=first['pass'] and last['pass'] and retreat['pass'] and descent['clear']
    return {'kind':'sixteen_unit_step_construction','from_feet':a,'to_feet':b,
            'lower_station':first,'upper_station':last,'up_vertical':rise,'up_advance':advance,
            'down_retreat':retreat,'down_vertical':descent,'forward_pass':up,'reverse_pass':down,'pass':up and down,
            'native_step_or_continuous_grounded_motion_certified':False}


def paired_leg(world,liquids,envelopes,a,b):
    if a[2]!=b[2]:return step_pair(world,liquids,envelopes,a,b)
    forward=static_leg(world,liquids,envelopes,a,b)
    reverse=static_leg(world,liquids,envelopes,b,a)
    return {'kind':'flat','from_feet':a,'to_feet':b,'forward':forward,'reverse':reverse,
            'forward_pass':forward['pass'],'reverse_pass':reverse['pass'],'pass':forward['pass'] and reverse['pass']}


def load_reference(p):
    for pin in p['source_pins']:
        path=Path(pin['path'])
        if sha(path)!=pin['sha256'] or path.stat().st_size!=pin['bytes']:raise ValueError('Reference source pin differs')
    bsp,world,liquids,envelopes=load(p)
    for e in envelopes:
        entity=bsp.entities[e['entity']]
        if origin(entity)!=[0,0,0]:raise ValueError('Unreviewed brush-entity translation')
        if entity['classname']=='func_door':
            if entity.get('angle')!='270':raise ValueError('Door differs from inspected Y-only movement')
            extent=e['bounds'][1][1]-e['bounds'][0][1]
            e['bounds'][0][1]-=extent;e['bounds'][1][1]+=extent
            e['contract']='Authored Y-only door bounds conservatively extended a full model depth both directions; never executed'
        elif entity['classname']!='func_plat':raise ValueError('Unreviewed mover class')
    pads={}
    for eid,e in enumerate(bsp.entities):
        kind=e.get('classname')
        if kind in ('misc_teleporter','misc_teleporter_dest'):
            o=origin(e)
            pads[-eid-1]=box([o[j]+(-32,-32,-24)[j] for j in range(3)],[o[j]+(32,32,-16)[j] for j in range(3)])
            if kind=='misc_teleporter':
                envelopes.append({'entity':f'stock-source-trigger:{eid}','class':kind,
                    'bounds':[[o[j]+(-8,-8,8)[j] for j in range(3)],[o[j]+(8,8,24)[j] for j in range(3)]],
                    'contract':'Conservative exclusion of actual stock-generated source trigger bounds'})
    return bsp,{**world,**pads},liquids,envelopes,pads


def build_audit():
    path=OUT/'protocol-v1.json'
    if sha(path)!=PROTOCOL_SHA:raise ValueError('Frozen atrium protocol changed')
    p=json.loads(path.read_text(encoding='utf-8'))
    if sha(OUT/'source-inspection.json')!=p['selection']['source_inspection_sha256']:raise ValueError('Retained pre-protocol inspection differs')
    bsp,world,liquids,envelopes,pads=load_reference(p)
    paths={}
    for name,spec in p['paths'].items():
        points=spec['points']
        if spec['kind']!='stairs' and any(a[2]!=b[2] for a,b in zip(points,points[1:])):raise ValueError('Unexpected nonflat path')
        legs=[paired_leg(world,liquids,envelopes,a,b) for a,b in zip(points,points[1:])]
        paths[name]={'kind':spec['kind'],'points':points,'legs':legs,
            'forward_pass':all(l['forward_pass'] for l in legs),'reverse_pass':all(l['reverse_pass'] for l in legs),
            'pass':all(l['pass'] for l in legs),'xy_length':sum(abs(a[0]-b[0])+abs(a[1]-b[1]) for a,b in zip(points,points[1:])),
            'rise':points[-1][2]-points[0][2],'step_changes':sum(a[2]!=b[2] for a,b in zip(points,points[1:]))}
    stations={}
    for name,s in p['stations'].items():
        if paths[s['path']]['points'][s['waypoint']]!=s['feet']:raise ValueError('Station is not bound to declared route waypoint')
        stations[name]={**s,'standing':static_leg(world,liquids,envelopes,s['feet'],s['feet']),
                        'bound_complete_path_pass':paths[s['path']]['pass']}
    rays=[]
    for left,right in p['ray_pairs']:
        for source,target in [(left,right),(right,left)]:
            a=[*stations[source]['feet']];b=[*stations[target]['feet']];a[2]+=46;b[2]+=46
            rays.append({'source':source,'target':target,
                'source_supported':stations[source]['standing']['pass'],'target_supported':stations[target]['standing']['pass'],
                'source_bound_complete_path_pass':stations[source]['bound_complete_path_pass'],'target_bound_complete_path_pass':stations[target]['bound_complete_path_pass'],
                **point_ray(bsp,pads,envelopes,a,b,p['ray_contract']['world_mask'])})
    v=p['vertical_projection_candidate']
    vertical=clearance_leg(world,liquids,envelopes,v['from_feet'],v['to_feet'])
    lower=paths['lower_between_stair_starts']
    stair_bindings={name:{'stair_start':paths[name]['points'][0],
        'matches_lower_endpoint':paths[name]['points'][0]==lower['points'][i],
        'both_paths_pass':paths[name]['pass'] and lower['pass']}
        for name,i in [('west_bent_ascent',0),('east_straight_ascent',-1)]}
    if not all(x['matches_lower_endpoint'] for x in stair_bindings.values()):raise ValueError('Stair lower endpoint identity differs')
    low=paths['lower_overlap'];high=paths['upper_overlap']
    if [x[:2] for x in low['points']]!=[x[:2] for x in high['points']]:raise ValueError('Shared XY differs')
    for name,main in [('lower_overlap','lower_between_stair_starts'),('upper_overlap','high_crossing')]:
        points=paths[name]['points'];mainpoints=paths[main]['points']
        if not any(mainpoints[i:i+2]==points for i in range(len(mainpoints)-1)):raise ValueError('Overlap not an exact main-route segment')
    return numeric({'schema_version':1,'id':p['id'],'protocol_sha256':PROTOCOL_SHA,'source_bsp_sha256':bsp.sha256,
        'world_blocking_brushes':len(world)-len(pads),'stock_pad_solids':{k:s.bounds for k,s in pads.items()},
        'stock_pad_id_contract':'Negative solid ID equals minus(entity index+1); nonnegative IDs are actual BSP world brushes',
        'liquid_brushes':len(liquids),'mechanism_exclusion_envelopes':envelopes,'paths':paths,'stations':stations,
        'stairs_to_lower_bindings':stair_bindings,'overlap':{**p['overlap'],'xy_length':64,'full_swept_xy':[[144,0],[240,32]],
            'both_subroutes_pass':low['pass'] and high['pass'],'both_main_routes_pass':lower['pass'] and paths['high_crossing']['pass']},
        'vertical_projection_candidate':vertical,'rays':rays,
        'authored_resource_context':[dict(bsp.entities[i],entity_id=i) for i in p['authored_resource_context']],
        'upper288_to_stair128_connection_certified':False,'native_or_pickup_tested':False,
        'limits':['Two inspected maps, one fixed area audited; every failed waypoint retained',
            'The two stairs end at128; the high crossing is288. These observations are not a connected opposed-stair Z-bridge precedent',
            'Geometric step constructions prove full support and clearance in their declared sequences, not native stepping or motion time',
            'Fixed rays are point exposure observations, not whole-map visibility, firing safety or height advantage',
            'No archive geometry/assets copied into generation; no human match or model training']})


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--verify',action='store_true');args=parser.parse_args()
    a=build_audit();path=OUT/'audit.json'
    if args.verify:
        if json.loads(path.read_text(encoding='utf-8'))!=a:raise ValueError('Full audit rederivation differs')
    else:
        if path.exists():raise ValueError('Retained first audit exists; use --verify')
        path.write_text(json.dumps(a,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'paths':{k:{'pass':v['pass'],'xy_length':v['xy_length'],'rise':v['rise']} for k,v in a['paths'].items()},
        'stations':{k:v['standing']['pass'] for k,v in a['stations'].items()},'overlap':a['overlap'],
        'vertical_clear':a['vertical_projection_candidate']['clear'],'clear_rays':[(r['source'],r['target']) for r in a['rays'] if r['clear_static_ray']]},indent=2))


if __name__=='__main__':main()
