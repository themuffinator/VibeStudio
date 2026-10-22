"""Compiled checks for a default-pipeline portfolio, without recipe-specific counts.

Retains local hull/support evidence and separate graph connectivity. Neither
certifies native traversal between every node nor establishes multiplayer balance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from tools.map_study.binary import Bsp
from tools.map_study.features import analyze
from tools.map_recipes.drop_audit import _world, _flat_support, _sweep
from tools.map_recipes.geometry import intersects_aabb, model_brushes
from tools.map_recipes.gate_audit import audit_gates
from tools.map_recipes.packing_audit import packing


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def save(path, data):
    path.write_text(json.dumps(data, indent=2, allow_nan=False) + '\n', encoding='utf-8')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validated_world(bsp):
    # Inspect every authored world brush before the player-mask helper filters
    # nonblocking volumes. Otherwise an accidental WATER brush disappears.
    indices = model_brushes(bsp)
    require(indices and all(bsp.brushes[index][2] == 1 for index in indices),
            'Expected every model-zero brush to be SOLID architecture')
    return _world(bsp)


def validate_report(layout, report):
    player = report.get('analysis',{}).get('navigation',{}).get('player',{})
    require(report.get('validation',{}).get('ok') is True and
            report.get('layout_sha256') == layout.get('layout_sha256'), 'Invalid/mismatched typed layout')
    require(player.get('available') is True and player.get('ok') is True and
            player.get('layout_sha256') == layout.get('layout_sha256'),
            'Unavailable, invalid or mismatched player navigation')


def supported_anchor(world, origin):
    """Find a full-hull supported pose below the authored entity origin.

    Default spawns can begin above the floor. This is a geometric vertical
    settling candidate, not a replay of native spawn or pickup physics.
    """
    source = (origin[0], origin[1], origin[2] - 24)
    low = (source[0]-16, source[1]-16, source[2])
    high = (source[0]+16, source[1]+16, source[2]+56)
    require(not any(intersects_aabb(shape, low, high) for _, _, shape in world),
            f'Authored anchor hull blocked: {origin}')
    heights = sorted({p[3]/p[2] for _, _, shape in world for p in shape.planes
                      if p[0] == p[1] == 0 and p[2] > 0 and source[2]-64 <= p[3]/p[2] <= source[2]},
                     reverse=True)
    for floor in heights:
        target = (source[0], source[1], floor)
        try:
            supports = _flat_support(world, target, 'anchor settling candidate')
        except ValueError:
            continue
        counts = {'world_pair_checks': 0, 'exact_swept_checks': 0}
        _sweep(world, source, target, 'anchor settling sweep', counts)
        return {'authored_origin': origin, 'supported_feet': [float(v) for v in target],
                'support_brushes': supports, 'native_settlement_tested': False}
    raise ValueError(f'No full-footprint support within64 units below anchor: {origin}')


def inspect(output, map_name, assets, *, full_lighting=False, baseline=False):
    output = output.resolve(strict=True)
    bsp_path = output / (map_name + '.bsp')
    bsp = Bsp(bsp_path.read_bytes())
    require(not bsp.file_issues and not bsp.entity_issues, 'BSP parsing failed')
    qbsp_log = (output/(map_name+'-qbsp.log')).read_text(encoding='utf-8-sig').lower()
    require((output/(map_name+'.prt')).is_file(), 'Missing sealed portal output')
    require(not any((output/(map_name+s)).exists() for s in ('.lin','.pts')), 'Leak evidence present')
    for marker in ('leaked', 'no visible sides on brush', 'outside world', 'invalid brush',
                   'brush with no faces', 'multiple face contents', 'error:'):
        require(marker not in qbsp_log, 'Compiler error: '+marker)
    if full_lighting:
        vis = (output/(map_name+'-vis.log')).read_text(encoding='utf-8-sig').lower()
        light = (output/(map_name+'-light.log')).read_text(encoding='utf-8-sig').lower()
        require('calculating full vis' in vis and 'visdatasize:' in vis, 'Full VIS incomplete')
        require('lighting completed' in light and '0 empty lightmaps' in light, 'Full lighting incomplete')
    analysis, _ = analyze(bsp)
    require(not analysis['unsupported_pickup_classes'], 'Unrecognized stock pickup class')
    textures = sorted({p['texture'] for p in bsp.polygons()})
    texture_physics = {}
    for texture in textures:
        wal = assets/'textures'/(texture+'.wal')
        require(wal.is_file(), 'Missing stock material: '+texture)
        flags, contents, value = struct.unpack_from('<iii', wal.read_bytes(), 88)
        require(contents == 0 and not flags & (8|16|32), 'Architecture uses non-solid/warped/translucent material: '+texture)
        texture_physics[texture] = {'flags':flags, 'contents':contents, 'value':value, 'wal_sha256':sha(wal)}
    world = validated_world(bsp)
    anchor_checks, anchor_failures = [], []
    for anchor in analysis['anchors']:
        try:
            anchor_checks.append(supported_anchor(world, anchor['origin']))
        except ValueError as error:
            anchor_failures.append({'anchor':anchor, 'reason':str(error)})
    require(baseline or not anchor_failures, 'Compiled generated anchor failures: '+str(anchor_failures))
    bounds = [[float(min(shape.bounds[0][axis] for _,_,shape in world)) for axis in range(3)],
              [float(max(shape.bounds[1][axis] for _,_,shape in world)) for axis in range(3)]]
    record = {'schema_version':1, 'map':map_name, 'source_sha256':sha(output/(map_name+'.map')),
              'bsp_sha256':sha(bsp_path), 'sealed':True, 'full_vis':full_lighting,
              'extra4_one_bounce_lighting':full_lighting, 'texture_physics':texture_physics,
              'compiled_world_brushes':len(world), 'compiled_world_bounds':bounds,
              'compiled_world_xy_envelope_area':(bounds[1][0]-bounds[0][0])*(bounds[1][1]-bounds[0][1]),
              'authored_anchor_hulls_and_settling_candidates':anchor_checks,
              'anchor_failures':anchor_failures, 'ok':not anchor_failures,
              'metrics':analysis['metrics'], 'runtime_playtested':False,
              'limits':['Compiled envelope includes shell walls; compare only with the same descriptor.',
                        'Node-center and anchor checks are local poses; graph edges are not continuous native journeys.',
                        'Support below authored origins is a geometric settling candidate, not native spawn/item settlement.',
                        'Static line-of-sight and packing descriptors do not prove safety, balance or encounter quality.']}
    if not baseline:
        layout = read(output/'layout.json')
        report = read(output/(map_name+'.qmg.json'))
        validate_report(layout, report)
        gates = audit_gates(layout, report)
        require(not gates['baseline_missing_pairs'], 'Ordinary gameplay journey missing')
        nodes = report['analysis']['navigation']['player']['graph']['nodes']
        checked_nodes = []
        for node in nodes:
            area = node['area']
            require(area['slope'] is None, 'Slope audit is outside this portfolio contract')
            feet = ((area['min_x']+area['max_x'])/2, (area['min_y']+area['max_y'])/2, area['floor_z'])
            supporters = _flat_support(world, feet, f'graph node {node["id"]}')
            checked_nodes.append({'node':node['id'], 'feet':feet, 'support_brushes':supporters})
        tread_checks = []
        for run in layout['stair_runs']:
            for low, high in run:
                feet = ((low[0]+high[0])/2, (low[1]+high[1])/2, high[2])
                tread_checks.append({'feet':feet, 'support_brushes':_flat_support(world, feet, 'stair tread')})
        rectangles = [r['bounds'][0][:2]+r['bounds'][1][:2] for r in layout['rooms']]
        rectangles += [v[0][:2]+v[1][:2] for c in layout['connections'] for v in c['volumes']]
        record.update({'layout_sha256':layout['layout_sha256'], 'ordinary_gate_checks':gates,
                       'compiled_graph_node_centers':checked_nodes, 'compiled_stair_centers':tread_checks,
                       'authored_room_passage_packing':packing(rectangles),
                       'quality_summary':report['analysis']['quality']['summary'],
                       'quality_score':report['analysis']['quality']['score'],
                       'analysis_findings':{
                           'navigation':report['analysis']['navigation']['player']['findings'],
                           'visibility':report['analysis']['visibility']['findings'],
                           'lighting':report['analysis']['lighting']['findings'],
                           'quality':report['analysis']['quality']['findings']}})
        save(output/'compiled-analysis.json',analysis)
        save(output/'verification.json',record)
    else:
        save(output/(map_name+'-compiled-analysis.json'),analysis)
        save(output/(map_name+'-verification.json'),record)
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--map-name',required=True)
    parser.add_argument('--assets',type=Path,default=Path('E:/_SOURCE/_ASSETS/Q2'))
    parser.add_argument('--full-lighting',action='store_true')
    parser.add_argument('--baseline',action='store_true')
    args = parser.parse_args()
    result = inspect(args.output,args.map_name,args.assets,full_lighting=args.full_lighting,baseline=args.baseline)
    print(json.dumps({'map':result['map'],'anchors':len(result['authored_anchor_hulls_and_settling_candidates']),
                      'compiled_world_brushes':result['compiled_world_brushes'],
                      'anchor_failures':len(result['anchor_failures']),'ok':result['ok']}))
