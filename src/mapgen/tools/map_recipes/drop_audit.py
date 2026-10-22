"""Bounded compiled drop clearance plus independently searched ordinary return.

The candidate descent is an axial horizontal departure at fixed feet height,
then a vertical box translation onto a supported landing. It is deliberately
not a ballistic trajectory, pmove simulation, fall-damage or harmless-fall test.
Return reachability comes from the serialized typed navigation graph, with exact
local endpoint binding; it is not a full compiled sweep of that return route.
"""

from collections import deque
from fractions import Fraction
from math import isfinite
import re

from tools.map_recipes.geometry import (
    GeometryError, convex_polyhedron, intersects_aabb, intersects_swept_aabb, model_brushes,
)


MASK_PLAYERSOLID = 1 | 2 | (1 << 16) | (1 << 25)
MAX_WORLD_BRUSHES = 4096
MAX_DROPS = 16
MAX_NODES = 8192
MAX_EDGES = 65536
MAX_COORDINATE = 65536
MAX_DEPARTURE_DISTANCE = 1024
MAX_DROP_HEIGHT = 128
ORDINARY_KINDS = frozenset(('walk', 'step', 'door'))
EDGE_KINDS = ORDINARY_KINDS | frozenset(('jump', 'drop', 'lift', 'teleporter'))
HULL_MINS = (-16, -16, -24)
HULL_MAXS = (16, 16, 32)


def _require(condition, message):
    if not condition:
        raise GeometryError(message)


def _number(value):
    _require(not isinstance(value, bool) and isinstance(value, (int, float, Fraction)),
             'drop audit requires numeric coordinates')
    _require(not isinstance(value, float) or isfinite(value), 'drop audit has a nonfinite coordinate')
    value = Fraction(value)
    _require(abs(value) <= MAX_COORDINATE, 'drop coordinate exceeds cap')
    return value


def _vector(value):
    _require(isinstance(value, (list, tuple)) and len(value) == 3, 'drop pose requires three coordinates')
    return tuple(_number(v) for v in value)


def _integer(value, label):
    _require(isinstance(value, int) and not isinstance(value, bool) and 0 < value <= 0xffffffff,
             f'{label} must be a positive integer')
    return value


def _record_number(value):
    value = Fraction(value)
    return int(value) if value.denominator == 1 else float(value)


def _record_vector(value):
    return [_record_number(v) for v in value]


def _box(feet):
    return ((feet[0] - 16, feet[1] - 16, feet[2]),
            (feet[0] + 16, feet[1] + 16, feet[2] + 56))


def _pose_record(feet):
    return {'feet': _record_vector(feet), 'feet_exact': [str(v) for v in feet],
            'standing_origin': _record_vector((feet[0], feet[1], feet[2] + 24)),
            'hull': [_record_vector(v) for v in _box(feet)]}


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def _world(bsp):
    indices = model_brushes(bsp)
    _require(len(indices) <= MAX_WORLD_BRUSHES, 'drop world-brush budget exceeded')
    world = []
    for index in indices:
        first, count, contents = bsp.brushes[index]
        if not contents & MASK_PLAYERSOLID:
            continue
        _require(isinstance(first, int) and isinstance(count, int) and first >= 0 and count >= 0
                 and first + count <= len(bsp.brushsides), 'drop brushside range is invalid')
        planes = []
        for plane_index, _ in bsp.brushsides[first:first + count]:
            _require(isinstance(plane_index, int) and not isinstance(plane_index, bool)
                     and 0 <= plane_index < len(bsp.planes), 'drop brush plane index is invalid')
            planes.append(bsp.planes[plane_index])
        world.append((index, contents, convex_polyhedron(planes)))
    return world


def _flat_support(world, feet, label):
    low, high = _box(feet)
    corners = [(x, y, feet[2]) for x in (low[0], high[0]) for y in (low[1], high[1])]
    supporters = []
    for index, _, shape in world:
        if (any(not p[0] and not p[1] and p[2] > 0 and p[3] / p[2] == feet[2]
                for p in shape.planes)
                and all(all(_dot(p[:3], corner) <= p[3] for p in shape.planes) for corner in corners)):
            supporters.append(index)
        _require(not intersects_aabb(shape, low, high), f'{label} standing hull overlaps brush {index}')
    _require(supporters, f'{label} lacks compiled flat support for the complete standing footprint')
    return supporters


def _sweep(world, source, target, label, counts):
    start, finish = _box(source), _box(target)
    delta = tuple(target[i] - source[i] for i in range(3))
    low = tuple(min(start[0][i], finish[0][i]) for i in range(3))
    high = tuple(max(start[1][i], finish[1][i]) for i in range(3))
    for index, _, shape in world:
        counts['world_pair_checks'] += 1
        if any(high[i] <= shape.bounds[0][i] or low[i] >= shape.bounds[1][i] for i in range(3)):
            continue
        counts['exact_swept_checks'] += 1
        _require(not intersects_swept_aabb(shape, *start, delta),
                 f'{label} continuous standing-hull sweep overlaps player-blocking brush {index}')


