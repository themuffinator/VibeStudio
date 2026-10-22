"""Write compact summary and one final evidence manifest after raw rederivation."""
import argparse
import json
from pathlib import Path
from tools.native_teleport.build import PROJECT, OUTPUT, SOURCE, sha, save
from tools.native_teleport.verify import read, require, evidence_result


def summary():
    path=OUTPUT/'summary.json';require(not path.exists(),'Never replace a released native summary')
    checked=evidence_result();protocol=read(OUTPUT/'relay-protocol.json');comparison=read(OUTPUT/'relay-v1/comparison.json')
    rows=[]
    for row in comparison['trials']:
        rows.append({key:row[key] for key in ('id','direction','input','success','failure_reasons',
            'activation_frame','activation_time_ms','first_goal','actual_final_feet','final_velocity',
            'final_center_on_first_static_departure_segment')})
    value={'schema_version':1,'map':'qmg_relay','bsp_sha256':protocol['bsp_sha256'],
        'layout_sha256':protocol['layout_sha256'],'map_sha256':protocol['map_sha256'],
        'build':protocol['build'],'relay_protocol_sha256':sha(OUTPUT/'relay-protocol.json'),
        'relay_comparison_sha256':sha(OUTPUT/'relay-v1/comparison.json'),
        'development_protocol_sha256':sha(OUTPUT/'development-v2-protocol.json'),
        'development_comparison_sha256':sha(OUTPUT/'development-v2/comparison.json'),
        'retained_failed_host_initializations':8,'development_expected_outcomes_observed':8,
        'development_gameplay_journeys':False,'relay_first_attempts':4,'relay_successful_journeys':sum(r['success'] for r in rows),
        'native_ClientThink_states':checked['native_ClientThink_states'],
        'source_origin':[-704,-624,24],'source_approach_feet':[-704,-720,0],
        'destination_origin':[-480,672,24],'native_immediate_arrival_origin':[-480,672,34],
        'disc_top_z':8,'geometric_off_disc_stations':{'east':[-352,672,0],'west':[-608,672,0]},
        'trials':rows,'world_only':False,'stock_game_module':True,'client_launched':False,
        'input_devices_used':False,'network_packets':False,'onward_resource_native_journey':False,
        'resource_pickup_contact_claimed':False,'human_balance_claimed':False,
        'scope':'Controlled stock3.19 game API fixture with actual CM/Pmove, SV entity collision, ClientThink trigger activation and KillBox. Four Relay source-entry/hold/intentional128-unit departure-and-stop journeys; subsequent resource and circuit claims remain static.',
        'clock':'16ms synthetic commands; stock RunFrame at100ms boundaries after two initialization ticks. Times are command elapsed time, not wall-clock or a network client.',
        'limits':['The unkillable development arrival teleports but overlaps the blocker; it is not a safe route.',
                  'Immediate entity/view state, pre-touch Pmove shorts, after-ClientThink and after-RunFrame observations remain distinct.',
                  'Grounded endpoint dwell does not expose internal Pmove substeps.',
                  'Stock3.19 behavior is not Q2REPRO or client-prediction equivalence.']}
    save(path,value)
    source_path=SOURCE/'server/sv_game.c'
    save(OUTPUT/'import-contract-reference.json',{'path':str(source_path),'sha256':sha(source_path),
         'reason':'Read-only PF_Configstring import comparison: NULL normalizes to empty at lines197–198. Not a compiled source or copied implementation.'})
    return {'summary_sha256':sha(path),'relay_journeys':4,'development_controls':8}


def record():
    path=OUTPUT/'record.json';require(not path.exists(),'Never overwrite final evidence manifest')
    checks=evidence_result()
    require((OUTPUT/'summary.json').exists() and (PROJECT/'docs/Q2_NATIVE_TELEPORT.md').exists(),'Finalize study before binding')
    files=[p for p in OUTPUT.rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.name not in ('record.json','verification-final.log')]
    files += [p for p in (PROJECT/'tools/native_teleport').iterdir() if p.is_file()]
    files += [PROJECT/'tests/test_native_teleport.py',PROJECT/'docs/Q2_NATIVE_TELEPORT.md']
    require(len(files)==len(set(files)),'Duplicate evidence paths')
    save(path,{'schema_version':1,'scope':'Separate optional stock-game teleporter study; no prior evidence changed.',
        'raw_rederivation':checks,'files':[{'path':p.relative_to(PROJECT).as_posix(),'sha256':sha(p)} for p in sorted(files)],
        'external_GPL_source_copied':False,'external_binary_packaged':False,
        'scratch_directory':str(PROJECT/'.agents/tmp/relay-native-v1'),
        'native_processes_completed':20,'successful_native_execution_exits':12,'retained_startup_exit2':8})
    return {'record_sha256':sha(path),'bound_files':len(files)}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('mode',choices=('summary','record'))
    print(json.dumps({'summary':summary,'record':record}[p.parse_args().mode](),indent=2))
