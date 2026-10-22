"""Audit the single Bench ascent intervention in the original Talus recipe.

The published baseline is read-only. Source brush blocks must be byte-identical
outside one wedge/eight-tread replacement; compiled plane/material signatures,
entities and continuous standing-box routes are independently checked. This is
bounded static evidence, never an engine movement or measured-flow comparison.
"""

from collections import Counter, defaultdict
from copy import deepcopy
from fractions import Fraction
import argparse
import hashlib
import heapq
import json
import math
from pathlib import Path
import re
import struct
import subprocess

from tools.map_study.binary import Bsp
from tools.map_study.features import analyze, describe
from tools.map_recipes.geometry import convex_polyhedron, intersects_aabb, intersects_swept_aabb, model_brushes
from tools.map_recipes.ramp_audit import audit_ramps


PROJECT = Path(__file__).resolve().parents[2]
ALLOWED = {'walk', 'step', 'door'}
BENCH = ((-736, -160, 0), (-576, 320, 128))
TREADS = [((-736, -160 + i * 60, 0), (-576, -100 + i * 60, (i + 1) * 16)) for i in range(8)]
PINS = {'qbsp.exe': 'fad5a4dc4daf42773ad9a812349b2667221bec47a920cad10cb821ae442c8eb8',
        'vis.exe': '01be572845e05c87b01a0b667e590ac49bdaba23369af8529fe82fcfbc4fe6cf',
        'light.exe': '8e4fe9821f73c2d5c8a13655be20d3a71f357e76171de37c02413f4da3131a2f'}
PAIR = re.compile(r'("(?:[^"\\]|\\.)*") ("(?:[^"\\]|\\.)*")')
FACE = re.compile(r'\( ([^()]*) \) \( ([^()]*) \) \( ([^()]*) \) ([^\s]+) (.*)')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path):
    require(path.stat().st_size <= 4 * 1024 * 1024, 'JSON evidence exceeds cap')
    return json.loads(path.read_text(encoding='utf-8-sig'))


def save(path, data):
    path.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')


def source_map(path):
    """Read-only grouping of bounded, canonical generated Valve220 source.

    Faces remain exact source strings, including plane points, materials,
    projections and optional metadata. This does not transform/decompile maps;
    qbsp separately parses and validates the source before audit acceptance.
    Unsupported syntax fails instead of being salvaged by a broad regex.
    """
    require(path.stat().st_size <= 2 * 1024 * 1024, 'Source MAP exceeds experiment cap')
    entities, entity, brush = [], None, None
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        line = line.strip()
        if not line or line.startswith('//'):
            continue
        if line == '{':
            if entity is None:
                entity = {'properties': [], 'brushes': []}
            else:
                require(brush is None, 'Nested MAP primitive outside generated brush contract')
                brush = []
        elif line == '}':
            require(entity is not None, 'Unmatched MAP closing brace')
            if brush is not None:
                require(4 <= len(brush) <= 32, 'Unsupported source brush face count')
                entity['brushes'].append(tuple(brush)); brush = None
            else:
                entities.append(entity); entity = None
        elif brush is not None:
            match = FACE.fullmatch(line)
            require(match is not None, 'Malformed generated Valve220 brush face')
            for points in match.groups()[:3]:
                values = points.split()
                require(len(values) == 3, 'Malformed source plane point')
                require(all(math.isfinite(float(v)) and abs(float(v)) <= 32768 for v in values),
                        'Source face coordinate outside experiment cap')
            brush.append(line)
        else:
            require(entity is not None, 'MAP property outside entity')
            match = PAIR.fullmatch(line)
            require(match is not None, 'Malformed generated entity property')
            pair = tuple(json.loads(v) for v in match.groups())
            require(pair[0] not in dict(entity['properties']), 'Duplicate generated entity key')
            entity['properties'].append(pair)
    require(entity is None and brush is None and 1 <= len(entities) <= 256, 'Incomplete/oversized source MAP')
    require(dict(entities[0]['properties']).get('classname') == 'worldspawn', 'Missing source worldspawn')
    return entities


def source_bounds(block):
    points = [tuple(Fraction(v) for v in group.split()) for line in block
              for group in FACE.fullmatch(line).groups()[:3]]
    return tuple(min(p[i] for p in points) for i in range(3)), tuple(max(p[i] for p in points) for i in range(3))


