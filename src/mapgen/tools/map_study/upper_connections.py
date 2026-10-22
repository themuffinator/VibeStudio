"""Frozen upper/lower inter-space route attempt in Powertrip's local BSP."""
from pathlib import Path
import hashlib,json

from tools.map_study.raised_resource_circuit import load,static_leg,clearance_leg
from tools.map_study.interlocking_topology import numeric

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'out/upper-connection-study'
PROTOCOL_SHA='2dc2998a4e5df9641ac975951b9bab7c440d94a5ac0c99d0925a12f35c6c90cd'


def build_audit():
    path=OUT/'protocol.json'
    if hashlib.sha256(path.read_bytes()).hexdigest()!=PROTOCOL_SHA:
        raise ValueError('Frozen route protocol changed; retain a separate revision instead')
    p=json.loads(path.read_text(encoding='utf-8'))
    bsp,world,liquids,envelopes=load(p)
    paths={}
    for name,spec in p['paths'].items():
        points=spec['points']
        legs=[static_leg(world,liquids,envelopes,a,b) for a,b in zip(points,points[1:])]
        paths[name]={'points':points,'legs':legs,'pass':all(x['pass'] for x in legs),
                     'xy_length':sum(abs(b[0]-a[0])+abs(b[1]-a[1]) for a,b in zip(points,points[1:])),
                     'contract':'Flat axial complete swept32x32x56 hull clearance and exact full-footprint support; no native trajectory.'}
    stations={name:static_leg(world,liquids,envelopes,point,point) for name,point in p['named_stations'].items()}
    bindings={}
    for name,point in p['named_stations'].items():
        expected='upper_west_to_east' if name.startswith('upper') else 'lower_west_to_east'
        matches=[i for i,x in enumerate(p['paths'][expected]['points']) if x==point]
        if len(matches)!=1:raise ValueError('Named station not uniquely bound to its frozen main path')
        bindings[name]={'main_path':expected,'waypoint':matches[0],'station_pass':stations[name]['pass'],'main_path_pass':paths[expected]['pass']}
    v=p['vertical_projection_shortcut']
    vertical=clearance_leg(world,liquids,envelopes,v['from_feet'],v['to_feet'])
    rays=[]
    for source in p['sightline_sources']:
        for target in p['sightline_targets']:
            if source==target:continue
            a=[*p['named_stations'][source]];b=[*p['named_stations'][target]]
            a[2]+=p['eye_height_above_feet'];b[2]+=p['eye_height_above_feet']
            exclusions=[{'entity':e['entity'],'strictly_disjoint':any(max(a[i],b[i])<e['bounds'][0][i] or e['bounds'][1][i]<min(a[i],b[i]) for i in range(3))} for e in envelopes]
            fraction=bsp.trace(a,b,mask=p['sightline_mask'])
            rays.append({'source':source,'target':target,'source_eye':a,'target_eye':b,'world_fraction':fraction,
                         'world_visible':fraction==1,'source_supported':stations[source]['pass'],'target_supported':stations[target]['pass'],
                         'mechanism_exclusions':exclusions})
    upper=p['paths']['upper_overlap']['points'];lower=p['paths']['lower_overlap']['points']
    assert [x[:2] for x in upper]==[x[:2] for x in lower]
    upper_main=p['paths']['upper_west_to_east']['points'];lower_main=p['paths']['lower_west_to_east']['points']
    assert upper[0] in upper_main and upper[1] in upper_main and lower_main[1][1:]==lower[0][1:] and lower_main[3][1:]==lower[1][1:]
    assert lower_main[1][0]<=lower[0][0]<=lower[1][0]<=lower_main[3][0]
    result={'schema_version':1,'id':p['id'],'source_bsp':p['bsp_path'],'source_bsp_sha256':bsp.sha256,'protocol_sha256':PROTOCOL_SHA,
            'world_blocking_brushes':len(world),'world_liquid_brushes':len(liquids),'mechanism_envelopes':envelopes,
            'paths':paths,'stations':stations,'station_bindings':bindings,'vertical_projection_shortcut':vertical,
            'stacked_connector':{'upper_path':'upper_overlap','lower_path':'lower_overlap','xy_centerline':[[480,-256],[560,-256]],
                'centerline_length':80,'full_swept_footprint_xy':[[464,-272],[576,-240]],'foot_heights':[-256,0],
                'both_selected_subroutes_pass':paths['upper_overlap']['pass'] and paths['lower_overlap']['pass'],
                'both_complete_main_routes_pass':paths['upper_west_to_east']['pass'] and paths['lower_west_to_east']['pass'],
                'upper_main_waypoint_range':[4,6],'lower_main_waypoint_range':[1,3],
                'vertical_separation':256,'interfloor_connection_certified':False},
            'rays':rays,'ray_contract':{'mask':p['sightline_mask'],'plane_comparison_tolerance':p['sightline_plane_tolerance'],
                                     'method':'Existing floating-point BSP-tree point clipper; separate from exact hull SAT.'},
            'authored_resource_context':[dict(bsp.entities[i],entity_id=i) for i in [13,74]],
            'native_tested':False,'gameplay_tested':False,'item_settlement_or_contact_tested':False,
            'limitations':['These are fixed selected routes, not shortest paths or the whole map.',
                'Main routes share endpoint XY but differ by256Z; no interchangeable route cost or ordinary inter-floor return is certified.',
                'No player trajectory, facing, sound, splash, item contact or human match was executed.',
                'Reference geometry was not copied into a generated map; no model weights were trained.']}
    return numeric(result)


def main():
    result=build_audit()
    (OUT/'audit.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'paths':{k:v['pass'] for k,v in result['paths'].items()},
        'stations':{k:v['pass'] for k,v in result['stations'].items()},'vertical_clear':result['vertical_projection_shortcut']['clear'],
        'clear_rays':{name:sum(r['world_visible'] for r in result['rays'] if r['source']==name) for name in result['stations']}},indent=2))


if __name__=='__main__':main()
