"""Retained native wet/dry path comparison against separately compiled controls."""
from collections import Counter
from copy import deepcopy
import argparse
import json
from pathlib import Path
import subprocess

from tools.map_recipes.geometry import model_brushes
from tools.map_recipes.ramp_stair_comparison import FACE, PINS, source_map
from tools.native_movement.run import EVIDENCE, PROJECT, preflight, require, run_trial, save, sha

TOOLS = Path('E:/_SOURCE/_CODE/ericw-tools-2.0.0-alpha11')
ASSETS = Path('E:/_SOURCE/_ASSETS/Q2')


def signature(bsp, model_index=0):
    result = Counter()
    for index in model_brushes(bsp, model_index):
        first, count, contents = bsp.brushes[index]
        if contents & 1:
            sides = tuple(sorted((tuple(bsp.planes[p][:4]), tuple(bsp.texinfo[t]))
                                 for p, t in bsp.brushsides[first:first+count]))
            result[(contents, sides)] += 1
    return result


def remove_water(before):
    """A bounded canonical-MAP intervention, preserving every other face string.

    The generated MAP is independently parsed by qbsp; exact compiled collision
    planes/full texinfo/contents of all remaining brushes must subsequently match.
    """
    after = deepcopy(before)
    water = []
    for index, brush in enumerate(before[0]['brushes']):
        parsed = [FACE.fullmatch(line) for line in brush]
        if any(match.group(4) == 'e3u2/water2' for match in parsed):
            require(len(brush) == 6 and all(match.group(4) == 'e3u2/water2' and
                                          match.group(5).split()[-3:] == ['32', '24', '0'] for match in parsed),
                    'Water intervention must be one explicitly authored six-face WATER/WARP/TRANS33 box')
            water.append(index)
    require(len(water) == 1, 'Matched control requires exactly one world water brush')
    del after[0]['brushes'][water[0]]
    return after


def map_text(entities):
    lines = []
    for entity in entities:
        lines.append('{')
        lines.extend(json.dumps(key)+' '+json.dumps(value) for key, value in entity['properties'])
        for brush in entity['brushes']:
            lines.extend(['{', *brush, '}'])
        lines.append('}')
    return '\n'.join(lines)+'\n'