def source_identity(before, after):
    require(before[1:] == after[1:], 'Non-world source entities, items, lights or primitives changed')
    a, b = dict(before[0]['properties']), dict(after[0]['properties'])
    require(a.pop('message') == 'Talus' and b.pop('message') == 'Talus Bench Stairs', 'Unexpected identity change')
    require(a == b, 'Worldspawn properties changed beyond experiment title')
    old, new = Counter(before[0]['brushes']), Counter(after[0]['brushes'])
    removed, added = list((old-new).elements()), list((new-old).elements())
    require(len(removed) == 1 and len(added) == 8, 'Source intervention is not exactly one brush to eight')
    require(source_bounds(removed[0]) == BENCH, 'Removed source brush is not the Bench wedge')
    require(Counter(source_bounds(block) for block in added) == Counter(TREADS), 'Replacement tread bounds changed')
    require(all(FACE.fullmatch(line).group(4) == 'e2u1/mine04_1' for block in removed+added for line in block),
            'Intervention changed the ramp material')
    return {'unchanged_source_brush_blocks': sum((old & new).values()), 'removed_brushes': 1, 'added_brushes': 8,
            'unchanged_nonworld_entities': len(before)-1, 'world_property_exceptions': ['message'],
            'method': 'Exact complete generated source brush strings: planes, materials, projections and metadata'}


def source_negative_controls(before, after):
    altered = deepcopy(after)
    key, _ = altered[1]['properties'][0]
    altered[1]['properties'][0] = (key, 'unexpected_entity')
    try:
        source_identity(before, altered)
    except ValueError as error:
        require('Non-world source entities' in str(error), 'Entity negative control failed for the wrong reason')
    else:
        raise ValueError('Source entity negative control was not rejected')
    altered = deepcopy(after)
    for index, block in enumerate(altered[0]['brushes']):
        if source_bounds(block) == TREADS[0]:
            changed = list(block)
            changed[0] = changed[0].replace('e2u1/mine04_1', 'e1u1/stairs1_1', 1)
            altered[0]['brushes'][index] = tuple(changed)
            break
    else:
        raise ValueError('Missing tread for material negative control')
    try:
        source_identity(before, altered)
    except ValueError as error:
        require('Intervention changed the ramp material' in str(error), 'Material control failed for the wrong reason')
    else:
        raise ValueError('Source material negative control was not rejected')
    return {'source_entity_change_rejected': True, 'source_intervention_material_change_rejected': True,
            'artifact_files_modified': False}


def world(bsp):
    result = []
    ids = model_brushes(bsp)
    require(len(ids) <= 1024, 'World brush count exceeds experiment cap')
    for index in ids:
        first, count, contents = bsp.brushes[index]
        require(contents == 1, 'Ordinary architectural brush is not compiled CONTENTS_SOLID')
        sides = bsp.brushsides[first:first+count]
        planes = [tuple(bsp.planes[p][:4]) for p, _ in sides]
        signature = (contents, tuple(sorted((tuple(bsp.planes[p][:4]), tuple(bsp.texinfo[t])) for p, t in sides)))
        result.append({'id': index, 'shape': convex_polyhedron(planes), 'signature': signature})
    return result


def compiled_identity(old, new, a, b):
    require(a.entities[1:] == b.entities[1:], 'Compiled non-world entity properties changed')
    ea, eb = dict(a.entities[0]), dict(b.entities[0])
    ea.pop('message'); eb.pop('message')
    require(ea == eb, 'Compiled world properties changed beyond title')
    ca, cb = Counter(x['signature'] for x in old), Counter(x['signature'] for x in new)
    removed, added = ca-cb, cb-ca
    require(sum(removed.values()) == 1 and sum(added.values()) == 8,
            'Compiled collision/material intervention differs from one wedge/eight treads')
    changed = [x for x in new if x['signature'] in added]
    require(Counter(x['shape'].bounds for x in changed) == Counter(TREADS), 'Compiled replacement tread bounds changed')
    require(all(texinfo[10].split(b'\0',1)[0] == b'e2u1/mine04_1'
                for brush in changed for _,texinfo in brush['signature'][1]),
            'Compiled intervention material differs from the final controlled MAP')
    return {'unchanged_brushes': sum((ca & cb).values()), 'removed_brushes': 1, 'added_brushes': 8,
            'unchanged_nonworld_entities': len(a.entities)-1,
            'all_added_brushside_materials': 'e2u1/mine04_1',
            'method': 'Exact stored outward planes, full texinfo values and brush contents; indices normalized'}


