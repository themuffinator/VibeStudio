"""Pinned world-only native ramp/drop probes for the original Ravel layout.

No input device, game client, game module, renderer or damage logic is involved.
The released native/ascent evidence and shared runner remain unmodified.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from fractions import Fraction
from pathlib import Path

from tools.map_study.binary import Bsp
from tools.native_movement.run import BUILD_RECORD, NATIVE, PROJECT, require, save, sha

OUTPUT = PROJECT/'out/ravel/native-routes'
RUNNER = PROJECT/'tools/native_movement/run.py'
BSP = PROJECT/'out/ravel/qmg_ravel.bsp'
MAP = PROJECT/'out/ravel/qmg_ravel.map'
BSP_SHA = '8b88bf771c64783a812aa20160250da60201d625ba8eb8d7920b006e7097b69c'
MAP_SHA = '3af5b50fc6c0e82b4ae250dcc165b54b075789ad3cd95985cfb2f057e8891197'
LAYOUT_SHA = '2f7f315bb2863b229e9e5cebc9a5998bff77328469cfa1a625e827d61678d864'


def runner():
    spec = importlib.util.spec_from_file_location('_ravel_native_runner', RUNNER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.EVIDENCE = OUTPUT
    return module


def trial(route, forward):
    require(route in ('ramp-up', 'ramp-down', 'drop') and forward in (200, 400), 'Undeclared Ravel protocol')
    if route == 'ramp-up':
        feet, axis, coordinate, direction, target_z, lane, yaw = [-736, -640, 0], 'y', -64, 1, 128, [-737, -735], 90
    elif route == 'ramp-down':
        feet, axis, coordinate, direction, target_z, lane, yaw = [-736, -64, 128], 'y', -640, -1, 0, [-737, -735], -90
    else:
        feet, axis, coordinate, direction, target_z, lane, yaw = [176, 16, 256], 'x', 0, -1, 128, [15, 17], 180
    return {'schema_version': 1, 'id': f'ravel-{route}-forward{forward}', 'bsp': str(BSP),
            'start': {'feet': feet, 'clearance_z': 0.125, 'velocity': [0, 0, 0]},
            'gravity': 800, 'timestep_ms': 16, 'settle_frames': 8,
            'commands': [{'frames': 400, 'forward': forward, 'side': 0, 'up': 0, 'yaw': yaw, 'pitch': 0}],
            'goal': {'axis': axis, 'coordinate': coordinate, 'direction': direction, 'feet_z': target_z,
                     'z_tolerance': 1, 'cross_axis_range': lane, 'waterlevel': 0, 'grounded': True}}


def standing(frame):
    return frame['hull_mins'] == [-16, -16, -24] and frame['hull_maxs'] == [16, 16, 32]


def event(frame):
    if frame is None:
        return None
    return {key: frame[key] for key in ('frame', 'time_ms', 'origin', 'feet_z', 'velocity', 'grounded', 'waterlevel')}


def audit(spec, result, frames):
    """Independently derive station/landing events without runner goal helpers."""
    require(len(frames) == 408 and all(f['frame'] == i and f['time_ms'] == i*16 and f['msec'] == 16
                                     for i, f in enumerate(frames, 1)), 'Incomplete 16ms native budget')
    require(all(not f['post_startsolid'] and not f['post_allsolid'] for f in frames), 'Embedded native hull')
    settled = frames[6:8]
    feet = spec['start']['feet']
    require(all(f['origin'][:2] == feet[:2] and abs(f['feet_z']-feet[2]) <= 1 and f['grounded'] and
                f['waterlevel'] == 0 and f['velocity'] == [0, 0, 0] and standing(f) for f in settled) and
            settled[0]['origin'] == settled[1]['origin'], 'Start did not settle stably')
    goal = spec['goal']; axis = 'xy'.index(goal['axis']); cross_axis = 1-axis
    crossing = accepted = None
    lane_failure = None
    for frame in frames[8:]:
        if not goal['cross_axis_range'][0] <= frame['origin'][cross_axis] <= goal['cross_axis_range'][1]:
            lane_failure = frame; break
        planar = (frame['origin'][axis]-goal['coordinate'])*goal['direction'] >= 0
        if planar and crossing is None:
            crossing = frame
        if (planar and abs(frame['feet_z']-goal['feet_z']) <= 1 and frame['grounded'] and
                frame['waterlevel'] == 0 and standing(frame)):
            accepted = frame; break
    require((accepted is not None) == result['success'] and
            (accepted is None or (accepted['frame'] == result['goal_frame'] and
                                 accepted['time_ms']-128 == result['movement_time_ms'])),
            'Independent first grounded goal disagrees with native runner result')
    overshoot = (accepted['origin'][axis]-goal['coordinate'])*goal['direction'] if accepted else None
    bounded_success = accepted is not None and overshoot <= 32
    scope_end = accepted['frame'] if accepted else len(frames)
    observed = frames[8:scope_end]
    require(all(standing(f) and f['waterlevel'] == 0 for f in observed), 'Standing/dry route invariant failed')
    airborne = [f for f in observed if not f['grounded']]
    first_air = airborne[0] if airborne else None
    target_landing = next((f for f in observed if first_air and f['frame'] > first_air['frame'] and
                           f['grounded'] and abs(f['feet_z']-goal['feet_z']) <= 1), None)
    previous = frames[7]
    vertical = []
    for frame in observed:
        vertical.append({'frame': frame['frame'], 'movement_time_ms': frame['time_ms']-128,
                         'origin': frame['origin'], 'feet_z': frame['feet_z'],
                         'delta_feet_z': frame['feet_z']-previous['feet_z'],
                         'velocity': frame['velocity'], 'grounded': frame['grounded']})
        previous = frame
    return {'schema_version': 1, 'success': bounded_success,
            'failure_reason': None if bounded_success else ('grounded_goal_beyond_32_unit_gate' if accepted else
                                                             result['failure_reason']),
            'independent_first_goal_matches_runner': True, 'stable_standing_dry_start_verified': True,
            'standing_dry_through_observation': True, 'planar_station_crossing': event(crossing),
            'first_grounded_station_goal': event(accepted),
            'first_airborne_frame': event(first_air), 'first_target_floor_landing_after_airborne': event(target_landing),
            'grounded_goal_overshoot_units': overshoot, 'maximum_allowed_goal_overshoot_units': 32,
            'lane_failure': event(lane_failure),
            'bounded_goal_time_ms': accepted['time_ms']-128 if bounded_success else None,
            'airborne_frame_end_count': len(airborne), 'airborne_frame_end_duration_ms': len(airborne)*16,
            'vertical_velocity_range': [min(f['velocity'][2] for f in observed), max(f['velocity'][2] for f in observed)],
            'vertical_response': vertical,
            'observation_interval': 'First movement frame through first native grounded station goal, or all400 frames if none; later commands remain in raw evidence.',
            'static_drop_candidate': {'departure_feet': [176, 16, 256], 'clearance_feet': [112, 16, 256],
                                      'landing_feet': [112, 16, 128],
                                      'interpretation': 'Authored geometric clearance/support candidate; native momentum is not constrained to this landing point.'}
                                      if goal['axis'] == 'x' else None,
            'scope': 'Official Quake II3.19 world-only Pmove; native velocity/grounded samples are not game-module falling damage, sound, camera motion, aim or combat evidence.'}


def verify_pins():
    require(sha(BSP) == BSP_SHA and sha(MAP) == MAP_SHA, 'Frozen Ravel MAP/BSP changed')
    require(json.loads((PROJECT/'out/ravel/layout.json').read_text())['layout_sha256'] == LAYOUT_SHA,
            'Frozen Ravel canonical layout changed')


def summary(comparison):
    """Compact evidence for diagram status; candidate geometry stays separate."""
    rows = []
    for row in comparison['trials']:
        spec = json.loads((OUTPUT/row['id']/'trial.json').read_text())
        evidence = json.loads((OUTPUT/row['id']/'route-audit.json').read_text())
        landing = evidence['first_target_floor_landing_after_airborne']
        goal = evidence['first_grounded_station_goal']
        rows.append({'id': row['id'], 'route': row['route'], 'forwardmove': row['forwardmove'],
                     'success': row['success'], 'failure_reason': row['failure_reason'],
                     'bounded_goal_time_ms': row['bounded_goal_time_ms'],
                     'source_feet': spec['start']['feet'], 'explicit_launch_clearance_z': 0.125,
                     'actual_first_landing_feet': [*landing['origin'][:2], landing['feet_z']] if landing else None,
                     'actual_goal_feet': [*goal['origin'][:2], goal['feet_z']] if goal else None,
                     'actual_goal_velocity': goal['velocity'] if goal else None,
                     'route_audit_sha256': row['route_audit_sha256'],
                     'result_sha256': sha(OUTPUT/row['id']/'result.json')})
    return {'schema_version': 1, 'id': 'ravel-native-route-summary-v1', 'bsp_sha256': BSP_SHA,
            'layout_sha256': LAYOUT_SHA, 'layout_file_sha256': sha(PROJECT/'out/ravel/layout.json'),
            'map_sha256': MAP_SHA, 'world_only': True, 'gameplay_tested': False,
            'scope': 'Official Quake II3.19 world-only CM/Pmove fixed centerline commands; no game module, fall damage, opponent, camera or rendered trajectory.',
            'ramp': {'id': 1, 'trials': [r for r in rows if r['route'].startswith('ramp-')]},
            'drop': {'id': 1, 'geometric_candidate_departure': [112, 16, 256],
                     'geometric_candidate_landing': [112, 16, 128],
                     'trials': [r for r in rows if r['route'] == 'drop'],
                     'diagram_rule': 'Drawn drop endpoint is the geometric candidate, not a replay of either native momentum trajectory.'}}


def translation_check():
    """Read-only compare nominally translated ramps; do not infer full causation."""
    talus = PROJECT/'out/talus/qmg_talus.bsp'
    require(sha(talus) == '6ac8fb109af4062fc08b51c5bc766edb36fd728f5afc7d2461f127c821329b03', 'Talus comparison input changed')
    planes = []
    for path, index in ((talus, 140), (BSP, 154)):
        bsp = Bsp(path.read_bytes()); first, count, contents = bsp.brushes[index]
        require(contents == 1, 'Nominal ramp not compiled SOLID')
        tops = [tuple(Fraction(v) for v in bsp.planes[p][:4]) for p, _ in bsp.brushsides[first:first+count]
                if bsp.planes[p][0] == 0 and bsp.planes[p][1] < 0 and bsp.planes[p][2] > 0]
        require(len(tops) == 1, 'Expected one upward slope plane')
        planes.append(tops[0])
    require(planes[0][:3] == planes[1][:3], 'Nominal ramp normal changed')
    distance_error = planes[1][3]-(planes[0][3]+planes[0][1]*-432)
    rows = []
    for direction in ('up', 'down'):
        for speed in (400, 200):
            old = PROJECT/f'out/native-ascent/talus-ramp-{direction}-f{speed}-x656-r1'
            new = OUTPUT/f'ravel-ramp-{direction}-forward{speed}'
            require((old/'commands.txt').read_bytes() == (new/'commands.txt').read_bytes(), 'Compared command stream changed')
            specs = [json.loads((p/'trial.json').read_text()) for p in (old, new)]
            require(all(abs(s['goal']['coordinate']-s['start']['feet'][1]) == 576 for s in specs), 'Station span differs')
            datasets = [[json.loads(s) for s in (p/'frames.ndjson').read_text().splitlines()][9:] for p in (old, new)]
            divergences = []
            for label, get in [('position', lambda f: f['origin']), ('horizontal_y', lambda f: f['origin'][1]),
                               ('velocity', lambda f: f['velocity']), ('grounded', lambda f: f['grounded'])]:
                for i, (a, b) in enumerate(zip(*datasets), 1):
                    translated = {**a, 'origin': [a['origin'][0]-80, a['origin'][1]-432, a['origin'][2]]}
                    if get(translated) != get(b):
                        divergences.append({'quantity': label, 'movement_frame': i, 'movement_time_ms': i*16,
                                            'talus_translated': get(translated), 'ravel': get(b)})
                        break
            rows.append({'direction': direction, 'forwardmove': speed,
                         'commands_identical': True, 'station_span_both': 576,
                         'talus_frames_sha256': sha(old/'frames.ndjson'), 'ravel_frames_sha256': sha(new/'frames.ndjson'),
                         'first_divergences': divergences})
    record = {'schema_version': 1, 'talus_bsp_sha256': sha(talus), 'ravel_bsp_sha256': BSP_SHA,
              'nominal_translation_talus_to_ravel': [-80, -432, 0],
              'both_native_ramp_brush_contents': 1, 'exact_compiled_normals_match': True,
              'talus_top_plane_exact': list(map(str, planes[0])), 'ravel_top_plane_exact': list(map(str, planes[1])),
              'translated_plane_distance_error_exact': str(distance_error),
              'translated_plane_height_error_units': float(distance_error/planes[1][2]), 'profiles': rows,
              'conclusion': 'Equal nominal dimensions and matched inputs do not yield translated-identical compiled planes or native frame trajectories. Downhill positions already diverge on the first flat-landing movement command. These checks identify differences; they do not isolate the full timing cause or rank the maps.'}
    save(OUTPUT/'talus-ramp-translation-check.json', record)


def experiment():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    save(OUTPUT/'comparison.json', {'schema_version': 1, 'status': 'attempt_in_progress', 'success': False})
    verify_pins()
    protocol = runner(); protocol.verify_build(NATIVE)
    rows = []
    for route in ('ramp-up', 'ramp-down', 'drop'):
        for forward in (400, 200):
            spec = trial(route, forward); folder = OUTPUT/spec['id']
            result = protocol.run_trial(spec, folder)
            frames = [json.loads(line) for line in (folder/'frames.ndjson').read_text().splitlines()][1:]
            try:
                independent = audit(spec, result, frames)
            except Exception as error:
                save(folder/'route-audit.json', {'success': False, 'failure_reason': 'independent_audit_rejected', 'error': str(error)})
                raise
            save(folder/'route-audit.json', independent)
            rows.append({'id': spec['id'], 'route': route, 'forwardmove': forward,
                         'success': independent['success'], 'failure_reason': independent['failure_reason'],
                         'bounded_goal_time_ms': independent['bounded_goal_time_ms'],
                         'native_result': result, 'route_audit_sha256': sha(folder/'route-audit.json')})
            print(json.dumps({k: rows[-1][k] for k in ('id', 'success', 'failure_reason', 'bounded_goal_time_ms')}), flush=True)
    verify_pins()
    record = {'schema_version': 1, 'id': 'ravel-native-routes-v1', 'status': 'completed_bounded_native_probes',
              'success': all(r['success'] for r in rows), 'bsp_sha256': BSP_SHA, 'map_sha256': MAP_SHA,
              'native_build_record_sha256': sha(BUILD_RECORD), 'native_executable_sha256': sha(NATIVE),
              'orchestrator_sha256': sha(Path(__file__)), 'runner_sha256': sha(RUNNER), 'trials': rows,
              'limits': ['Six fixed center-line no-jump commands; no human, multiplayer, optimal route or full-game inference.',
                         'First grounded station event must be within32 units beyond the station; failures are retained without widening the gate.',
                         'World model0 only. No game module or dynamic entities, falling damage, sound, camera, items or weapons.',
                         '16ms simulated command resolution, eighth-unit state, explicit0.125 launch clearance; stable8-frame settle excluded.',
                         'Full400 command budgets retained; post-goal trajectories are not part of route completion.',
                         'No external sources, binaries or assets copied; private native dependency pins verified.']}
    save(OUTPUT/'comparison.json', record)
    save(OUTPUT/'summary.json', summary(record))
    translation_check()
    return record


def verify_record():
    verify_pins()
    comparison = json.loads((OUTPUT/'comparison.json').read_text())
    require(comparison['status'] == 'completed_bounded_native_probes' and len(comparison['trials']) == 6,
            'Missing completed six-trial probe record')
    require(comparison['orchestrator_sha256'] == sha(Path(__file__)) and comparison['runner_sha256'] == sha(RUNNER),
            'Probe implementation changed')
    require(summary(comparison) == json.loads((OUTPUT/'summary.json').read_text()), 'Viewer summary differs from route evidence')
    runner().verify_build(NATIVE)
    for row in comparison['trials']:
        folder = OUTPUT/row['id']
        spec, result = (json.loads((folder/name).read_text()) for name in ('trial.json', 'result.json'))
        require(row['native_result'] == result, 'Comparison embeds a different native result')
        for filename, key in [('trial.json', 'trial_sha256'), ('commands.txt', 'commands_sha256'), ('frames.ndjson', 'frames_sha256')]:
            require(sha(folder/filename) == result[key], 'Native trial hash mismatch')
        require(result['bsp_sha256'] == BSP_SHA and result['runner_sha256'] == sha(RUNNER), 'Native input identity mismatch')
        frames = [json.loads(line) for line in (folder/'frames.ndjson').read_text().splitlines()][1:]
        derived = audit(spec, result, frames)
        require(derived == json.loads((folder/'route-audit.json').read_text()) and
                sha(folder/'route-audit.json') == row['route_audit_sha256'], 'Independent route audit changed')
        require(row['success'] == derived['success'] and row['bounded_goal_time_ms'] == derived['bounded_goal_time_ms'],
                'Summary success differs from bounded audit')
    files = [Path(__file__), RUNNER, BSP, MAP, PROJECT/'tools/native_movement/reference.json',
             PROJECT/'tests/test_native_ravel.py',
             PROJECT/'out/native-movement/build-provenance.json',
             *sorted(p for p in OUTPUT.rglob('*') if p.is_file() and p.name != 'artifact-manifest.json')]
    manifest = {'schema_version': 1, 'id': 'ravel-native-route-artifacts-v1',
                'independent_first_grounded_goals_rederived': 6,
                'successful_bounded_trials': sum(r['success'] for r in comparison['trials']),
                'failed_bounded_trials_retained': sum(not r['success'] for r in comparison['trials']),
                'artifacts': [{'path': p.relative_to(PROJECT).as_posix(), 'bytes': p.stat().st_size, 'sha256': sha(p)}
                              for p in sorted(set(files))]}
    save(OUTPUT/'artifact-manifest.json', manifest)
    print(json.dumps({'verified_trials': 6, 'bound_files': len(files), 'successes': manifest['successful_bounded_trials']}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    verify_record() if args.verify else experiment()
