"""Physical gate control and fixed pause-factor turning study on frozen Weft.

The prior eight failed circuit results are never modified. New physical-contract
interpretations are separate evidence. All timing is declared before native runs;
an empirical timing estimate is not a replacement for native collision/movement.
"""
from __future__ import annotations

import argparse
import copy
import importlib
import importlib.util
import json
import math
from itertools import product
from pathlib import Path

from tools.map_recipes.drop_audit import _sweep, _world
from tools.map_recipes.geometry import model_brushes
from tools.native_movement import motif_circuits as prior
from tools.native_movement.run import PROJECT, BUILD_RECORD, NATIVE, preflight, require, save, sha

OUTPUT = PROJECT/'out/native-motif-turning'
OLD = PROJECT/'out/native-motif-circuits'
OLD_RECORD_SHA = 'cc6af5ee0eb7bd72b4144ebb339708f12210cd9a0b628f6fa44b61c4f89cd855'
OBS = importlib.import_module('out.native-motif-circuits.inspect_observations')
PAUSES = (0,24)


def read(path): return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def frames_at(folder):
    return [json.loads(line) for line in (folder/'frames.ndjson').read_text().splitlines()][1:]


def pin_inputs():
    result = prior.pin_inputs()
    require(sha(OLD/'record.json')==OLD_RECORD_SHA,'Released prior circuit record changed')
    for entry in read(OLD/'record.json')['files']:
        require(sha(PROJECT/entry['path'])==entry['sha256'],'Released prior evidence changed: '+entry['path'])
    references = []
    for speed in (200,400):
        folder = PROJECT/f'out/native-recovery/ravel-recovery-f{speed}-release'
        native = read(folder/'result.json')
        for name,key in [('trial.json','trial_sha256'),('commands.txt','commands_sha256'),('frames.ndjson','frames_sha256')]:
            require(sha(folder/name)==native[key],'Prior stopping trace changed')
        references.extend({'path':(folder/name).relative_to(PROJECT).as_posix(),'sha256':sha(folder/name)}
                          for name in ('trial.json','commands.txt','frames.ndjson','result.json'))
    return {**result,'prior_circuit_record_sha256':OLD_RECORD_SHA,'stopping_references':references}


def runner():
    spec = importlib.util.spec_from_file_location('_native_motif_turning_runner',prior.RUNNER)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    module.EVIDENCE = OUTPUT
    return module


def timing_basis():
    result = {}
    for speed in (200,400):
        folder=OLD/f'weft-crossing6-forward-f{speed}'; frames=frames_at(folder)
        prefix=frames[17]['origin'][1]-frames[7]['origin'][1]
        steady=frames[19]['origin'][1]-frames[18]['origin'][1]
        stopfolder=PROJECT/f'out/native-recovery/ravel-recovery-f{speed}-release'
        stop_spec=read(stopfolder/'trial.json'); stopframes=frames_at(stopfolder)
        release=8+stop_spec['approach_frames']; start=stopframes[release-1]
        stop=next(f for f in stopframes[release:] if f['velocity']==[0,0,0])
        coast=start['origin'][0]-stop['origin'][0]
        moving=sum(f['velocity'][0]!=0 for f in stopframes[release:stop['frame']])
        # Transfer the recorded eighth-grid motion between signed-coordinate
        # classes. This is an explicit timing approximation, not a CM/Pmove port.
        away=coast-moving*.125 if speed==200 else coast
        toward=coast if speed==200 else coast+moving*.125
        result[str(speed)]={'native_wish_speed':min(speed,300),
                            'acceleration_prefix_frames':10,'away_prefix_distance':prefix,
                            'toward_prefix_distance':prefix+10*.125,
                            'away_steady_frame_distance':steady,'toward_steady_frame_distance':steady+.125,
                            'away_estimated_stop_distance':away,'toward_estimated_stop_distance':toward,
                            'measured_reference_stop_distance':coast,'reference_stop_frames':stop['frame']-release,
                            'nonzero_release_velocity_frames':moving,
                            'acceleration_frames_sha256':sha(folder/'frames.ndjson'),
                            'stopping_frames_sha256':sha(stopfolder/'frames.ndjson')}
    return {'by_input':result,
            'source_basis':{'pmove_source_sha256':read(BUILD_RECORD)['external_dependencies'][0]['sha256'],
                            'ground_friction':6,'stop_speed':100,'position_velocity_grid':.125,
                            'reviewed_functions':['PM_Friction','PM_Accelerate','PM_SnapPosition']},
            'assumption':'Use ten-frame stationary startup and steady increments from prior Weft native traces. Transfer an eighth-unit per nonzero frame between movement away from and toward coordinate zero; all selected legs stay on one side of zero. Estimate coast from retained ground-release traces. No exact native equivalence, optimality or fitted new-trial timing claim.'}