def hull(x, y, feet):
    return ((Fraction(x-16), Fraction(y-16), Fraction(feet)),
            (Fraction(x+16), Fraction(y+16), Fraction(feet+56)))


def flat_support(worlds, box):
    low, high = box
    corners = [(x, y, low[2]) for x in (low[0], high[0]) for y in (low[1], high[1])]
    return [w['id'] for w in worlds
            if any(not p[0] and not p[1] and p[2] > 0 and p[3]/p[2] == low[2] for p in w['shape'].planes)
            and all(all(sum(p[i]*c[i] for i in range(3)) <= p[3] for p in w['shape'].planes) for c in corners)]


def clear_sweep(worlds, first, second, counts):
    delta = tuple(second[0][i]-first[0][i] for i in range(3))
    low = tuple(min(first[0][i], second[0][i]) for i in range(3))
    high = tuple(max(first[1][i], second[1][i]) for i in range(3))
    for w in worlds:
        counts['world_pair_checks'] += 1
        bounds = w['shape'].bounds
        if any(high[i] <= bounds[0][i] or low[i] >= bounds[1][i] for i in range(3)):
            continue
        counts['exact_swept_checks'] += 1
        require(not intersects_swept_aabb(w['shape'], *first, delta),
                f"Continuous stair hull sweep blocked by world brush {w['id']}")


def stairs_audit(worlds):
    counts = {'world_pair_checks': 0, 'exact_swept_checks': 0}
    lanes = []
    for x in (-712, -656, -600):
        poses = [('lower approach', hull(x, -208, 0))]
        supports = []
        for step in range(8):
            front, low = -160+step*60, step*16
            before, after = hull(x, front-16, low), hull(x, front+16, low+16)
            for label, pose in [('before riser', before), ('full tread', after)]:
                supporters = flat_support(worlds, pose)
                require(supporters, f'Stair {step+1} {label} lacks full-footprint compiled support')
                require(not any(intersects_aabb(w['shape'], *pose) for w in worlds), 'Supported stair pose collides')
                supports.append({'step': step+1, 'phase': label, 'brushes': supporters})
            poses += [(f'before step {step+1}', before), (f'raise at step {step+1}', hull(x, front-16, low+16)),
                      (f'cross step {step+1}', after)]
        poses += [('stair crest', hull(x, 304, 128)), ('full upper deck', hull(x, 336, 128)),
                  ('upper approach', hull(x, 368, 128))]
        require(flat_support(worlds, poses[0][1]) and flat_support(worlds, poses[-1][1]), 'Ascent endpoints lack support')
        segments = [(a, b) for (_, a), (_, b) in zip(poses, poses[1:]) if a != b]
        for first, second in segments:
            clear_sweep(worlds, first, second, counts)
            clear_sweep(worlds, second, first, counts)
        lanes.append({'x': x, 'up_segments': len(segments), 'down_segments': len(segments),
                      'step_height': 16, 'full_footprint_support': supports, 'continuous_sweeps_clear': True})
    turns=[]
    for y,feet,label in ((-208,0,'lower approach'),(368,128,'upper landing')):
        first,second=hull(-712,y,feet),hull(-600,y,feet)
        broad=(first[0],second[1])
        require(flat_support(worlds,broad), 'Lateral landing turn lacks full-footprint support')
        clear_sweep(worlds,first,second,counts);clear_sweep(worlds,second,first,counts)
        turns.append({'station':label,'from_x':-712,'to_x':-600,'feet':feet,'both_directions_clear':True})
    return {'lanes': lanes, 'hull': [32, 32, 56], 'landing_feet': [0, 128], 'rise_per_step': 16,
            'max_native_planned_step': 18, 'tread_depth': 60, 'tread_width': 160,
            'landing_turns':turns,'total_swept_route_segments':sum(l['up_segments']+l['down_segments'] for l in lanes)+4,
            'approach_extension': 48, 'collision_epsilon': 0, **counts, 'runtime_verified': False,
            'limit': 'Conservative raise/cross/lower translations are geometry, not the engine step solver or timed movement.'}


