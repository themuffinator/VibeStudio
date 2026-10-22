"""Compiled arrival geometry and explicit ordinary journeys for Relay.

Full standing-hull sweeps and exact floor-cap unions are separate from stock
game-module activation. Entity discs participate in the route collision checks.
No path is selected by searching until it passes.
"""
import argparse
from itertools import product
import json
from math import dist, isfinite
from pathlib import Path

from tools.map_recipes.geometry import convex_polyhedron, intersects_aabb, intersects_swept_aabb
from tools.map_study.binary import Bsp
from tools.map_study.interlocking_topology import check_segment, hull, numeric
from tools.mp_portfolio.inspect import read, sha, validate_report, validated_world


def require(ok, message):
    if not ok:
        raise ValueError(message)


def point(value):
    require(isinstance(value, (list, tuple)) and len(value) == 3 and
            all(type(v) in (int, float) and isfinite(v) and abs(v) <= 65536 for v in value),
            'Expected a finite bounded XYZ point')
    return list(value)


def box(low, high):
    return convex_polyhedron([(1, 0, 0, high[0]), (-1, 0, 0, -low[0]),
                              (0, 1, 0, high[1]), (0, -1, 0, -low[1]),
                              (0, 0, 1, high[2]), (0, 0, -1, -low[2])])


def entity_origin(entity):
    return point([float(v) for v in entity['origin'].split()])


def unique_entity(entities, classname):
    matches = [(i, e) for i, e in enumerate(entities) if e.get('classname') == classname]
    require(len(matches) == 1, 'Need exactly one ' + classname)
    return matches[0]


def pads_and_arrival(bsp, world, specification):
    source_id, source = unique_entity(bsp.entities, 'misc_teleporter')
    destination_id, destination = unique_entity(bsp.entities, 'misc_teleporter_dest')
    require(source.get('target') and source['target'] == destination.get('targetname'),
            'Teleporter target mismatch')
    require(sum(e.get('targetname') == source['target'] for e in bsp.entities) == 1,
            'Teleporter target is not unique across all entities')
    require(entity_origin(source) == point(specification['source_origin']) and
            entity_origin(destination) == point(specification['destination_origin']),
            'Compiled mechanism origin differs from declared contract')
    yaw = float(destination.get('angle', 'nan'))
    require(isfinite(yaw) and 0 <= yaw < 360 and yaw == specification['arrival_yaw'],
            'Compiled arrival yaw differs from declared contract')
    rows, dynamic = [], {}
    for index, (eid, entity, offset) in enumerate(((source_id, source, 8), (destination_id, destination, 10))):
        origin = entity_origin(entity)
        floor = origin[2] - 24
        low, high = [origin[0]-32, origin[1]-32, floor], [origin[0]+32, origin[1]+32, floor+8]
        support = [check_segment(world, {}, [origin[0]+dx, origin[1]+dy, floor],
                                  [origin[0]+dx, origin[1]+dy, floor])
                   for dx, dy in product((-16, 16), repeat=2)]
        require(all(p['pass'] for p in support), 'Full 64x64 disc support/clearance failed')
        feet = [origin[0], origin[1], floor + offset]
        require(not any(intersects_aabb(shape, *hull(feet)) for shape in world.values()),
                'Raised native standing pose lacks headroom')
        require(not any(intersects_aabb(shape, low, high) for shape in world.values()),
                'Native disc overlaps world collision')
        dynamic[-index-1] = box(low, high)
        rows.append({'entity': eid, 'origin': origin, 'disc_bounds': [low, high],
                     'standing_feet': feet, 'standing_origin': [*feet[:2], feet[2]+24],
                     'support_quadrants': support})
    arrival = rows[1]['standing_feet']
    settled = [*arrival[:2], arrival[2]-2]
    complete = dict(world)
    complete.update(dynamic)
    require(not any(intersects_swept_aabb(shape, *hull(arrival), (0, 0, -2))
                    for shape in complete.values()), 'Arrival-to-disc geometric settling column blocked')
    return rows, dynamic, {'arrival_feet': arrival, 'disc_settled_feet': settled,
                            'clear_two_unit_settle': True, 'yaw': yaw,
                            'native_activation_tested_by_this_audit': False}


def audit_paths(world, paths):
    require(isinstance(paths, list) and 4 <= len(paths) <= 8, 'Need a bounded explicit path set')
    require(len({p['id'] for p in paths}) == len(paths), 'Repeated path identity')
    results = []
    for path in paths:
        require(path['kind'] in {'departure', 'ordinary_bypass', 'resource_approach'}, 'Unknown path kind')
        points = [point(p) for p in path['points']]
        require(2 <= len(points) <= 64, 'Need 2..64 path stations')
        legs = []
        for a, b in zip(points, points[1:]):
            require(a[2] == b[2] and sum(a[i] != b[i] for i in range(2)) == 1,
                    'Path must consist of nonzero flat axial legs')
            legs.append(check_segment(world, {}, a, b))
        results.append({**path, 'legs': legs, 'pass': all(s['pass'] for s in legs),
                        'xy_length': sum(dist(a[:2], b[:2]) for a, b in zip(points, points[1:]))})
    return results