def dry_control(name):
    source = PROJECT/f'out/{name}/qmg_{name}.map'
    wet = PROJECT/f'out/{name}/qmg_{name}.bsp'
    # Original generated map geometry is useful retained experiment evidence.
    # The GPL-linked executable and compiler intermediates stay in scratch.
    scratch = EVIDENCE/f'design-controls/{name}'
    scratch.mkdir(parents=True, exist_ok=True)
    control = scratch/f'qmg_{name}_dry.map'
    dry = control.with_suffix('.bsp')
    before = source_map(source)
    after = remove_water(before)
    control.write_text(map_text(after), encoding='utf-8')
    reread = source_map(control)
    require(reread == after and before[1:] == after[1:] and before[0]['properties'] == after[0]['properties'],
            'Control MAP changed unrelated entities or properties')
    ca, cb = Counter(before[0]['brushes']), Counter(after[0]['brushes'])
    require(sum((ca-cb).values()) == 1 and not cb-ca, 'Control MAP changed more than one water brush')
    qbsp = TOOLS/'qbsp.exe'
    require(sha(qbsp) == PINS['qbsp.exe'], 'Control compiler pin mismatch')
    arguments = [str(qbsp), '-q2bsp', '-leaktest', '-noallowupgrade', '-lowpriority', '0',
                 '-nodefaultpaths', '-path', str(ASSETS), '-nolog', '-nocolor', '-quiet', str(control), str(dry)]
    with (scratch/'qbsp.log').open('w', encoding='utf-8') as log:
        subprocess.run(arguments, cwd=PROJECT, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
    a, b = preflight(wet), preflight(dry)
    require(a.entities == b.entities, 'Compiled control entities changed')
    require(len(a.models) == len(b.models), 'Compiled control submodel count changed')
    for index in range(len(a.models)):
        require(signature(a, index) == signature(b, index), 'Compiled solid collision planes/materials/contents changed')
    wet_contents = [a.brushes[i][2] for i in model_brushes(a)]
    dry_contents = [b.brushes[i][2] for i in model_brushes(b)]
    require(sum(bool(c & 32) for c in wet_contents) == 1 and not any(c & 56 for c in dry_contents),
            'Compiled control did not remove exactly the water volume')
    require(len(wet_contents) == len(dry_contents)+1 and all(c == 1 for c in dry_contents),
            'Unexpected compiled non-water content change')
    proof = {'source_map': str(source), 'source_map_sha256': sha(source), 'wet_bsp_sha256': sha(wet),
             'control_map': str(control), 'control_map_sha256': sha(control), 'dry_bsp': str(dry), 'dry_bsp_sha256': sha(dry),
             'unchanged_source_brush_blocks': len(after[0]['brushes']), 'unchanged_nonworld_entities': len(after)-1,
             'unchanged_compiled_solid_brushes_all_models': sum(sum(signature(a, i).values()) for i in range(len(a.models))),
             'removed_source_water_brushes': 1, 'removed_compiled_water_brushes': 1,
             'source_proof': 'Exact ordered face strings and properties retained, one water brush removed; bounded generated Valve220 grouping and independent qbsp parse',
             'compiled_proof': 'Exact outward stored planes, full texinfo and contents of every solid brush, separately per native model, with indices normalized',
             'qbsp_sha256': sha(qbsp), 'qbsp_arguments': arguments[1:], 'qbsp_log_sha256': sha(scratch/'qbsp.log'),
             'control_rendering': 'Collision-only qbsp build; no VIS/light needed by Pmove; no rendering comparison'}
    save(EVIDENCE/(name+'-water-control.json'), proof)
    return dry, proof


def trial(name, speed, bsp, wet=True, timestep=16, profile='level'):
    if name == 'fold':
        start, axis, yaw, coordinate, lane = [632, -144, 0], 'y', 90, 240, [616, 648]
    elif name == 'lockyard':
        start, axis, yaw, coordinate, lane = [-320, -560, 0], 'x', 0, 832, [-576, -544]
    else:
        raise ValueError('Unsupported bounded recipe trial')
    commands = [{'frames': 800 if name == 'lockyard' else 300, 'forward': speed, 'side': 0, 'up': 0, 'yaw': yaw, 'pitch': 0}]
    suffix = ''
    if profile == 'phased-pitch30':
        require(name == 'lockyard', 'Phased dive trial is defined only for Lockyard')
        first, dive = (160, 96) if speed == 400 else (220, 144)
        commands = [{'frames': count, 'forward': speed, 'side': 0, 'up': 0, 'yaw': 0, 'pitch': pitch}
                    for count, pitch in ((first, 0), (dive, 30), (500, 0))]
        suffix = '-phased-pitch30'
    elif profile == 'constant-pitch30':
        require(name == 'lockyard', 'Constant dive trial is defined only for Lockyard')
        commands[0]['pitch'] = 30
        suffix = '-constant-pitch30'
    else:
        require(profile == 'level', 'Unknown command profile')
    return {'schema_version': 1, 'id': f'{name}-{"wet" if wet else "dry"}-forward{speed}{suffix}-{timestep}ms',
            'bsp': str(bsp), 'start': {'feet': start, 'clearance_z': 0.125, 'velocity': [0, 0, 0]},
            'gravity': 800, 'timestep_ms': timestep, 'settle_frames': 8,
            'commands': commands,
            'goal': {'axis': axis, 'coordinate': coordinate, 'direction': 1, 'feet_z': 0, 'z_tolerance': 1,
                     'cross_axis_range': lane, 'waterlevel': 0, 'grounded': True}}


def experiment(name):
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    dry, proof = dry_control(name)
    wet = PROJECT/f'out/{name}/qmg_{name}.bsp'
    rows = []
    for speed, profile in [(s, p) for s in (400, 200) for p in
                            (('level', 'phased-pitch30', 'constant-pitch30') if name == 'lockyard' else ('level',))]:
        records = []
        for state, path in ((True, wet), (False, dry)):
            spec = trial(name, speed, path, state, profile=profile)
            result = run_trial(spec, EVIDENCE/spec['id'])
            if name == 'lockyard' and state and result['success']:
                frames = [json.loads(line) for line in (EVIDENCE/spec['id']/'frames.ndjson').read_text(encoding='utf-8').splitlines()][1:]
                frames = frames[:result['goal_frame']]
                full_under = [f for f in frames if 272 <= f['origin'][0] <= 336]
                require(full_under and all(f['waterlevel'] == 3 and f['hull_mins'] == [-16, -16, -24] and
                        f['hull_maxs'] == [16, 16, 32] and f['origin'][2]+32 <= -40 for f in full_under),
                        'Successful wet trial lacks a fully submerged standing passage beneath the authored beam')
                result['submerged_beam_crossing'] = {'full_hull_center_x_interval': [272, 336], 'sample_count': len(full_under),
                                                     'all_waterlevel_three': True, 'all_standing_56_high': True,
                                                     'maximum_player_top_z': max(f['origin'][2]+32 for f in full_under),
                                                     'ceiling_underside_z': -40, 'no_upmove_command': True}
                save(EVIDENCE/spec['id']/'result.json', result)
            records.append(result)
            print(json.dumps({k: result[k] for k in ('id', 'success', 'movement_time_ms', 'failure_reason', 'waterlevel_sequence')}), flush=True)
        require(records[0]['commands_sha256'] == records[1]['commands_sha256'], 'Paired input commands changed')
        rows.append({'forwardmove': speed, 'command_profile': profile, 'wet': records[0], 'dry': records[1],
                     'wet_minus_dry_ms': records[0]['movement_time_ms']-records[1]['movement_time_ms']
                     if all(r['success'] for r in records) else None})
    record = {'schema_version': 1, 'id': name+'-native-water-control-v1', 'reference': 'Official Quake II 3.19 portable C CM/Pmove',
              'control_proof': proof, 'pairs': rows,
              'limits': ['Static world collision only; no client, game simulation, dynamic entities, damage, oxygen, inventory, server command filtering or multiplayer encounters.',
                         'The forward400 and200 labels are literal scripted usercmd values; they are not captured human run/walk behaviour.',
                         'First valid dry grounded goal event after eight settling frames; 16ms command quantization; fixed input has no adaptive navigation.',
                         'Water is removed in a separately compiled control. No native contents result or movement friction is overridden.',
                         'The paired dry control is the same culvert without water, not Lockyard\'s ordinary perimeter alternative; these times do not measure which map route is strategically better.',
                         'Dive profiles are successful fixed command instances, not optimized human trajectories or fastest-time claims.',
                         'This pins official 3.19 C behaviour on one local toolchain, not equivalence to Q2REPRO or all Quake II releases.']}
    save(EVIDENCE/(name+'-comparison.json'), record)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('map', choices=('fold', 'lockyard'))
    args = parser.parse_args()
    experiment(args.map)


if __name__ == '__main__':
    main()
