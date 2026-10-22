"""Exact static support and independent frame interpretation for upper links.

This module does not launch a client or manipulate user input. Experiment
orchestration is added only after final geometry and complete commands have
been declared. Earlier native records are read-only timing references.
"""
from __future__ import annotations

from fractions import Fraction
import argparse
import hashlib
import importlib.util
from itertools import product
import json
import math
from pathlib import Path

from tools.map_recipes.drop_audit import _sweep, _world
from tools.map_recipes.geometry import intersects_aabb, intersects_swept_aabb, model_brushes
from tools.mp_portfolio.upper_link_controls import variants, world_brushes
from tools.native_movement.run import (BUILD_RECORD, NATIVE, PROJECT, preflight, prepare,
                                      require, save, sha, summarize, verify_build)


OUTPUT = Path(__file__).resolve().parents[2] / 'out/native-upper-link'
RUNNER = PROJECT / 'tools/native_movement/run.py'
ANALYSIS_DEPENDENCIES = ('tools/map_study/binary.py', 'tools/map_recipes/geometry.py',
                         'tools/map_recipes/drop_audit.py', 'tools/mp_portfolio/upper_link_controls.py')
TIMING_PROTOCOL = PROJECT / 'out/native-motif-turning/protocol.json'
TIMING_PROTOCOL_SHA = '07ee02088ccc15b147fd014735c4c16d3fb5640eb99c705879cdc80d293e1110'
HULL_MINS = [-16, -16, -24]
HULL_MAXS = [16, 16, 32]


def number(value):
    require(type(value) in (int, float, Fraction) and math.isfinite(value),
            'Coordinates must be finite numbers')
    return Fraction(value)


def support_union(tops, low, high, floor):
    """Cover the entire rectangle with exact coplanar compiled top faces."""
    low, high = tuple(map(number, low)), tuple(map(number, high))
    floor = number(floor)
    require(len(low) == len(high) == 2 and all(a < b for a, b in zip(low, high)),
            'Support rectangle must have positive area')
    clipped = []
    for top in tops:
        if number(top['floor']) != floor:
            continue
        a = tuple(max(low[i], number(top['low'][i])) for i in range(2))
        b = tuple(min(high[i], number(top['high'][i])) for i in range(2))
        if all(x < y for x, y in zip(a, b)):
            clipped.append((top['brush'], a, b))
    xs = sorted({low[0], high[0], *(x for _, a, b in clipped for x in (a[0], b[0]))})
    ys = sorted({low[1], high[1], *(y for _, a, b in clipped for y in (a[1], b[1]))})
    covered = Fraction(0)
    gaps = []
    for x0, x1 in zip(xs, xs[1:]):
        for y0, y1 in zip(ys, ys[1:]):
            if any(a[0] <= x0 and x1 <= b[0] and a[1] <= y0 and y1 <= b[1]
                   for _, a, b in clipped):
                covered += (x1 - x0) * (y1 - y0)
            else:
                gaps.append([[float(x0), float(y0)], [float(x1), float(y1)]])
    area = (high[0] - low[0]) * (high[1] - low[1])
    return {'fully_supported': covered == area, 'required_area': float(area),
            'covered_area': float(covered), 'uncovered_rectangles': gaps,
            'supporting_brushes': sorted({ident for ident, _, _ in clipped}),
            'floor_z': float(floor)}


