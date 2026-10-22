"""Frozen two-map teleporter departure geometry; no native or pickup simulation.

Run ``python -B -m tools.map_study.teleporter_arrivals [--verify]``. Protocol
and reference bytes are immutable inputs. Original analytical controls live in
tests/test_teleporter_arrivals.py; archived geometry is never exported as a map.
"""
from __future__ import annotations

import argparse
from fractions import Fraction as F
import hashlib
from itertools import product
import json
import math
from pathlib import Path

from tools.map_study.binary import Bsp, read_bytes
from tools.map_study.interlocking_topology import hull, numeric, broad_overlap
from tools.map_study.raised_resource_circuit import envelope_checks
from tools.map_recipes.geometry import convex_polyhedron, model_brushes, intersects_swept_aabb

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'out/teleporter-arrival-study'
PROTOCOL_SHA = '584900ad28e6cccd3fe6e9e9cc8c973993bad45bfaeed308f5f5d57ff7c3f3b3'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def box(lo, hi):
    return convex_polyhedron([(1, 0, 0, hi[0]), (-1, 0, 0, -lo[0]),
        (0, 1, 0, hi[1]), (0, -1, 0, -lo[1]), (0, 0, 1, hi[2]), (0, 0, -1, -lo[2])])


def support(world, a, b, band=0):
    """Conservative exact convex-cap coverage, including a declared 4-unit band.

    All four admitted cap corners satisfy the actual convex planes. Rectangular
    subdivisions cover the complete swept footprint, not just its centerline.
    A nonzero band proves nearby ground, not continuously grounded movement.
    """
    if band not in (0, 4) or a[2] != b[2] or (a[0] != b[0] and a[1] != b[1]):
        raise ValueError('Only flat axial segments with the frozen 0/4 floor band')
    low = tuple(F(min(a[i], b[i])) - 16 for i in range(2))
    high = tuple(F(max(a[i], b[i])) + 16 for i in range(2))
    cuts = [{low[i], high[i]} for i in range(2)]
    caps = []
    for bid, solid in world.items():
        lo = tuple(max(low[i], solid.bounds[0][i]) for i in range(2))
        hi = tuple(min(high[i], solid.bounds[1][i]) for i in range(2))
        if any(lo[i] >= hi[i] for i in range(2)):
            continue
        for p in solid.planes:
            if p[2] <= 0:
                continue
            vertices = [(*xy, (p[3] - p[0]*xy[0] - p[1]*xy[1])/p[2]) for xy in product(*zip(lo, hi))]
            if all(a[2]-band <= v[2] <= a[2] and all(sum(q[i]*v[i] for i in range(3)) <= q[3] for q in solid.planes) for v in vertices):
                caps.append((bid, lo, hi))
                for i in range(2):
                    cuts[i].update((lo[i], hi[i]))
                break
    axes = [sorted(x) for x in cuts]
    used = set()
    count = 0
    for x0, x1 in zip(axes[0], axes[0][1:]):
        for y0, y1 in zip(axes[1], axes[1][1:]):
            found = [bid for bid, lo, hi in caps if lo[0] <= x0 and x1 <= hi[0] and lo[1] <= y0 and y1 <= hi[1]]
            if not found:
                return {'pass': False, 'uncovered_cell': [x0, y0, x1, y1], 'band': band}
            used.update(found)
            count += 1
    return {'pass': True, 'support_solids': sorted(used), 'covered_cells': count,
            'footprint_xy': [low, high], 'floor_height_band': [a[2]-band, a[2]]}


def sweep(shapes, a, b, lower=0):
    lo, hi = hull(a)
    lo = (*lo[:2], lo[2]-lower)
    delta = tuple(F(b[i])-F(a[i]) for i in range(3))
    slo = tuple(min(lo[i], lo[i]+delta[i]) for i in range(3))
    shi = tuple(max(hi[i], hi[i]+delta[i]) for i in range(3))
    return [bid for bid, s in shapes.items() if broad_overlap(slo, shi, s.bounds)
            and intersects_swept_aabb(s, lo, hi, delta)]


