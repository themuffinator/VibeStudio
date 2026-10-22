"""Bounded compiled corridor evidence for q2rdm2's offset lower circuit.

This is a static full-hull certificate, not native movement or whole-map topology.
Run from the repository root; all generated evidence stays under out/.
"""
from __future__ import annotations

import argparse
from fractions import Fraction as F
import hashlib
import heapq
from itertools import product
import json
from pathlib import Path
import sys

from tools.map_study.binary import Bsp, read_bytes
from tools.map_recipes.geometry import convex_polyhedron, model_brushes, intersects_aabb, intersects_swept_aabb

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'out/interlocking-topology-study'
SOURCE = Path('E:/Development/q2maparchive-wip/maps/q2rmappack/pack/maps/q2rdm2.bsp')
EXPECTED_SHA = 'd6ee3b39d4e557e7ad5cd39d2bf955e8251edd7e491f2033eee80500791d7199'
MASK = 1 | 2 | (1 << 16) | (1 << 25)


def numeric(value):
    if isinstance(value, F):
        return int(value) if value.denominator == 1 else float(value)
    if isinstance(value, (list, tuple)):
        return [numeric(v) for v in value]
    if isinstance(value, dict):
        return {k: numeric(v) for k, v in value.items()}
    return value


def hull(point):
    return tuple(F(v) + d for v, d in zip(point, (-16, -16, 0))), tuple(F(v) + d for v, d in zip(point, (16, 16, 56)))


def broad_overlap(low, high, bounds):
    return all(low[i] < bounds[1][i] and bounds[0][i] < high[i] for i in range(3))


def floor_support(world, start, end, max_floor_drop=0):
    """Conservative exact rectangle partition of the complete swept footprint.

    A candidate cap rectangle is admitted only when all its corners lie inside
    that actual convex solid at the requested horizontal plane. Curved/diagonal
    caps can be rejected conservatively; no bounds-only support inference. A
    nonzero band proves continuous actual ground within that depth below the
    clear hull envelope, not constant floor contact or simulated native motion.
    """
    if start[2] != end[2] or (start[0] != end[0] and start[1] != end[1]):
        raise ValueError('Support certificates require flat axial segments')
    low = tuple(F(min(start[i], end[i]) - 16) for i in range(2))
    high = tuple(F(max(start[i], end[i]) + 16) for i in range(2))
    cuts = [{low[i], high[i]} for i in range(2)]
    caps = []
    max_floor_drop = F(max_floor_drop)
    if max_floor_drop not in (0, 2, F(129, 64)):
        raise ValueError('Only the declared flat, nominal two-unit or 129/64-unit shallow band is supported')
    for bid, solid in world.items():
        lo = tuple(max(low[i], solid.bounds[0][i]) for i in range(2))
        hi = tuple(min(high[i], solid.bounds[1][i]) for i in range(2))
        if any(lo[i] >= hi[i] for i in range(2)):
            continue
        planes = []
        for top in solid.planes:
            if top[2] <= 0:
                continue
            vertices = [(*xy, (top[3] - top[0] * xy[0] - top[1] * xy[1]) / top[2])
                        for xy in product(*zip(lo, hi))]
            if all(start[2] - max_floor_drop <= v[2] <= start[2]
                   and all(sum(p[i] * v[i] for i in range(3)) <= p[3] for p in solid.planes)
                   for v in vertices):
                planes.append(top)
        if not planes:
            continue
        caps.append((bid, lo, hi))
        for i in range(2):
            cuts[i].update((lo[i], hi[i]))
    cells, used = [], set()
    axes = [sorted(v) for v in cuts]
    for x0, x1 in zip(axes[0], axes[0][1:]):
        for y0, y1 in zip(axes[1], axes[1][1:]):
            found = [bid for bid, lo, hi in caps if lo[0] <= x0 and x1 <= hi[0] and lo[1] <= y0 and y1 <= hi[1]]
            if not found:
                return {'pass': False, 'uncovered_cell': [x0, y0, x1, y1]}
            used.update(found)
            cells.append({'bounds': [[x0, y0], [x1, y1]], 'brushes': found})
    return {'pass': True, 'support_brushes': sorted(used), 'cells': cells,
            'floor_height_band': [start[2] - max_floor_drop, start[2]],
            'contract': 'constant full-footprint support' if not max_floor_drop else 'complete projected ground within declared band below clear hull envelope; native contact untested'}


def load_world():
    bsp = Bsp(read_bytes(SOURCE))
    if bsp.sha256 != EXPECTED_SHA:
        raise ValueError('Reference BSP differs from the reviewed version')
    world, liquids = {}, {}
    for bid in model_brushes(bsp):
        first, count, contents = bsp.brushes[bid]
        if not contents & (MASK | 56):
            continue
        solid = convex_polyhedron([bsp.planes[bsp.brushsides[i][0]] for i in range(first, first + count)])
        if contents & MASK:
            world[bid] = solid
        if contents & 56:
            liquids[bid] = solid
    return bsp, world, liquids