class World:
    def __init__(self, bsp_path):
        bsp = preflight(Path(bsp_path))
        require(len(bsp.models) == 1, 'Upper-link audit excludes brush entities')
        raw = model_brushes(bsp)
        require(raw and all(bsp.brushes[i][2] == 1 for i in raw),
                'Upper-link audit requires every raw model0 brush to be SOLID')
        self.world = _world(bsp)
        require(all(set(s.vertices) == set(product(*zip(*s.bounds))) for _, _, s in self.world),
                'Upper-link rectangle support requires exact compiled AABBs')
        self.tops = [{'brush': i, 'low': s.bounds[0][:2], 'high': s.bounds[1][:2],
                      'floor': s.bounds[1][2]} for i, _, s in self.world]
        self.bsp_sha256 = bsp.sha256
        self.point_entities = [e for e in bsp.entities if e.get('classname') != 'worldspawn']

    def flat_chord(self, a, b, floor):
        low = [min(a[i], b[i]) - 16 for i in range(2)]
        high = [max(a[i], b[i]) + 16 for i in range(2)]
        result = support_union(self.tops, low, high, floor)
        counts = {'world_pair_checks': 0, 'exact_swept_checks': 0}
        try:
            _sweep(self.world, [*a[:2], floor], [*b[:2], floor], 'upper-link chord', counts)
            result['clear'] = True
        except ValueError as error:
            result.update({'clear': False, 'collision_error': str(error)})
        return {**result, **counts}

    def flat_region(self, center_bounds, floor):
        """Certify a full standing-center rectangle, including every edge."""
        low = [number(center_bounds[0][i]) - 16 for i in range(2)]
        high = [number(center_bounds[1][i]) + 16 for i in range(2)]
        result = support_union(self.tops, low, high, floor)
        blocked = [i for i, _, shape in self.world if
                   intersects_aabb(shape, (*low, number(floor)), (*high, number(floor) + 56))]
        return {**result, 'clear': not blocked, 'overlapping_brushes': blocked}

    def wall_distance(self, frame):
        """Closest hull-to-box XY gap among vertically overlapping blockers."""
        floor = frame['feet_z']
        a = [frame['origin'][i] - 16 for i in range(2)]
        b = [frame['origin'][i] + 16 for i in range(2)]
        distances = []
        for ident, _, shape in self.world:
            low, high = shape.bounds
            if high[2] <= floor or low[2] >= floor + 56:
                continue
            gap = [max(float(low[i]) - b[i], a[i] - float(high[i]), 0) for i in range(2)]
            distances.append((math.hypot(*gap), ident))
        if not distances:
            return None
        distance, ident = min(distances)
        return {'distance_xy': distance, 'brush': ident,
                'scope': 'Nearest static box with vertical hull overlap; no braking-cause attribution.'}

    def strip_sweep(self, source, target, axis, cross_limits):
        """Sweep all centers across the full stair width in one expanded hull."""
        low = [number(source[0]) - 16, number(source[1]) - 16, number(source[2])]
        high = [number(source[0]) + 16, number(source[1]) + 16, number(source[2]) + 56]
        low[1-axis], high[1-axis] = map(number, cross_limits)
        delta = [number(target[i]) - number(source[i]) for i in range(3)]
        blocked = [ident for ident, _, shape in self.world
                   if intersects_swept_aabb(shape, low, high, delta)]
        return {'clear': not blocked, 'overlapping_brushes': blocked,
                'source_feet': list(source), 'target_feet': list(target),
                'full_cross_footprint': list(cross_limits)}


def audit_transfer(world, source, target, width=160):
    """Full-width upper strip plus center and both full-hull edge lanes."""
    require(len(source) == len(target) == 3 and source[2] == target[2] and
            sum(source[i] != target[i] for i in range(2)) == 1,
            'Upper transfer requires distinct same-height axial endpoints')
    require(width == 160, 'This construction declares a 160-unit upper strip')
    axis = 0 if source[0] != target[0] else 1
    cross = 1 - axis
    half_band = width / 2 - 16
    low = [min(source[i], target[i]) for i in range(2)]
    high = [max(source[i], target[i]) for i in range(2)]
    low[cross] -= half_band
    high[cross] += half_band
    region = world.flat_region([low, high], source[2])
    lanes = []
    for offset in (-half_band, 0, half_band):
        a, b = list(source), list(target)
        a[cross] += offset
        b[cross] += offset
        lanes.append({'cross_offset': offset, 'from_feet': a, 'to_feet': b,
                      **world.flat_chord(a, b, source[2])})
    return {'success': region['fully_supported'] and region['clear'] and
                       all(x['fully_supported'] and x['clear'] for x in lanes),
            'full_width': width, 'physical_center_band_width': 2 * half_band,
            'center_xy_bounds': [low, high], 'full_strip': region, 'lanes': lanes,
            'scope': 'Exact static full-width support/clearance; three geometric lanes are not native journeys.'}