def leg(world, pads, liquids, envelopes, a, b, band=0):
    solid = {**world, **pads}
    blockers = sweep(solid, a, b)
    wet = sweep(liquids, a, b, band)
    exclusions = envelope_checks(envelopes, a, b, band)
    if a[2] == b[2]:
        ground = support(solid, a, b, band)
        kind = 'flat' if not band else 'four_unit_ground_band'
    else:
        if a[:2] != b[:2] or a[2]-b[2] not in (2, 4) or band:
            raise ValueError('Only the frozen two/four-unit vertical settle')
        ground = support(solid, b, b)
        kind = 'vertical_geometric_settle'
    return {'kind': kind, 'from_feet': a, 'to_feet': b,
            'world_blockers': [x for x in blockers if x.startswith('world:')],
            'stock_pad_blockers': [x for x in blockers if x.startswith('pad:')],
            'liquid_brushes': wet, 'support': ground, 'mechanism_exclusions': exclusions,
            'pass': not blockers and not wet and ground['pass'] and all(x['strictly_disjoint'] for x in exclusions)}


def origin(entity):
    return [F(x) for x in entity.get('origin', '0 0 0').split()]


def arrival_feet(destination):
    p = origin(destination)
    p[2] += 10-24
    return p


def linked_destination(entities, source_id, destination_id):
    source = entities[source_id]
    if source.get('classname') != 'misc_teleporter':
        raise ValueError('Source is not a stock teleporter')
    matching = [i for i, e in enumerate(entities) if e.get('targetname') == source.get('target')]
    if matching != [destination_id] or entities[destination_id].get('classname') != 'misc_teleporter_dest':
        raise ValueError('Target is not the frozen unique destination')
    return entities[destination_id]


def mover_envelopes(bsp):
    result = []
    for i, e in enumerate(bsp.entities):
        if not e.get('model', '').startswith('*'):
            continue
        row = bsp.models[int(e['model'][1:])]
        lo, hi = [F(x) for x in row[:3]], [F(x) for x in row[3:6]]
        o = origin(e)
        kind = e['classname']
        if kind in ('func_rotating', 'func_door_rotating'):
            # Local submodel bounds surround its authored origin. L1 radius is
            # conservative for all rotations, even when the exact axis differs.
            radius = sum(max(abs(lo[j]), abs(hi[j])) for j in range(3))
            lo, hi = [x-radius for x in o], [x+radius for x in o]
            basis = 'Authored origin plus all-rotation L1-radius cube of local model bounds'
        elif kind == 'func_plat':
            lo, hi = [lo[j]+o[j] for j in range(3)], [hi[j]+o[j] for j in range(3)]
            extent = max(abs(F(e['height'])), hi[2]-lo[2])
            lo[2] -= extent
            hi[2] += extent
            basis = 'Model bounds plus origin, expanded both Z directions by max(height, extent)'
        elif kind == 'func_button':
            extent = sum(hi[j]-lo[j] for j in range(3))
            lo, hi = [lo[j]+o[j]-extent for j in range(3)], [hi[j]+o[j]+extent for j in range(3)]
            basis = 'Model bounds plus origin expanded by L1 size in all directions'
        else:
            raise ValueError('Unreviewed brush entity: '+kind)
        result.append({'entity': i, 'class': kind, 'bounds': [lo, hi], 'contract': basis})
    return result


def load_case(spec, protocol):
    for pin in spec['pins']:
        p = Path(pin['path'])
        if sha(p) != pin['sha256'] or p.stat().st_size != pin['bytes']:
            raise ValueError('Reference source pin changed')
    bsp = Bsp(read_bytes(Path(spec['bsp_path'])))
    if bsp.sha256 != spec['bsp_sha256']:
        raise ValueError('BSP pin changed')
    world, liquids, pads = {}, {}, {}
    for bid in model_brushes(bsp):
        first, count, contents = bsp.brushes[bid]
        if contents & (protocol['collision_mask'] | protocol['liquid_mask']):
            shape = convex_polyhedron([bsp.planes[bsp.brushsides[j][0]] for j in range(first, first+count)])
            if contents & protocol['collision_mask']:
                world[f'world:{bid}'] = shape
            if contents & protocol['liquid_mask']:
                liquids[f'liquid:{bid}'] = shape
    for eid, e in enumerate(bsp.entities):
        if e.get('classname') in ('misc_teleporter', 'misc_teleporter_dest'):
            o = origin(e)
            pads[f'pad:{eid}'] = box([o[j]+(-32,-32,-24)[j] for j in range(3)], [o[j]+(32,32,-16)[j] for j in range(3)])
    return bsp, world, pads, liquids, mover_envelopes(bsp)