def graph_evidence(layout, report):
    graph = report['analysis']['navigation']['player']['graph']
    require(len(graph['nodes']) <= 4096 and len(graph['edges']) <= 32768, 'Graph exceeds experiment caps')
    nodes = {n['id']: n for n in graph['nodes']}
    binds = {b['placement']: b['node'] for b in graph['placement_bindings']}
    adjacent = defaultdict(list)
    for e in graph['edges']:
        if e['kind'] in ALLOWED:
            adjacent[e['from']].append(e['to'])
    checks = 0
    for s in (p for p in layout['placements'] if p['role'] == 1):
        seen = {binds[s['id']]}; pending = list(seen)
        while pending:
            for n in adjacent[pending.pop()]:
                if n not in seen: seen.add(n); pending.append(n)
        for p in layout['placements']:
            require(binds[p['id']] in seen, 'Missing required no-drop ordinary anchor journey')
            checks += 1
    require(checks == 264, 'Unexpected controlled experiment anchor count')
    return {'nodes': len(nodes), 'edges': len(graph['edges']), 'edge_kinds': dict(Counter(e['kind'] for e in graph['edges'])),
            'ordinary_pairs': checks, 'ordinary_policy': sorted(ALLOWED), 'runtime_verified': False}


def floor(n, x, y):
    s = n['area']['slope']
    return (math.ceil(Fraction(s['low_z']) + Fraction(s['rise']*((x if s['along_x'] else y)-s['start']+s['support_offset']),
                                                     s['end']-s['start'])) if s else n['area']['floor_z'])


def local_proxy(report):
    g = report['analysis']['navigation']['player']['graph']; nodes = {n['id']: n for n in g['nodes']}
    def binding(point):
        candidates = []
        for n in nodes.values():
            a=n['area']; x,y,z=point
            if a['min_x'] <= x <= a['max_x'] and a['min_y'] <= y <= a['max_y'] and floor(n,x,y)+24 == z:
                candidates.append((abs(x-n['origin'][0])+abs(y-n['origin'][1]), n['id']))
        require(candidates, 'Local comparison endpoint is not exactly supported')
        return nodes[min(candidates)[1]]
    start, end = (-656,-208,24), (-656,368,152)
    a,b=binding(start),binding(end); adj=defaultdict(list)
    for e in g['edges']:
        if e['kind'] in ALLOWED: adj[e['from']].append((e['to'],int(e['cost'])))
    distance={a['id']:0}; previous={}; q=[(0,a['id'])]
    while q:
        cost,n=heapq.heappop(q)
        if cost != distance[n]: continue
        if n == b['id']: break
        for target,weight in adj[n]:
            candidate=cost+weight
            if candidate < distance.get(target,1<<64):
                distance[target]=candidate;previous[target]=n;heapq.heappush(q,(candidate,target))
    require(b['id'] in distance, 'Local ascent ordinary route missing')
    path=[b['id']]
    while path[-1] != a['id']: path.append(previous[path[-1]])
    path.reverse()
    approach=sum(abs(start[i]-a['origin'][i])+abs(end[i]-b['origin'][i]) for i in range(3))*1000
    edges = [min((e for e in g['edges'] if e['from'] == first and e['to'] == second and e['kind'] in ALLOWED),
                 key=lambda e: int(e['cost'])) for first, second in zip(path,path[1:])]
    return {'from':start,'to':end,'cost':distance[b['id']]+approach,'graph_cost':distance[b['id']],
            'endpoint_approach_cost':approach,'node_path':path,'path_edges':edges,
            'path_edge_kinds':dict(Counter(e['kind'] for e in edges)),
            'node_sources':[nodes[n]['source'] for n in path], 'policy':sorted(ALLOWED),
            'basis':'Manhattan/node-anchor/end-gate proxy including transition penalties; not engine seconds'}


def fixed_local_rays(before, after):
    """Paired static-world queries with exactly the same origins and directions.

    These six supported flat stations are shared by the independent ascent
    sweeps. No point on the changed ascent surface is treated as a matched eye.
    """
    rays=[]
    for y,feet,label in ((-208,0,'lower approach'),(368,128,'upper landing')):
        for x in (-712,-656,-600):
            eye=(x,y,feet+46)
            require(before.trace(eye,eye)==after.trace(eye,eye)==1, 'Fixed comparison eye lies in solid')
            for dx,dy in ((1,0),(1,1),(0,1),(-1,1),(-1,0),(-1,-1),(0,-1),(1,-1)):
                scale=1024/math.hypot(dx,dy)
                end=(eye[0]+dx*scale,eye[1]+dy*scale,eye[2])
                a,b=1024*before.trace(eye,end),1024*after.trace(eye,end)
                rays.append({'station':label,'eye':eye,'direction':[dx,dy,0],'maximum_length':1024,
                             'baseline_length':a,'variant_length':b,'delta':b-a})
    return {'eye_offset_from_feet':46,'player_spawn_eye_offset_from_origin':22,
            'pairs':len(rays),'changed_pairs':sum(abs(r['delta'])>1e-6 for r in rays),
            'rows':rays,'basis':'Same six flat supported eye positions and eight horizontal directions in each compiled BSP',
            'limits':'Point rays against static world collision; no moving players, visual alpha surfaces, material shading, camera motion or encounter outcome'}