def audit_stair(world, stair):
    """Exact full-width caps and fixed 18-unit rise/advance/lower constructions.

    These static constructions are not native internal step traces. Each covers
    every standing center across the actual 128-unit tread width, including the
    raised hull over the previous tread before advancing to the next tread.
    """
    axis, direction, floor = stair['axis'], stair['ascending_direction'], stair['ground_z']
    require(axis in (0, 1) and direction in (-1, 1), 'Axial stair contract required')
    treads = stair['treads']
    require(len(treads) == 8, 'This audit requires eight 16-unit treads')
    cross = 1-axis
    cross_limits = [treads[0]['bounds'][0][cross], treads[0]['bounds'][1][cross]]
    require(cross_limits[1] - cross_limits[0] == 128, 'Full stair width must be 128')
    centers, caps = [], []
    for i, tread in enumerate(treads):
        low, high = tread['bounds']
        require(high[axis] - low[axis] == 36 and [low[cross], high[cross]] == cross_limits and
                low[2] == floor and high[2] == floor + 16*(i+1), 'Unexpected exact tread dimensions')
        if i:
            previous = treads[i-1]['bounds']
            require((low[axis] == previous[1][axis]) if direction == 1 else
                    (high[axis] == previous[0][axis]), 'Stair treads are not face-adjacent in ascent order')
        center = [(low[j] + high[j])/2 for j in range(2)] + [high[2]]
        centers.append(center)
        bounds = [[low[j]+16 for j in range(2)], [high[j]-16 for j in range(2)]]
        caps.append({'source_solid': tread['id'], 'center_xy_bounds': bounds,
                     **world.flat_region(bounds, high[2])})
    first = treads[0]['bounds']
    source = list(centers[0])
    source[axis] = first[0 if direction == 1 else 1][axis] - direction*16
    source[2] = floor
    entry_bounds = [list(source[:2]), list(source[:2])]
    entry_bounds[0][cross], entry_bounds[1][cross] = cross_limits[0]+16, cross_limits[1]-16
    source_support = world.flat_region(entry_bounds, floor)
    motions = []
    for target in centers:
        raised = [*source[:2], source[2]+18]
        advanced = [*target[:2], raised[2]]
        legs = [world.strip_sweep(a, b, axis, cross_limits)
                for a,b in ((source, raised), (raised, advanced), (advanced, target))]
        motions.append({'from_feet': source, 'to_feet': target, 'legs': legs,
                        'clear': all(x['clear'] for x in legs)})
        source = target
    return {'label': stair['label'], 'success': source_support['fully_supported'] and source_support['clear'] and
            all(x['fully_supported'] and x['clear'] for x in caps) and all(x['clear'] for x in motions),
            'ground_entry': source_support, 'full_tread_caps': caps, 'ascending_step_constructions': motions,
            'scope': 'Full-width compiled caps and reversible static box translations; not a runtime stair trajectory.'}


def standing_dry(frame):
    return (frame['hull_mins'] == HULL_MINS and frame['hull_maxs'] == HULL_MAXS and
            frame['waterlevel'] == 0 and not frame['post_startsolid'] and not frame['post_allsolid'])


def in_bounds(frame, bounds):
    return all(bounds[0][i] <= frame['origin'][i] <= bounds[1][i] for i in range(2))


def gate_matches(frame, gate):
    return (in_bounds(frame, gate['center_xy_bounds']) and
            abs(frame['feet_z'] - gate['feet_z']) <= gate['feet_tolerance'] and
            (not gate['grounded'] or frame['grounded']))


def qualifies(frame, goal):
    return (standing_dry(frame) and frame['grounded'] and gate_matches(frame, goal) and
            math.hypot(*frame['velocity'][:2]) <= goal['maximum_horizontal_speed'] and
            abs(frame['velocity'][2]) <= goal['maximum_absolute_vertical_speed'])


def event(frame, settle=8):
    return {key: frame[key] for key in ('frame', 'time_ms', 'origin', 'feet_z', 'velocity',
                                       'grounded', 'waterlevel')} | {
        'movement_time_ms': frame['time_ms'] - settle * 16}


def validate_contract(spec):
    require(spec['timestep_ms'] == 16 and spec['settle_frames'] == 8,
            'Protocol requires eight settle frames and 16ms sampling')
    goal = spec['physical_goal']
    require(goal['consecutive_samples'] == 9 and goal['first_to_last_dwell_ms'] == 128 and
            goal['maximum_horizontal_speed'] == 10 and goal['maximum_absolute_vertical_speed'] == 10 and
            goal['feet_tolerance'] == 1 and goal['grounded'] is True,
            'The fixed grounded low-speed 128ms goal contract changed')
    require(spec['segments'] and len(spec['segments']) <= 16, 'Bounded ordered segments required')
    require(spec['segments'][-1]['kind'] != 'stair', 'Finish needs a separately supported flat phase')
    require(len({s['label'] for s in spec['segments']}) == len(spec['segments']), 'Duplicate route labels')
    for segment in spec['segments']:
        require(segment['kind'] in ('stair', 'upper', 'ground'), 'Unknown route phase')
        low, high = segment['center_xy_bounds']
        require(len(low) == len(high) == 2 and all(number(a) < number(b) for a, b in zip(low, high)),
                'Physical phase center bounds require positive area')
        require(segment['gate']['feet_tolerance'] == 1 and segment['gate']['grounded'] is True,
                'Unsupported route gate contract')
    for gate in [goal, *(s['gate'] for s in spec['segments'])]:
        low, high = gate['center_xy_bounds']
        require(len(low) == len(high) == 2 and all(number(a) < number(b) for a,b in zip(low, high)) and
                number(gate['feet_z']) == gate['feet_z'], 'Every gate requires finite bounded geometry')
    require(all(c.get('up', 0) == c.get('side', 0) == c.get('buttons', 0) == 0 and
                c.get('pitch', 0) == 0 for c in spec['commands']), 'No jumping, side input, buttons or pitch change')
    require(spec['commands'][-1]['frames'] == 128 and spec['commands'][-1]['forward'] == 0,
            'Final release budget must remain 128 frames')