def facing_end(eye, yaw, length):
    angle = math.radians(yaw)
    return [float(eye[0])+length*math.cos(angle), float(eye[1])+length*math.sin(angle), float(eye[2])]


def box_ray_fraction(a, b, bounds):
    enter, leave = 0.0, 1.0
    for i in range(3):
        d = float(b[i]-a[i])
        if d == 0:
            if not float(bounds[0][i]) <= float(a[i]) <= float(bounds[1][i]):
                return 1.0
            continue
        t0 = (float(bounds[0][i])-float(a[i]))/d
        t1 = (float(bounds[1][i])-float(a[i]))/d
        enter, leave = max(enter, min(t0,t1)), min(leave, max(t0,t1))
        if enter > leave:
            return 1.0
    return enter


def point_ray(bsp, pads, envelopes, a, b, mask):
    a, b = [float(x) for x in a], [float(x) for x in b]
    f = bsp.trace(a, b, mask=mask)
    pad_hits = {bid: box_ray_fraction(a,b,s.bounds) for bid,s in pads.items() if box_ray_fraction(a,b,s.bounds) < 1}
    exclusions = [{'entity': e['entity'], 'strictly_disjoint': any(max(a[i],b[i]) < e['bounds'][0][i] or e['bounds'][1][i] < min(a[i],b[i]) for i in range(3))} for e in envelopes]
    return {'from': a, 'to': b, 'world_fraction': f, 'stock_pad_hits': pad_hits,
            'mechanism_exclusions': exclusions,
            'clear_static_ray': f == 1 and not pad_hits and all(x['strictly_disjoint'] for x in exclusions)}


