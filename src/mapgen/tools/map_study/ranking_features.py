"""Original geometry/partition controls for interpreting inherited ranker inputs.

This reconstructs feature arithmetic; it does not replace or tune the ranker.
Actual ggml behavior is independently checked by test_ranker_diagnostics.cpp.
"""
from __future__ import annotations

import argparse
import hashlib
from itertools import combinations
import json
import math
from pathlib import Path
import struct
import subprocess

from tools.map_study.binary import Bsp, read_bytes
from tools.map_recipes.geometry import convex_polyhedron, model_brushes
from tools.map_study.interlocking_topology import check_segment, numeric

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'out/ranking-feature-study'
QBSP = Path('E:/_SOURCE/_CODE/ericw-tools-2.0.0-alpha11/qbsp.exe')
QBSP_SHA = 'fad5a4dc4daf42773ad9a812349b2667221bec47a920cad10cb821ae442c8eb8'
ASSETS = Path('E:/_SOURCE/_ASSETS/Q2')
ENVELOPE = [[0,0,0],[2048,1024,256]]
MASK = 33619971


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def partitions(axis, sizes):
    rooms, position = [], 0
    for i, size in enumerate(sizes):
        low, high = list(ENVELOPE[0]), list(ENVELOPE[1])
        low[axis], high[axis] = position, position + size
        rooms.append({'id':i+1,'bounds':[low,high]})
        position += size
    assert position == ENVELOPE[1][axis]
    return rooms


def volume(bounds):
    return math.prod(bounds[1][i] - bounds[0][i] for i in range(3))


def verify_partition(rooms):
    bounds = [r['bounds'] for r in rooms]
    assert all(all(ENVELOPE[0][i] <= b[0][i] < b[1][i] <= ENVELOPE[1][i] for i in range(3)) for b in bounds)
    assert all(any(a[1][i] <= b[0][i] or b[1][i] <= a[0][i] for i in range(3)) for a,b in combinations(bounds,2))
    assert sum(volume(b) for b in bounds) == volume(ENVELOPE)
    return {'contained':True,'interiors_disjoint':True,'volume_sum':volume(ENVELOPE),
            'exact_same_closed_union':True,'method':'Finite closed axis partitions with disjoint interiors, matching outer extent and exact total volume.'}