def passage_stations(points, volumes):
    """Find actual path intersections with a passage's hull-eroded interior."""
    result = []
    for a, b in zip(points, points[1:]):
        axis = next(i for i in range(2) if a[i] != b[i])
        cross = 1-axis
        for low, high in volumes:
            if (a[2] != low[2] or a[2]+56 > high[2] or
                    not low[cross]+16 <= a[cross] <= high[cross]-16):
                continue
            start = max(min(a[axis], b[axis]), low[axis]+16)
            end = min(max(a[axis], b[axis]), high[axis]-16)
            if start <= end:
                station = list(a)
                station[axis] = (start+end)/2
                result.append(station)
    return result


def bind_departures(layout, paths, arrival_feet):
    """Certify ground-room departure, passage crossing and opposite-room arrival.

    Relay's destination origin is floor+24 and the stock arrival adds10. These
    room bindings concern its ordinary floor, separately from native settlement.
    """
    def inside(room, station):
        low, high = room['bounds']
        return (station[2] == low[2] and station[2]+56 <= high[2] and
                all(low[i]+16 <= station[i] <= high[i]-16 for i in range(2)))

    arrival_floor = [*arrival_feet[:2], arrival_feet[2]-10]
    rooms = layout['rooms']
    arrival_rooms = [r for r in rooms if inside(r, arrival_floor)]
    require(len(arrival_rooms) == 1, 'Arrival must bind one ordinary ground room')
    room = arrival_rooms[0]
    other_rooms, connection_ids, bindings = [], [], []
    departures = [p for p in paths if p['kind'] == 'departure']
    require(len(departures) == 2, 'Need two declared departures')
    for path in departures:
        require(inside(room, path['points'][0]), 'Off-disc departure must begin in the arrival room')
        require(dist(path['points'][0][:2], arrival_feet[:2]) >= 64,
                'Ordinary departure must start off the native disc')
        candidates = [c for c in layout['connections'] if c['id'] == path['exit_connection']]
        require(len(candidates) == 1, 'Unknown departure exit connection')
        connection = candidates[0]
        endpoints = [connection['from'], connection['to']]
        require(endpoints.count(room['id']) == 1, 'Departure connection must be incident to the arrival room')
        other_id = next(i for i in endpoints if i != room['id'])
        others = [r for r in rooms if r['id'] == other_id]
        require(len(others) == 1 and inside(others[0], path['points'][-1]),
                'Departure must finish beyond its passage inside the opposite room')
        stations = passage_stations(path['points'], connection['volumes'])
        require(stations, 'Departure does not cross a full-footprint station in its named passage')
        path['derived_passage_stations'] = stations
        other_rooms.append(other_id)
        connection_ids.append(connection['id'])
        bindings.append({'path': path['id'], 'arrival_room': room['id'],
                         'connection': connection['id'], 'destination_room': other_id,
                         'full_footprint_passage_stations': stations})
    require(len(set(connection_ids)) == len(set(other_rooms)) == 2,
            'Departures must reach distinct passages and opposite rooms')
    return bindings