def audit():
    protocol_path = OUT/'protocol-v1.json'
    if sha(protocol_path) != PROTOCOL_SHA:
        raise ValueError('Frozen protocol changed')
    p = json.loads(protocol_path.read_text(encoding='utf-8'))
    code_pin = p['stock_semantics_source']
    if sha(code_pin['path']) != code_pin['sha256'] or Path(code_pin['path']).stat().st_size != code_pin['bytes']:
        raise ValueError('Stock semantics source changed')
    cases = {}
    for name, spec in p['cases'].items():
        bsp, world, pads, liquids, envelopes = load_case(spec, p)
        dest = linked_destination(bsp.entities, spec['source_entity'], spec['destination_entity'])
        if dest != spec['destination_properties'] or bsp.entities[spec['source_entity']] != spec['source_properties'] or F(dest.get('angle','0')) != spec['destination_yaw']:
            raise ValueError('Frozen teleporter properties differ')
        initial, settled = arrival_feet(dest), spec['settled_feet']
        settlement = leg(world,pads,liquids,envelopes,initial,settled)
        paths = {}
        stations = {'settled_arrival': {'feet': settled, 'path': None, 'waypoint': None}}
        for key, route in spec['paths'].items():
            points = route['points']
            if points[0] != settled:
                raise ValueError('Departure does not begin at the frozen landing')
            legs = []
            for j,(a,b) in enumerate(zip(points,points[1:])):
                band = 4 if name == 'ztn2dm1' and j == 0 else 0
                legs.append(leg(world,pads,liquids,envelopes,a,b,band))
            paths[key] = {'kind': route['kind'], 'points': points, 'legs': legs,
                'pass': all(x['pass'] for x in legs),
                'xy_length': sum(abs(b[0]-a[0])+abs(b[1]-a[1]) for a,b in zip(points,points[1:])),
                'geometric_descent': sum(max(0,a[2]-b[2]) for a,b in zip(points,points[1:]))}
            if route['kind'] == 'onward':
                stations[key] = {'feet': points[-1], 'path': key, 'waypoint': len(points)-1}
        for station in stations.values():
            station['standing'] = leg(world,pads,liquids,envelopes,station['feet'],station['feet'])
            station['connected_from_arrival'] = settlement['pass'] and (station['path'] is None or paths[station['path']]['pass'])
        resources = []
        for r in spec['resources']:
            e = bsp.entities[r['entity']]
            feet = r['station_feet']
            if feet[:2] != origin(e)[:2]:
                raise ValueError('Resource station differs from authored XY')
            linked = r['onward_endpoint']
            if linked is not None and paths[linked]['points'][-1] != feet:
                raise ValueError('Resource binding is not an exact path endpoint')
            resources.append({'entity': r['entity'], 'properties': e, 'authored_origin': origin(e),
                'chosen_feet': feet, 'authored_height_above_chosen_floor': origin(e)[2]-feet[2],
                'standing': leg(world,pads,liquids,envelopes,feet,feet),
                'bound_path': linked, 'connected_approach_certified': linked is not None and paths[linked]['pass'] and settlement['pass'],
                'pickup_settlement_or_contact_tested': False})
        rays = []
        for key, station in stations.items():
            eye = [*station['feet']]; eye[2] += 46
            for relative in p['fixed_rays']['relative_yaws']:
                end = facing_end(eye, spec['destination_yaw']+relative, p['fixed_rays']['length'])
                rays.append({'kind':'facing', 'source':key, 'relative_yaw':relative, 'absolute_yaw':spec['destination_yaw']+relative,
                    'source_standing':station['standing']['pass'], 'source_reached':station['connected_from_arrival'],
                    **point_ray(bsp,pads,envelopes,eye,end,p['sightline_mask'])})
            for resource in resources:
                rays.append({'kind':'authored_resource', 'source':key, 'target_entity':resource['entity'],
                    'source_standing':station['standing']['pass'], 'source_reached':station['connected_from_arrival'],
                    'target_standing':resource['standing']['pass'], 'target_approach_certified':resource['connected_approach_certified'],
                    **point_ray(bsp,pads,envelopes,eye,resource['authored_origin'],p['sightline_mask'])})
        cases[name] = {'title':spec['title'], 'author':spec['author'], 'bsp_sha256':bsp.sha256,
            'source_entity':spec['source_entity'], 'destination_entity':spec['destination_entity'],
            'source_properties':bsp.entities[spec['source_entity']], 'destination_properties':dest,
            'initial_arrival_origin':[initial[0],initial[1],initial[2]+24], 'initial_arrival_feet':initial,
            'settled_feet':settled, 'settlement':settlement,
            'world_blocking_brush_count':len(world), 'liquid_brush_count':len(liquids),
            'stock_pads':{k:s.bounds for k,s in pads.items()}, 'mechanism_envelopes':envelopes,
            'paths':paths, 'stations':stations, 'resources':resources, 'rays':rays}
    return numeric({'schema_version':1, 'id':p['id'], 'protocol_sha256':PROTOCOL_SHA, 'cases':cases,
        'native_execution':False, 'human_gameplay_tested':False,
        'scope':'Fixed static world, stock solid pads and conservative mover exclusions. Stock arrival pose is source-derived; vertical settlement is a geometric candidate. Rays are floating-point point tests, hull/support use exact rational geometry. Dynamic players, KillBox results, item settlement/contact, input recovery, whole-map balance and safety are not tested.'})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    result = audit()
    target = OUT/'audit.json'
    if args.verify:
        if json.loads(target.read_text(encoding='utf-8')) != result:
            raise ValueError('Retained audit differs from full rederivation')
    else:
        if target.exists():
            raise ValueError('Refusing to overwrite retained audit; use --verify')
        target.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({name:{'settlement':c['settlement']['pass'], 'paths':{k:v['pass'] for k,v in c['paths'].items()},
        'stations':{k:v['standing']['pass'] for k,v in c['stations'].items()},
        'resources':{r['entity']:r['standing']['pass'] for r in c['resources']},
        'facing_rays':{key:[r['clear_static_ray'] for r in c['rays'] if r['kind']=='facing' and r['source']==key] for key in c['stations']}}
        for name,c in result['cases'].items()},indent=2))


if __name__ == '__main__':
    main()
