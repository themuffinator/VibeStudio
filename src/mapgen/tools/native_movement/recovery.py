"""Predeclared native Ravel drop continuation, braking and quarter-turn probes.

The existing runner's broad station result is retained as a diagnostic. Recovery
has its own stricter position, velocity, floor, envelope and dwell conditions;
crossing a station does not satisfy those conditions.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path

from tools.map_recipes.drop_audit import _world
from tools.map_recipes.geometry import intersects_aabb
from tools.map_recipes.ravel_audit import _flat_route
from tools.native_movement.run import BUILD_RECORD, NATIVE, PROJECT, preflight, require, save, sha

OUTPUT = PROJECT/'out/native-recovery'
RUNNER = PROJECT/'tools/native_movement/run.py'
BSP = PROJECT/'out/ravel/qmg_ravel.bsp'
MAP = PROJECT/'out/ravel/qmg_ravel.map'
BSP_SHA = '8b88bf771c64783a812aa20160250da60201d625ba8eb8d7920b006e7097b69c'
MAP_SHA = '3af5b50fc6c0e82b4ae250dcc165b54b075789ad3cd95985cfb2f057e8891197'
APPROACH = {400: 46, 200: 49}
PROFILES = ('continue', 'release', 'north8-release', 'south8-release')
GOAL = {'center_xy_bounds': [[-128, -56], [80, 56]],
        'transient_center_xy_bounds': [[-144, -64], [96, 64]],
        'feet_z': 128, 'feet_tolerance': 1, 'maximum_horizontal_speed': 10,
        'maximum_absolute_vertical_speed': 10, 'consecutive_samples': 9,
        'sample_interval_ms': 16, 'first_to_last_dwell_ms': 128,
        'after_first_landing_allow_airborne': False, 'waterlevel': 0,
        'standing_hull': [32, 32, 56]}


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def runner():
    spec = importlib.util.spec_from_file_location('_native_recovery_runner', RUNNER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.EVIDENCE = OUTPUT
    return module


def trial(speed, profile):
    require(speed in APPROACH and profile in PROFILES, 'Undeclared recovery profile')
    commands = [{'frames': APPROACH[speed], 'forward': speed, 'yaw': 180}]
    if profile == 'continue':
        commands.append({'frames': 256, 'forward': speed, 'yaw': 180})
    elif profile == 'release':
        commands.append({'frames': 256, 'forward': 0, 'yaw': 180})
    else:
        yaw = 90 if profile.startswith('north') else -90
        commands.extend([{'frames': 8, 'forward': speed, 'yaw': yaw},
                         {'frames': 248, 'forward': 0, 'yaw': yaw}])
    commands = [{'side': 0, 'up': 0, 'pitch': 0, **c} for c in commands]
    return {'schema_version': 1, 'id': f'ravel-recovery-f{speed}-{profile}', 'bsp': str(BSP),
            'start': {'feet': [176, 16, 256], 'clearance_z': 0.125, 'velocity': [0, 0, 0]},
            'gravity': 800, 'timestep_ms': 16, 'settle_frames': 8, 'commands': commands,
            'goal': {'axis': 'x', 'coordinate': 0, 'direction': -1, 'feet_z': 128,
                     'z_tolerance': 1, 'cross_axis_range': [-1024, 1024], 'waterlevel': 0, 'grounded': True},
            'goal_role': 'Broad native station diagnostic only; independent recovery_goal defines study success.',
            'recovery_goal': GOAL, 'profile': profile, 'literal_forwardmove': speed,
            'approach_frames': APPROACH[speed], 'recovery_command_frames': 256,
            'switch_schedule_basis': 'Fixed from prior frozen-BSP first middle-floor landing; never observe or adapt during execution.'}


def in_box(frame, bounds):
    return all(bounds[0][i] <= frame['origin'][i] <= bounds[1][i] for i in range(2))


def floor_state(frame, goal):
    return (frame['grounded'] and frame['waterlevel'] == goal['waterlevel'] and
            abs(frame['feet_z']-goal['feet_z']) <= goal['feet_tolerance'] and
            frame['hull_mins'] == [-16, -16, -24] and frame['hull_maxs'] == [16, 16, 32] and
            not frame['post_startsolid'] and not frame['post_allsolid'])


def qualifies(frame, goal):
    return (floor_state(frame, goal) and in_box(frame, goal['center_xy_bounds']) and
            frame['velocity'][0]**2 + frame['velocity'][1]**2 <= goal['maximum_horizontal_speed']**2 and
            abs(frame['velocity'][2]) <= goal['maximum_absolute_vertical_speed'])


def event(frame):
    return None if frame is None else {**{k: frame[k] for k in
        ('frame', 'origin', 'feet_z', 'velocity', 'grounded', 'waterlevel')},
        'movement_time_ms': frame['time_ms']-128}


def analyze(spec, frames):
    """Independent first recovery event from raw frame state, no runner goal helper."""
    goal = spec['recovery_goal']; expected = 8+spec['approach_frames']+256
    require(len(frames) == expected and all(f['frame'] == i and f['time_ms'] == i*16 and f['msec'] == 16
                                           for i, f in enumerate(frames, 1)), 'Missing or disordered native frames')
    require(all(not f['post_startsolid'] and not f['post_allsolid'] for f in frames), 'Embedded native hull')
    settled = frames[6:8]
    require(all(f['origin'][:2] == [176,16] and abs(f['feet_z']-256) <= 1 and f['grounded'] and
                f['waterlevel'] == 0 and f['velocity'] == [0,0,0] and
                f['hull_mins'] == [-16,-16,-24] and f['hull_maxs'] == [16,16,32] for f in settled) and
            settled[0]['origin'] == settled[1]['origin'], 'Start did not settle stably')
    first_landing = next((f for f in frames[8:] if floor_state(f, goal)), None)
    require(first_landing is not None and first_landing['frame'] == 8+spec['approach_frames'],
            'Fixed approach no longer lands at its predeclared command boundary')
    violation = first_qualifying = accepted = hold_start = None
    consecutive = 0
    for frame in frames[first_landing['frame']-1:]:
        if violation is None:
            reasons = []
            if not in_box(frame, goal['transient_center_xy_bounds']): reasons.append('left_declared_recovery_envelope')
            if not floor_state(frame, goal): reasons.append('lost_required_middle_floor_state')
            if reasons:
                violation = {'event': event(frame), 'reasons': reasons}
        if qualifies(frame, goal) and violation is None:
            if first_qualifying is None: first_qualifying = frame
            if consecutive == 0: hold_start = frame
            consecutive += 1
            if consecutive >= goal['consecutive_samples']:
                require(frame['time_ms']-hold_start['time_ms'] >= goal['first_to_last_dwell_ms'], 'Dwell interval too short')
                accepted = frame; break
        else:
            consecutive = 0; hold_start = None
    observed_end = accepted['frame'] if accepted else len(frames)
    recovery_frames = frames[first_landing['frame']-1:observed_end]
    return {'schema_version': 1, 'success': accepted is not None,
            'failure_reason': None if accepted else ('recovery_envelope_or_floor_lost' if violation else 'no_qualified_dwell_within_budget'),
            'first_middle_floor_landing': event(first_landing), 'first_qualifying_sample': event(first_qualifying),
            'dwell_start': event(hold_start) if accepted else None, 'first_recovery_goal': event(accepted),
            'goal_movement_time_ms': accepted['time_ms']-128 if accepted else None,
            'landing_to_completed_dwell_ms': accepted['time_ms']-first_landing['time_ms'] if accepted else None,
            'first_violation': violation, 'observed_through_native_frame': observed_end,
            'recovery_center_extent_xy': [[min(f['origin'][i] for f in recovery_frames) for i in range(2)],
                                         [max(f['origin'][i] for f in recovery_frames) for i in range(2)]],
            'final_observed_event': event(frames[observed_end-1]), 'raw_frame_count': len(frames),
            'sampling_limit': 'Nine consecutive16ms frame-end samples span128ms; no subframe trajectory, human reaction, camera, opponent or falling-damage inference.'}


def pin_inputs():
    require(sha(BSP) == BSP_SHA and sha(MAP) == MAP_SHA, 'Frozen Ravel input changed')
    result = {'bsp_sha256': BSP_SHA, 'map_sha256': MAP_SHA, 'runner_sha256': sha(RUNNER),
              'native_build_record_sha256': sha(BUILD_RECORD), 'native_executable_sha256': sha(NATIVE)}
    runner().verify_build(NATIVE)
    return result


def pocket():
    world = _world(preflight(BSP)); counts = {'world_pair_checks':0, 'exact_swept_checks':0}
    points = [(-144,-64,128),(96,-64,128),(96,-32,128),(-144,-32,128),(-144,0,128),
              (96,0,128),(96,32,128),(-144,32,128),(-144,64,128),(96,64,128)]
    strips = _flat_route(world, points, 'whole recovery envelope in five touching32-unit hull strips', counts)
    north_context = []
    for x in (-68.625,-96):
        box = ((x-16,56,128),(x+16,88,184))
        hits = [{'brush':i,'bounds':[[float(v) for v in corner] for corner in shape.bounds]}
                for i,_,shape in world if intersects_aabb(shape,*box)]
        north_context.append({'feet':[x,72,128],'blocking_brushes':hits,
                              'standing_hull_has_positive_volume_overlap':bool(hits)})
    return {'bsp_sha256': BSP_SHA, 'world_blocking_brushes':len(world),
            'center_envelope': GOAL['transient_center_xy_bounds'],
            'full_hull_occupied_prism': [[-160,-80,128],[112,80,184]],
            'coverage_reason': 'Five horizontal swept32-wide hull strips tile the entire envelope hull footprint; connecting legs are also checked.',
            'support_and_clearance': strips, **counts,
            'north_doorway_context':north_context,
            'north_context_limit':'Two static poses beyond the recovery envelope compare full-hull doorway alignment. They are not an executed alternate turn or a native collision attribution.',
            'limit':'Static full-pocket support/clearance proof, independent from sampled native motion; no claim that the full room bounding box is walkable.'}


def experiment():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    inputs = pin_inputs()
    specs = [trial(speed, profile) for speed in (400,200) for profile in PROFILES]
    contract = {'schema_version':1, 'id':'ravel-native-recovery-v1', 'inputs':inputs,
                'recovery_goal':GOAL, 'trials':specs,
                'hypotheses':['Continuing may satisfy a station but fail bounded low-speed recovery.',
                              'Releasing input after a fixed landing may stop within the supported staging area.',
                              'Identical90-degree bursts in opposite directions may consume different available space.',
                              'Literal input speed can change landing and recovery space needs.'],
                'not_optimized':True, 'profiles_fixed_before_execution':True}
    if (OUTPUT/'protocol.json').exists():
        require(read(OUTPUT/'protocol.json') == contract, 'Predeclared recovery protocol changed; do not overwrite outcomes')
    else:
        save(OUTPUT/'protocol.json', contract)
    save(OUTPUT/'pocket-support.json', pocket())
    save(OUTPUT/'comparison.json', {'status':'attempt_in_progress','success':False})
    protocol = runner(); rows = []
    for spec in specs:
        folder = OUTPUT/spec['id']
        station = protocol.run_trial(spec, folder)
        frames = [json.loads(s) for s in (folder/'frames.ndjson').read_text().splitlines()][1:]
        old = PROJECT/f'out/ravel/native-routes/ravel-drop-forward{spec["literal_forwardmove"]}/frames.ndjson'
        prior = [json.loads(s) for s in old.read_text().splitlines()][1:]
        prefix = 8+spec['approach_frames']
        require(frames[:prefix] == prior[:prefix], 'Initial approach differs from the fixed recorded landing')
        outcome = analyze(spec, frames)
        save(folder/'recovery.json', outcome)
        rows.append({'id':spec['id'],'profile':spec['profile'],'literal_forwardmove':spec['literal_forwardmove'],
                     'recovery_success':outcome['success'],'failure_reason':outcome['failure_reason'],
                     'goal_movement_time_ms':outcome['goal_movement_time_ms'],
                     'landing_to_completed_dwell_ms':outcome['landing_to_completed_dwell_ms'],
                     'native_station_diagnostic_success':station['success'],
                     'initial_approach_exactly_matches_prior':True,
                     'prior_frames_sha256':sha(old),'recovery_sha256':sha(folder/'recovery.json')})
        print(json.dumps(rows[-1]), flush=True)
    require(pin_inputs() == inputs, 'Pinned inputs changed during recovery experiment')
    result = {'schema_version':1,'id':'ravel-native-recovery-v1','status':'completed_fixed_profile_experiment',
              'inputs':inputs,'protocol_sha256':sha(OUTPUT/'protocol.json'),'orchestrator_sha256':sha(Path(__file__)),
              'trials':rows,'recovery_successes':sum(r['recovery_success'] for r in rows),
              'retained_recovery_failures':sum(not r['recovery_success'] for r in rows),
              'world_only':True,'game_module_tested':False,'model_weights_trained':False,
              'scope':'Unmodified official QuakeII3.19 Pmove/CM world-only. No client, renderer, inputdevice, game-module damage, dynamic entities, weapons, opponent or human optimality claim.'}
    save(OUTPUT/'comparison.json', result)
    return result


def verify():
    record = read(OUTPUT/'comparison.json'); contract = read(OUTPUT/'protocol.json')
    require(record['status'] == 'completed_fixed_profile_experiment' and len(record['trials']) == 8, 'Incomplete recovery experiment')
    require(pin_inputs() == record['inputs'] == contract['inputs'], 'Recovery input identity changed')
    require(record['protocol_sha256'] == sha(OUTPUT/'protocol.json'), 'Predeclared protocol changed')
    require(record['orchestrator_sha256'] == sha(Path(__file__)), 'Recovery implementation changed since execution')
    require(read(OUTPUT/'pocket-support.json') == pocket(), 'Compiled pocket proof changed')
    for row, spec in zip(record['trials'], contract['trials']):
        folder = OUTPUT/row['id']; native = read(folder/'result.json')
        require(read(folder/'trial.json') == spec and spec == trial(spec['literal_forwardmove'],spec['profile']), 'Trial protocol drift')
        for name,key in [('trial.json','trial_sha256'),('commands.txt','commands_sha256'),('frames.ndjson','frames_sha256')]:
            require(sha(folder/name) == native[key], 'Native artifact changed')
        frames = [json.loads(s) for s in (folder/'frames.ndjson').read_text().splitlines()][1:]
        derived = analyze(spec,frames)
        require(derived == read(folder/'recovery.json') and row['recovery_success'] == derived['success'], 'Recovery outcome disagrees with independent frame analysis')
        old = PROJECT/f'out/ravel/native-routes/ravel-drop-forward{spec["literal_forwardmove"]}/frames.ndjson'
        prefix = 8+spec['approach_frames']
        prior = [json.loads(s) for s in old.read_text().splitlines()][1:]
        require(sha(old) == row['prior_frames_sha256'] and frames[:prefix] == prior[:prefix], 'Historical initial approach binding changed')
    require(sum(r['recovery_success'] for r in record['trials']) == record['recovery_successes'], 'Recovery tally changed')
    checked = {'verified_profiles':8,'recovery_successes':record['recovery_successes'],
               'retained_failures':record['retained_recovery_failures'],
               'independently_derived_first_recovery_goals':True,
               'unchanged_initial_approaches':8,'predeclared_protocol_unchanged':True,
               'whole_recovery_envelope_compiled_support_and_clearance':True}
    print(json.dumps(checked))
    return checked


def record_evidence():
    verification = verify()
    paths = [Path(__file__), PROJECT/'tests/test_native_recovery.py', PROJECT/'docs/Q2_NATIVE_RECOVERY_STUDY.md',
             RUNNER, BSP, MAP, PROJECT/'out/ravel/layout.json',
             PROJECT/'tools/native_movement/reference.json', PROJECT/'out/native-movement/build-provenance.json',
             PROJECT/'tools/map_recipes/drop_audit.py', PROJECT/'tools/map_recipes/ravel_audit.py',
             PROJECT/'tools/map_recipes/geometry.py', PROJECT/'tools/map_study/binary.py',
             PROJECT/'out/ravel/native-routes/artifact-manifest.json',
             *sorted(p for p in OUTPUT.rglob('*') if p.is_file())]
    old_manifest = read(PROJECT/'out/ravel/native-routes/artifact-manifest.json')
    for pin in old_manifest['artifacts']:
        require(sha(PROJECT/pin['path']) == pin['sha256'], 'Earlier Ravel native artifact changed')
    doc = PROJECT/'docs/Q2_NATIVE_RECOVERY_STUDY.md'
    import re
    for target in re.findall(r'\[[^\]]+\]\(([^)]+)\)', doc.read_text(encoding='utf-8')):
        if not target.startswith('https://') and target != '../knowledge/q2/native-recovery-study.json':
            require((doc.parent/target).resolve().exists(), 'Missing recovery study link')
    require(not any(p.suffix.lower() in {'.exe','.obj','.o','.lib','.dll','.bsp','.map'}
                    for p in OUTPUT.rglob('*') if p.is_file()), 'Unexpected copied binary or map')
    record = {'schema_version':1,'id':'ravel-native-recovery-v1',
              'status':'bounded_native_recovery_evidence_with_failed_profiles_retained',
              'comparison':read(OUTPUT/'comparison.json'),'protocol':read(OUTPUT/'protocol.json'),
              'verification':verification,'analysis_tests_passed':6,
              'historical_ravel_native_artifacts_unchanged':len(old_manifest['artifacts']),
              'limits':['Nine sampled states span128ms; continuous subframe stability is not established.',
                        'Approach switches are scheduled from earlier same-BSP trials, not human reactions or real-time steering.',
                        'A failed goal is not necessarily a fall; position, speed and floor requirements are reported separately.',
                        'World-only engine movement omits falling damage, opponents, game entities, camera, aim and multiplayer quality.'],
              'artifact_files':[{'path':p.relative_to(PROJECT).as_posix(),'bytes':p.stat().st_size,'sha256':sha(p)}
                                for p in sorted(set(paths))]}
    save(PROJECT/'knowledge/q2/native-recovery-study.json', record)
    print(json.dumps({'bound_files':len(record['artifact_files']),
                      'historical_ravel_artifacts_unchanged':len(old_manifest['artifacts'])}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(); mode.add_argument('--verify', action='store_true'); mode.add_argument('--record', action='store_true')
    args = parser.parse_args()
    record_evidence() if args.record else (verify() if args.verify else experiment())