def features(rooms, raised_pieces=0):
    count = len(rooms)
    volumes = [volume(r['bounds']) for r in rooms]
    mean = sum(volumes)/count
    variance = sum(((v-mean)/mean)**2 for v in volumes)/count
    centers = [[(r['bounds'][0][i]+r['bounds'][1][i])//2 for i in range(3)] for r in rooms]
    distances = [sum(abs(b[i]-a[i]) for i in range(2)) for a,b in zip(centers,centers[1:])]
    average = f32(sum(distances)/len(distances))
    normalized_distance = f32(abs(f32(average-1280))/1280)
    affinity = f32(1 - min(1, max(0, normalized_distance)))
    values = [1,0,min(1,raised_pieces/(count*.5)),min(1,f32(f32(math.sqrt(variance))*2)),0,1,1,affinity]
    weights = [f32(w) for w in [1.4,1.5,1.1,.35,.4,1.2,1.1,.3]]
    return {'feature_order':['connection_count_ratio','cycle_rank_ratio','raised_platform_count_ratio','room_volume_cv_scaled',
            'branching_room_count_ratio','spawn_count_ratio','weapon_count_ratio','connected_room_center_distance_score'],
            'volumes':volumes,'room_centers':centers,'room_volume_cv':math.sqrt(variance),
            'connected_center_distances':distances,'average_center_xy_manhattan':sum(distances)/len(distances),
            'features':[f32(v) for v in values],'weights':weights,
            'reconstructed_dot_product':math.fsum(f32(v)*w for v,w in zip(values,weights)),
            'arithmetic_scope':'Feature F32 reconstruction and real sum of feature×weight products; not a new ggml execution or bit-identical reduction claim.'}


def map_box(low, high):
    x,y,z = low;X,Y,Z = high
    faces = [([(X,y,z),(X,y,Z),(X,Y,z)], '[ 0 1 0 0 ] [ 0 0 -1 0 ]'),
        ([(x,y,z),(x,Y,z),(x,y,Z)], '[ 0 1 0 0 ] [ 0 0 -1 0 ]'),
        ([(x,Y,z),(X,Y,z),(x,Y,Z)], '[ 1 0 0 0 ] [ 0 0 -1 0 ]'),
        ([(x,y,z),(x,y,Z),(X,y,z)], '[ 1 0 0 0 ] [ 0 0 -1 0 ]'),
        ([(x,y,Z),(x,Y,Z),(X,y,Z)], '[ 1 0 0 0 ] [ 0 -1 0 0 ]'),
        ([(x,y,z),(X,y,z),(x,Y,z)], '[ 1 0 0 0 ] [ 0 -1 0 0 ]')]
    return '{\n'+'\n'.join(' '.join('( '+' '.join(map(str,p))+' )' for p in points)+' e2u1/mine04_1 '+uv+' 0 1 1' for points,uv in faces)+'\n}\n'


def write_geometry(name, barrier):
    boxes = [[[-32,-32,-32],[2080,1056,0]],[[-32,-32,256],[2080,1056,288]],
        [[-32,-32,0],[0,1056,256]],[[2048,-32,0],[2080,1056,256]],
        [[0,-32,0],[2048,0,256]],[[0,1024,0],[2048,1056,256]]]
    if barrier:
        boxes.append([[896,384,0],[1152,640,192]])
    text = '{\n"classname" "worldspawn"\n"mapversion" "220"\n"message" "Original ranking feature control"\n'
    text += ''.join(map_box(*box) for box in boxes)+'}\n'
    for x,y in [(128,128),(640,256),(1408,768),(1920,896)]:
        text += '{\n"classname" "info_player_deathmatch"\n"origin" "'+f'{x} {y} 24'+'"\n}\n'
        text += '{\n"classname" "weapon_shotgun"\n"origin" "'+f'{x+32} {y+32} 24'+'"\n}\n'
    path = OUT/(name+'.map')
    path.write_text(text,encoding='utf-8')
    return path


def compile_control(path):
    if sha(QBSP) != QBSP_SHA:
        raise ValueError('QBSP does not match the established compiler pin')
    command = [str(QBSP),'-q2bsp','-leaktest','-noallowupgrade','-nodefaultpaths','-path',str(ASSETS),
               '-lowpriority','0','-nolog','-nocolor','-quiet',str(path),str(path.with_suffix('.bsp'))]
    result = subprocess.run(command,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,check=False)
    path.with_name(path.stem+'-qbsp.log').write_text(result.stdout,encoding='utf-8')
    if result.returncode or not path.with_suffix('.prt').is_file() or path.with_suffix('.prt').stat().st_size == 0:
        raise ValueError('Synthetic control did not compile sealed')
    if path.with_suffix('.pts').exists() or path.with_suffix('.lin').exists():
        raise ValueError('Synthetic control produced a leak witness')
    return {'command':command,'exit_code':result.returncode,'qbsp_sha256':QBSP_SHA,'sealed_portals':True,
            'full_vis':False,'lighting':False}


def compiled_paths(path):
    bsp = Bsp(read_bytes(path))
    world = {}
    for bid in model_brushes(bsp):
        first,count,contents = bsp.brushes[bid]
        if contents & MASK:
            world[bid] = convex_polyhedron([bsp.planes[bsp.brushsides[i][0]] for i in range(first,first+count)])
    paths = {'direct':[[64,512,0],[1984,512,0]],
             'north-detour':[[64,512,0],[64,704,0],[1984,704,0],[1984,512,0]]}
    result = {}
    for name, points in paths.items():
        segments = [check_segment(world,{},a,b) for a,b in zip(points,points[1:])]
        result[name] = {'pass':all(s['pass'] for s in segments),'length':sum(sum(abs(b[i]-a[i]) for i in range(2)) for a,b in zip(points,points[1:])), 'segments':segments}
    return {'map_sha256':sha(path.with_suffix('.map')),'bsp_sha256':bsp.sha256,'world_brushes':len(world),
            'standing_hull':[32,32,56],'mask':MASK,'paths':result,'native_tested':False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compile',action='store_true',help='Rebuild the two original synthetic QBSP controls with pinned local tools')
    args = parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    controls = {}
    for name,axis,sizes in [('equal-x',0,[512]*4),('unequal-x',0,[256,256,512,1024]),
                            ('cv-only-x',0,[256,768,256,768]),('equal-y',1,[256]*4)]:
        rooms = partitions(axis,sizes)
        controls[name] = {'rooms':rooms,'connections':[[1,2],[2,3],[3,4]],'partition_check':verify_partition(rooms),
                          'ranker_inputs':features(rooms),'geometry':'open-chamber.bsp'}
    compiles = {}
    if args.compile:
        for name,barrier in [('open-chamber',False),('barrier-chamber',True)]:
            compiles[name] = compile_control(write_geometry(name,barrier))
        (OUT/'compile-record.json').write_text(json.dumps(compiles,indent=2)+'\n',encoding='utf-8')
    worlds = {name:compiled_paths(OUT/(name+'.bsp')) for name in ['open-chamber','barrier-chamber']}
    assert worlds['open-chamber']['paths']['direct']['pass']
    assert not worlds['barrier-chamber']['paths']['direct']['pass']
    assert all(world['paths']['north-detour']['pass'] for world in worlds.values())
    reference = json.loads((ROOT/'out/interlocking-topology-study/route-audit.json').read_text(encoding='utf-8'))
    start,end = reference['witness_graph']['start'],reference['witness_graph']['goal']
    distance = sum(abs(end[i]-start[i]) for i in range(2))
    platform = [[768,256,104],[1280,768,128]]
    pieces = [[[768,256,104],[1024,768,128]],[[1024,256,104],[1280,768,128]]]
    assert sum(volume(piece) for piece in pieces) == volume(platform)
    assert pieces[0][1][0] == pieces[1][0][0]
    assert all(piece[0][1:] == platform[0][1:] and piece[1][1:] == platform[1][1:] for piece in pieces)
    raised_values = [features(controls['equal-x']['rooms'], raised_pieces=count)['features'][2] for count in [1,2]]
    assert raised_values == [.5,1.]
    result = {'schema_version':1,'scope':'Original metadata/collision controls plus retained q2rdm2 evidence; no training or ranker change.',
        'fixed_free_envelope':ENVELOPE,'target_players':4,'spawns':4,'weapons':4,'prefer_loops':True,'verticality':.5,
        'partition_controls':controls,'compiled_worlds':worlds,
        'barrier_comparison':{'same_room_and_connection_records':True,'barrier_purpose':'non-raised structural obstruction',
            'all_inherited_features_equal':True,'direct_length':1920,'clear_detour_length':2304,'detour_not_proven_shortest':True},
        'raised_piece_count_control':{'same_box_union':platform,'pieces':pieces,'exact_same_union_verified':True,
            'one_piece':1,'two_adjacent_pieces_split_x':1024,'room_count':4,
            'feature_one_piece':raised_values[0],'feature_two_pieces':raised_values[1],'scope':'Exact solid-union arithmetic; not another compiled geometry or elevated-route test.'},
        'reference_comparison':{'map':'q2rdm2','bsp_sha256':reference['bsp_sha256'],
            'start_feet':start,'end_feet':end,'endpoint_xy_manhattan':distance,
            'endpoint_1280_affinity':1-abs(distance-1280)/1280,
            'selected_north_length':1312,'selected_south_length':1760,
            'ratios_to_endpoint_l1':{'north':1312/distance,'south':1760/distance},
            'north_contract':'Shallow ground band; no constant-floor/native-contact certificate.',
            'south_contract':'Full-footprint flat support and full standing-hull sweep.',
            'central_832_unit_polyline_blocked':True,
            'limit':'The endpoint L1 comparison illustrates information loss; these are not extracted room centers or a whole-map rank score.'},
        'limits':['Partition metadata does not create interior walls in this control; all variants refer to one compiled BSP.',
            'No claim that arbitrary repartition is accepted by the production Layout validator or canonical MapBuilder.',
            'BSP/hull checks do not simulate player motion or establish match quality.',
            'Reconstructed feature arithmetic is distinct from real ggml diagnostic tests.']}
    (OUT/'results.json').write_text(json.dumps(numeric(result),indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'controls':{k:round(v['ranker_inputs']['reconstructed_dot_product'],6) for k,v in controls.items()},
                      'worlds':{k:{n:r['pass'] for n,r in v['paths'].items()} for k,v in worlds.items()},
                      'q2rdm2_endpoint_l1':distance},indent=2))


if __name__ == '__main__':
    main()
