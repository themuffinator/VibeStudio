"""Predeclared complete Weft motif circuits with pinned Q2 3.19 world movement.

One fixed command schedule per motif, direction and input speed. Ordered route
gates and a bounded grounded low-speed dwell are derived from raw native frames;
the shared runner's single station remains a separate diagnostic.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import math
from itertools import product
from pathlib import Path

from tools.map_recipes.drop_audit import _flat_support, _sweep, _world
from tools.map_recipes.geometry import model_brushes
from tools.map_recipes.ravel_audit import _flat_route
from tools.native_movement.run import BUILD_RECORD, NATIVE, PROJECT, preflight, require, save, sha

OUTPUT = PROJECT / 'out/native-motif-circuits'
RUNNER = PROJECT / 'tools/native_movement/run.py'
BSP = PROJECT / 'out/weft/qmg_weft.bsp'
MAP = PROJECT / 'out/weft/qmg_weft.map'
LAYOUT = PROJECT / 'out/weft/layout.json'
PINS = {
    'bsp_sha256': '980b22bf6aaedb032df1f04db5cc46d864d72f961a9852809486fbd64abf50f0',
    'map_sha256': 'a00415003b3a72aaa206ad38d8ea02f5a12296441b70d746539bda41b31aa3c6',
    'layout_file_sha256': '72650518c27ec0c5614ec65246fe73abbe3a513ae5709923b8c4bad3a9123e50',
    'layout_sha256': 'aaddb7862052e2c3b0264ef99952f356f78dffd45ef23eb70e31eb3e7de6adf7',
}
MOTIFS = {
    'crossing6': {'room_id': 6, 'motif': 'raised_crosswalk', 'rotation': 0,
                  'points': [[88,552,0],[88,960,128],[696,960,128],[696,552,0]],
                  'stair_axis': 1, 'ascent_sign': 1, 'first_riser': 592,
                  'solid_ids': list(range(37,54))},
    'gallery1': {'room_id': 1, 'motif': 'horseshoe_gallery', 'rotation': 1,
                 'points': [[-48,-1168,0],[-680,-1168,128],[-680,-496,128],[-48,-496,0]],
                 'stair_axis': 0, 'ascent_sign': -1, 'first_riser': -88,
                 'solid_ids': list(range(18,37))},
}
SCOPE = ('Unmodified official Quake II 3.19 CM/Pmove, compiled world model 0 only. '
         'No game module, client, input device, renderer, damage, pickups, dynamic entities, '
         'opponents, human performance or optimality claim; no model weights trained.')


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def runner():
    spec = importlib.util.spec_from_file_location('_native_motif_circuit_runner', RUNNER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.EVIDENCE = OUTPUT
    return module


def pin_inputs():
    actual = {'bsp_sha256': sha(BSP), 'map_sha256': sha(MAP),
              'layout_file_sha256': sha(LAYOUT), 'layout_sha256': read(LAYOUT)['layout_sha256']}
    require(actual == PINS, 'Frozen Weft artifacts changed')
    runner().verify_build(NATIVE)
    return {**actual, 'runner_sha256': sha(RUNNER), 'native_build_record_sha256': sha(BUILD_RECORD),
            'native_executable_sha256': sha(NATIVE)}


def stair_point(motif, bottom, offset, z):
    p = list(bottom)
    p[motif['stair_axis']] = motif['first_riser'] + motif['ascent_sign'] * offset
    p[2] = z
    return p


def trial(name, direction, speed):
    require(name in MOTIFS and direction in ('forward','reverse') and speed in (200,400),
            'Undeclared motif circuit profile')
    motif = MOTIFS[name]
    points = motif['points'] if direction == 'forward' else list(reversed(motif['points']))
    nominal_speed = min(speed,300)
    commands = []
    for i, (a,b) in enumerate(zip(points,points[1:])):
        distance = abs(b[0]-a[0])+abs(b[1]-a[1])
        # An explicit geometric timing hypothesis, not fitted to native results.
        allowance = 24 - (32 if i == 2 else 0)
        count = math.ceil((distance+allowance)/(nominal_speed*.016))
        yaw = math.degrees(math.atan2(b[1]-a[1],b[0]-a[0]))
        commands.append({'frames': count, 'forward': speed, 'side': 0, 'up': 0,
                         'yaw': yaw, 'pitch': 0})
    commands.append({'frames':128,'forward':0,'side':0,'up':0,'yaw':commands[-1]['yaw'],'pitch':0})
    room = next(r for r in read(LAYOUT)['rooms'] if r['id'] == motif['room_id'])
    end = points[-1]
    bounds = [[max(end[i]-32,room['bounds'][0][i]+16) for i in range(2)],
              [min(end[i]+32,room['bounds'][1][i]-16) for i in range(2)]]
    stair_axis = motif['stair_axis']
    if motif['ascent_sign'] == 1:
        bounds[1][stair_axis] = min(bounds[1][stair_axis],motif['first_riser']-16)
    else:
        bounds[0][stair_axis] = max(bounds[0][stair_axis],motif['first_riser']+16)
    def gate(label,p,xy=24,z=1,grounded=True):
        return {'label':label,'feet':p,'xy_tolerance':xy,'feet_tolerance':z,'grounded':grounded}
    checkpoints = [gate('ascent_mid',stair_point(motif,points[0],126,64),10,17,False),
                   gate('upper_entry',stair_point(motif,points[0],304,128)),
                   gate('first_upper_corner',points[1],48),
                   gate('second_upper_corner',points[2],48),
                   gate('upper_exit',stair_point(motif,end,304,128)),
                   gate('descent_mid',stair_point(motif,end,126,64),10,24,False)]
    # Mid-gate tolerance is narrow along the run and full standing-hull width
    # along its cross lane. Corner carry-over may occupy that actual stair width.
    for g in (checkpoints[0],checkpoints[-1]):
        g['xy_tolerance'] = [48,48]
        g['xy_tolerance'][motif['stair_axis']] = 10
    axis = 1-motif['stair_axis']
    return {'schema_version':1,'id':f'weft-{name}-{direction}-f{speed}','bsp':str(BSP),
            'start':{'feet':points[0],'clearance_z':.125,'velocity':[0,0,0]},
            'gravity':800,'timestep_ms':16,'settle_frames':8,'commands':commands,
            'goal':{'axis':'xy'[axis],'coordinate':end[axis],
                    'direction':1 if end[axis]>points[0][axis] else -1,'feet_z':0,
                    'z_tolerance':1,'cross_axis_range':[-2000,2000],'waterlevel':0,'grounded':True},
            'goal_role':'Broad station diagnostic; circuit_goal and ordered route conditions define success.',
            'motif':name,'direction':direction,'literal_forwardmove':speed,
            'timing_nominal_units_per_second':nominal_speed,'route_points':points,
            'ordered_checkpoints':checkpoints,'route_center_half_width':48,
            'circuit_goal':{'nominal_feet':end,'center_xy_bounds':bounds,'feet_z':0,'feet_tolerance':1,
                            'maximum_horizontal_speed':10,'maximum_absolute_vertical_speed':10,
                            'consecutive_samples':9,'first_to_last_dwell_ms':128},
            'upper_floor_invariant':{'from_checkpoint':'upper_entry','through_checkpoint':'upper_exit',
                                     'feet_z':128,'feet_tolerance':1,'grounded':True}}


def standing_dry(frame):
    return (frame['hull_mins'] == [-16,-16,-24] and frame['hull_maxs'] == [16,16,32]
            and frame['waterlevel'] == 0 and not frame['post_startsolid'] and not frame['post_allsolid'])


def in_bounds(frame,bounds):
    return all(bounds[0][i] <= frame['origin'][i] <= bounds[1][i] for i in range(2))


def gate_matches(frame,gate):
    tolerance = gate['xy_tolerance']
    if isinstance(tolerance,(int,float)):
        tolerance = [tolerance,tolerance]
    return (standing_dry(frame) and
            all(abs(frame['origin'][i]-gate['feet'][i]) <= tolerance[i] for i in range(2)) and
            abs(frame['feet_z']-gate['feet'][2]) <= gate['feet_tolerance'] and
            (not gate['grounded'] or frame['grounded']))


def qualifies(frame,goal):
    return (standing_dry(frame) and frame['grounded'] and in_bounds(frame,goal['center_xy_bounds']) and
            abs(frame['feet_z']-goal['feet_z']) <= goal['feet_tolerance'] and
            frame['velocity'][0]**2+frame['velocity'][1]**2 <= goal['maximum_horizontal_speed']**2 and
            abs(frame['velocity'][2]) <= goal['maximum_absolute_vertical_speed'])


def on_route(frame,points,width):
    return any(all(min(a[i],b[i])-width <= frame['origin'][i] <= max(a[i],b[i])+width
                   for i in range(2)) for a,b in zip(points,points[1:]))


def event(frame):
    return None if frame is None else {**{k:frame[k] for k in
        ('frame','time_ms','origin','feet_z','velocity','grounded','waterlevel')},
        'movement_time_ms':frame['time_ms']-128}


def analyze(spec,frames):
    """Re-derive the first ordered complete circuit and dwell from native states."""
    expected = spec['settle_frames']+sum(c['frames'] for c in spec['commands'])
    require(len(frames) == expected and all(f['frame']==i and f['time_ms']==i*16 and f['msec']==16
                                          for i,f in enumerate(frames,1)), 'Incomplete native sequence')
    settled = frames[6:8]
    require(all(standing_dry(f) and f['grounded'] and f['velocity']==[0,0,0] and
                f['origin'][:2]==spec['start']['feet'][:2] and
                abs(f['feet_z']-spec['start']['feet'][2])<=1 for f in settled) and
            settled[0]['origin']==settled[1]['origin'], 'Unstable configured start')
    checkpoints = spec['ordered_checkpoints']
    passed = []; violation = accepted = hold_start = first_qualifying = None
    consecutive = 0; goal = spec['circuit_goal']
    upper_active = False
    for f in frames[8:]:
        reasons = []
        if not standing_dry(f): reasons.append('standing_dry_clear_hull_lost')
        if not on_route(f,spec['route_points'],spec['route_center_half_width']):
            reasons.append('left_declared_route_center_envelope')
        if not -1 <= f['feet_z'] <= 129: reasons.append('left_declared_vertical_envelope')
        if upper_active and (not f['grounded'] or abs(f['feet_z']-128)>1):
            reasons.append('lost_upper_gallery_floor_before_exit')
        if reasons and violation is None: violation = {'event':event(f),'reasons':reasons}
        if len(passed)<len(checkpoints) and gate_matches(f,checkpoints[len(passed)]):
            label = checkpoints[len(passed)]['label']
            passed.append({'label':label,'event':event(f)})
            if label == 'upper_entry': upper_active = True
            if label == 'upper_exit': upper_active = False
        if len(passed)==len(checkpoints) and violation is None and qualifies(f,goal):
            if first_qualifying is None: first_qualifying = f
            if consecutive == 0: hold_start = f
            consecutive += 1
            if consecutive >= goal['consecutive_samples']:
                require(f['time_ms']-hold_start['time_ms'] >= goal['first_to_last_dwell_ms'],
                        'Dwell interval too short')
                accepted = f
                break
        else:
            consecutive = 0; hold_start = None
    boundaries = []; cursor = 8
    for command in spec['commands']:
        cursor += command['frames']
        boundaries.append({'command':command,'end_event':event(frames[cursor-1])})
    reason = None
    if accepted is None:
        reason = ('route_invariant_failed' if violation else
                  'ordered_route_gate_not_reached' if len(passed)<len(checkpoints) else
                  'no_bounded_low_speed_dwell_within_budget')
    return {'schema_version':1,'success':accepted is not None,'failure_reason':reason,
            'ordered_gate_events':passed,'next_missing_checkpoint':checkpoints[len(passed)]['label']
                if len(passed)<len(checkpoints) else None,
            'first_violation':violation,'first_qualifying_sample':event(first_qualifying),
            'dwell_start':event(hold_start) if accepted else None,'first_circuit_goal':event(accepted),
            'movement_time_ms':accepted['time_ms']-128 if accepted else None,
            'phase_boundaries':boundaries,'full_budget_final_event':event(frames[-1]),
            'native_frame_count':len(frames),'observation_through_native_frame':accepted['frame'] if accepted else len(frames),
            'maximum_horizontal_speed':max(math.hypot(*f['velocity'][:2]) for f in frames[8:]),
            'limits':'Route invariants and nine-sample dwell are checked at16ms frame ends. Raw commands continue after an accepted goal. No subframe envelope or game-module traversal claim.'}


def geometry_audit():
    bsp = preflight(BSP)
    raw = model_brushes(bsp)
    require(all(bsp.brushes[i][2]==1 for i in raw),'Static audit requires all-SOLID world brushes')
    world = _world(bsp); layout = read(LAYOUT)
    counts = {'world_pair_checks':0,'exact_swept_checks':0}; rows = []
    for name,m in MOTIFS.items():
        matches = []
        for s in (s for s in layout['solids'] if s['id'] in m['solid_ids']):
            bounds = tuple(tuple(v) for v in s['bounds'])
            expected_vertices = set(product(*zip(*bounds)))
            found = [i for i,_,shape in world if shape.bounds==bounds and set(shape.vertices)==expected_vertices]
            require(found,f'Selected source solid {s["id"]} differs from compiled AABB')
            matches.append({'source_solid':s['id'],'purpose':s['purpose'],'bounds':s['bounds'],'compiled_brushes':found})
        require(len(matches)==len(m['solid_ids']),'Missing selected source solids')
        a,c1,c2,b = m['points']; runs = []
        for bottom in (a,b):
            points = [bottom]; tread_support = []
            for step in range(8):
                # Raise only when the leading full hull reaches the riser plane.
                at_riser = stair_point(m,bottom,step*36-16,step*16)
                raised = [*at_riser[:2],(step+1)*16]
                points.extend((at_riser,raised))
                center = stair_point(m,bottom,step*36+18,(step+1)*16)
                tread_support.append({'feet':center,'support_brushes':_flat_support(world,center,'complete tread footprint')})
            points.append(stair_point(m,bottom,304,128))
            for p,q in zip(points,points[1:]):
                _sweep(world,p,q,'complete piecewise step clearance',counts)
            runs.append({'bottom_feet':bottom,'clearance_path':points,'tread_support':tread_support,
                         'forward_and_reverse_clearance':True,
                         'interpretation':'Continuous piecewise configuration-space clearance; vertical segments are geometric16-unit riser lifts, not simulated movements or continuous grounded support.'})
        upper = _flat_route(world,[runs[0]['clearance_path'][-1],c1,c2,runs[1]['clearance_path'][-1]],
                            name+' entire upper route including both elbows',counts)
        pockets = []
        for c in (c1,c2):
            points = [[c[0]-32,c[1]-32,128],[c[0]+32,c[1]-32,128],
                      [c[0]+32,c[1],128],[c[0]-32,c[1],128],
                      [c[0]-32,c[1]+32,128],[c[0]+32,c[1]+32,128]]
            pockets.append(_flat_route(world,points,name+'96-square full-hull turn pocket',counts))
        # Goal rectangles are clipped both to room walls and the first riser;
        # certify their entire occupied hull prism with touching32-unit strips.
        for bottom in (a,b): _flat_support(world,bottom,'start/end nominal full footprint')
        goals = []
        for direction in ('forward','reverse'):
            bounds = trial(name,direction,200)['circuit_goal']['center_xy_bounds']
            ys = list(range(bounds[0][1],bounds[1][1]+1,32))
            if ys[-1] != bounds[1][1]: ys.append(bounds[1][1])
            strips = []
            for i,y in enumerate(ys):
                ends = [bounds[0][0],bounds[1][0]] if i%2==0 else [bounds[1][0],bounds[0][0]]
                strips.extend([[x,y,0] for x in ends])
            goals.append({'direction':direction,'center_xy_bounds':bounds,
                          'support_clearance':_flat_route(world,strips,name+' whole lower goal pocket',counts)})
        rows.append({'motif':name,'room_id':m['room_id'],'rotation':m['rotation'],
                     'source_compiled_matches':matches,'stair_runs':runs,'entire_upper_route':upper,
                     'turn_pockets':pockets,'whole_goal_pockets':goals})
    return {'schema_version':1,'ok':True,**PINS,'world_brush_count':len(world),
            'selected_stair_runs':4,'selected_treads':32,'routes':rows,**counts,
            'limit':'Exact compiled-plane geometric proof for selected paths and pockets. Tread centers have complete footprint support; rising step configurations need not. Movement is tested separately.'}


def contract():
    return {'schema_version':1,'id':'weft-native-motif-circuits-v1','inputs':pin_inputs(),
            'trials':[trial(n,d,s) for n in MOTIFS for d in ('forward','reverse') for s in (200,400)],
            'timing_hypothesis':{'nominal_speed':'min(literal forwardmove,300) units/s;400 is input amplitude, not400 units/s.',
                                 'phase_frames':'ceil((planar leg length+24-(32 on final leg))/(nominal_speed*.016))',
                                 'allowance_units':24,'final_release_distance_reserve_units':32,
                                 'final_release_frames':128,'basis':'Geometry and explicit coarse acceleration/coast allowances, chosen before any circuit execution; not fitted or calibrated to these native traces.'},
            'hypotheses':['Full stair/gallery/stair circuits may expose failures hidden by isolated tread and node poses.',
                          'Momentum at fixed90-degree turns may displace the second stair approach.',
                          'Nominally identical direction reversals may differ under native quantization.',
                          'An endpoint station crossing may fail ordered-route or low-speed recovery conditions.'],
            'profiles_fixed_before_execution':True,'adaptive_steering':False,'optimized':False,'scope':SCOPE}


def experiment():
    require(not (OUTPUT/'protocol.json').exists(),'Protocol already exists; use --verify, never overwrite first attempts')
    OUTPUT.mkdir(parents=True,exist_ok=True)
    protocol = contract()
    save(OUTPUT/'protocol.json',protocol)
    save(OUTPUT/'geometry-audit.json',geometry_audit())
    save(OUTPUT/'comparison.json',{'status':'attempt_in_progress','success':False})
    rows = []; native = runner()
    for spec in protocol['trials']:
        folder = OUTPUT/spec['id']
        try:
            station = native.run_trial(spec,folder)
            frames = [json.loads(line) for line in (folder/'frames.ndjson').read_text().splitlines()][1:]
            outcome = analyze(spec,frames)
            save(folder/'circuit.json',outcome)
            row = {'id':spec['id'],'motif':spec['motif'],'direction':spec['direction'],
                   'literal_forwardmove':spec['literal_forwardmove'],'success':outcome['success'],
                   'failure_reason':outcome['failure_reason'],'movement_time_ms':outcome['movement_time_ms'],
                   'native_station_diagnostic_success':station['success'],
                   'circuit_sha256':sha(folder/'circuit.json')}
        except Exception as error:
            row = {'id':spec['id'],'success':False,'failure_reason':'execution_or_analysis_rejected','error':str(error)}
            save(folder/'circuit.json',row)
        rows.append(row); print(json.dumps(row),flush=True)
    require(pin_inputs()==protocol['inputs'],'Frozen inputs changed during native circuits')
    comparison = {'schema_version':1,'id':protocol['id'],'status':'completed_fixed_profile_experiment',
                  'inputs':protocol['inputs'],'protocol_sha256':sha(OUTPUT/'protocol.json'),
                  'orchestrator_sha256':sha(Path(__file__)),'trials':rows,
                  'circuit_successes':sum(r['success'] for r in rows),
                  'retained_failures':sum(not r['success'] for r in rows),
                  'world_only':True,'gameplay_tested':False,'model_weights_trained':False,'scope':SCOPE}
    save(OUTPUT/'comparison.json',comparison)
    return comparison


def verify():
    comparison = read(OUTPUT/'comparison.json'); protocol = read(OUTPUT/'protocol.json')
    require(comparison['status']=='completed_fixed_profile_experiment' and len(comparison['trials'])==8,
            'Incomplete circuit experiment')
    require(protocol==contract() and comparison['inputs']==protocol['inputs'],'Protocol/input drift')
    require(comparison['protocol_sha256']==sha(OUTPUT/'protocol.json') and
            comparison['orchestrator_sha256']==sha(Path(__file__)),'Circuit implementation/protocol changed')
    require(read(OUTPUT/'geometry-audit.json')==geometry_audit(),'Compiled geometric evidence changed')
    for row,spec in zip(comparison['trials'],protocol['trials']):
        folder = OUTPUT/spec['id']; native = read(folder/'result.json')
        require(row['id']==spec['id'] and read(folder/'trial.json')==spec,'Trial binding changed')
        require(native['bsp_sha256']==PINS['bsp_sha256'],'Native BSP binding differs')
        for name,key in [('trial.json','trial_sha256'),('commands.txt','commands_sha256'),('frames.ndjson','frames_sha256')]:
            require(sha(folder/name)==native[key],'Native evidence hash changed: '+name)
        frames = [json.loads(line) for line in (folder/'frames.ndjson').read_text().splitlines()][1:]
        outcome = analyze(spec,frames)
        require(outcome==read(folder/'circuit.json') and row['circuit_sha256']==sha(folder/'circuit.json') and
                row['success']==outcome['success'] and row['failure_reason']==outcome['failure_reason'] and
                row['movement_time_ms']==outcome['movement_time_ms'],'Independent first circuit outcome changed')
    require(comparison['circuit_successes']==sum(r['success'] for r in comparison['trials']) and
            comparison['retained_failures']==sum(not r['success'] for r in comparison['trials']),
            'Comparison totals changed')
    return {'ok':True,'independent_raw_frame_rederivations':8,'circuit_successes':comparison['circuit_successes'],
            'retained_failures':comparison['retained_failures'],'comparison_sha256':sha(OUTPUT/'comparison.json')}


def record():
    result = verify()
    paths = [Path(__file__),PROJECT/'tests/test_native_motif_circuits.py',
             PROJECT/'docs/Q2_NATIVE_MOTIF_CIRCUITS.md',PROJECT/'knowledge/q2/native-motif-circuits.json']
    paths += [p for p in OUTPUT.rglob('*') if p.is_file() and p.name!='record.json']
    entries = [{'path':p.relative_to(PROJECT).as_posix(),'sha256':sha(p)} for p in sorted(paths)]
    save(OUTPUT/'record.json',{'schema_version':1,'id':'weft-native-motif-circuits-v1',
                            'verification':result,'inputs':pin_inputs(),'files':entries})
    return {'bound_files':len(entries),'record_sha256':sha(OUTPUT/'record.json'),**result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--verify',action='store_true')
    mode.add_argument('--record',action='store_true')
    args = parser.parse_args()
    print(json.dumps(record() if args.record else verify() if args.verify else experiment(),indent=2))


if __name__=='__main__': main()