def compile_variant(output, tools, assets, run):
    for name, expected in PINS.items():
        require(sha(tools/name) == expected, f'Compiler pin changed: {name}')
    bsp=output/'qmg_talus_stairs.bsp'
    common=['-lowpriority','0','-nodefaultpaths','-path',str(assets),'-nolog','-nocolor','-quiet']
    stages=[('qbsp.exe',['-q2bsp','-leaktest','-noallowupgrade',*common,str(output/'qmg_talus_stairs.map'),str(bsp)]),
            ('vis.exe',['-threads','2','-nostate',*common,str(bsp)]),
            ('light.exe',['-extra4','-bounce','1','-soft','-threads','2',*common,str(bsp)])]
    if run:
        for name,args in stages:
            with (output/(name[:-4]+'.log')).open('w',encoding='utf-8') as log:
                subprocess.run([str(tools/name),*args],cwd=PROJECT,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
            print(name+' completed',flush=True)
    return {name:{'sha256':PINS[name],'arguments':args} for name,args in stages}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline',type=Path,default=PROJECT/'out/talus')
    parser.add_argument('--output',type=Path,default=PROJECT/'out/talus-stairs')
    parser.add_argument('--assets',type=Path,default=Path('E:/_SOURCE/_ASSETS/Q2'))
    parser.add_argument('--tools',type=Path,default=Path('E:/_SOURCE/_CODE/ericw-tools-2.0.0-alpha11'))
    parser.add_argument('--compile',action='store_true')
    args=parser.parse_args(); baseline=args.baseline.resolve(strict=True); output=args.output.resolve(strict=True)
    require(output == (PROJECT/'out/talus-stairs').resolve(), 'This bounded experiment writes only out/talus-stairs')
    require(not output.is_symlink() and baseline != output, 'Experiment output must not alias baseline')
    baseline_files=['layout.json','flow.json','qmg_talus.map','qmg_talus.bsp','qmg_talus.qmg.json','verification.json']
    untouched={name:sha(baseline/name) for name in baseline_files}
    compiler=compile_variant(output,args.tools,args.assets,args.compile)
    require(all(compiler[n]['sha256']==v for n,v in PINS.items()), 'Compiler identity mismatch')
    la,lb=read_json(baseline/'layout.json'),read_json(output/'layout.json')
    ra,rb=read_json(baseline/'qmg_talus.qmg.json'),read_json(output/'qmg_talus_stairs.qmg.json')
    for key in ('rooms','connections','placements','levels','players','mechanisms'):
        require(la[key]==lb[key],f'Unrelated typed layout field changed: {key}')
    require(la['ordinary_route_edge_kinds']==lb['ordinary_route_edge_kinds']==['walk','step','door'], 'Route policy changed')
    require(len(la['ramps'])==2 and len(lb['ramps'])==1 and lb['ramps'][0]['surface']==la['ramps'][1]['surface'],
            'Headworks ramp did not remain unchanged')
    require(lb['stair_runs'][:-1]==la['stair_runs'] and tuple(tuple(tuple(v) for v in b) for b in lb['stair_runs'][-1])==tuple(TREADS),
            'Existing stairs or replacement dimensions changed')
    sa,sb=source_map(baseline/'qmg_talus.map'),source_map(output/'qmg_talus_stairs.map')
    source=source_identity(sa,sb)
    controls=source_negative_controls(sa,sb)
    require(source['unchanged_source_brush_blocks']==173, 'Unexpected shared source brush count')
    ba=Bsp((baseline/'qmg_talus.bsp').read_bytes());bb=Bsp((output/'qmg_talus_stairs.bsp').read_bytes())
    require(not bb.entity_issues and not bb.file_issues and all(bb.lumps[i] for i in range(16)), 'Invalid/incomplete variant BSP')
    for suffix in ('.pts','.lin'): require(not (output/('qmg_talus_stairs'+suffix)).exists(),'Compiler leak pointfile exists')
    require((output/'qmg_talus_stairs.prt').stat().st_size>0,'Missing portal data')
    logs={name:(output/(name+'.log')).read_text(encoding='utf-8-sig').lower() for name in ('qbsp','vis','light')}
    for marker in ('leaked','no visible sides on brush','outside world','invalid brush','brush with no faces','multiple face contents','error:'):
        require(marker not in logs['qbsp'],'Compiler error: '+marker)
    require('calculating full vis' in logs['vis'] and 'visdatasize:' in logs['vis'],'Full VIS evidence missing')
    require('lighting completed' in logs['light'] and '0 empty lightmaps' in logs['light'],'Light evidence missing')
    wa,wb=world(ba),world(bb);compiled=compiled_identity(wa,wb,ba,bb)
    aa,_=analyze(ba);ab,_=analyze(bb)
    require(aa['anchors']==ab['anchors'],'Compiled anchor identities/positions changed')
    textures=sorted({t[10].split(b'\0',1)[0].decode('ascii') for t in bb.texinfo})
    texture_physics={name:dict(zip(('flags','contents','value'),struct.unpack_from('<3i',(args.assets/'textures'/(name+'.wal')).read_bytes(),88))) for name in textures}
    require(all(not v['contents'] and not v['flags']&(8|16|32) for v in texture_physics.values()),'Invalid architectural material physics')
    checks=0
    for anchor in ab['anchors']:
        x,y,z=anchor['origin'];box=hull(x,y,z-24)
        for w in wb:
            require(not intersects_aabb(w['shape'],*box),'Compiled gameplay anchor hull collision');checks+=1
    require(not any(rb['analysis']['quality']['score'].values()),'Variant analyzer finding requires review')
    na,nb=graph_evidence(la,ra),graph_evidence(lb,rb)
    print('Source/BSP identity and both sets of 264 ordinary graph journeys verified',flush=True)
    stairs=stairs_audit(wb)
    ramp_before=audit_ramps(ba,la);ramp_after=audit_ramps(bb,lb)
    flow_a,flow_b=read_json(baseline/'flow.json'),read_json(output/'flow.json')
    fa={(p['spawn'],p['target'],p['kind']):p for p in flow_a['timings']};fb={(p['spawn'],p['target'],p['kind']):p for p in flow_b['timings']}
    require(fa.keys()==fb.keys(),'Timing pair identity changed')
    differences=[{'spawn':k[0],'target':k[1],'kind':k[2],'before':fa[k]['cost'],'after':fb[k]['cost'],'delta':fb[k]['cost']-fa[k]['cost']} for k in sorted(fa)]
    keys=('combat_height_p90_p10','combat_height_bands_128','world_diagonal','upward_surface_area','sampled_world_sightline_p50','sampled_world_sightline_p90','spawn_pair_world_los_fraction')
    smoke={}
    for label,directory,bsp in (('baseline',baseline,ba),('variant',output,bb)):
        path=directory/'engine-smoke/engine-smoke.json'
        if path.exists():
            evidence=read_json(path)
            require(evidence['ok'] and evidence['bsp_sha256']==bsp.sha256, 'Dedicated smoke mismatch')
            smoke[label]={'path':str(path.relative_to(PROJECT)).replace('\\','/'),'sha256':sha(path),
                          'kind':evidence['kind'],'ok':True,'bsp_sha256':bsp.sha256,
                          'engine':evidence['engine'],'dependency_hashes':evidence['dependency_hashes'],
                          'proves_player_movement':False}
    source_paths=('tools/map_recipes/talus.cpp','tools/map_recipes/talus_stairs.cpp',
                  'tools/map_recipes/recipe_support.hpp','tools/map_recipes/ramp_stair_comparison.py',
                  'tools/map_recipes/ramp_audit.py','tools/map_recipes/geometry.py','tools/map_study/features.py',
                  'build/windows-clang/qmg_core.lib','build/windows-clang/qmg_talus_stairs_recipe.exe')
    record={'schema_version':1,'id':'talus-bench-ramp-versus-stairs-v1','status':'controlled_compiled_static_comparison_native_paired_movement_pending',
            'implementation_sha256':{path:sha(PROJECT/path) for path in source_paths},
            'baseline':{'map':'qmg_talus','files_sha256':untouched},
            'variant':{'map':'qmg_talus_stairs','layout_sha256':lb['layout_sha256'],'map_sha256':sha(output/'qmg_talus_stairs.map'),'bsp_sha256':bb.sha256},
            'intervention':{'removed':'Bench surface rises from 8 to 128: 120 rise over 480 run',
                            'added':'Eight treads, each 60 units deep with a 16-unit rise',
                            'bounds':BENCH,'landing_heights':[0,128],'width':160,'material':'e2u1/mine04_1',
                            'post_lowering_material_control':{'changed_top_faces':8,'final_material':'e2u1/mine04_1',
                                'shared_palette_outputs_overridden':{'e1u1/stairs1_1':7,'e1u1/grate1_5':1},
                                'method':'Native MapParser and validated MapDocumentWriter; exact six-plane box identity and unique eight-box match',
                                'typed_material_role':'ramp','actual_final_source_materials_verified':True,
                                'nonintervention_planes_projections_materials_metadata_unchanged':True},
                            'headworks_ramp_unchanged':True,'source_identity':source,'compiled_identity':compiled},
            'construction_gates':{'sealed':True,'full_vis':True,'lit':True,'all_world_brush_contents_solid':True,
                                  'texture_physics':texture_physics,'gameplay_anchors':len(ab['anchors']),'standing_hull_checks':checks,
                                  'compiler':compiler,'negative_controls':controls},
            'dedicated_initialization_smoke':smoke,
            'navigation':{'baseline':na,'variant':nb,'local_ascent_proxy':{'baseline':local_proxy(ra),'variant':local_proxy(rb)}},
            'compiled_ascent':{'baseline_ramps':ramp_before,'variant_headworks':ramp_after,'variant_bench_stairs':stairs},
            'descriptors':{k:{'baseline':aa['metrics'][k],'variant':ab['metrics'][k]} for k in keys},
            'fixed_local_world_rays':fixed_local_rays(ba,bb),
            'spawn_los':{'baseline':aa['spawn_los'],'variant':ab['spawn_los'],
                         'sampling':'Compiled static-world ray from standing spawn origin plus 22 units to matching eye point'},
            'timing_proxy':{'paired_rows':len(differences),'changed_rows':sum(d['delta']!=0 for d in differences),
                            'delta_distribution':describe([d['delta'] for d in differences]),'rows':differences,
                            'basis':'Endpoint-aware Manhattan/node-anchor/slope-end-gate graph costs with penalties; not native time',
                            'edge_policy':'Full exported quality graph, including generated jump/drop shortcuts; distinct from ordinary-route proof'},
            'runtime_verified':False,'measured_flow':False,'trained_weights':False,'archive_geometry_copied':False,
            'limits':['One authored ascent geometry intervention with exact held source/BSP controls; no controlled human or engine-movement outcome.',
                      'The ramp begins with an 8-unit surface lip; the stairs begin at 16. Their lower/upper landings and footprint match, while the internal support profile intentionally differs.',
                      'Step sweeps prove conservative geometric translations, not native stepping, acceleration, friction, camera/aim rhythm, sound or pursuit.',
                      'More stair edges add configured step penalties and change graph anchors; proxy cost increases cannot be interpreted as slower real travel.',
                      'Global rendered-face sightline samples can shift after compiler subdivision; these descriptors are not matched encounter observations.',
                      'The geometry intervention changes face orientations, subdivisions and lightmaps; identical material names, fixed light entities and compiler flags cannot hold the rendered image or bounced illumination constant.',
                      'Dedicated smoke validates initialization only. No paired client movement, input or screenshot test was run; unchanged placements do not prove unchanged spawn safety during play.']}
    require(untouched=={name:sha(baseline/name) for name in baseline_files},'Published baseline changed during audit')
    save(output/'compiled-analysis.json',ab);save(output/'comparison.json',record)
    print(json.dumps({'source_common':source['unchanged_source_brush_blocks'],'compiled_common':compiled['unchanged_brushes'],
                      'ordinary_pairs':nb['ordinary_pairs'],'local_proxy':record['navigation']['local_ascent_proxy'],
                      'timing_changed':record['timing_proxy']['changed_rows']},indent=2),flush=True)


if __name__ == '__main__':
    main()