def analyze(spec, frames, world):
    """First ordered route+dwell outcome, independently derived from full frames."""
    validate_contract(spec)
    require(len(frames) == 8 + sum(c['frames'] for c in spec['commands']) and
            all(f['frame'] == i and f['time_ms'] == i * 16 and f['msec'] == 16
                for i, f in enumerate(frames, 1)), 'Incomplete native frame sequence')
    require(all(len(f['origin']) == len(f['velocity']) == 3 and
                all(type(v) in (int, float) and math.isfinite(v) for v in (*f['origin'], *f['velocity'], f['feet_z'])) and
                f['feet_z'] == f['origin'][2] + f['hull_mins'][2] for f in frames),
            'Native origin/feet/velocity contract differs')
    start = spec['start']['feet']
    require(all(standing_dry(f) and f['grounded'] and f['velocity'] == [0, 0, 0] and
                f['origin'][:2] == start[:2] and abs(f['feet_z'] - start[2]) <= 1
                for f in frames[6:8]) and frames[6]['origin'] == frames[7]['origin'],
            'Declared start did not settle to a stable dry standing station')
    index = 0
    passed, proofs = [], []
    violation = accepted = final_dwell = hold = final_hold = None
    consecutive = final_count = 0
    previous = frames[7]
    release = 9 + sum(c['frames'] for c in spec['commands'][:-1])
    segments = spec['segments']
    for frame in frames[8:]:
        reasons = []
        if not standing_dry(frame):
            reasons.append('standing_dry_clear_hull_lost')
        segment = segments[min(index, len(segments) - 1)]
        if not in_bounds(frame, segment['center_xy_bounds']):
            reasons.append('declared_physical_phase_corridor_lost')
        if segment['kind'] == 'stair':
            if not segment['feet_range'][0] <= frame['feet_z'] <= segment['feet_range'][1]:
                reasons.append('stair_vertical_envelope_lost')
        else:
            if not frame['grounded'] or abs(frame['feet_z'] - segment['feet_z']) > 1:
                reasons.append('flat_floor_contact_lost')
            proof = world.flat_chord(previous['origin'], frame['origin'], segment['feet_z'])
            proofs.append({'from_frame': previous['frame'], 'to_frame': frame['frame'],
                           'phase': segment['label'], **proof})
            if not proof['fully_supported']:
                reasons.append('full_footprint_support_lost')
            if not proof['clear']:
                reasons.append('reconstructed_flat_chord_blocked')
        if index < len(segments) and gate_matches(frame, segment['gate']):
            passed.append({'label': segment['label'], 'event': event(frame)})
            index += 1  # Never consume several ordered gates with one state.
            if index < len(segments) and segments[index]['kind'] != 'stair':
                next_segment = segments[index]
                proof = world.flat_chord(frame['origin'], frame['origin'], next_segment['feet_z'])
                proofs.append({'from_frame': frame['frame'], 'to_frame': frame['frame'],
                               'phase': next_segment['label'], **proof})
                if not frame['grounded'] or abs(frame['feet_z'] - next_segment['feet_z']) > 1:
                    reasons.append('flat_entry_contact_lost')
                if not proof['fully_supported'] or not proof['clear']:
                    reasons.append('flat_entry_support_or_clearance_lost')
        if reasons and violation is None:
            violation = {'event': event(frame), 'reasons': reasons, 'phase': segment['label']}
        good = frame['frame'] >= release and qualifies(frame, spec['physical_goal'])
        if good:
            if final_count == 0:
                final_hold = frame
            final_count += 1
            if final_count == 9 and final_dwell is None:
                final_dwell = {'start': event(final_hold), 'end': event(frame)}
        else:
            final_count, final_hold = 0, None
        if good and index == len(segments) and violation is None:
            if consecutive == 0:
                hold = frame
            consecutive += 1
            if consecutive == 9:
                require(frame['time_ms'] - hold['time_ms'] == 128, 'Nine states must span 128ms')
                accepted = {'start': event(hold), 'end': event(frame)}
        else:
            consecutive, hold = 0, None
        previous = frame
        if accepted:
            break
    reason = None if accepted else ('physical_route_failed' if violation else
             'ordered_gate_not_reached' if index < len(segments) else 'no_bounded_low_speed_dwell')
    return {'schema_version': 1, 'success': accepted is not None, 'failure_reason': reason,
            'gate_events': passed, 'first_violation': violation, 'first_route_dwell': accepted,
            'movement_time_ms': accepted['end']['movement_time_ms'] if accepted else None,
            'next_missing_gate': segments[index]['label'] if index < len(segments) else None,
            'late_pocket_dwell_ignoring_route_failure': final_dwell,
            'flat_chord_proofs': proofs, 'flat_chord_count': len(proofs),
            'raw_frame_count': len(frames), 'observation_through_frame': previous['frame'],
            'full_budget_final_event': event(frames[-1]),
            'accepted_wall_distance': world.wall_distance(previous) if accepted else None,
            'scope': 'Native frame ends plus reconstructed horizontal chords; no native internal substeps, game module or multiplayer claim.'}


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def timing_basis():
    """Retain the declared old approximation and independently bind its raw inputs."""
    require(sha(TIMING_PROTOCOL) == TIMING_PROTOCOL_SHA, 'Prior timing declaration changed')
    prior = read(TIMING_PROTOCOL)
    basis = prior['timing_basis']
    files = [{'path': TIMING_PROTOCOL.relative_to(PROJECT).as_posix(), 'sha256': sha(TIMING_PROTOCOL)}]
    for speed in (200, 400):
        row = basis['by_input'][str(speed)]
        paths = [(f'out/native-motif-circuits/weft-crossing6-forward-f{speed}/frames.ndjson',
                  row['acceleration_frames_sha256']),
                 (f'out/native-recovery/ravel-recovery-f{speed}-release/frames.ndjson',
                  row['stopping_frames_sha256'])]
        for path, digest in paths:
            require(sha(PROJECT/path) == digest, 'Prior raw timing evidence changed')
            files.append({'path': path, 'sha256': digest})
    return {'basis': basis, 'bound_files': files}


