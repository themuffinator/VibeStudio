"""Matched native Talus Bench ramp/stair trials; retained maps stay read-only.

The existing pinned runner is loaded into an isolated module namespace so its
strict evidence root can be this experiment's directory without mutating the
historical runner or its output. Native CM/Pmove remains the movement authority.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path

from tools.map_recipes.ramp_stair_comparison import (compiled_identity, source_identity,
                                                    source_map, world)
from tools.native_movement.run import BUILD_RECORD, NATIVE, PROJECT, preflight, require, save, sha

OUTPUT = PROJECT / 'out/native-ascent'
RUNNER = PROJECT / 'tools/native_movement/run.py'
PINS = {
    'ramp': {'map': 'out/talus/qmg_talus.map', 'bsp': 'out/talus/qmg_talus.bsp',
             'map_sha256': '4583964d19748a00b2686d2b30b48fa4131c852534786d0f75cc5574119f37a6',
             'bsp_sha256': '6ac8fb109af4062fc08b51c5bc766edb36fd728f5afc7d2461f127c821329b03'},
    'stairs': {'map': 'out/talus-stairs/qmg_talus_stairs.map', 'bsp': 'out/talus-stairs/qmg_talus_stairs.bsp',
               'map_sha256': '39429344c545a08f508298ed17caafb19724f53f401e3e6b5cec2abc7f72b6e4',
               'bsp_sha256': 'b41c235f024f351c903c4da813512478ed50f75b285b9e75ff411f06d7daf740'},
}
LANES = (-712, -656, -600)


def runner():
    spec = importlib.util.spec_from_file_location('_native_ascent_pinned_runner', RUNNER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.EVIDENCE = OUTPUT
    return module


def trial(geometry, forward, direction, lane, repetition=1):
    require(geometry in PINS and forward in (200, 400) and direction in ('up', 'down') and lane in LANES,
            'Unsupported bounded Talus ascent protocol')
    require(repetition in (1, 2), 'Only the declared deterministic repeat is supported')
    uphill = direction == 'up'
    y, z, goal_y, goal_z = (-208, 0, 368, 128) if uphill else (368, 128, -208, 0)
    return {'schema_version': 1,
            'id': f'talus-{geometry}-{direction}-f{forward}-x{abs(lane)}-r{repetition}',
            'bsp': PINS[geometry]['bsp'],
            'start': {'feet': [lane, y, z], 'clearance_z': 0.125, 'velocity': [0, 0, 0]},
            'gravity': 800, 'timestep_ms': 16, 'settle_frames': 8,
            'commands': [{'frames': 400, 'forward': forward, 'side': 0, 'up': 0,
                          'yaw': 90 if uphill else -90, 'pitch': 0}],
            'goal': {'axis': 'y', 'coordinate': goal_y, 'direction': 1 if uphill else -1,
                     'feet_z': goal_z, 'z_tolerance': 1, 'cross_axis_range': [lane-1, lane+1],
                     'waterlevel': 0, 'grounded': True}}


def profile(spec, result, frames):
    """Inspect frame-end vertical response only through the accepted goal.

    Quantized feet displacement is separate from Pmove's stored Z velocity:
    successful native steps can move feet upward while leaving velocity Z zero.
    Neither quantity is a rendered camera/aim, collision impulse or damage log.
    """
    settle = spec['settle_frames']
    require(len(frames) > settle and all(f['frame'] == i for i, f in enumerate(frames, 1)),
            'Profile needs complete ordered native frames')
    end = result['goal_frame'] if result['success'] else len(frames)
    require(type(end) is int and settle < end <= len(frames), 'Invalid accepted frame boundary')
    selected = frames[settle:end]
    previous = frames[settle-1]
    rows = []
    for frame in selected:
        delta = frame['feet_z'] - previous['feet_z']
        rows.append({'frame': frame['frame'], 'movement_time_ms': frame['time_ms']-settle*spec['timestep_ms'],
                     'y': frame['origin'][1], 'feet_z': frame['feet_z'], 'delta_feet_z': delta,
                     'velocity_y': frame['velocity'][1], 'velocity_z': frame['velocity'][2],
                     'grounded': frame['grounded'],
                     'station_interval': 'ascent' if -176 <= frame['origin'][1] <= 336 else 'landing'})
        previous = frame
    # A planar station must lead to a real landing near that station, not any
    # later grounded pose arbitrarily far beyond it after overshoot/falling.
    goal = spec['goal']
    accepted_beyond = ((selected[-1]['origin'][1]-goal['coordinate'])*goal['direction'])
    require(not result['success'] or 0 <= accepted_beyond <= 32,
            'Grounded goal overshot the bounded 32-unit landing gate')
    airborne_runs, run = [], []
    for row in rows:
        if not row['grounded']:
            run.append(row)
        elif run:
            airborne_runs.append(run); run = []
    if run:
        airborne_runs.append(run)
    changes = [row for row in rows if row['delta_feet_z']]
    station_crossing = next((frame for frame in selected if
                             (frame['origin'][1]-goal['coordinate'])*goal['direction'] >= 0), None)
    return {'sample_basis': '16ms native frame-end samples from settled start through first accepted goal; post-goal commands excluded',
            'standing_hull_through_goal': all(f['hull_mins'] == [-16, -16, -24] and
                                            f['hull_maxs'] == [16, 16, 32] for f in selected),
            'all_dry_through_goal': all(f['waterlevel'] == 0 for f in selected),
            'goal_overshoot_y_units': accepted_beyond if result['success'] else None,
            'first_planar_station_crossing': {'frame': station_crossing['frame'],
                'movement_time_ms': station_crossing['time_ms']-settle*spec['timestep_ms'],
                'origin': station_crossing['origin'], 'feet_z': station_crossing['feet_z'],
                'grounded': station_crossing['grounded'], 'velocity': station_crossing['velocity']}
                if station_crossing else None,
            'planar_crossing_to_accepted_goal_ms': result['movement_time_ms']-
                (station_crossing['time_ms']-settle*spec['timestep_ms'])
                if result['success'] and station_crossing else None,
            'vertical_change_frames': len(changes),
            'largest_upward_feet_delta': max((r['delta_feet_z'] for r in rows), default=0),
            'largest_downward_feet_delta': min((r['delta_feet_z'] for r in rows), default=0),
            'total_positive_feet_delta': sum(max(0, r['delta_feet_z']) for r in rows),
            'total_negative_feet_delta': sum(min(0, r['delta_feet_z']) for r in rows),
            'native_velocity_z_range': [min(r['velocity_z'] for r in rows), max(r['velocity_z'] for r in rows)],
            'airborne_frame_end_count': sum(len(r) for r in airborne_runs),
            'airborne_frame_end_duration_ms': sum(len(r) for r in airborne_runs)*spec['timestep_ms'],
            'airborne_runs': [{'first_frame': r[0]['frame'], 'last_frame': r[-1]['frame'],
                               'frame_end_duration_ms': len(r)*spec['timestep_ms'],
                               'first_y': r[0]['y'], 'last_y': r[-1]['y']} for r in airborne_runs],
            'vertical_response': rows,
            'interpretation_limit': 'Frame-end feet displacement and native velocity are not camera smoothing, aim disturbance, fall damage or combat outcomes.'}


def identity():
    before = {}
    for geometry, pin in PINS.items():
        for kind in ('map', 'bsp'):
            path = PROJECT/pin[kind]
            before[pin[kind]] = sha(path)
            require(before[pin[kind]] == pin[kind+'_sha256'], f'Retained {geometry} {kind} pin changed')
    a, b = (preflight(PROJECT/PINS[g]['bsp']) for g in ('ramp', 'stairs'))
    proof = {'files_sha256': before,
             'source': source_identity(*(source_map(PROJECT/PINS[g]['map']) for g in ('ramp', 'stairs'))),
             'compiled': compiled_identity(world(a), world(b), a, b),
             'historical_static_comparison_sha256': sha(PROJECT/'out/talus-stairs/comparison.json')}
    return proof


def experiment():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    save(OUTPUT/'comparison.json', {'schema_version': 1, 'id': 'talus-native-ascent-v1',
                                  'status': 'attempt_in_progress', 'success': False})
    proof = identity()
    protocol = runner()
    protocol.verify_build(NATIVE)
    pairs = []
    for lane in LANES:
        for direction in ('up', 'down'):
            for forward in (400, 200):
                records = []
                # Counterbalance launch order without attributing stochastic
                # population meaning to this deterministic native process.
                order = ('ramp', 'stairs') if (lane == -656) == (direction == 'up') else ('stairs', 'ramp')
                for geometry in order:
                    spec = trial(geometry, forward, direction, lane)
                    destination = OUTPUT/spec['id']
                    result = protocol.run_trial(spec, destination)
                    frames = [json.loads(line) for line in (destination/'frames.ndjson').read_text().splitlines()][1:]
                    try:
                        response = profile(spec, result, frames)
                    except Exception as error:
                        result.update({'success': False, 'failure_reason': 'vertical_profile_rejected', 'error': str(error)})
                        save(destination/'result.json', result)
                        raise
                    save(destination/'vertical-profile.json', response)
                    result.update({'geometry': geometry, 'vertical_profile_sha256': sha(destination/'vertical-profile.json'),
                                   'ascent_orchestrator_sha256': sha(Path(__file__))})
                    save(destination/'result.json', result)
                    records.append(result)
                    print(json.dumps({k: result[k] for k in ('id', 'success', 'movement_time_ms', 'failure_reason')}), flush=True)
                by = {r['geometry']: r for r in records}
                require(by['ramp']['commands_sha256'] == by['stairs']['commands_sha256'], 'Paired command stream changed')
                pairs.append({'lane_x': lane, 'direction': direction, 'forwardmove': forward,
                              'launch_order': list(order), 'ramp': by['ramp'], 'stairs': by['stairs'],
                              'stairs_minus_ramp_ms': by['stairs']['movement_time_ms']-by['ramp']['movement_time_ms']
                              if all(r['success'] for r in records) else None})
    repeated = []
    for pair in (p for p in pairs if p['lane_x'] == -656):
        for geometry in ('ramp', 'stairs'):
            spec = trial(geometry, pair['forwardmove'], pair['direction'], -656, repetition=2)
            result = protocol.run_trial(spec, OUTPUT/spec['id'])
            baseline = pair[geometry]
            require(result['frames_sha256'] == baseline['frames_sha256'] and
                    result['commands_sha256'] == baseline['commands_sha256'], 'Fixed native repetition changed frames/commands')
            repeated.append({'id': spec['id'], 'baseline': baseline['id'], 'same_full_frames_and_commands': True,
                             'result': result})
    require(identity() == proof, 'Read-only retained map identities changed during experiment')
    record = {'schema_version': 1, 'id': 'talus-native-ascent-v1',
              'status': 'completed_bounded_native_comparison',
              'success': all(p[g]['success'] for p in pairs for g in ('ramp', 'stairs')) and
                         all(r['result']['success'] for r in repeated),
              'reference': 'Official Quake II 3.19 portable C CM/Pmove; model 0 only',
              'intervention_identity': proof, 'native_build_record_sha256': sha(BUILD_RECORD),
              'native_executable_sha256': sha(NATIVE), 'runner_sha256': sha(RUNNER),
              'ascent_orchestrator_sha256': sha(Path(__file__)), 'pairs': pairs, 'deterministic_repeats': repeated,
              'protocol': {'lanes_x': list(LANES), 'lower_station_y': -208, 'upper_station_y': 368,
                           'flat_floor_heights': [0, 128], 'maximum_grounded_goal_overshoot': 32,
                           'start_clearance_z': 0.125, 'settle_frames': 8, 'timestep_ms': 16,
                           'gravity': 800, 'standing_hull': [32, 32, 56], 'phase_frames': 400},
              'limits': ['Synthetic fixed commands on an unchanged native 3.19 world-only baseline; no input devices or game client.',
                         'Literal forward400/200 inputs are not measured human running/walking and may encounter native wishspeed limits.',
                         'No jumping, strafing, turning, opponents, weapons, camera smoothing, sound or damage simulation.',
                         'Times resolve to one 16ms command. Eight settle frames are excluded; all 400 movement frames are retained.',
                         'Airborne duration bins classify command-end grounded flags, not subframe exact flight duration.',
                         'A single isolated support-profile intervention with specific lanes/inputs does not establish fastest movement or multiplayer preference.',
                         'The local combined GPL dependency remains private; no external implementation/assets/binaries are copied into the evidence.']}
    save(OUTPUT/'comparison.json', record)
    return record


if __name__ == '__main__':
    argparse.ArgumentParser(description=__doc__).parse_args()
    experiment()
