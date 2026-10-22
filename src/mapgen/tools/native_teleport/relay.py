"""Four predeclared stock-game source-entry, hold and off-disc Relay journeys."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess

from tools.native_teleport import run as common
from tools.native_teleport.build import PROJECT, OUTPUT, EXECUTABLE, sha, save

common.BUILD_RECORD = OUTPUT/'build-attempt-03.json'
PROTOCOL = OUTPUT/'relay-protocol.json'
RESULTS = OUTPUT/'relay-v1'


def read(path): return common.read(path)
def require(value, message): return common.require(value, message)


def contract():
    features_path = PROJECT/'out/relay/features.json'
    features = read(features_path)
    bsp = PROJECT/'out/relay/qmg_relay.bsp'
    common.preflight(bsp)
    require(features['source_approach']==[-704,-720,0] and
            features['teleporter']=={'source_origin':[-704,-624,24], 'destination_origin':[-480,672,24], 'arrival_yaw':0},
            'Frozen geometry stations differ')
    old_path = PROJECT/'out/native-motif-turning/protocol.json'
    old = read(old_path)
    timing = old['timing_basis']
    trials = []
    for speed in (200,400):
        for direction, sign in (('east',1), ('west',-1)):
            label = 'toward' if sign==1 else 'away'
            basis = timing['by_input'][str(speed)]
            estimated = 10+(144-basis[label+'_estimated_stop_distance']-basis[label+'_prefix_distance'])/basis[label+'_steady_frame_distance']
            count = math.ceil(estimated-.5)
            # Source approach is16 units longer than the development fixture.
            # These counts safely reach its first expanded trigger contact band;
            # zero input then covers the native hold and stationary disc settle.
            approach = 27 if speed==200 else 19
            yaw = 90 if sign==1 else 270
            spec = {'id':f'relay-{direction}-f{speed}', 'expected':'clear_teleport',
                'bsp':'out/relay/qmg_relay.bsp','target':'qmg_link_35',
                'start_feet':features['source_approach'],'initial_command_yaw':90,
                'destination_yaw':999,'occupancy':0,'missing_target':0,
                'input':speed,'direction':direction,'sign':sign,
                'departure_command_yaw':yaw,'expected_departure_view_yaw':0 if sign==1 else 180,
                'departure_frames':count,'departure_start_frame':8+approach+32+1,
                'release_start_frame':8+approach+32+count+1,
                'count_estimate':estimated,
                'goal_geometric_feet':[-480+sign*128,672,0],
                'commands':[{'frames':8,'forward':0,'yaw':90},
                            {'frames':approach,'forward':speed,'yaw':90},
                            {'frames':32,'forward':0,'yaw':90},
                            {'frames':count,'forward':speed,'yaw':yaw},
                            {'frames':64,'forward':0,'yaw':yaw}]}
            spec['commands_sha256'] = hashlib.sha256(common.commands(spec).encode()).hexdigest()
            trials.append(spec)
    return {'schema_version':1,'id':'relay-native-teleport-v1','build':common.build_identity(),
        'orchestrator_sha256':sha(Path(__file__)),'common_runner_sha256':sha(Path(common.__file__)),
        'bsp_sha256':sha(bsp),'map_sha256':sha(PROJECT/'out/relay/qmg_relay.map'),
        'layout_sha256':features['layout_sha256'],'features_sha256':sha(features_path),
        'development_protocol_sha256':sha(OUTPUT/'development-v2-protocol.json'),
        'development_comparison_sha256':sha(OUTPUT/'development-v2/comparison.json'),
        'timing_basis':timing,'timing_protocol_path':old_path.relative_to(PROJECT).as_posix(),
        'timing_protocol_sha256':sha(old_path),
        'count_rule':'Nearest whole count to10+(144-estimated coast-10-frame startup)/steady frame distance. Signed coordinate quantization and prior coast transfer are approximations, not new-map movement predictions. Start on an8-unit disc, then drop to ground; no new-result tuning.',
        'trials':trials,'native_attempts':4,'timestep_ms':16,'game_tick_ms':100,
        'initial_game_ticks':2,'adaptive_inputs':False,'actor_relocated_after_initial_fixture':False,
        'contract':{
            'source_center_band':{'x':[-705,-703],'y':[-721,-647],'feet_z':[0,8.25]},
            'departure_center_band':{'signed_x_from_arrival':[0,160],'y':[671,673],'feet_z':[0,10.25]},
            'hold':'One actual stock teleporter transition, immediate state and next9 held frames, tenth release; at planned departure start preceding state stationary on destination disc feet8±.25 with teleport flag clear.',
            'goal':'After release starts, signed X displacement128..160 inclusive, Y671..673, feet Z0±.25; standing32x32x56, alive, dry, clear hull, grounded, velocity norm<=1. Nine consecutive16ms end states spanning128ms; first such run is final goal.',
            'failure':'Any band/clearance/standing/dry/life violation at a retained ClientThink state is irreversible. No reinterpretation or widening after first attempts.',
            'support_scope':'Native CM plus stock dynamic pads and stock Pmove ground contacts. Full-footprint world-floor support is separately statically audited by Relay; discrete native endpoints do not expose internal Pmove substeps.',
            'onward':'No native movement beyond this off-disc goal strip is claimed; compare actual final center with the first declared static departure segment separately.'},
        'scope':'Stock game API activation, held arrival, intentional facing and first128-unit off-disc departure in a controlled synthetic actor fixture. No client/network, human play, multiplayer balance or native onward-resource journey.'}


def standing(actor):
    return actor['mins']==[-16,-16,-24] and actor['maxs']==[16,16,32] and not actor['pm_flags'] & 1


def speed(actor): return math.sqrt(sum(v*v for v in actor['velocity']))


def settled_goal(actor, spec):
    displacement = spec['sign']*(actor['origin'][0]+480)
    return (128<=displacement<=160 and 671<=actor['origin'][1]<=673 and
            abs(actor['origin'][2]-24)<=.25 and standing(actor) and actor['health']>0 and
            actor['waterlevel']==0 and not actor['post_startsolid'] and not actor['post_allsolid'] and
            actor['groundentity']>=0 and speed(actor)<=1)


def first_dwell(frames, spec):
    run=[]
    for row in frames:
        if row['frame']>=spec['release_start_frame'] and settled_goal(row['actor'],spec):
            if run and row['frame']!=run[-1]['frame']+1: run=[]
            run.append(row)
            if len(run)==9 and row['time_ms']-run[0]['time_ms']==128:
                return {'start_frame':run[0]['frame'],'end_frame':row['frame'],
                        'start_time_ms':run[0]['time_ms'],'end_time_ms':row['time_ms'],
                        'first_feet':[run[0]['actor']['origin'][0],run[0]['actor']['origin'][1],run[0]['actor']['origin'][2]-24],
                        'last_feet':[row['actor']['origin'][0],row['actor']['origin'][1],row['actor']['origin'][2]-24],
                        'maximum_speed':max(speed(r['actor']) for r in run)}
        else: run=[]
    return None


def interpret(spec, records):
    base = common.interpret(spec,records)
    fixture=records[0]
    require(fixture['source_pad']['origin']==[-704,-624,24] and fixture['destination_origin']==[-480,672,24] and fixture['destination_angles']==[0,0,0], 'Native Relay entity pair differs')
    frames=[r for r in records if r['type']=='after_clientthink']
    require(base['expected_outcome_observed'],'Stock activation/arrival/hold failed')
    activation=base['arrival']['frame']
    violations=[]
    for row in frames:
        actor=row['actor']; x,y,z=actor['origin']; feet=z-24
        if not standing(actor) or actor['waterlevel'] or actor['health']<=0 or actor['post_startsolid'] or actor['post_allsolid']:
            violations.append({'frame':row['frame'],'reason':'life/standing/dry/clear-hull contract'})
        if row['frame']<activation:
            in_band=-705<=x<=-703 and -721<=y<=-647 and 0<=feet<=8.25
        else:
            in_band=0<=spec['sign']*(x+480)<=160 and 671<=y<=673 and 0<=feet<=10.25
        if not in_band:
            violations.append({'frame':row['frame'],'reason':'declared physical center band','feet':[x,y,feet]})
    before=frames[spec['departure_start_frame']-2]['actor']
    staging=(abs(before['origin'][0]+480)<=.25 and abs(before['origin'][1]-672)<=.25 and
             abs(before['origin'][2]-32)<=.25 and speed(before)<=1 and before['groundentity']>=0 and not before['pm_flags']&32)
    facing=all(common.angle_close(row['actor']['viewangles'][1],spec['expected_departure_view_yaw'])
               for row in frames[spec['departure_start_frame']-1:])
    dwell=first_dwell(frames,spec)
    failures=[]
    if not staging: failures.append('planned departure did not begin settled on disc')
    if not facing: failures.append('departure facing differs')
    if violations: failures.append('physical state/band violation')
    if dwell is None: failures.append('no first128ms grounded low-speed off-disc goal dwell')
    final=frames[-1]['actor']; point=[final['origin'][0],final['origin'][1],final['origin'][2]-24]
    # Both declared static paths begin at128 and run another160/192 in the
    # same cardinal direction. This is collinearity, not another native run.
    on_static=abs(point[1]-672)<=.125 and abs(point[2])<=.25 and 128<=spec['sign']*(point[0]+480)<= (320 if spec['sign']==1 else 288)
    return {'success':not failures,'failure_reasons':failures,'activation_frame':activation,
        'activation_time_ms':base['arrival']['time_ms'],'stock_state':base,
        'staging_before_departure':before,'planned_staging_pass':staging,
        'departure_facing_matches':facing,'violations':violations,'first_goal':dwell,
        'actual_final_feet':point,'final_velocity':final['velocity'],
        'final_center_on_first_static_departure_segment':on_static,
        'native_ClientThink_frames':len(frames),'after_RunFrame_records':sum(r['type']=='after_gameframe' for r in records),
        'raw_link_records':sum(r['type']=='link' for r in records)}


def declare():
    require(not PROTOCOL.exists(),'Never replace declared Relay inputs')
    save(PROTOCOL,contract())
    return {'declared':True,'native_runs':0,'protocol_sha256':sha(PROTOCOL)}


def execute():
    protocol=read(PROTOCOL);require(protocol==contract(),'Relay protocol changed')
    require(not RESULTS.exists(),'Never replace Relay first attempts');RESULTS.mkdir()
    rows=[]
    for spec in protocol['trials']:
        folder=RESULTS/spec['id'];folder.mkdir();save(folder/'trial.json',spec)
        stdin=common.commands(spec);(folder/'commands.txt').write_bytes(stdin.encode())
        require(sha(folder/'commands.txt')==spec['commands_sha256'],'Declared command bytes differ')
        argv=[str(EXECUTABLE),str(PROJECT/spec['bsp']),spec['target'],*[str(v) for v in spec['start_feet']],
              str(spec['initial_command_yaw']),str(spec['destination_yaw']),str(spec['occupancy']),str(spec['missing_target'])]
        save(folder/'process.json',{'argv':argv,'cwd':str(PROJECT),'timeout_seconds':30})
        try:
            result=subprocess.run(argv,input=stdin,text=True,capture_output=True,cwd=PROJECT,timeout=30)
            stdout,stderr,code=result.stdout,result.stderr,result.returncode
        except subprocess.TimeoutExpired as error:
            stdout=(error.stdout or b'').decode() if isinstance(error.stdout,bytes) else (error.stdout or '')
            stderr=(error.stderr or b'').decode() if isinstance(error.stderr,bytes) else (error.stderr or '')
            code='timeout'
        (folder/'events.ndjson').write_bytes(stdout.encode());(folder/'native-stderr.txt').write_bytes(stderr.encode())
        row={'id':spec['id'],'direction':spec['direction'],'input':spec['input'],'exit_code':code,'success':False,
             'events_sha256':sha(folder/'events.ndjson'),'commands_sha256':sha(folder/'commands.txt')}
        if code==0:
            try: row.update(interpret(spec,common.event_records(folder)))
            except Exception as error: row['interpretation_error']=str(error)
        else: row['native_error']=stderr[-1200:]
        save(folder/'result.json',row);rows.append(row)
        print(json.dumps({k:v for k,v in row.items() if k not in ('stock_state','staging_before_departure','violations')}),flush=True)
        require(protocol==contract(),'Relay inputs changed during first matrix')
    comparison={'protocol_sha256':sha(PROTOCOL),'trials':rows,'first_attempts':4,
                'successful_journeys':sum(r['success'] for r in rows),'bsp_sha256':protocol['bsp_sha256'],
                'layout_sha256':protocol['layout_sha256'],'scope':protocol['scope']}
    save(RESULTS/'comparison.json',comparison)
    return {k:v for k,v in comparison.items() if k!='trials'}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('mode',choices=('declare','run'))
    print(json.dumps({'declare':declare,'run':execute}[parser.parse_args().mode](),indent=2))