def estimated_frames(source, target, speed, reserve_coast):
    """Old ten-frame startup/steady/coast estimate; never fit new native frames."""
    require(sum(source[i] != target[i] for i in range(2)) == 1, 'Timing leg must be axial')
    axis = 0 if source[0] != target[0] else 1
    require(source[axis]*target[axis] > 0, 'Signed timing estimate cannot cross coordinate zero')
    row = timing_basis()['basis']['by_input'][str(speed)]
    label = 'toward' if abs(target[axis]) < abs(source[axis]) else 'away'
    distance = abs(target[axis]-source[axis])
    coast = row[label+'_estimated_stop_distance'] if reserve_coast else 0
    estimate = 10+(distance-coast-row[label+'_prefix_distance'])/row[label+'_steady_frame_distance']
    return {'nominal_start': source, 'timing_destination': target, 'distance': distance,
            'toward_coordinate_zero': label == 'toward', 'coast_reserved': reserve_coast,
            'active_frames': max(10, math.ceil(estimate-.5))}


def frames_at(folder):
    values = [json.loads(line) for line in (folder / 'frames.ndjson').read_text().splitlines()]
    require(values and values[0]['type'] == 'initial' and
            all(v['type'] == 'frame' for v in values[1:]), 'Invalid retained native output')
    return values[1:]


def validate_factorial(trials):
    expected = {('rotation', d, s, p) for d in ('forward', 'reverse') for s in (200, 400) for p in (0, 24)}
    expected |= {('transfer', d, s, 0) for d in ('forward', 'reverse') for s in (200, 400)}
    keys = [(t['group'], t['direction'], t['literal_forwardmove'], t['corner_pause_frames']) for t in trials]
    require(len(trials) == 12 and len({t['id'] for t in trials}) == 12 and set(keys) == expected,
            'Require eight matched rotations and four straight upper controls')
    for spec in trials:
        validate_contract(spec)
        prepare(spec)  # Validate exact native command/start bounds without running.
        active = [c for c in spec['commands'] if c['forward']]
        require(active and all(c['forward'] == spec['literal_forwardmove'] for c in active),
                'Trial input label does not match active commands')
        points = spec['route_waypoints']
        require(len(points) == len(active)+1 and points[0] == spec['start']['feet'] and
                points[-1][2] == spec['physical_goal']['feet_z'] and
                all(spec['physical_goal']['center_xy_bounds'][0][i] <= points[-1][i] <=
                    spec['physical_goal']['center_xy_bounds'][1][i] for i in range(2)),
                'Declared route endpoints differ from native start or bounded physical goal')
        for a, b, command in zip(points, points[1:], active):
            require(len(a) == len(b) == 3 and sum(a[i] != b[i] for i in range(2)) == 1,
                    'Every active route leg must have a distinct axial direction')
            yaw = math.degrees(math.atan2(b[1]-a[1], b[0]-a[0]))
            require((command.get('yaw', 0)-yaw) % 360 == 0, 'Command yaw differs from the frozen route leg')
        expected_commands = []
        for i, command in enumerate(active):
            expected_commands.append(command)
            if i + 1 < len(active) and spec['corner_pause_frames']:
                expected_commands.append({**command, 'frames': 24, 'forward': 0})
        expected_commands.append({**active[-1], 'frames': 128, 'forward': 0})
        require(spec['commands'] == expected_commands, 'Undeclared pauses or command phases')
        if spec['group'] == 'transfer':
            require(len(active) == 1 and points[0][2] == points[-1][2] and
                    all(s['kind'] == 'upper' for s in spec['segments']),
                    'Transfer controls must remain straight and wholly upper')
        else:
            require(len(active) == 4 and spec['start']['feet'][2] == spec['physical_goal']['feet_z'] and
                    sum(s['kind'] == 'stair' for s in spec['segments']) >= 2 and
                    any(s['kind'] == 'upper' for s in spec['segments']),
                    'Rotation must contain ascent, inter-room upper transfer and descent')
    for direction in ('forward', 'reverse'):
        for speed in (200, 400):
            pair = [t for t in trials if t['group'] == 'rotation' and t['direction'] == direction and
                    t['literal_forwardmove'] == speed]
            require([c for c in pair[0]['commands'] if c['forward']] ==
                    [c for c in pair[1]['commands'] if c['forward']] and
                    pair[0]['segments'] == pair[1]['segments'] and
                    pair[0]['physical_goal'] == pair[1]['physical_goal'] and pair[0]['start'] == pair[1]['start'] and
                    pair[0]['route_waypoints'] == pair[1]['route_waypoints'],
                    'Pause comparison changed active commands, geometry, start or goal')
    for group in ('rotation', 'transfer'):
        for speed in (200, 400):
            pair = [t for t in trials if t['group'] == group and t['literal_forwardmove'] == speed and
                    t['corner_pause_frames'] == 0]
            require(pair[0]['route_waypoints'] == pair[1]['route_waypoints'][::-1],
                    'Direction comparison must reverse the identical route endpoints and corners')


