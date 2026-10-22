"""Bounded scripted commands for the optional external Quake II 3.19 CM/Pmove harness.

Only orchestration/validation lives here. Native C performs all movement, player
collision and contents queries; the Python BSP reader is a format preflight.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import struct

from tools.map_study.binary import Bsp

PROJECT = Path(__file__).resolve().parents[2]
NATIVE = PROJECT / '.agents/tmp/native-movement/build/qmg_native_pmove.exe'
EVIDENCE = PROJECT / 'out/native-movement'
BUILD_RECORD = PROJECT / '.agents/tmp/native-movement/build-record.json'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')


def numeric(value, low, high, label, integer=False):
    require(type(value) in (int, float) and math.isfinite(value) and low <= value <= high,
            f'{label} outside supported bounds')
    require(not integer or int(value) == value, f'{label} must be an integer')
    return int(value) if integer else value


def vector(value, low, high, label):
    require(isinstance(value, list) and len(value) == 3, f'{label} requires three coordinates')
    for v in value:
        numeric(v, low, high, label)
        require(v * 8 == round(v * 8), f'{label} must use native eighth-unit coordinates')
    return value


def prepare(spec):
    require(spec.get('schema_version') == 1, 'Unsupported trial schema')
    identifier = spec.get('id', '')
    require(isinstance(identifier, str) and 1 <= len(identifier) <= 80 and
            all(c in 'abcdefghijklmnopqrstuvwxyz0123456789-_' for c in identifier), 'Invalid trial id')
    start = spec['start']
    feet = vector(start['feet'], -3000, 3000, 'start.feet')
    # Explicitly declared, never silently move an embedded start to safety.
    lift = numeric(start.get('clearance_z', 0), 0, 1, 'start.clearance_z')
    require(lift * 8 == round(lift * 8), 'clearance_z must use native eighth-unit coordinates')
    actual_feet = [*feet[:2], feet[2] + lift]
    velocity = vector(start.get('velocity', [0, 0, 0]), -1000, 1000, 'start.velocity')
    gravity = numeric(spec.get('gravity', 800), 1, 2000, 'gravity', True)
    timestep = numeric(spec['timestep_ms'], 8, 50, 'timestep_ms', True)
    settle = numeric(spec.get('settle_frames', 8), 2, 128, 'settle_frames', True)
    phases = spec['commands']
    require(isinstance(phases, list) and 1 <= len(phases) <= 32, 'Commands need 1..32 phases')
    lines = [f'{timestep} 0 0 0 0 0 0'] * settle
    active = []
    for phase in phases:
        count = numeric(phase['frames'], 1, 4096, 'phase.frames', True)
        values = [numeric(phase.get(key, 0), -400, 400, key, True) for key in ('forward', 'side', 'up')]
        values += [numeric(phase.get('yaw', 0), -360, 360, 'yaw'),
                   numeric(phase.get('pitch', 0), -89, 89, 'pitch'),
                   numeric(phase.get('buttons', 0), 0, 255, 'buttons', True)]
        line = ' '.join(str(v) for v in [timestep, *values])
        lines.extend([line] * count)
        active.extend([any(values[:3])] * count)
    require(len(lines) <= 4096 and len(lines) * timestep <= 120000, 'Trial exceeds frame/time cap')
    goal = spec.get('goal')
    if goal is not None:
        require(goal.get('axis') in ('x', 'y') and type(goal.get('direction')) is int and
                goal['direction'] in (-1, 1), 'Goal needs horizontal axis and direction')
        axis = 'xy'.index(goal['axis'])
        coordinate = numeric(goal['coordinate'], -3000, 3000, 'goal.coordinate')
        require((coordinate-feet[axis])*goal['direction'] > 0, 'Goal must be ahead of the start')
        numeric(goal['feet_z'], -3000, 3000, 'goal.feet_z')
        numeric(goal.get('z_tolerance', 1), 0.125, 2, 'goal.z_tolerance')
        lane = goal['cross_axis_range']
        require(isinstance(lane, list) and len(lane) == 2, 'Goal requires a lane interval')
        for v in lane:
            numeric(v, -3000, 3000, 'goal.cross_axis_range')
        require(lane[0] < lane[1] and lane[0] <= feet[1-axis] <= lane[1], 'Invalid goal/start lane')
        numeric(goal.get('waterlevel', 0), 0, 3, 'goal.waterlevel', True)
        require(type(goal.get('grounded', True)) is bool, 'Goal grounded must be boolean')
    else:
        require('expected_stationary_waterlevel' in spec, 'Trial requires a goal or stationary contents assertion')
        numeric(spec['expected_stationary_waterlevel'], 0, 3, 'expected_stationary_waterlevel', True)
        require(not any(active), 'Stationary contents trial cannot command movement')
    return actual_feet, velocity, gravity, lines, active


def preflight(path):
    require(path.stat().st_size <= 8 * 1024 * 1024, 'BSP exceeds native harness file cap')
    bsp = Bsp(path.read_bytes())
    require(not bsp.entity_issues, 'BSP entity syntax is ambiguous')
    # Native 3.19 CM arrays plus its generated box-hull slots.
    for label, cap in [('models', 1024), ('brushes', 8192), ('planes', 65536), ('nodes', 65536),
                       ('leaves', 65536), ('brushsides', 65536), ('leafbrushes', 65536), ('texinfo', 8192)]:
        require(len(getattr(bsp, label)) <= cap, f'Native {label} limit exceeded')
    require(all(0 <= t < len(bsp.texinfo) for _, t in bsp.brushsides), 'Native brush-side texinfo index invalid')
    require(bsp.leaves[0][0] == 1, 'Native leaf zero must be CONTENTS_SOLID')
    # Collision area loading also runs even though movement does not use portals.
    areas = bsp.records(17, '<ii')
    portals = bsp.records(18, '<ii')
    require(0 < len(areas) <= 256 and len(portals) <= 1024, 'Native area/portal cap exceeded')
    require(all(n >= 0 and first >= 0 and first+n <= len(portals) for n, first in areas), 'Invalid area portal range')
    require(all(0 <= p < 1024 and 0 <= other < len(areas) for p, other in portals), 'Invalid area portal index')
    require(all(0 <= leaf[2] < len(areas) for leaf in bsp.leaves), 'Invalid leaf area')
    require(len(bsp.lumps[0]) <= 0x40000 and len(bsp.lumps[3]) <= 0x100000, 'Native entity/visibility byte cap exceeded')
    visibility = bsp.lumps[3]
    if visibility:
        require(len(visibility) >= 4, 'Truncated native visibility header')
        clusters = struct.unpack_from('<i', visibility)[0]
        require(0 <= clusters <= len(bsp.leaves) and 4+clusters*8 <= len(visibility), 'Invalid native visibility count')
        offsets = struct.unpack_from('<'+'i'*(clusters*2), visibility, 4)
        require(all(offset == -1 or 4+clusters*8 <= offset < len(visibility) for offset in offsets),
                'Invalid native visibility offset')
    return bsp


def goal_frame(frame, goal):
    axis = 'xy'.index(goal['axis'])
    return ((frame['origin'][axis]-goal['coordinate'])*goal['direction'] >= 0 and
            goal['cross_axis_range'][0] <= frame['origin'][1-axis] <= goal['cross_axis_range'][1] and
            abs(frame['feet_z']-goal['feet_z']) <= goal.get('z_tolerance', 1) and
            frame['waterlevel'] == goal.get('waterlevel', 0) and
            frame['grounded'] == goal.get('grounded', True) and
            frame.get('hull_mins') == [-16, -16, -24] and frame.get('hull_maxs') == [16, 16, 32] and
            not frame['post_startsolid'] and not frame['post_allsolid'])


def summarize(spec, frames, active):
    settle = spec.get('settle_frames', 8)
    movement = frames[settle:]
    require(movement and len(movement) == len(active), 'Native frame count differs from commands')
    require(all(not f['post_startsolid'] and not f['post_allsolid'] for f in frames),
            'Native result contains embedded player hull')
    settled = frames[settle-2:settle]
    expected_water = spec.get('expected_stationary_waterlevel', 0)
    require(len(settled) == 2 and all(f['grounded'] and f['velocity'][:2] == [0, 0] and
            (f['velocity'][2] == 0 if expected_water == 0 else f['velocity'][2] <= 0) and
            f['origin'][:2] == spec['start']['feet'][:2] and abs(f['feet_z']-spec['start']['feet'][2]) <= 1 and
            f['waterlevel'] == expected_water and f['hull_mins'] == [-16, -16, -24] and
            f['hull_maxs'] == [16, 16, 32] for f in settled) and settled[0]['origin'] == settled[1]['origin'] and
            settled[0]['velocity'] == settled[1]['velocity'],
            'Configured settling did not produce the required stable standing grounded start')
    goal = spec.get('goal')
    accepted = None
    reason = None
    if goal:
        axis = 'xy'.index(goal['axis'])
        for f in movement:
            if not goal['cross_axis_range'][0] <= f['origin'][1-axis] <= goal['cross_axis_range'][1]:
                reason = 'left_declared_trial_lane'; break
            if goal_frame(f, goal):
                accepted = f; break
        if accepted is None and reason is None:
            tail = movement[-32:]
            stalled = len(tail) == 32 and all(active[-32:]) and all(
                max(f['origin'][i] for f in tail)-min(f['origin'][i] for f in tail) < 1 for i in range(3))
            reason = 'stalled_against_collision_or_movement_constraint' if stalled else 'goal_not_reached_within_command_budget'
        observed = movement[:movement.index(accepted)+1] if accepted else movement
    else:
        expected = spec['expected_stationary_waterlevel']
        stationary = all(max(f['origin'][i] for f in movement)-min(f['origin'][i] for f in movement) <= 0.125
                         for i in range(3))
        okay = stationary and all(f['waterlevel'] == expected for f in movement)
        accepted = movement[-1] if okay else None
        reason = None if okay else 'stationary_contents_expectation_failed'
        observed = movement
    levels = [observed[0]['waterlevel']]
    for f in observed[1:]:
        if f['waterlevel'] != levels[-1]:
            levels.append(f['waterlevel'])
    zero_progress, longest, consecutive = 0, 0, 0
    prior = settled[-1]
    for index, frame in enumerate(observed):
        if active[index] and frame['origin'][:2] == prior['origin'][:2]:
            zero_progress += 1; consecutive += 1; longest = max(longest, consecutive)
        else:
            consecutive = 0
        prior = frame
    return {'success': accepted is not None, 'failure_reason': reason,
            'goal_frame': accepted['frame'] if accepted and goal else None,
            'movement_time_ms': accepted['time_ms']-settle*spec['timestep_ms'] if accepted and goal else None,
            'time_resolution_ms': spec['timestep_ms'], 'settle_time_excluded_ms': settle*spec['timestep_ms'],
            'waterlevel_sequence': levels, 'maximum_waterlevel': max(f['waterlevel'] for f in observed),
            'waterlevel_duration_ms': {str(level): sum(f['msec'] for f in observed if f['waterlevel'] == level)
                                       for level in range(4)},
            'water_duration_basis': 'Command duration classified by frame-end waterlevel, through accepted goal or whole failed budget; not continuous immersed/oxygen time',
            'stable_grounded_settle_verified': True, 'settled_origin': settled[-1]['origin'],
            'settled_velocity': settled[-1]['velocity'],
            'zero_horizontal_progress_frames': zero_progress,
            'zero_horizontal_progress_ms': zero_progress*spec['timestep_ms'],
            'longest_zero_horizontal_progress_ms': longest*spec['timestep_ms'],
            'zero_progress_scope': 'Exact quantized XY unchanged under nonzero move input; includes startup/constraints, not a unique wall-collision attribution',
            'final_observed_frame': observed[-1], 'simulated_frames': len(frames),
            'native_trace_calls': sum(f['trace_calls'] for f in frames),
            'native_pointcontents_calls': sum(f['pointcontents_calls'] for f in frames),
            'callback_counts_scope': 'Entire scripted frame budget, including settling and frames after an accepted goal',
            'collision_scope': 'Compiled model 0 through unmodified native CM_BoxTrace/MASK_PLAYERSOLID; no dynamic entities',
            'time_basis': 'Native Pmove simulated usercmd milliseconds, fixed scripted input; not wall clock or multiplayer flow'}


def verify_build(native):
    record = json.loads(BUILD_RECORD.read_text(encoding='utf-8'))
    require(sha(native) == record['executable_sha256'], 'Private native executable changed since build record')
    for group in ('external_dependencies', 'local_sources'):
        for entry in record[group]:
            require(sha(Path(entry['path'])) == entry['sha256'], f'Build dependency changed: {entry["path"]}')
    return record


def run_trial(spec, output, native=NATIVE):
    output = Path(output).resolve()
    require(output.is_relative_to(EVIDENCE.resolve()) and output != EVIDENCE.resolve(), 'Trial output must be beneath out/native-movement')
    output.mkdir(parents=True, exist_ok=True)
    # Replace any prior success before validation or process launch. A rejection,
    # timeout, malformed output or interrupted attempt cannot masquerade as it.
    failure = {'schema_version': 1, 'id': spec.get('id'), 'success': False, 'failure_reason': 'attempt_in_progress'}
    save(output/'result.json', failure)
    for name in ('commands.txt', 'frames.ndjson', 'native-stderr.txt'):
        (output/name).write_text('', encoding='utf-8')
    save(output/'trial.json', spec)
    try:
        actual_feet, velocity, gravity, lines, active = prepare(spec)
        bsp_path = (PROJECT / spec['bsp']).resolve(strict=True)
        bsp = preflight(bsp_path)
        build = verify_build(native)
        commands = '\n'.join(lines) + '\n'
        argv = [str(native), str(bsp_path), *(str(v) for v in actual_feet+velocity), str(gravity)]
        (output/'commands.txt').write_text(commands, encoding='utf-8')
        result = subprocess.run(argv, input=commands, text=True, capture_output=True, cwd=PROJECT, timeout=30)
        (output/'frames.ndjson').write_text(result.stdout, encoding='utf-8')
        (output/'native-stderr.txt').write_text(result.stderr, encoding='utf-8')
        require(result.returncode == 0, f'Native trial rejected ({result.returncode}): {result.stderr.strip()}')
        records = [json.loads(line) for line in result.stdout.splitlines()]
        require(records and records[0]['type'] == 'initial' and all(f['type'] == 'frame' for f in records[1:]), 'Invalid native output sequence')
        require(len(records) == len(lines)+1, 'Native output frame count mismatch')
        for i, frame in enumerate(records[1:], 1):
            require(frame['frame'] == i and frame['time_ms'] == i*spec['timestep_ms'], 'Native timestep sequence mismatch')
        record = {'schema_version': 1, 'id': spec['id'], 'engine_reference': 'id Software Quake II 3.19 portable C Pmove/CM',
                  'bsp': str(bsp_path), 'bsp_sha256': bsp.sha256, 'native_build_record_sha256': sha(BUILD_RECORD),
                  'native_executable_sha256': build['executable_sha256'], 'runner_sha256': sha(Path(__file__)),
                  'trial_sha256': sha(output/'trial.json'),
                  'commands_sha256': sha(output/'commands.txt'), 'frames_sha256': sha(output/'frames.ndjson'),
                  'initial_native_state': records[0], 'actual_start_feet': actual_feet,
                  **summarize(spec, records[1:], active)}
    except subprocess.TimeoutExpired as error:
        for name, data in (('frames.ndjson', error.stdout), ('native-stderr.txt', error.stderr)):
            (output/name).write_text(data.decode('utf-8', errors='replace') if isinstance(data, bytes) else data or '', encoding='utf-8')
        save(output/'result.json', {**failure, 'failure_reason': 'native_process_timeout', 'timeout_seconds': error.timeout})
        raise ValueError('Native process timed out; partial evidence retained') from error
    except Exception as error:
        save(output/'result.json', {**failure, 'failure_reason': 'trial_rejected', 'error': str(error)})
        raise
    save(output/'result.json', record)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trial', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    spec = json.loads(args.trial.read_text(encoding='utf-8-sig'))
    result = run_trial(spec, args.output or EVIDENCE/spec['id'])
    print(json.dumps({key: result[key] for key in ('id', 'success', 'failure_reason', 'movement_time_ms', 'waterlevel_sequence')}))


if __name__ == '__main__':
    main()
