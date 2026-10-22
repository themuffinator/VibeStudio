"""Independent raw-event checks and immutable bindings; never executes native code."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path

from tools.native_teleport.build import PROJECT, SOURCE, OUTPUT, EXECUTABLE, sha, save


def read(path): return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def require(value, message):
    if not value: raise ValueError(message)


def raw(path): return [json.loads(line) for line in Path(path).read_text().splitlines()]


def command_text(spec):
    return ''.join((f"16 {p['forward']} 0 0 {p['yaw']} 0 0\n")*p['frames'] for p in spec['commands'])


def full_standing(a):
    return a['mins']==[-16,-16,-24] and a['maxs']==[16,16,32] and not a['pm_flags']&1


def usable(a):
    return (full_standing(a) and a['health']>0 and a['waterlevel']==0 and
            not a['post_startsolid'] and not a['post_allsolid'])


def first_goal(records, spec):
    """Find first complete9-state interval directly, without using stored results."""
    frames=[r for r in records if r['type']=='after_clientthink']
    for offset in range(len(frames)-8):
        interval=frames[offset:offset+9]
        first=interval[0]
        if first['frame']<spec['release_start_frame']: continue
        if any(r['frame']!=first['frame']+i or r['time_ms']!=first['time_ms']+16*i for i,r in enumerate(interval)): continue
        matches=True
        for r in interval:
            a=r['actor']; x,y,z=a['origin']; v=a['velocity']
            matches &= (usable(a) and 128<=spec['sign']*(x+480)<=160 and 671<=y<=673 and
                        abs(z-24)<=.25 and a['groundentity']>=0 and sum(w*w for w in v)<=1)
        if matches:
            return {'start_frame':first['frame'],'end_frame':interval[-1]['frame'],
                    'start_time_ms':first['time_ms'],'end_time_ms':interval[-1]['time_ms'],
                    'first_feet':[first['actor']['origin'][0],first['actor']['origin'][1],first['actor']['origin'][2]-24],
                    'last_feet':[interval[-1]['actor']['origin'][0],interval[-1]['actor']['origin'][1],interval[-1]['actor']['origin'][2]-24],
                    'maximum_speed':max(math.sqrt(sum(w*w for w in r['actor']['velocity'])) for r in interval)}
    return None


def transition_records(records):
    fixture=records[0]; destination=fixture['destination_origin']; position=[*destination[:2],destination[2]+10]
    return [r for r in records if r['type']=='link' and r['changed_entity']==r['actor']['id'] and
            r['actor']['origin']==position and r['actor']['pm_time']==20 and r['actor']['pm_flags']&32]


def yaw_equal(actual, expected): return abs((actual-expected+180)%360-180)<=360/65536+.0001


def inspect_activation(records, spec):
    require(records[0]['type']=='fixture' and records[0]['game_api']==3,'Missing actual game fixture')
    f=records[0]
    require(f['occupancy_mode']==spec['occupancy'] and f['missing_target']==spec['missing_target'],'Wrong occupied/target fixture')
    if spec['destination_yaw']!=999:
        require(f['destination_angles']==[0,spec['destination_yaw'],0],'Declared development yaw override differs')
    frames=[r for r in records if r['type']=='after_clientthink']
    count=sum(p['frames'] for p in spec['commands'])
    require(len(frames)==count and all(r['frame']==i and r['time_ms']==16*i for i,r in enumerate(frames,1)), 'Missing or unordered command snapshots')
    initial=next(r for r in records if r['type']=='initial')['actor']
    require(initial['origin']==[spec['start_feet'][0],spec['start_feet'][1],spec['start_feet'][2]+24.125], 'Explicit initial fixture differs')
    ticks=[r for r in records if r['type']=='after_gameframe']
    require(len(ticks)==count*16//100 and all(r['game_frame']==i+3 and 0<=r['time_ms']-100*(i+1)<16 for i,r in enumerate(ticks)), 'Stock100ms frame schedule differs')
    transitions=transition_records(records)
    if spec['expected']=='no_teleport':
        require(not transitions,'Negative control activated')
        require(not any(r['type']=='link' and r['changed_entity']==r['actor']['id'] and r['actor']['pm_time']==20 and r['actor']['pm_flags']&32 for r in records), 'Negative control activated a different target')
        return {'activation_count':0,'frames':count}
    require(len(transitions)==1,'Expected exactly one stock arrival relink')
    at=transitions[0]; a=at['actor']; index=at['frame']; destination=f['destination_origin']
    require(a['event']==6 and at['source_event']==6,'Stock teleport events missing at arrival relink')
    require(a['old_origin']==destination and a['velocity']==[0,0,0] and
            a['angles']==a['viewangles']==a['v_angle']==[0,0,0], 'Immediate stock copied/cleared fields differ')
    # ClientThink populated these shorts before stock touch changed entity state.
    # They deliberately differ at relink and synchronize on the next command.
    require(a['pm_origin']!=[int(v*8) for v in a['origin']], 'Immediate pre-touch Pmove state unexpectedly relabelled')
    require(a['pm_velocity']!=[0,0,0], 'Expected moving trigger contact not observed')
    held=frames[index:index+9]; tenth=frames[index+9]['actor']
    require(len(held)==9 and [r['actor']['pm_time'] for r in held]==[18,16,14,12,10,8,6,4,2], 'Incorrect160ms timer decrement')
    require(all(r['actor']['origin']==a['origin'] and r['actor']['pm_flags']&32 and r['actor']['velocity']==[0,0,0] for r in held), 'Held arrival moves')
    require(tenth['pm_time']==0 and not tenth['pm_flags']&32, 'Tenth command did not release movement')
    require(yaw_equal(held[0]['actor']['viewangles'][1],f['destination_angles'][1]), 'Next ClientThink facing differs')
    victim=at['victim']
    clear=not a['post_startsolid'] and not a['post_allsolid']
    if spec['expected']=='telefrag_clear':
        require(clear and victim is not None and victim['health']<=0 and victim['solid']==0 and at['means_of_death']==21,
                'Real stock MOD_TELEFRAG player clearing missing')
    elif spec['expected']=='transported_but_blocked':
        require(a['post_startsolid'] and victim is not None and victim['health']==100 and victim['solid']==2,
                'Unkillable occupancy was incorrectly considered clear')
    else: require(clear,'Unoccupied arrival blocked')
    return {'activation_count':1,'activation_frame':index,'frames':count,'arrival_clear':clear}


def inspect_relay(records, spec):
    activation=inspect_activation(records,spec)
    f=records[0]
    require(f['source_pad']['origin']==[-704,-624,24] and f['destination_origin']==[-480,672,24] and f['destination_angles']==[0,0,0], 'Wrong Relay native pad/target')
    frames=[r for r in records if r['type']=='after_clientthink']; violations=[]
    for r in frames:
        a=r['actor'];x,y,z=a['origin'];feet=z-24
        band=(-705<=x<=-703 and -721<=y<=-647 and 0<=feet<=8.25) if r['frame']<activation['activation_frame'] else (0<=spec['sign']*(x+480)<=160 and 671<=y<=673 and 0<=feet<=10.25)
        if not usable(a) or not band: violations.append(r['frame'])
    stage=frames[spec['departure_start_frame']-2]['actor']
    staging=(abs(stage['origin'][0]+480)<=.25 and abs(stage['origin'][1]-672)<=.25 and abs(stage['origin'][2]-32)<=.25 and
             sum(v*v for v in stage['velocity'])<=1 and stage['groundentity']>=0 and not stage['pm_flags']&32)
    facing=all(yaw_equal(r['actor']['viewangles'][1],spec['expected_departure_view_yaw']) for r in frames[spec['departure_start_frame']-1:])
    goal=first_goal(records,spec)
    return {'success':not violations and staging and facing and goal is not None,
            'first_goal':goal,'physical_violation_frames':violations,'staging':staging,'facing':facing,
            'activation':activation}


def verify_build(protocol, historical=False):
    pin=protocol['build'];record_path=PROJECT/pin['build_record'];record=read(record_path)
    require(sha(record_path)==pin['build_record_sha256'] and record['executable_sha256']==pin['executable_sha256'], 'Build record binding differs')
    for entry in record['external_dependencies']:
        require(sha(SOURCE/entry['relative_path'])==entry['sha256'],'External GPL source changed')
    for entry in record['local_sources']:
        location=(OUTPUT/'revisions/development-v1'/Path(entry['path']).name) if historical else PROJECT/entry['path']
        require(sha(location)==entry['sha256'],'Original glue/build source differs')
    if not historical and EXECUTABLE.exists(): require(sha(EXECUTABLE)==pin['executable_sha256'],'Private executable differs')


def verify_trials(protocol_path, directory, development):
    protocol=read(protocol_path);verify_build(protocol)
    bsp_paths={s['bsp'] for s in protocol['trials']}
    require(len(bsp_paths)==1 and sha(PROJECT/next(iter(bsp_paths)))==protocol['bsp_sha256'],'Development/Relay BSP provenance differs')
    comparison=read(directory/'comparison.json')
    require(comparison['protocol_sha256']==sha(protocol_path),'Comparison protocol binding differs')
    require([r['id'] for r in comparison['trials']]==[s['id'] for s in protocol['trials']],'Trial order or population differs')
    for spec,row in zip(protocol['trials'],comparison['trials']):
        folder=directory/spec['id']; records=raw(folder/'events.ndjson')
        require(read(folder/'trial.json')==spec and read(folder/'result.json')==row,'Stored trial/result labels differ')
        require((folder/'commands.txt').read_text()==command_text(spec),'Fixed commands differ')
        for name,key in [('commands.txt','commands_sha256'),('events.ndjson','events_sha256')]:
            require(sha(folder/name)==row[key],'Raw command/event hash differs')
        require(row['exit_code']==0,'Native execution failed')
        if development:
            derive=inspect_activation(records,spec)
            require(row['expected']==spec['expected'] and row['expected_outcome_observed'] and row['teleport_activations']==derive['activation_count'] and row['native_frame_count']==derive['frames'],'Development expected result differs')
            if derive['activation_count']:
                require(row['arrival']==transition_records(records)[0] and row['arrival_hull_clear']==derive['arrival_clear'],'Development relink snapshot differs')
            from tools.native_teleport import run
            secondary=run.interpret(spec,records)
            require(all(row[k]==val for k,val in secondary.items()),'Secondary development fields differ')
        else:
            derive=inspect_relay(records,spec)
            require(row['direction']==spec['direction'] and row['input']==spec['input'] and row['success']==derive['success'] and
                    row['first_goal']==derive['first_goal'] and row['activation_frame']==derive['activation']['activation_frame'] and
                    row['departure_facing_matches']==derive['facing'] and row['planned_staging_pass']==derive['staging'], 'Independent Relay result differs')
            # The entire original interpretation is also reproduced for all
            # secondary labels, snapshots and reported final positions.
            from tools.native_teleport import relay
            derived=relay.interpret(spec,records)
            require(all(row[k]==v for k,v in derived.items()),'Secondary Relay interpretation differs')
    count=len(protocol['trials'])
    if development:
        require(comparison['first_native_attempts']==count==8 and comparison['expected_outcomes_observed']==sum(r['expected_outcome_observed'] for r in comparison['trials']), 'Development aggregate counts differ')
    else:
        require(comparison['first_attempts']==count==4 and comparison['successful_journeys']==sum(r['success'] for r in comparison['trials']), 'Relay aggregate counts differ')
    return len(protocol['trials'])


def evidence_result():
    old=read(OUTPUT/'development-protocol.json');verify_build(old,True)
    old_summary=read(OUTPUT/'development-v1/comparison.json')
    require(old_summary['protocol_sha256']==sha(OUTPUT/'development-protocol.json') and old_summary['expected_outcomes_observed']==0,'First host failures were relabelled')
    require(len(old['trials'])==len(old_summary['trials'])==old_summary['first_native_attempts']==8 and
            [r['id'] for r in old_summary['trials']]==[s['id'] for s in old['trials']], 'First host-failure population differs')
    for spec,row in zip(old['trials'],old_summary['trials']):
        folder=OUTPUT/'development-v1'/spec['id']
        require(row['exit_code']==2 and not row['expected_outcome_observed'] and not (folder/'events.ndjson').read_bytes() and
                'configstring cap' in (folder/'native-stderr.txt').read_text() and read(folder/'result.json')==row and read(folder/'trial.json')==spec and
                (folder/'commands.txt').read_text()==command_text(spec), 'Retained startup failure differs')
    repaired=read(OUTPUT/'development-v2-protocol.json')
    require(repaired['trials']==old['trials'] and repaired['previous_protocol_sha256']==sha(OUTPUT/'development-protocol.json'),'Host repair changed native inputs')
    require(repaired['runner_sha256']==sha(PROJECT/'tools/native_teleport/run.py') and repaired['repair_runner_sha256']==sha(PROJECT/'tools/native_teleport/development_v2.py'),'Development runner differs')
    development=verify_trials(OUTPUT/'development-v2-protocol.json',OUTPUT/'development-v2',True)
    relay_protocol=read(OUTPUT/'relay-protocol.json')
    for name,key in [('tools/native_teleport/relay.py','orchestrator_sha256'),('tools/native_teleport/run.py','common_runner_sha256'),('out/relay/qmg_relay.bsp','bsp_sha256'),('out/relay/qmg_relay.map','map_sha256'),('out/relay/features.json','features_sha256')]:
        require(sha(PROJECT/name)==relay_protocol[key], 'Frozen Relay source/map binding differs')
    require(read(PROJECT/'out/relay/features.json')['layout_sha256']==relay_protocol['layout_sha256'],'Canonical Relay binding differs')
    timing_path=PROJECT/relay_protocol['timing_protocol_path']
    require(sha(timing_path)==relay_protocol['timing_protocol_sha256'] and read(timing_path)['timing_basis']==relay_protocol['timing_basis'],'Prior timing evidence differs')
    relay_count=verify_trials(OUTPUT/'relay-protocol.json',OUTPUT/'relay-v1',False)
    states=sum(sum(p['frames'] for p in s['commands']) for s in repaired['trials']+relay_protocol['trials'])
    return {'ok':True,'development_trials_rederived':development,'relay_trials_rederived':relay_count,
            'retained_startup_failures':8,'native_ClientThink_states':states,
            'native_executable_available':EXECUTABLE.exists()}


def verify():
    result=evidence_result()
    summary_path=OUTPUT/'summary.json'
    if summary_path.exists():
        summary=read(summary_path);protocol=read(OUTPUT/'relay-protocol.json');comparison=read(OUTPUT/'relay-v1/comparison.json')
        require(summary['bsp_sha256']==protocol['bsp_sha256'] and summary['layout_sha256']==protocol['layout_sha256'] and
                summary['map_sha256']==protocol['map_sha256'] and summary['relay_protocol_sha256']==sha(OUTPUT/'relay-protocol.json') and
                summary['relay_comparison_sha256']==sha(OUTPUT/'relay-v1/comparison.json') and
                summary['development_protocol_sha256']==sha(OUTPUT/'development-v2-protocol.json') and
                summary['development_comparison_sha256']==sha(OUTPUT/'development-v2/comparison.json'), 'Compact summary evidence bindings differ')
        require(summary['relay_first_attempts']==4 and summary['relay_successful_journeys']==comparison['successful_journeys'] and
                summary['retained_failed_host_initializations']==8 and summary['development_expected_outcomes_observed']==8 and
                summary['native_ClientThink_states']==result['native_ClientThink_states'], 'Compact summary counts differ')
        require([r['id'] for r in summary['trials']]==[r['id'] for r in comparison['trials']], 'Compact summary population differs')
        for small,full in zip(summary['trials'],comparison['trials']):
            require(all(key in full and full[key]==value for key,value in small.items()), 'Compact summary native observations differ')
        require(summary['stock_game_module'] and not any(summary[key] for key in ('world_only','client_launched','input_devices_used',
                'network_packets','onward_resource_native_journey','resource_pickup_contact_claimed','human_balance_claimed')),
                'Compact summary scope differs')
    import_reference=OUTPUT/'import-contract-reference.json'
    if import_reference.exists():
        reference=read(import_reference)
        require(sha(reference['path'])==reference['sha256'],'Read-only stock configstring import reference changed')
    record_path=OUTPUT/'record.json'
    if record_path.exists():
        record=read(record_path)
        require(len({f['path'] for f in record['files']})==len(record['files']),'Duplicate evidence binding')
        for entry in record['files']:
            require(sha(PROJECT/entry['path'])==entry['sha256'],'Retained evidence bytes changed: '+entry['path'])
        result['bound_files_verified']=len(record['files'])
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.parse_args()
    print(json.dumps(verify(),indent=2))