def plan_inputs(plan):
    require(plan['schema_version'] == 1 and plan['id'] == 'hinge-native-upper-link-v1', 'Unknown upper-link plan')
    seen = set()
    for entry in plan['files']:
        path = (PROJECT / entry['path']).resolve(strict=True)
        require(path.is_relative_to(PROJECT) and path.is_file() and entry['path'] not in seen,
                'Missing, duplicate or outside plan file')
        require(sha(path) == entry['sha256'], 'Frozen geometry/evidence changed: ' + entry['path'])
        seen.add(entry['path'])
    require({plan[k] for k in ('bsp', 'map', 'layout', 'closed_bsp', 'closed_map',
                              'missing_floor_bsp', 'missing_floor_map', 'control_record')} <= seen,
            'Positive layout/MAP/BSP and both complete compiled negative controls must be pinned')
    validate_factorial(plan['trials'])
    require(all(t['bsp'] == plan['bsp'] for t in plan['trials']), 'Trial targets a different BSP')
    require(read(PROJECT/plan['layout'])['layout_sha256'] == plan['canonical_layout_sha256'],
            'Canonical layout identity differs from the frozen plan')
    for trial in plan['trials']:
        active = [c for c in trial['commands'] if c['forward']]
        require(len(trial['timing_derivation']) == len(active), 'Missing per-leg timing derivation')
        for index, (derivation, command) in enumerate(zip(trial['timing_derivation'], active)):
            expected = {'leg': index, **estimated_frames(trial['route_waypoints'][index],
                derivation['timing_destination'], trial['literal_forwardmove'], derivation['coast_reserved'])}
            require(derivation == expected and command['frames'] == expected['active_frames'],
                    'Declared command differs from the old-evidence timing calculation')
    verify_build(NATIVE)
    return {'files': plan['files'], 'runner_sha256': sha(RUNNER),
            'native_build_record_sha256': sha(BUILD_RECORD), 'native_executable_sha256': sha(NATIVE),
            'analysis_dependencies': [{'path': p, 'sha256': sha(PROJECT/p)} for p in ANALYSIS_DEPENDENCIES],
            'prior_timing': timing_basis()}