def estimated_frames(distance,speed,toward,basis,stop_at_end):
    row=basis['by_input'][str(speed)]; label='toward' if toward else 'away'
    coast=row[label+'_estimated_stop_distance'] if stop_at_end else 0
    value=10+(distance-coast-row[label+'_prefix_distance'])/row[label+'_steady_frame_distance']
    # Nearest whole command count; ties choose the earlier boundary. The
    # Crossing reverse200 second leg is exactly183.5 and therefore selects183.
    return max(10,math.ceil(value-.5))


def physical_spec(spec):
    result=copy.deepcopy(spec); m=prior.MOTIFS[spec['motif']]; axis=m['stair_axis']
    for g in result['ordered_checkpoints']:
        if g['label'] in ('upper_entry','upper_exit'):
            g['xy_tolerance']=[48,48]; g['xy_tolerance'][axis]=12
    result['physical_contract']={
        'stair_center_cross_half_width':48,'stair_width':128,'hull_width':32,
        'upper_entry_exit_axis_half_width':12,
        'stair_phase_axis_progress_from_first_riser':[-48,316],
        'axis_bound_basis':'Lower room-wall hull inset is48 units behind the first riser; upper station304 plus12 gate tolerance bounds the other end.',
        'upper_requirement':'Every frame standing, dry, grounded128±1, full footprint support; every consecutive reconstructed horizontal upper chord continuously clear with full conservative rectangular floor coverage.',
        'stair_requirement':'Full standing-hull cross lane throughout each stair phase; normal sampled airborne descent permitted. Vertical envelope0±1..128±1. Not constant whole-footprint support on one tread height.',
        'finish_requirement':'Same previously declared clipped lower pocket, grounded dry standing, velocity normXY≤10 and |Z|≤10 for9 consecutive16ms frame ends spanning128ms, after final release starts.',
        'original_result_replaced':False,'runtime_internal_substeps_recorded':False}
    return result


def new_trial(name,direction,speed,pause,basis):
    require(pause in PAUSES,'Undeclared pause factor')
    result=physical_spec(prior.trial(name,direction,speed))
    points=result['route_points']; commands=[]; derivation=[]
    for i,(a,b) in enumerate(zip(points,points[1:])):
        axis=0 if a[0]!=b[0] else 1; destination=list(b)
        if i==2:
            m=prior.MOTIFS[name]
            destination[axis]=m['first_riser']+m['ascent_sign']*18
        distance=abs(destination[axis]-a[axis]); toward=abs(destination[axis])<abs(a[axis])
        require(a[axis]*destination[axis]>0,'Timing transfer may not cross coordinate zero')
        count=estimated_frames(distance,speed,toward,basis,stop_at_end=i<2)
        yaw=math.degrees(math.atan2(b[1]-a[1],b[0]-a[0]))
        commands.append({'frames':count,'forward':speed,'side':0,'up':0,'yaw':yaw,'pitch':0})
        derivation.append({'leg':i,'nominal_start':a,'timing_destination':destination,
                           'distance':distance,'toward_coordinate_zero':toward,'active_frames':count,
                           'coast_reserved':i<2})
        if i<2 and pause:
            commands.append({'frames':pause,'forward':0,'side':0,'up':0,'yaw':yaw,'pitch':0})
    commands.append({'frames':128,'forward':0,'side':0,'up':0,'yaw':commands[-1]['yaw'],'pitch':0})
    result.update({'id':f'weft-{name}-{direction}-f{speed}-pause{pause}','commands':commands,
                   'corner_pause_frames':pause,'policy':'rolling' if pause==0 else 'release24',
                   'timing_derivation':derivation,
                   'goal_role':'Broad station diagnostic only; physical-circuit outcome is independently derived.'})
    return result


class World:
    def __init__(self):
        bsp=preflight(prior.BSP); raw=model_brushes(bsp)
        require(all(bsp.brushes[i][2]==1 for i in raw),'Physical corridor requires frozen all-SOLID world')
        self.world=_world(bsp)
        require(all(set(s.vertices)==set(product(*zip(*s.bounds))) for _,_,s in self.world),
                'Physical support helper requires exact compiled AABBs')
        self.rectangles=[(tuple(float(v) for v in s.bounds[0][:2]),tuple(float(v) for v in s.bounds[1][:2]),
                          float(s.bounds[1][2])) for _,_,s in self.world]

    def upper(self,a,b):
        low=[min(a['origin'][i],b['origin'][i])-16 for i in range(2)]
        high=[max(a['origin'][i],b['origin'][i])+16 for i in range(2)]
        result=OBS.support(self.rectangles,low,high,128)
        counts={'world_pair_checks':0,'exact_swept_checks':0}
        try:
            _sweep(self.world,[*a['origin'][:2],128],[*b['origin'][:2],128],'upper circuit chord',counts)
            result['clear']=True
        except ValueError as error:
            result.update({'clear':False,'error':str(error)})
        return {**result,**counts,'from_frame':a['frame'],'to_frame':b['frame']}