def check_segment(world, liquids, start, end, max_floor_drop=0):
    low, high = hull(start)
    delta = tuple(F(end[i] - start[i]) for i in range(3))
    swept_low = tuple(min(low[i], low[i] + delta[i]) for i in range(3))
    swept_high = tuple(max(high[i], high[i] + delta[i]) for i in range(3))
    blockers = [bid for bid, s in world.items() if broad_overlap(swept_low, swept_high, s.bounds)
                and intersects_swept_aabb(s, low, high, delta)]
    wet_low = (*low[:2], low[2] - F(max_floor_drop))
    wet_swept_low = (*swept_low[:2], swept_low[2] - F(max_floor_drop))
    wet = [bid for bid, s in liquids.items() if broad_overlap(wet_swept_low, swept_high, s.bounds)
           and intersects_swept_aabb(s, wet_low, high, delta)]
    support = floor_support(world, start, end, max_floor_drop)
    return {'from_feet': start, 'to_feet': end, 'world_blockers': blockers,
            'liquid_brushes': wet, 'support': support, 'pass': not blockers and not wet and support['pass']}


def mechanism_envelopes(bsp):
    result = []
    for entity, properties in enumerate(bsp.entities):
        if not properties.get('model', '').startswith('*'):
            continue
        model = bsp.models[int(properties['model'][1:])]
        low, high = list(model[:3]), list(model[3:6])
        kind = properties['classname']
        if kind == 'func_door':
            if properties.get('angle') != '-2' or properties.get('origin', '0 0 0') != '0 0 0':
                raise ValueError('Door movement differs from the reviewed downward-only contract')
            # Authored lip0 receives stock default8. Full model height is a
            # conservative translation bound covering both positions and travel.
            low[2] -= high[2] - low[2]
            basis = 'Downward full-height sweep, conservative over stock lip8 distance'
        elif kind in ('trigger_push', 'trigger_multiple'):
            basis = 'Authored trigger submodel bounds'
        else:
            raise ValueError(f'Unreviewed brush entity: {kind}')
        result.append({'entity': entity, 'properties': properties, 'bounds': [low, high], 'basis': basis})
    return result


def route_samples(points, spacing):
    lengths = [sum(abs(b[i] - a[i]) for i in range(2)) for a, b in zip(points, points[1:])]
    total = sum(lengths)
    bends, distance = [0], 0
    for length in lengths:
        distance += length
        bends.append(distance)
    samples = []
    for distance in sorted(set(range(0, total + 1, spacing)) | set(bends)):
        edge = min(next((i for i, limit in enumerate(bends[1:]) if distance <= limit), len(lengths) - 1), len(lengths) - 1)
        t = F(distance - bends[edge], lengths[edge])
        point = [F(points[edge][i]) + t * (points[edge+1][i] - points[edge][i]) for i in range(3)]
        samples.append({'distance': distance, 'envelope_feet': point})
    return samples