def geometry_audit(plan):
    transfer = plan['upper_transfer']
    worlds = {key: World(PROJECT / plan[key]) for key in ('bsp', 'closed_bsp', 'missing_floor_bsp')}
    results = {key: audit_transfer(world, transfer['source_feet'], transfer['target_feet'])
               for key, world in worlds.items()}
    require(results['bsp']['success'], 'Positive compiled upper transfer lacks full-width support/clearance')
    require(not results['closed_bsp']['success'] and not results['closed_bsp']['full_strip']['clear'],
            'Closed-portal negative control must block the declared transfer')
    require(not results['missing_floor_bsp']['full_strip']['fully_supported'],
            'Missing-floor negative control must retain an actual upper support hole')
    tails = []
    for key in ('map', 'closed_map', 'missing_floor_map'):
        text = (PROJECT / plan[key]).read_bytes().decode('utf-8')
        _, _, tail = world_brushes(text)
        tails.append(text[tail:])
    original = (PROJECT/plan['map']).read_bytes().decode('utf-8')
    expected_maps, expected_geometry = variants(original, transfer['portal_bounds'])
    require(all((PROJECT/plan[key]).read_bytes().decode('utf-8') == expected_maps[kind]
                for key, kind in (('closed_map', 'closed'), ('missing_floor_map', 'missing-floor'))),
            'Control MAPs differ from the exact declared one-brush interventions')
    control_record = read(PROJECT/plan['control_record'])
    require(control_record['geometry'] == expected_geometry and
            control_record['source_map_sha256'] == sha(PROJECT/plan['map']) and
            control_record['source_layout_sha256'] == sha(PROJECT/plan['layout']),
            'Control construction record differs from source geometry')
    require(tails[0] == tails[1] == tails[2] and
            all(w.point_entities == worlds['bsp'].point_entities for w in worlds.values()),
            'Portal controls changed authored or compiled point entities')
    layout = read(PROJECT / plan['layout'])
    portals = [v for c in layout['connections'] for v in c['volumes']
               if v[0][2] > min(r['bounds'][0][2] for r in layout['rooms'])]
    require(portals == [transfer['portal_bounds']], 'Transfer portal differs from the only raised connection')
    source_solids = {s['id']: s for s in layout['solids']}
    require(len(plan['stairs']) == 2 and len({s['label'] for s in plan['stairs']}) == 2,
            'Both distinct rotation stairs must be declared')
    stairs = []
    for stair in plan['stairs']:
        require(all(t['id'] in source_solids and source_solids[t['id']]['bounds'] == t['bounds']
                    for t in stair['treads']), 'Declared stair differs from frozen source layout')
        proof = audit_stair(worlds['bsp'], stair)
        require(proof['success'], 'Compiled full-width stair construction failed: ' + stair['label'])
        stairs.append(proof)
    regions = []
    unique = {}
    for trial in plan['trials']:
        for segment in trial['segments']:
            if segment['kind'] != 'stair':
                key = json.dumps([segment['center_xy_bounds'], segment['feet_z']])
                unique[key] = segment
    for segment in unique.values():
        proof = worlds['bsp'].flat_region(segment['center_xy_bounds'], segment['feet_z'])
        require(proof['fully_supported'] and proof['clear'], 'Declared flat phase is not completely supported/clear: ' + segment['label'])
        regions.append({'label': segment['label'], 'center_xy_bounds': segment['center_xy_bounds'], **proof})
    return {'schema_version': 1, 'transfer_controls': results, 'flat_phase_regions': regions,
            'stairs': stairs, 'point_entities_unchanged': True,
            'point_entity_count': len(worlds['bsp'].point_entities),
            'authored_nonworld_entity_tail_sha256': hashlib.sha256(tails[0].encode('utf-8')).hexdigest(),
            'compiled_point_entities_sha256': hashlib.sha256(json.dumps(worlds['bsp'].point_entities,
                sort_keys=True, separators=(',', ':')).encode()).hexdigest(),
            'scope': 'Exact compiled full-width upper strip, flat regions and static stair constructions; no native substep or gameplay claim.'}


def contract():
    plan = read(OUTPUT / 'plan.json')
    return {'schema_version': 1, 'id': plan['id'], 'inputs': plan_inputs(plan),
            'plan_sha256': sha(OUTPUT / 'plan.json'), 'orchestrator_sha256': sha(Path(__file__)),
            'controls_sha256': sha(PROJECT / 'tests/test_native_upper_link.py'),
            'trials': plan['trials'], 'geometry': plan['upper_transfer'],
            'normalized_commands': [{'id': spec['id'], 'frames': len(prepare(spec)[3]),
                'utf8_lf_sha256': hashlib.sha256(('\n'.join(prepare(spec)[3])+'\n').encode()).hexdigest()}
                for spec in plan['trials']],
            'geometry_audit_sha256': sha(OUTPUT / 'geometry-audit.json'),
            'adaptive_inputs': False, 'first_attempts_only': True,
            'scope': 'Frozen model0 CM/Pmove only; eight inter-room rotations and four upper transfers, no full ground-return circuit or multiplayer gameplay.'}


def declared():
    protocol = read(OUTPUT / 'protocol.json')
    require(protocol == contract(), 'Frozen native protocol, inputs or implementation changed')
    return protocol


def declare():
    require(not (OUTPUT / 'protocol.json').exists() and not (OUTPUT / 'geometry-audit.json').exists(),
            'Never replace a declared protocol or geometry audit')
    plan = read(OUTPUT / 'plan.json')
    plan_inputs(plan)
    proof = geometry_audit(plan)
    save(OUTPUT / 'geometry-audit.json', proof)
    protocol = contract()
    save(OUTPUT / 'protocol.json', protocol)
    return {'declared': True, 'native_runs': 0, 'trials': 12, 'protocol_sha256': sha(OUTPUT / 'protocol.json')}


def runner():
    module_spec = importlib.util.spec_from_file_location('_native_upper_link_runner', RUNNER)
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    module.EVIDENCE = OUTPUT
    return module