def stair_band(spec,frame,descending):
    m=prior.MOTIFS[spec['motif']]; axis=m['stair_axis']; cross=1-axis
    bottom=spec['route_points'][-1 if descending else 0]
    along=(frame['origin'][axis]-m['first_riser'])*m['ascent_sign']
    return abs(frame['origin'][cross]-bottom[cross])<=48 and -48<=along<=316


def analyze(spec,frames,world):
    expected=8+sum(c['frames'] for c in spec['commands'])
    require(len(frames)==expected and all(f['frame']==i and f['time_ms']==i*16 and f['msec']==16
                                        for i,f in enumerate(frames,1)), 'Incomplete native sequence')
    require(all(prior.standing_dry(f) and f['grounded'] and f['velocity']==[0,0,0] and
                f['origin'][:2]==spec['start']['feet'][:2] and abs(f['feet_z'])<=1 for f in frames[6:8]) and
            frames[6]['origin']==frames[7]['origin'],'Unstable declared start')
    stage='ascent'; passed=[]; violation=None; accepted=None; hold=None; consecutive=0; upper=[]
    first_final_sample=None; final_dwell=None; final_hold=None; final_count=0
    release_start=8+sum(c['frames'] for c in spec['commands'][:-1])+1
    checkpoints=spec['ordered_checkpoints']; previous=frames[7]
    for f in frames[8:]:
        reasons=[]
        if not prior.standing_dry(f): reasons.append('standing_dry_clear_hull_lost')
        if not -1<=f['feet_z']<=129: reasons.append('vertical_envelope_lost')
        if stage in ('ascent','descent') and not stair_band(spec,f,stage=='descent'):
            reasons.append('full_hull_stair_corridor_lost')
        if stage=='upper':
            if not f['grounded'] or abs(f['feet_z']-128)>1:
                reasons.append('upper_floor_contact_lost')
            proof=world.upper(previous,f); upper.append(proof)
            if not proof['fully_supported']: reasons.append('upper_full_footprint_support_lost')
            if not proof['clear']: reasons.append('upper_reconstructed_chord_blocked')
        if len(passed)<len(checkpoints) and prior.gate_matches(f,checkpoints[len(passed)]):
            label=checkpoints[len(passed)]['label']; passed.append({'label':label,'event':prior.event(f)})
            if label=='upper_entry':
                stage='upper'; proof=world.upper(f,f); upper.append(proof)
                if not proof['fully_supported'] or not proof['clear']:
                    reasons.append('upper_entry_full_footprint_or_clearance_lost')
            if label=='upper_exit': stage='descent'
        if reasons and violation is None: violation={'event':prior.event(f),'reasons':reasons}
        good=f['frame']>=release_start and prior.qualifies(f,spec['circuit_goal'])
        if good:
            if final_count==0: final_hold=f
            final_count+=1
            if first_final_sample is None: first_final_sample=f
            if final_count==9 and final_dwell is None: final_dwell={'start':prior.event(final_hold),'end':prior.event(f)}
        else: final_count=0; final_hold=None
        if good and len(passed)==len(checkpoints) and violation is None:
            if consecutive==0: hold=f
            consecutive+=1
            if consecutive==9 and accepted is None:
                require(f['time_ms']-hold['time_ms']==128,'Nine-state dwell must span128ms')
                accepted={'start':prior.event(hold),'end':prior.event(f)}
        else: consecutive=0; hold=None
        previous=f
        # First acceptance ends the interpreted interval. The full raw budget
        # and command-boundary/final observations remain retained separately.
        if accepted: break
    boundaries=[]; cursor=8
    for c in spec['commands']:
        cursor+=c['frames']; boundaries.append({'command':c,'event':prior.event(frames[cursor-1])})
    reason=None if accepted else ('physical_corridor_failed' if violation else
        'ordered_route_gate_not_reached' if len(passed)<len(checkpoints) else 'no_bounded_low_speed_final_dwell')
    return {'schema_version':1,'success':accepted is not None,'failure_reason':reason,
            'gate_events':passed,'next_missing_gate':checkpoints[len(passed)]['label'] if len(passed)<len(checkpoints) else None,
            'first_violation':violation,'first_physical_circuit_dwell':accepted,
            'movement_time_ms':accepted['end']['movement_time_ms'] if accepted else None,
            'final_pocket_dwell_ignoring_route_failure':final_dwell,
            'upper_chord_proofs':upper,'upper_chord_count':len(upper),
            'phase_boundaries':boundaries,'full_budget_final_event':prior.event(frames[-1]),
            'raw_frame_count':len(frames),'observation_through_frame':accepted['end']['frame'] if accepted else len(frames),
            'original_protocol_relabelled':False,
            'limit':'Physical corridor and ground contact use native frame-end states. Upper chords are reconstructed geometric certificates, not native internal substep recordings. Step transitions do not require one constant whole-footprint support height.'}