def audit(bsp, layout, report, features):
    validate_report(layout, report)
    require(features['schema_version'] == 1 and features['map'] == layout['map_name'] == 'qmg_relay' and
            features['layout_sha256'] == layout['layout_sha256'], 'Feature/layout identity mismatch')
    world = {i: shape for i, _, shape in validated_world(bsp)}
    require(0 < len(world) <= 4096, 'Invalid compiled world size')
    pads, discs, arrival = pads_and_arrival(bsp, world, features['teleporter'])
    complete = {**world, **discs}
    partition = features['partition']
    solid = [s for s in layout['solids'] if s['id'] == partition['solid_id']]
    require(len(solid) == 1 and solid[0]['bounds'] == partition['bounds'] and
            solid[0].get('contents', 'solid') == 'solid', 'Partition has no exact typed solid binding')
    corners = set(product(*zip(*partition['bounds'])))
    partition_ids = [i for i, shape in world.items() if set(shape.vertices) == corners]
    require(len(partition_ids) == 1, 'Compiled partition is missing or differs from its complete authored box')
    resource = features['resource']
    placements = [p for p in layout['placements'] if p['id'] == resource['placement_id']]
    require(len(placements) == 1 and placements[0]['role'] in (4, 5, 6) and
            placements[0]['origin'] == resource['origin'], 'Resource has no exact typed placement binding')
    matches = [(i, e) for i, e in enumerate(bsp.entities) if e.get('classname') == resource['classname'] and
               e.get('origin') and entity_origin(e) == point(resource['origin'])]
    require(len(matches) == 1, 'Resource has no exact compiled entity binding')
    resource_feet = [*resource['origin'][:2], resource['origin'][2]-24]
    paths = audit_paths(complete, features['paths'])
    departures = [p for p in paths if p['kind'] == 'departure']
    bypass = [p for p in paths if p['kind'] == 'ordinary_bypass']
    approaches = [p for p in paths if p['kind'] == 'resource_approach']
    require(len(departures) == 2 and len(bypass) == len(approaches) == 1, 'Need two exits and one matched resource pair')
    require(bypass[0]['points'][0] == features['source_approach'] and
            bypass[0]['points'][-1] == approaches[0]['points'][-1] == resource_feet,
            'Ordinary bypass and arrival approach must finish at the same resource')
    require(approaches[0]['points'][0] in [p['points'][0] for p in departures],
            'Resource approach must start at a declared off-disc departure station')
    departure_bindings = bind_departures(layout, paths, arrival['arrival_feet'])
    overlaps = []
    for p in layout['placements']:
        if p['role'] not in (1, 4, 5, 6):
            continue
        feet = [*p['origin'][:2], p['origin'][2]-24]
        for disc_id, disc in discs.items():
            if intersects_aabb(disc, *hull(feet)):
                overlaps.append({'placement': p['id'], 'disc': disc_id})
    require(not overlaps, 'Gameplay anchor overlaps a native disc')
    observer_rows = []
    for observer in features.get('observer_stations', []):
        feet = point(observer['feet'])
        certificate = check_segment(complete, {}, feet, feet)
        require(certificate['pass'], 'Observer station is unsupported or blocked')
        eye = [*feet[:2], feet[2]+46]
        target = [*arrival['arrival_feet'][:2], arrival['arrival_feet'][2]+46]
        fraction = bsp.trace(eye, target, mask=3)
        observer_rows.append({**observer, 'station_certificate': certificate,
                              'arrival_eye': target, 'world_visible': fraction == 1, 'fraction': fraction})
    spawn_rays = []
    for p in layout['placements']:
        if p['role'] == 1:
            eye = [*p['origin'][:2], p['origin'][2]+22]
            target = [*arrival['arrival_feet'][:2], arrival['arrival_feet'][2]+46]
            spawn_rays.append({'spawn': p['id'], 'eye': eye, 'arrival_eye': target,
                               'world_visible': bsp.trace(eye, target, mask=3) == 1,
                               'xy_distance': dist(eye[:2], target[:2])})
    direct = bsp.trace([*arrival['arrival_feet'][:2], arrival['arrival_feet'][2]+46],
                       [*resource_feet[:2], resource_feet[2]+46], mask=3)
    require(layout['navigation_edge_counts']['teleporter'] == 1, 'Expected one directed typed teleport edge')
    mechanisms = [m for m in layout['mechanisms'] if m['kind'] == 'teleporter']
    require(len(mechanisms) == 1 and mechanisms[0]['from'] == [*pads[0]['origin'][:2], pads[0]['origin'][2]-24]
            and mechanisms[0]['to'] == [*pads[1]['origin'][:2], pads[1]['origin'][2]-24],
            'Displayed mechanism endpoints differ from native entities')
    return numeric({'schema_version': 1, 'map': features['map'], 'layout_sha256': layout['layout_sha256'],
                    'bsp_sha256': bsp.sha256, 'ok': all(p['pass'] for p in paths),
                    'world_brushes': len(world), 'pads': pads, 'arrival': arrival,
                    'partition_brush': partition_ids[0], 'resource_entity': matches[0][0],
                    'arrival_resource_world_visible': direct == 1, 'paths': paths,
                    'departure_room_bindings': departure_bindings,
                    'observer_stations': observer_rows, 'arrival_spawn_rays': spawn_rays,
                    'anchor_disc_overlaps': overlaps,
                    'limits': ['Paths begin at off-disc floor stations. Pad departure is a separate native test.',
                               'Full swept hull and floor support certify these polylines, not shortest paths or movement times.',
                               'Ordinary and teleported journeys do not share their initial route leg in this static audit.',
                               'World point rays omit actors and do not prove survival, visibility of effects, or multiplayer balance.']})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve(strict=True)
    paths = [output/name for name in ('qmg_relay.bsp', 'layout.json', 'qmg_relay.qmg.json', 'features.json')]
    result = audit(Bsp(paths[0].read_bytes()), *(read(p) for p in paths[1:]))
    result['input_hashes'] = {p.name: sha(p) for p in paths}
    (output/'arrival-audit.json').write_text(json.dumps(result, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    print(json.dumps({'ok': result['ok'], 'paths': {p['id']: p['pass'] for p in result['paths']},
                      'arrival_spawn_visible': sum(p['world_visible'] for p in result['arrival_spawn_rays'])}))
    if not result['ok']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
