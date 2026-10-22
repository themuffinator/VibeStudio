"""Bounded first-attempt stock-game teleport protocols; no engine client or network."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import subprocess

from tools.native_movement.run import preflight
from tools.native_teleport.build import PROJECT, LOCAL, OUTPUT, SOURCE, EXECUTABLE, sha, save

BUILD_RECORD = OUTPUT/'build-attempt-02.json'


def require(value, message):
    if not value:
        raise ValueError(message)


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def build_identity():
    record = read(BUILD_RECORD)
    require(record['status']=='completed' and sha(EXECUTABLE)==record['executable_sha256'], 'Private executable differs')
    for entry in record['external_dependencies']:
        require(sha(SOURCE/entry['relative_path'])==entry['sha256'], 'External stock source differs')
    for entry in record['local_sources']:
        require(sha(PROJECT/entry['path'])==entry['sha256'], 'Native bridge/build source differs')
    return {'build_record':str(BUILD_RECORD.relative_to(PROJECT)), 'build_record_sha256':sha(BUILD_RECORD),
            'executable_sha256':sha(EXECUTABLE)}


def commands(spec):
    result=[]
    for phase in spec['commands']:
        require(type(phase['frames']) is int and 0<phase['frames']<=512 and
                phase['forward'] in (0,200,400) and -360<=phase['yaw']<=360, 'Unsupported fixed commands')
        result += [f"16 {phase['forward']} 0 0 {phase['yaw']} 0 0"]*phase['frames']
    require(0<len(result)<=1024, 'Command cap')
    return '\n'.join(result)+'\n'


def development_contract():
    bsp=PROJECT/'out/counterweight/qmg_counterweight.bsp'
    preflight(bsp)
    trials=[]
    for speed in (200,400):
        for yaw in (0,90):
            trials.append({'id':f'counterweight-clear-f{speed}-yaw{yaw}', 'expected':'clear_teleport',
                           'speed':speed,'destination_yaw':yaw,'occupancy':0,'missing_target':0,'lateral_offset':0})
    for label,expected,occupancy,missing,offset in (
        ('occupied-player','telefrag_clear',1,0,0), ('occupied-unkillable','transported_but_blocked',2,0,0),
        ('missing-target','no_teleport',0,1,0), ('trigger-near-miss','no_teleport',0,0,40)):
        trials.append({'id':'counterweight-'+label,'expected':expected,'speed':200,'destination_yaw':90,
                       'occupancy':occupancy,'missing_target':missing,'lateral_offset':offset})
    for spec in trials:
        spec.update({'bsp':str(bsp.relative_to(PROJECT)), 'target':'qmg_link_37',
            'start_feet':[464,-96+spec['lateral_offset'],0], 'initial_command_yaw':0,
            'commands':[{'frames':8,'forward':0,'yaw':0}, {'frames':48,'forward':spec['speed'],'yaw':0},
                        {'frames':64,'forward':0,'yaw':0}]})
    return {'schema_version':1,'id':'stock-teleport-development-v1', 'harness_development_only':True,
        'build':build_identity(),'bsp_sha256':sha(bsp),'runner_sha256':sha(Path(__file__)),
        'trials':trials,'native_attempts':8,'timestep_ms':16,'game_tick_ms':100,'initial_game_ticks':2,
        'scope':'Actual unmodified stock3.19 game touch, arrival, hold and dynamic occupied-exit behavior. Existing Counterweight BSP with explicit in-memory fixture actor/yaw/occupancy/target overrides; not a new map or useful escape certificate.',
        'client_launched':False,'input_devices_used':False,'socket_code_linked':False,
        'retained_first_attempts':True,'adaptive_input':False}


def declare_development():
    path=OUTPUT/'development-protocol.json'
    require(not path.exists(),'Do not replace the first development protocol')
    save(path,development_contract())
    return {'declared':True,'native_runs':0,'protocol_sha256':sha(path)}


def event_records(folder):
    return [json.loads(line) for line in (folder/'events.ndjson').read_text().splitlines()]


def angle_close(a,b):
    return abs((a-b+180)%360-180)<=360/65536+.0001


def interpret(spec, records):
    require(records and records[0]['type']=='fixture','Missing native instantiated fixture')
    fixture=records[0]
    require(fixture['game_api']==3 and fixture['occupancy_mode']==spec['occupancy'] and
            fixture['missing_target']==spec['missing_target'], 'Native fixture differs')
    frames=[r for r in records if r['type']=='after_clientthink']
    expected_count=len(commands(spec).splitlines())
    require(len(frames)==expected_count and all(r['frame']==i and r['time_ms']==16*i for i,r in enumerate(frames,1)),
            'Native ClientThink states incomplete')
    destination=fixture['destination_origin']; arrival=[destination[0],destination[1],destination[2]+10]
    links=[r for r in records if r['type']=='link' and r['changed_entity']==r['actor']['id'] and
           r['actor']['origin']==arrival and r['actor']['pm_time']==20 and r['actor']['pm_flags'] & 32]
    # Several hold frames can relink at the same origin; only the newly set20
    # record is the actual transition, and each command can activate at most once.
    transitions=list({r['frame']:r for r in links}.values())
    if spec['expected']=='no_teleport':
        return {'expected_outcome_observed':not transitions,'teleport_activations':len(transitions),
                'native_frame_count':len(frames),'arrival':None,'scope':'Negative activation control; no journey success claim.'}
    require(len(transitions)==1,'Expected one actual stock teleporter transition')
    at=transitions[0]; actor=at['actor']; first=at['frame']
    require(first+10<=len(frames),'Insufficient retained hold/release frames')
    next_state=frames[first]['actor']
    first_nine=[r['actor'] for r in frames[first:first+9]]
    tenth=frames[first+9]['actor']
    immediate=(actor['origin']==arrival and actor['old_origin']==destination and actor['velocity']==[0,0,0] and
               actor['angles']==actor['viewangles']==actor['v_angle']==[0,0,0] and actor['pm_time']==20)
    facing=angle_close(next_state['viewangles'][1],fixture['destination_angles'][1])
    hold=(all(s['origin']==arrival and s['pm_flags'] & 32 for s in first_nine) and
          [s['pm_time'] for s in first_nine]==list(range(18,0,-2)) and tenth['pm_time']==0 and not tenth['pm_flags'] & 32)
    occupancy_clear=not actor['post_startsolid'] and not actor['post_allsolid']
    victim=at['victim']
    expected=immediate and facing and hold
    if spec['expected']=='telefrag_clear':
        expected=expected and occupancy_clear and victim is not None and victim['health']<=0 and victim['solid']==0
    elif spec['expected']=='transported_but_blocked':
        expected=immediate and actor['post_startsolid'] and victim is not None and victim['health']==100 and victim['solid']==2
    else:
        expected=expected and occupancy_clear
    return {'expected_outcome_observed':bool(expected),'teleport_activations':1,'native_frame_count':len(frames),
        'immediate_stock_state_matches':immediate,'next_ClientThink_facing_matches':facing,'nine_held_then_tenth_release_matches':hold,
        'arrival_hull_clear':occupancy_clear,'arrival':at,'next_ClientThink':frames[first],
        'tenth_ClientThink':frames[first+9],
        'scope':'Expected behavior of actual stock game code in an explicitly set-up fixture; blocked arrival is not a safe route.'}


def run_development():
    path=OUTPUT/'development-protocol.json';protocol=read(path)
    require(protocol==development_contract(),'Frozen first development protocol changed')
    output=OUTPUT/'development-v1';require(not output.exists(),'Never retry or overwrite first development outputs')
    output.mkdir();rows=[]
    for spec in protocol['trials']:
        folder=output/spec['id'];folder.mkdir();save(folder/'trial.json',spec)
        stdin=commands(spec);(folder/'commands.txt').write_text(stdin)
        argv=[str(EXECUTABLE),str(PROJECT/spec['bsp']),spec['target'],*[str(v) for v in spec['start_feet']],
              str(spec['initial_command_yaw']),str(spec['destination_yaw']),str(spec['occupancy']),str(spec['missing_target'])]
        result=subprocess.run(argv,input=stdin,text=True,capture_output=True,cwd=PROJECT,timeout=30)
        (folder/'events.ndjson').write_text(result.stdout);(folder/'native-stderr.txt').write_text(result.stderr)
        row={'id':spec['id'],'expected':spec['expected'],'exit_code':result.returncode,'expected_outcome_observed':False,
             'commands_sha256':sha(folder/'commands.txt'),'events_sha256':sha(folder/'events.ndjson')}
        if result.returncode==0:
            try: row.update(interpret(spec,event_records(folder)))
            except Exception as error: row['interpretation_error']=str(error)
        else: row['native_error']=result.stderr[-1200:]
        save(folder/'result.json',row);rows.append(row);print(json.dumps({k:v for k,v in row.items() if k not in ('arrival','next_ClientThink','tenth_ClientThink')}),flush=True)
        require(protocol==development_contract(),'Inputs changed during first attempts')
    record={'protocol_sha256':sha(path),'trials':rows,'first_native_attempts':8,
            'expected_outcomes_observed':sum(r['expected_outcome_observed'] for r in rows),
            'scope':protocol['scope'],'build':protocol['build']}
    save(output/'comparison.json',record)
    return {k:v for k,v in record.items() if k!='trials'}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode',choices=('declare-development','run-development'))
    args=parser.parse_args()
    print(json.dumps({'declare-development':declare_development,'run-development':run_development}[args.mode](),indent=2))