def contract():
    basis=timing_basis()
    return {'schema_version':1,'id':'weft-native-motif-turning-v1','inputs':pin_inputs(),
            'orchestrator_sha256':sha(Path(__file__)),'physical_control_inputs':[
                {'id':s['id'],'spec':physical_spec(s),'raw_frames_sha256':sha(OLD/s['id']/'frames.ndjson')}
                for s in read(OLD/'protocol.json')['trials']],
            'new_trials':[new_trial(n,d,s,p,basis) for n in prior.MOTIFS
                          for d in ('forward','reverse') for s in (200,400) for p in PAUSES],
            'timing_basis':basis,'factorial_dimensions':{'motifs':2,'directions':2,'inputs':2,'corner_pause_frames':[0,24]},
            'hypotheses':['A separately declared physical stair gate can distinguish narrow-protocol misses from actual footprint loss.',
                          'At identical earlier turn command boundaries, a24-frame release can reduce lateral carry-over compared with an immediate rolling turn.',
                          'Separately measured startup, signed-coordinate increments and coast are a better timing hypothesis than constant nominal speed, without guaranteeing success.',
                          'Even a supported complete approach may fail the unchanged bounded low-speed finish.'],
            'no_original_result_relabelled':True,'adaptive_inputs':False,'profiles_fixed_before_new_execution':True,
            'scope':prior.SCOPE}


def declared():
    protocol=read(OUTPUT/'protocol.json')
    require(protocol==contract(),'Frozen turning protocol, implementation or inputs changed')
    return protocol


def declare():
    require(not (OUTPUT/'protocol.json').exists(),'Turning protocol already declared')
    OUTPUT.mkdir(parents=True,exist_ok=True)
    protocol=contract(); save(OUTPUT/'protocol.json',protocol)
    return {'declared':True,'protocol_sha256':sha(OUTPUT/'protocol.json'),'new_trials':16,'prior_control_traces':8}


def control():
    protocol=declared(); require(not (OUTPUT/'prior-physical-control.json').exists(),'Prior control already recorded')
    world=World(); rows=[]
    for ref in protocol['physical_control_inputs']:
        result=analyze(ref['spec'],frames_at(OLD/ref['id']),world)
        rows.append({'id':ref['id'],'original_strict_success':read(OLD/ref['id']/'circuit.json')['success'],
                     'raw_frames_sha256':ref['raw_frames_sha256'],'physical_control':result})
    result={'schema_version':1,'kind':'protocol_only_control_on_existing_raw_frames','native_runs_added':0,
            'protocol_sha256':sha(OUTPUT/'protocol.json'),'original_results_modified':False,'rows':rows,
            'physical_control_successes':sum(r['physical_control']['success'] for r in rows),
            'original_strict_successes':sum(r['original_strict_success'] for r in rows)}
    save(OUTPUT/'prior-physical-control.json',result)
    return {k:v for k,v in result.items() if k!='rows'}


def experiment():
    protocol=declared(); require((OUTPUT/'prior-physical-control.json').exists(),'Record protocol-only control first')
    require(not (OUTPUT/'comparison.json').exists(),'Never overwrite first new native attempts')
    save(OUTPUT/'comparison.json',{'status':'attempt_in_progress','success':False})
    native=runner(); world=World(); rows=[]
    for spec in protocol['new_trials']:
        folder=OUTPUT/spec['id']; station=native.run_trial(spec,folder)
        result=analyze(spec,frames_at(folder),world); save(folder/'physical-circuit.json',result)
        row={'id':spec['id'],'motif':spec['motif'],'direction':spec['direction'],
             'literal_forwardmove':spec['literal_forwardmove'],'corner_pause_frames':spec['corner_pause_frames'],
             'success':result['success'],'failure_reason':result['failure_reason'],
             'movement_time_ms':result['movement_time_ms'],'native_station_diagnostic_success':station['success'],
             'physical_circuit_sha256':sha(folder/'physical-circuit.json')}
        rows.append(row); print(json.dumps(row),flush=True)
    require(declared()==protocol,'Protocol changed during execution')
    result={'schema_version':1,'status':'completed_fixed_factorial_experiment','inputs':protocol['inputs'],
            'protocol_sha256':sha(OUTPUT/'protocol.json'),'trials':rows,'native_attempts':16,
            'successes':sum(r['success'] for r in rows),'retained_failures':sum(not r['success'] for r in rows),
            'world_only':True,'gameplay_tested':False,'scope':prior.SCOPE}
    save(OUTPUT/'comparison.json',result)
    return result


