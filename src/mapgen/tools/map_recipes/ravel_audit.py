"""Compiled support and clearance for Ravel's interleaved floor routes.

Axial path rectangles are covered by the union of actual horizontal convex top
faces. Every leg is continuously swept with the full 32 by 56 standing hull.
The separate ordinary gate experiment removes graph nodes, not compiled walls.
No native movement, combat balance or universal density threshold is inferred.
"""
from itertools import product

from tools.map_recipes.drop_audit import _world, _vector, _require, _sweep, _record_vector
from tools.map_recipes.gate_audit import audit_gates

MAX_ROUTE_POINTS = 32
MAX_FLOOR_CELLS = 4096


def _flat_route(world, points, label, counts):
    """Exact rectangular support union, including every side of every elbow."""
    _require(isinstance(points, (list, tuple)) and 2 <= len(points) <= MAX_ROUTE_POINTS,
             'flat route point count outside budget')
    points = list(map(_vector, points))
    records = []
    for a, b in zip(points, points[1:]):
        _require(a[2] == b[2] and (a[0] == b[0]) != (a[1] == b[1]),
                 'flat route needs nonzero axial equal-height legs')
        low = tuple(min(a[i], b[i]) - 16 for i in range(2))
        high = tuple(max(a[i], b[i]) + 16 for i in range(2))
        xs, ys = {low[0], high[0]}, {low[1], high[1]}
        rectangles = []
        for index, _, shape in world:
            if not any(not p[0] and not p[1] and p[2] > 0 and p[3] / p[2] == a[2]
                       for p in shape.planes):
                continue
            lo = tuple(max(low[i], shape.bounds[0][i]) for i in range(2))
            hi = tuple(min(high[i], shape.bounds[1][i]) for i in range(2))
            if any(lo[i] >= hi[i] for i in range(2)):
                continue
            corners = [(x, y, a[2]) for x, y in product((lo[0], hi[0]), (lo[1], hi[1]))]
            if not all(all(sum(p[i] * v[i] for i in range(3)) <= p[3] for p in shape.planes)
                       for v in corners):
                continue
            rectangles.append((index, lo, hi))
            xs.update((lo[0], hi[0])); ys.update((lo[1], hi[1]))
        xs, ys = sorted(xs), sorted(ys)
        cells = (len(xs) - 1) * (len(ys) - 1)
        _require(cells <= MAX_FLOOR_CELLS, 'flat support partition exceeds cell budget')
        supporters = set()
        for x0, x1 in zip(xs, xs[1:]):
            for y0, y1 in zip(ys, ys[1:]):
                matches = [i for i, lo, hi in rectangles
                           if lo[0] <= x0 and x1 <= hi[0] and lo[1] <= y0 and y1 <= hi[1]]
                _require(matches, label + ' lacks continuous full-footprint floor support')
                supporters.update(matches)
        _sweep(world, a, b, label, counts)
        records.append({'from': _record_vector(a), 'to': _record_vector(b),
                        'support_brushes': sorted(supporters), 'covered_partition_cells': cells,
                        'continuous_full_footprint_support': True,
                        'continuous_forward_and_reverse_clearance': True,
                        'reverse_clearance_basis': 'identical occupied swept volume in reverse; no dynamics inferred'})
    return {'label': label, 'legs': records,
            'axis_distance': int(sum(abs(a[0]-b[0])+abs(a[1]-b[1]) for a,b in zip(points,points[1:]))),
            'points': [_record_vector(p) for p in points]}


def _crossing(world, counts):
    bounds = ((-240,-400,104),(320,-240,128))
    corners = set(product(*zip(*bounds)))
    _require(any(shape.bounds == bounds and set(shape.vertices) == corners for _,_,shape in world),
             'Ravel crosswalk differs from the complete compiled 24-thick deck')
    lower = _flat_route(world, [(32,-528,0),(32,-144,0)], 'lower Crossing route', counts)
    upper = _flat_route(world, [(-48,-320,128),(112,-320,128)], 'raised Crossing route', counts)
    # These are the actual route intersection and two independent full hulls.
    # Common XY alone would not establish support, clearance or connectivity.
    return {'shared_xy': [32,-320], 'lower_feet': [32,-320,0],
            'upper_feet': [32,-320,128], 'deck_underside':104,
            'lower_standing_height':56, 'lower_clearance_above_hull':48,
            'lower':lower,'upper':upper}


def audit_ravel(bsp, layout, report):
    _require(layout.get('map_name') == 'qmg_ravel', 'wrong Ravel map identity')
    _require(layout['layout_sha256'] == report['layout_sha256'], 'Ravel layout identity differs')
    _require(layout['levels'] == [0,128,256], 'Ravel floor contract changed')
    _require(len(layout['ramps']) == 1 and len(layout['drops']) == 1, 'Ravel ramp/drop contract changed')
    ramp = layout['ramps'][0]
    _require(ramp['from'] == [-736,-592,8] and ramp['to'] == [-736,-112,128]
             and ramp['run'] == 480 and ramp['rise'] == 120 and ramp['width'] == 160,
             'Ravel ramp destination or profile changed')
    drop = layout['drops'][0]
    _require(drop['from'] == [176,16,256] and drop['departure'] == [112,16,256]
             and drop['to'] == [112,16,128], 'Ravel local third-floor drop changed')
    envelope = [[min(r['bounds'][0][i] for r in layout['rooms']) for i in range(2)],
                [max(r['bounds'][1][i] for r in layout['rooms']) for i in range(2)]]
    _require(envelope == [[-848,-720],[848,720]], 'Ravel authored room envelope changed')
    world = _world(bsp)
    counts = {'world_pair_checks':0,'exact_swept_checks':0}
    crossing = _crossing(world, counts)
    routes = [
        _flat_route(world, [(-736,-80,128),(-704,-80,128),(-704,240,128)],
                    'Spindle high-ramp elbow', counts),
        _flat_route(world, [(448,-176,128),(448,480,128),(496,480,128),(496,640,128),
                            (768,640,128),(768,208,128)], 'Return upper bent gallery', counts),
        _flat_route(world, [(-144,176,128),(16,176,128),(16,544,128),(-352,544,128)],
                    'Crown inset to Header connector', counts),
    ]
    gates = audit_gates(layout, report)
    _require(gates['baseline_pairs'] == 264 and not gates['baseline_missing_pairs'],
             'Ravel ordinary gameplay reachability failed')
    _require(len(gates['passages']) == 9 and all(not row['unreachable_pairs'] for row in gates['passages']),
             'Ravel has a required single-passage dependency')
    return {'ok':True, 'layout_sha256':layout['layout_sha256'],
            'method':'exact stored compiled planes; continuous axial standing-hull sweeps and coplanar floor union',
            'world_player_blocking_brushes':len(world), 'crossing':crossing, 'bent_routes':routes,
            'swept_route_segments':2+sum(len(row['legs']) for row in routes),
            'room_envelope_xy':envelope, 'room_footprint_dimensions':[1696,1440],
            'ordinary_gate_dependencies':gates, **counts,
            'runtime_verified':False, 'balance_verified':False,
            'limits':['The selected compiled paths establish local support and clearance, not an all-surface navigation mesh.',
                      'The long ramp and local drop have separate compiled audits; their dynamics require native trials.',
                      'Passage-node removal is a serialized graph intervention, not a compiled physical closure.',
                      'Floor packing is an authored envelope descriptor, not a quality or combat-density score.']}