def _floor(area, x, y):
    _require(area['min_x'] <= x <= area['max_x'] and area['min_y'] <= y <= area['max_y'],
             'drop return endpoint is outside its node area')
    slope = area.get('slope')
    if slope is None:
        return Fraction(area['floor_z'])
    _require(isinstance(slope, dict) and isinstance(slope.get('along_x'), bool), 'invalid return slope metadata')
    values = {key: _number(slope[key]) for key in ('start', 'end', 'low_z', 'rise', 'support_offset')}
    _require(all(v.denominator == 1 for v in values.values()), 'return slope metadata must use the integer grid')
    run = values['end'] - values['start']
    _require(run and 0 < values['rise'] <= abs(run), 'return slope is outside its axial contract')
    axis = x if slope['along_x'] else y
    exact = values['low_z'] + values['rise'] * (axis - values['start'] + values['support_offset']) / run
    # Match the serialized typed graph's conservative grid representation. This
    # is local endpoint binding, not a compiled ramp support-height substitute.
    return Fraction(-(-exact.numerator // exact.denominator))


def _graph(layout, report):
    _require(isinstance(report, dict), 'drop return requires the complete navigation report')
    try:
        navigation = report['analysis']['navigation']['player']
        graph, profile = navigation['graph'], navigation['profile']
    except (KeyError, TypeError) as error:
        raise GeometryError('drop return report lacks serialized player navigation') from error
    identity = layout.get('layout_sha256')
    _require(isinstance(identity, str) and re.fullmatch('[0-9a-f]{64}', identity),
             'drop layout needs its canonical SHA-256 identity')
    _require(report.get('layout_sha256') == navigation.get('layout_sha256') == identity,
             'drop return graph/layout identity mismatch')
    _require(navigation.get('ok') is True, 'drop return navigation report is not valid')
    _require(profile.get('game') == 'quake2' and profile.get('hull_mins') == list(HULL_MINS)
             and profile.get('hull_maxs') == list(HULL_MAXS), 'drop return requires the stock Q2 standing hull')
    _require(isinstance(graph, dict) and isinstance(graph.get('nodes'), list)
             and isinstance(graph.get('edges'), list), 'malformed drop return graph')
    _require(0 < len(graph['nodes']) <= MAX_NODES and len(graph['edges']) <= MAX_EDGES,
             'drop return graph budget exceeded')
    nodes = {}
    for node in graph['nodes']:
        node_id = _integer(node['id'], 'return node id')
        _require(node_id not in nodes, 'duplicate return node id')
        area = node['area']
        _require(isinstance(area, dict), 'return node area is malformed')
        for field in ('min_x', 'min_y', 'max_x', 'max_y', 'floor_z', 'ceiling_z'):
            _require(_number(area[field]).denominator == 1, 'return node area must use the integer grid')
        _require(area['min_x'] < area['max_x'] and area['min_y'] < area['max_y']
                 and area['floor_z'] < area['ceiling_z'], 'return node has degenerate area')
        origin = _vector(node['origin'])
        _require(origin[2] == _floor(area, origin[0], origin[1]) + 24,
                 'return node origin disagrees with its local surface height')
        nodes[node_id] = node
    adjacency = {key: [] for key in nodes}
    edges, excluded = {}, 0
    for edge in graph['edges']:
        edge_id = _integer(edge['id'], 'return edge id')
        _require(edge_id not in edges, 'duplicate return edge id')
        _require(edge.get('kind') in EDGE_KINDS, 'unknown return graph transition kind')
        _integer(edge.get('from'), 'return source node id')
        _integer(edge.get('to'), 'return destination node id')
        _require(edge.get('from') in nodes and edge.get('to') in nodes, 'return edge references a missing node')
        edges[edge_id] = edge
        if edge['kind'] in ORDINARY_KINDS:
            adjacency[edge['from']].append((edge['to'], edge_id))
        else:
            excluded += 1
    for neighbors in adjacency.values():
        neighbors.sort()
    return nodes, edges, adjacency, excluded


def _bindings(nodes, feet):
    matches = []
    for node_id, node in nodes.items():
        area = node['area']
        if (area['min_x'] <= feet[0] <= area['max_x'] and area['min_y'] <= feet[1] <= area['max_y']
                and _floor(area, feet[0], feet[1]) == feet[2]
                and feet[2] + 56 <= area['ceiling_z']):
            matches.append(node_id)
    _require(matches, 'drop return endpoint has no exact local-height graph binding')
    return sorted(matches)


def _ordinary_return(nodes, edges, adjacency, landing, takeoff):
    starts, goals = _bindings(nodes, landing), _bindings(nodes, takeoff)
    goal_set = set(goals)
    previous = {key: None for key in starts}
    pending = deque(starts)
    reached, expansions = None, 0
    while pending:
        current = pending.popleft()
        expansions += 1
        _require(expansions <= MAX_NODES, 'drop ordinary-return search budget exceeded')
        if current in goal_set:
            reached = current
            break
        for neighbor, edge_id in adjacency[current]:
            if neighbor not in previous:
                previous[neighbor] = (current, edge_id)
                pending.append(neighbor)
    _require(reached is not None, 'drop landing lacks an ordinary walk/step/door return to its takeoff')
    node_ids, edge_ids = [reached], []
    while previous[reached] is not None:
        reached, edge_id = previous[reached]
        node_ids.append(reached)
        edge_ids.append(edge_id)
    node_ids.reverse()
    edge_ids.reverse()
    return {'landing_binding_candidates': starts, 'takeoff_binding_candidates': goals,
            'bound_landing_node': node_ids[0], 'bound_takeoff_node': node_ids[-1],
            'node_ids': node_ids, 'edge_ids': edge_ids,
            'edge_kinds': [edges[key]['kind'] for key in edge_ids],
            'search_expansions': expansions, 'endpoint_snap_distance': 0,
            'graph_reachability_verified': True, 'compiled_entire_return_swept': False}


def audit_drops(bsp, layout, report):
    """Audit declared two-leg drops and ordinary returns; fail closed on either.

    layout.drops rows: {id, label, from:[x,y,feet_z], departure:[x,y,feet_z],
    to:[x,y,feet_z]}. Departure is one nonzero axial horizontal leg; its XY is
    also the landing XY. Drop height must be positive and at most 128 units.
    Node binding uses the supplied serialized report, never a nearest-node guess.
    """
    drops = layout.get('drops')
    _require(isinstance(drops, list) and 0 < len(drops) <= MAX_DROPS, 'drop count outside audit budget')
    world = _world(bsp)
    nodes, edges, adjacency, excluded = _graph(layout, report)
    counts = {'world_pair_checks': 0, 'exact_swept_checks': 0}
    records, identifiers = [], set()
    for drop in drops:
        _require(isinstance(drop, dict) and {'id', 'label', 'from', 'departure', 'to'} <= drop.keys(),
                 'drop metadata is incomplete')
        drop_id = _integer(drop['id'], 'drop id')
        _require(drop_id not in identifiers, 'duplicate drop id')
        identifiers.add(drop_id)
        _require(isinstance(drop['label'], str) and 0 < len(drop['label']) <= 160, 'drop label is invalid')
        source, departure, landing = (_vector(drop[key]) for key in ('from', 'departure', 'to'))
        delta = tuple(departure[i] - source[i] for i in range(2))
        _require(source[2] == departure[2] and sum(v != 0 for v in delta) == 1
                 and sum(abs(v) for v in delta) <= MAX_DEPARTURE_DISTANCE,
                 'drop departure must be one bounded axial horizontal leg')
        height = departure[2] - landing[2]
        _require(departure[:2] == landing[:2] and 0 < height <= MAX_DROP_HEIGHT,
                 'drop descent must be vertical and within the 128-unit geometric bound')
        top_support = _flat_support(world, source, 'drop takeoff')
        lower_support = _flat_support(world, landing, 'drop landing')
        _sweep(world, source, departure, 'drop departure', counts)
        _sweep(world, departure, landing, 'drop descent', counts)
        return_path = _ordinary_return(nodes, edges, adjacency, landing, source)
        records.append({'id': drop_id, 'label': drop['label'], 'from': _pose_record(source),
                        'departure': _pose_record(departure), 'to': _pose_record(landing),
                        'geometric_drop_height': _record_number(height),
                        'geometric_departure_distance': _record_number(sum(abs(v) for v in delta)),
                        'takeoff_full_footprint_support_brushes': top_support,
                        'landing_full_footprint_support_brushes': lower_support,
                        'supporting_ledge_included_in_collision_checks': True,
                        'departure_and_descent_sweeps_clear': True, 'ordinary_return': return_path})
    return {'kind': 'compiled_two_leg_drop_and_typed_ordinary_return_audit', 'ok': True,
            'layout_sha256': layout['layout_sha256'], 'drops_checked': len(records), 'drops': records,
            'world_player_blocking_brushes': len(world), 'player_collision_mask': MASK_PLAYERSOLID,
            'standing_hull_dimensions': [32, 32, 56], 'collision_epsilon': 0,
            'swept_route_segments': 2 * len(records), **counts,
            'return_allowed_edge_kinds': sorted(ORDINARY_KINDS),
            'excluded_return_graph_edges': excluded,
            'runtime_verified': False, 'ballistic_trajectory_tested': False, 'fall_damage_tested': False,
            'limits': ['The candidate holds feet height during horizontal departure, then descends vertically. Native acceleration, gravity, momentum and air control are not simulated.',
                       'A clear geometric drop is not evidence of a harmless fall, safe landing, usable combat escape or measured travel time.',
                       'All model-zero player-mask brushes, including the supporting ledge, remain in exact sweeps. Dynamic actors, mover states and occupied landings are untested.',
                       'Takeoff and landing have full-footprint compiled flat support and clear standing hulls. Airborne intermediate poses are not described as grounded.',
                       'The ordinary return is independently searched on the serialized typed graph with exact local endpoint binding and no snapping. Its entire route is not independently swept in the compiled world.',
                       'Jump, drop, lift and teleporter edges are excluded from every return. Door activation, where present, still depends on the source navigation contract.']}