def witness_graph(protocol):
    routes = [(name, protocol['routes'][name]) for name in protocol['expected_pass']]
    edges = []
    for name, spec in routes:
        points = spec if isinstance(spec, list) else spec['points']
        band = 0 if isinstance(spec, list) else spec.get('max_floor_drop', 0)
        for i, (a, b) in enumerate(zip(points, points[1:])):
            edges.append({'id': f'{name}/{i}', 'a': tuple(a), 'b': tuple(b),
                          'length': sum(abs(a[k] - b[k]) for k in range(2)),
                          'contract': 'shallow_band' if band else 'constant_floor'})
    first = routes[0][1]
    points = first if isinstance(first, list) else first['points']
    start, goal = tuple(points[0]), tuple(points[-1])

    def shortest(flat_only=False, removed=()):
        graph = {}
        for e in edges:
            if e['id'] in removed or (flat_only and e['contract'] != 'constant_floor'):
                continue
            for a, b in [(e['a'], e['b']), (e['b'], e['a'])]:
                graph.setdefault(a, []).append((b, e['length']))
        queue, seen = [(0, start)], {}
        while queue:
            distance, node = heapq.heappop(queue)
            if node in seen:
                continue
            seen[node] = distance
            if node == goal:
                return distance
            for neighbor, cost in graph.get(node, []):
                if neighbor not in seen:
                    heapq.heappush(queue, (distance + cost, neighbor))
        return None

    return {'nodes': sorted({tuple(p) for e in edges for p in (e['a'], e['b'])}), 'edges': edges,
            'start': start, 'goal': goal, 'all_geometric_contracts_length': shortest(),
            'constant_floor_only_length': shortest(True),
            'individual_edge_removals': [{'edge': e['id'], 'all_contracts_length': shortest(False, [e['id']]),
                                           'constant_floor_only_length': shortest(True, [e['id']])} for e in edges],
            'both_middle_approaches_removed_length': shortest(False, ['north-ground-band/1', 'south-flat/3']),
            'limit': 'Only the two measured polylines; no whole-map shortest path, centrality, ordinary-motion or physical closure claim'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--protocol', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(OUT.resolve()) or output.suffix != '.json':
        raise ValueError('Output must be a JSON file under the topology study output root')
    protocol = json.loads(args.protocol.read_text(encoding='utf-8'))
    bsp, world, liquids = load_world()
    results = {}
    for name, spec in protocol['routes'].items():
        points = spec if isinstance(spec, list) else spec['points']
        max_floor_drop = 0 if isinstance(spec, list) else spec.get('max_floor_drop', 0)
        segments = [check_segment(world, liquids, a, b, max_floor_drop) for a, b in zip(points, points[1:])]
        results[name] = {'segments': segments, 'pass': all(s['pass'] for s in segments),
                         'length': sum(sum(abs(b[i] - a[i]) for i in range(2)) for a, b in zip(points, points[1:]))}
        print(name, results[name]['pass'], [(s['from_feet'], s['world_blockers'], s['support'].get('uncovered_cell')) for s in segments if not s['pass']], flush=True)
    result = {'schema_version': 1, 'bsp_path': str(SOURCE), 'bsp_sha256': bsp.sha256,
              'blocking_world_brushes': len(world), 'liquid_world_brushes': len(liquids),
              'mask': MASK, 'hull': [32, 32, 56], 'collision_epsilon': 0,
              'protocol_sha256': hashlib.sha256(args.protocol.read_bytes()).hexdigest(), 'routes': results}
    if 'expected_pass' in protocol:
        for name in protocol['expected_pass']:
            if not results[name]['pass']:
                raise ValueError(f'Expected route certificate failed: {name}')
        for name in protocol['expected_fail']:
            if results[name]['pass']:
                raise ValueError(f'Negative control unexpectedly passed: {name}')
        envelopes = mechanism_envelopes(bsp)
        exclusions, rays = [], []
        for name in protocol['expected_pass']:
            spec = protocol['routes'][name]
            points = spec if isinstance(spec, list) else spec['points']
            band = 0 if isinstance(spec, list) else spec.get('max_floor_drop', 0)
            for i, (a, b) in enumerate(zip(points, points[1:])):
                low = [min(a[k], b[k]) + (-16, -16, -band)[k] for k in range(3)]
                high = [max(a[k], b[k]) + (16, 16, 56)[k] for k in range(3)]
                for envelope in envelopes:
                    separated = any(high[k] < envelope['bounds'][0][k] or envelope['bounds'][1][k] < low[k] for k in range(3))
                    if not separated:
                        raise ValueError(f'Route overlaps a known mechanism envelope: {name}/{i}')
                    exclusions.append({'route': name, 'segment': i, 'entity': envelope['entity'], 'strictly_disjoint': True})
            samples = route_samples(points, protocol['sampling']['spacing'])
            for source in protocol['sampling']['opponent_eyes']:
                sample_rays = []
                for sample in samples:
                    target = [float(v) for v in sample['envelope_feet']]
                    target[2] += protocol['sampling']['eye_above_envelope']
                    fraction = bsp.trace(source, target, mask=3)
                    sample_rays.append({**sample, 'target_eye': target, 'world_fraction': fraction, 'visible': fraction == 1})
                rays.append({'route': name, 'source_eye': source, 'visible_samples': sum(s['visible'] for s in sample_rays),
                             'total_samples': len(sample_rays), 'samples': sample_rays})
        result.update({'mechanism_envelopes': envelopes, 'mechanism_exclusions': exclusions,
                       'fixed_world_eye_rays': rays, 'witness_graph': witness_graph(protocol),
                       'viewpoint_station_checks': [check_segment(world, liquids,
                           [eye[0], eye[1], eye[2] - 46], [eye[0], eye[1], eye[2] - 46])
                           for eye in protocol['sampling']['opponent_eyes']],
                       'runtime_tested': False,
                       'limits': ['North uses a declared ground band rather than exact native or constant-floor contact.',
                                  'Rays use fixed clearance-envelope+46 sample height; not exposure time, facing or multiplayer control.',
                                  'A witness graph is not the complete reference navigation graph.']})
        if not all(row['pass'] for row in result['viewpoint_station_checks']):
            raise ValueError('An observer station lacks strict flat support or clearance')
        roof = min((p[3] - p[0] * 432 - p[1] * 816) / p[2] for p in world[106].planes if p[2] > 0)
        result['shallow_floor_detail'] = {'slope_brush': 106, 'probe_xy': [432, 816],
            'exact_roof_z_fraction': str(roof), 'roof_z': roof,
            'exact_gap_from_minus288_fraction': str(F(-288) - roof),
            'gap_from_minus288': F(-288) - roof, 'declared_band_depth': F(129, 64),
            'horizontal_recess_caps': [107, 108],
            'note': 'Stored BSP slope is slightly below nominal−290; no collision epsilon or plane snapping is used.'}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(numeric(result), indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