def verify():
    protocol=declared(); world=World(); control_record=read(OUTPUT/'prior-physical-control.json')
    require(control_record['protocol_sha256']==sha(OUTPUT/'protocol.json'),'Control protocol differs')
    require(len(control_record['rows'])==8 and control_record['native_runs_added']==0 and
            control_record['original_results_modified'] is False,'Control scope differs')
    for row,ref in zip(control_record['rows'],protocol['physical_control_inputs']):
        require(row['id']==ref['id'] and row['raw_frames_sha256']==ref['raw_frames_sha256'] and
                row['original_strict_success']==read(OLD/ref['id']/'circuit.json')['success'] and
                row['physical_control']==analyze(ref['spec'],frames_at(OLD/ref['id']),world),
                'Independent prior-trace interpretation differs')
    require(control_record['physical_control_successes']==sum(r['physical_control']['success'] for r in control_record['rows'])
            and control_record['original_strict_successes']==sum(r['original_strict_success'] for r in control_record['rows']),
            'Control aggregate differs')
    comparison=read(OUTPUT/'comparison.json')
    require(comparison['status']=='completed_fixed_factorial_experiment' and len(comparison['trials'])==16,
            'Incomplete factorial experiment')
    require(comparison['inputs']==protocol['inputs'] and comparison['protocol_sha256']==sha(OUTPUT/'protocol.json')
            and comparison['native_attempts']==16,'Comparison input/protocol binding differs')
    for row,spec in zip(comparison['trials'],protocol['new_trials']):
        folder=OUTPUT/spec['id']; native=read(folder/'result.json')
        require(row['id']==spec['id'] and read(folder/'trial.json')==spec,'Trial specification drift')
        require(native['bsp_sha256']==prior.PINS['bsp_sha256'],'Native BSP differs')
        for name,key in [('trial.json','trial_sha256'),('commands.txt','commands_sha256'),('frames.ndjson','frames_sha256')]:
            require(sha(folder/name)==native[key],'Native artifact hash mismatch')
        result=analyze(spec,frames_at(folder),world)
        require(result==read(folder/'physical-circuit.json') and row['physical_circuit_sha256']==sha(folder/'physical-circuit.json')
                and row['success']==result['success'] and row['movement_time_ms']==result['movement_time_ms'],
                'Independent new-trace interpretation differs')
    require(comparison['successes']==sum(r['success'] for r in comparison['trials']) and
            comparison['retained_failures']==sum(not r['success'] for r in comparison['trials']),
            'Comparison aggregate differs')
    return {'ok':True,'prior_raw_traces_rederived':8,'new_native_traces_rederived':16,
            'physical_control_successes':sum(r['physical_control']['success'] for r in control_record['rows']),
            'new_successes':sum(r['success'] for r in comparison['trials']),
            'comparison_sha256':sha(OUTPUT/'comparison.json')}


def record():
    result=verify()
    paths=[Path(__file__),PROJECT/'tests/test_native_motif_turning.py',PROJECT/'docs/Q2_NATIVE_MOTIF_TURNING.md',
           PROJECT/'knowledge/q2/native-motif-turning.json']
    paths += [p for p in OUTPUT.rglob('*') if p.is_file() and p.name!='record.json']
    save(OUTPUT/'record.json',{'schema_version':1,'id':'weft-native-motif-turning-v1','verification':result,
                             'files':[{'path':p.relative_to(PROJECT).as_posix(),'sha256':sha(p)} for p in sorted(paths)]})
    return {'bound_files':len(paths),'record_sha256':sha(OUTPUT/'record.json'),**result}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode',choices=('declare','control','run','verify','record'))
    args=parser.parse_args(); action={'declare':declare,'control':control,'run':experiment,'verify':verify,'record':record}[args.mode]
    print(json.dumps(action(),indent=2))


if __name__=='__main__': main()