def row_for(spec, result, native, folder):
    return {key: spec[key] for key in ('id', 'group', 'direction', 'literal_forwardmove', 'corner_pause_frames')} | {
        'success': result['success'], 'failure_reason': result['failure_reason'],
        'movement_time_ms': result['movement_time_ms'], 'native_station_diagnostic_success': native['success'],
        'physical_route_sha256': sha(folder / 'physical-route.json')}


def experiment():
    protocol = declared()
    require(not (OUTPUT / 'comparison.json').exists() and
            all(not (OUTPUT / t['id']).exists() for t in protocol['trials']),
            'First attempts cannot overwrite previous output')
    save(OUTPUT / 'comparison.json', {'status': 'attempts_in_progress', 'success': False})
    native = runner()
    world = World(PROJECT / read(OUTPUT / 'plan.json')['bsp'])
    rows = []
    for spec in protocol['trials']:
        folder = OUTPUT / spec['id']
        try:
            station = native.run_trial(spec, folder)
            result = analyze(spec, frames_at(folder), world)
            save(folder / 'physical-route.json', result)
            row = row_for(spec, result, station, folder)
        except Exception as error:
            # Preserve the first attempted inputs/partial frames and continue
            # the finite declared matrix. Never retry or invent a route result.
            row = {key: spec[key] for key in ('id', 'group', 'direction', 'literal_forwardmove', 'corner_pause_frames')}
            row.update({'success': False, 'failure_reason': 'native_or_interpretation_rejection',
                        'movement_time_ms': None, 'error': str(error)})
        rows.append(row)
        print(json.dumps(row), flush=True)
        require(declared() == protocol, 'Frozen inputs changed during native execution')
    result = {'schema_version': 1, 'status': 'completed_first_attempt_matrix',
              'protocol_sha256': sha(OUTPUT / 'protocol.json'), 'inputs': protocol['inputs'],
              'trials': rows, 'native_attempts': 12, 'successes': sum(r['success'] for r in rows),
              'retained_failures': sum(not r['success'] for r in rows), 'world_only': True, 'gameplay_tested': False}
    save(OUTPUT / 'comparison.json', result)
    return result


def verify():
    protocol = declared()
    plan = read(OUTPUT / 'plan.json')
    require(read(OUTPUT / 'geometry-audit.json') == geometry_audit(plan), 'Compiled geometry audit changed')
    comparison = read(OUTPUT / 'comparison.json')
    require(comparison['status'] == 'completed_first_attempt_matrix' and comparison['native_attempts'] == 12 and
            len(comparison['trials']) == 12 and comparison['inputs'] == protocol['inputs'] and
            comparison['protocol_sha256'] == sha(OUTPUT / 'protocol.json') and
            comparison['world_only'] is True and comparison['gameplay_tested'] is False,
            'Comparison scope or protocol binding changed')
    world = World(PROJECT / plan['bsp'])
    for row, spec in zip(comparison['trials'], protocol['trials']):
        folder = OUTPUT / spec['id']
        native = read(folder / 'result.json')
        require(read(folder / 'trial.json') == spec, 'Trial specification changed')
        require(native['bsp_sha256'] == world.bsp_sha256, 'Native BSP differs')
        for key in ('runner_sha256', 'native_build_record_sha256', 'native_executable_sha256'):
            require(native[key] == protocol['inputs'][key], 'Native provenance differs: ' + key)
        for name, key in (('trial.json', 'trial_sha256'), ('commands.txt', 'commands_sha256'),
                          ('frames.ndjson', 'frames_sha256')):
            require(sha(folder / name) == native[key], 'Native artifact hash differs: ' + name)
        actual_start, _, _, lines, active = prepare(spec)
        require((folder / 'commands.txt').read_text() == '\n'.join(lines) + '\n' and
                native['actual_start_feet'] == actual_start, 'Expanded native commands/start differ')
        frames = frames_at(folder)
        summary = summarize(spec, frames, active)
        require(all(native[k] == value for k, value in summary.items()), 'Broad native station diagnostic differs')
        result = analyze(spec, frames, world)
        require(read(folder / 'physical-route.json') == result and row == row_for(spec, result, native, folder),
                'Independent physical outcome or comparison label differs')
    require(comparison['successes'] == sum(r['success'] for r in comparison['trials']) and
            comparison['retained_failures'] == sum(not r['success'] for r in comparison['trials']),
            'Native comparison aggregate differs')
    return {'ok': True, 'trials_rederived': 12, 'successes': comparison['successes'],
            'retained_failures': comparison['retained_failures'], 'comparison_sha256': sha(OUTPUT / 'comparison.json')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('declare', 'run', 'verify'))
    args = parser.parse_args()
    print(json.dumps({'declare': declare, 'run': experiment, 'verify': verify}[args.mode](), indent=2))


if __name__ == '__main__':
    main()
