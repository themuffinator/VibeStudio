"""Check and package original recipes, preserving the limits of static evidence."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import zipfile

from tools.map_study.binary import Bsp
from tools.map_study.features import analyze
from tools.map_recipes.geometry import model_brushes, convex_polyhedron, intersects_aabb

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')

def ordinary_navigation(layout, report):
    graph=report['analysis']['navigation']['player']['graph']
    nodes={node['id']:node for node in graph['nodes']}
    bindings={row['placement']:row['node'] for row in graph['placement_bindings']}
    allowed={'walk','step','door'}
    adjacent={node:set() for node in nodes}
    for edge in graph['edges']:
        if edge['kind'] in allowed:
            adjacent[edge['from']].add(edge['to'])
    checked=0
    for spawn in (p for p in layout['placements'] if p['role']==1):
        reached={bindings[spawn['id']]}; pending=list(reached)
        while pending:
            for other in adjacent[pending.pop()]-reached:
                reached.add(other);pending.append(other)
        for placement in (p for p in layout['placements'] if p['role'] in (1,4,5,6)):
            assert bindings[placement['id']] in reached,'Missing walk/step-only gameplay route'
            checked+=1
    assert checked==264
    ramp_nodes=[]
    for ramp in layout['ramps']:
        matches=[node for node in nodes.values() if node['source']['kind']=='solid'
                 and node['source']['id']==ramp['solid_id']]
        assert len(matches)==1,'A ramp must have one continuous node'
        node=matches[0];slope=node['area']['slope'];axis=0 if slope['along_x'] else 1
        assert slope['start']==ramp['from'][axis] and slope['end']==ramp['to'][axis]
        assert slope['low_z']==ramp['from'][2] and slope['rise']==ramp['rise']
        assert slope['support_offset']==16
        gates=[e for e in graph['edges'] if node['id'] in (e['from'],e['to'])]
        assert len(gates)==4 and all(e['kind'] in ('walk','step') for e in gates)
        incoming={e['from'] for e in gates if e['to']==node['id']}
        outgoing={e['to'] for e in gates if e['from']==node['id']}
        assert incoming==outgoing and len(incoming)==2,'Both ramp ends need bidirectional ordinary gates'
        assert {nodes[n]['area']['floor_z'] for n in incoming}=={ramp['base_floor'],ramp['to'][2]}
        ramp_nodes.append({'ramp':ramp['id'],'node':node['id'],'slope':slope,'gate_edge_ids':[e['id'] for e in gates]})
    return {'source':'independent BFS over serialized typed graph','path_checks':checked,
            'allowed_edge_kinds':['walk','step','door'],'continuous_ramp_nodes':ramp_nodes,
            'runtime_verified':False}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--recipe',choices=('switchback','spillway','counterweight','talus','undercut','fold','lockyard','ravel'),default='switchback')
    parser.add_argument('--output',type=Path)
    parser.add_argument('--assets',type=Path,default=Path(r'E:\_SOURCE\_ASSETS\Q2'))
    args=parser.parse_args()
    recipe=args.recipe
    title=recipe.title()
    map_name='qmg_'+recipe
    expected_rooms=6 if recipe in ('spillway','talus','lockyard','ravel') else 5
    expected_connections={'counterweight':8,'talus':9,'undercut':8,'fold':9,'lockyard':10,'ravel':9}.get(recipe,10)
    previous={'ravel':'lockyard','lockyard':'fold','fold':'undercut','undercut':'talus','talus':'counterweight','counterweight':'spillway','spillway':'switchback','switchback':'crosscurrent'}[recipe]
    out=(args.output or Path('out')/recipe).resolve(strict=True)
    project=Path(__file__).resolve().parents[2]
    bsp_path=out/(map_name+'.bsp')
    bsp=Bsp(bsp_path.read_bytes())
    assert not bsp.entity_issues and not bsp.file_issues
    assert all(bsp.lumps[i] for i in range(16))
    assert (out/(map_name+'.prt')).stat().st_size>0
    assert not any((out/(map_name+suffix)).exists() for suffix in ('.pts','.lin'))
    logs={name:(out/(name+'.log')).read_text(encoding='utf-8-sig').lower() for name in ('qbsp','vis','light')}
    for marker in ('leaked','no visible sides on brush','outside world','invalid brush','brush with no faces','multiple face contents','error:'):
        assert marker not in logs['qbsp'],marker
    assert 'calculating full vis' in logs['vis'] and 'visdatasize:' in logs['vis']
    assert 'lighting completed' in logs['light'] and '0 empty lightmaps' in logs['light']
    analysis,floors=analyze(bsp)
    assert not analysis['unsupported_pickup_classes']
    assert analysis['metrics']['spawn_count']==8
    assert analysis['metrics']['weapon_types']==7
    assert analysis['metrics']['major_item_count']==2
    assert analysis['metrics']['combat_height_bands_128']==(2 if recipe in ('fold','lockyard') else 3)
    assert not analysis['spawn_los']['invalid_eye_entities']
    textures=sorted({row[10].split(b'\0',1)[0].decode('ascii') for row in bsp.texinfo})
    assert all((args.assets/'textures'/(name+'.wal')).is_file() for name in textures)
    texture_physics={name:dict(zip(('flags','contents','value'),struct.unpack_from('<3i',
        (args.assets/'textures'/(name+'.wal')).read_bytes(),88))) for name in textures}
    if recipe in ('talus','undercut','ravel'):
        assert all(not row['contents'] and not row['flags'] & (8|16|32) for row in texture_physics.values()),texture_physics
        assert all(bsp.brushes[i][2]==1 for i in model_brushes(bsp)),'Architecture must be compiled SOLID'
    # Exact convex checks include chamfers and slopes. Mover submodels are tested
    # separately at their stations; the world hull is not an entity snapshot.
    prisms=[]
    for brush_index in model_brushes(bsp):
        first,count,contents=bsp.brushes[brush_index]
        if not contents & (1 | 2 | 65536 | 33554432):continue
        prisms.append(convex_polyhedron(bsp.planes[i] for i,_ in bsp.brushsides[first:first+count]))
    checks=0
    for a in analysis['anchors']:
        p=a['origin'];low=[p[i]+(-24 if i==2 else -16) for i in range(3)];high=[p[i]+(32 if i==2 else 16) for i in range(3)]
        for prism in prisms:
            assert not intersects_aabb(prism,low,high),f'Anchor {a["entity"]} overlaps a world solid'
            checks+=1
    report=json.loads((out/(map_name+'.qmg.json')).read_text(encoding='utf-8'))
    assert report['validation']['ok'] and not report['validation']['issues']
    assert report['analysis']['navigation']['player']['ok']
    assert not any(report['analysis']['quality']['score'].values()),report['analysis']['quality']['score']
    layout=json.loads((out/'layout.json').read_text(encoding='utf-8'))
    assert layout['layout_sha256']==report['analysis']['navigation']['player']['layout_sha256']
    assert layout['path_checks']==264
    assert len(layout['connections'])==expected_connections
    assert len(layout['rooms'])==expected_rooms
    assert len({tuple(b-a for a,b in zip(room['bounds'][0][:2],room['bounds'][1][:2])) for room in layout['rooms']})==expected_rooms
    comparison=json.loads((out/'reference-comparison.json').read_text(encoding='utf-8'))
    assert comparison['bsp_sha256']==bsp.sha256
    analysis['navigation']=(ordinary_navigation(layout,report) if recipe in ('talus','undercut','fold','lockyard','ravel') else
        {'source':'typed layout with exact authored geometry','path_checks':264,'runtime_verified':False})
    save(out/'compiled-analysis.json',analysis)
    old_path=project/'out'/previous/'compiled-analysis.json'
    old=json.loads(old_path.read_text(encoding='utf-8')) if old_path.exists() else None
    metric_keys=('combat_height_p90_p10','combat_height_bands_128','world_diagonal','upward_surface_area','sampled_world_sightline_p50','sampled_world_sightline_p90','spawn_pair_world_los_fraction')
    before_after={'baseline':previous.title(),'candidate':title,'baseline_available':old is not None,
        'metrics':{k:{'before':old['metrics'][k] if old else None,'after':analysis['metrics'][k]} for k in metric_keys},
        'room_connections':{'before':{'crosscurrent':8,'switchback':10,'spillway':10,'counterweight':8,'talus':9,'undercut':8,'fold':9,'lockyard':10}[previous],'after':expected_connections},
        'room_graph_cycle_rank':{'before':{'crosscurrent':4,'switchback':6,'spillway':5,'counterweight':4,'talus':4,'undercut':4,'fold':5,'lockyard':5}[previous],'after':expected_connections-expected_rooms+1},
        'articulation_rooms':{'before':0,'after':report['analysis']['visibility']['summary']['articulation_rooms']},
        'limits':['Multiple design variables changed; this is not a controlled causal comparison.',
                  'Current costs include endpoint approaches but remain Manhattan/node-center estimates. Earlier artifacts use a different timing basis; do not compare their costs or call them measured travel time.',
                  'Point LOS is not a match, spawn-safety guarantee or rendered lighting check.']}
    save(out/('comparison-to-'+previous+'.json'),before_after)
    crossing=None
    traversal_samples=[]
    if recipe=='spillway':
        # Compiled-world evidence that the two crossing paths are open at their
        # respective eye heights and remain separated by solid structure.
        crossing={
            'lower_open':bsp.trace((-152,-496,48),(-152,272,48))==1,
            'upper_open':bsp.trace((-240,48,192),(48,48,192))==1,
            'solid_between':bsp.trace((-152,48,132),(-152,48,132))==0,
        }
        assert all(crossing.values()),crossing
    if recipe in ('spillway','counterweight','talus','undercut','fold','lockyard','ravel'):
        for run in layout['stair_runs']:
            for low,high in run:
                x,y=(low[0]+high[0])/2,(low[1]+high[1])/2
                traversal_samples.append((x,y,high[2]+24))
                assert bsp.trace((x,y,high[2]+.5),(x,y,high[2]-1))<1,'Unsupported compiled stair tread'
        if recipe=='spillway':
            traversal_samples.extend((-152,y,24) for y in range(-496,273,16))
            traversal_samples.extend((x,48,168) for x in range(-240,49,16))
        for p in traversal_samples:
            low=[p[0]-16,p[1]-16,p[2]-24]
            high=[p[0]+16,p[1]+16,p[2]+32]
            assert not any(intersects_aabb(prism,low,high) for prism in prisms),f'Compiled route hull blocked at {p}'
    mechanisms=None
    if recipe=='counterweight':
        assert layout['ordinary_path_checks']==264
        assert layout['ordinary_route_edge_kinds']==['walk','step','drop','door']
        assert layout['navigation_edge_counts']['door']==0
        assert len(layout['stair_runs'])==5 and all(len(run)==8 for run in layout['stair_runs'])
        assert layout['navigation_edge_counts']['lift']>=2 and layout['navigation_edge_counts']['teleporter']==1
        by_class={name:[e for e in bsp.entities if e.get('classname')==name]
                  for name in ('func_plat','misc_teleporter','misc_teleporter_dest','target_position')}
        assert not by_class['target_position']
        assert all(len(by_class[name])==1 for name in ('func_plat','misc_teleporter','misc_teleporter_dest'))
        lift=by_class['func_plat'][0]
        assert lift['height']=='240' and lift['speed']=='200' and 'origin' not in lift and 'targetname' not in lift
        lift_brush_indices=model_brushes(bsp,int(lift['model'][1:]))
        lift_prisms=[]
        for index in lift_brush_indices:
            first,count,contents=bsp.brushes[index]
            if contents&1:lift_prisms.append(convex_polyhedron(bsp.planes[i] for i,_ in bsp.brushsides[first:first+count]))
        assert len(lift_prisms)==1
        lp=lift_prisms[0]
        assert lp.bounds==((192,-64,240),(320,64,256))
        upper_low,upper_high=lp.bounds
        travel=int(lift['height'])
        sweep_low=(upper_low[0],upper_low[1],upper_low[2]-travel)
        passenger_low=(upper_low[0],upper_low[1],upper_high[2]-travel)
        passenger_high=(upper_high[0],upper_high[1],upper_high[2]+56)
        assert not any(intersects_aabb(prism,sweep_low,upper_high) for prism in prisms),'Compiled platform sweep blocked'
        assert not any(intersects_aabb(prism,passenger_low,passenger_high) for prism in prisms),'Compiled passenger envelope blocked'
        # Standing origins at both native stations and nine intermediate poses.
        # This only checks the empty shaft against world solids, not live motion.
        shaft_samples=[(256,0,z+24) for z in range(16,257,24)]
        for p in shaft_samples:
            assert not any(intersects_aabb(prism,(p[0]-16,p[1]-16,p[2]-24),
                                           (p[0]+16,p[1]+16,p[2]+32)) for prism in prisms),f'Lift shaft hull blocked: {p}'
        # Ensure no pickup/spawn lies in the platform's swept box.
        for a in analysis['anchors']:
            p=a['origin']
            assert not (p[0]-16<320 and p[0]+16>192 and p[1]-16<64 and p[1]+16>-64
                        and p[2]-24<256 and p[2]+32>0),f'Anchor {a["entity"]} in lift sweep'
        source=by_class['misc_teleporter'][0];destination=by_class['misc_teleporter_dest'][0]
        assert source['target']==destination['targetname'] and destination.get('angle')=='0'
        assert source['origin']=='544 -96 24' and destination['origin']=='-832 192 152'
        tele_samples=[]
        for entity,offset in ((source,8),(destination,10)):
            origin=tuple(float(v) for v in entity['origin'].split()); floor=origin[2]-24
            # Disc support at center/corners and fixed native pad clearance.
            for dx in (-31,0,31):
                for dy in (-31,0,31):
                    assert bsp.trace((origin[0]+dx,origin[1]+dy,floor+.5),
                                     (origin[0]+dx,origin[1]+dy,floor-1))<1
            assert not any(intersects_aabb(prism,(origin[0]-32,origin[1]-32,floor),
                                           (origin[0]+32,origin[1]+32,floor+8)) for prism in prisms)
            pose=(origin[0],origin[1],origin[2]+offset);tele_samples.append(pose)
            assert not any(intersects_aabb(prism,(pose[0]-16,pose[1]-16,pose[2]-24),
                                           (pose[0]+16,pose[1]+16,pose[2]+32)) for prism in prisms)
        mechanisms={'ordinary_path_checks':264,'edge_counts':layout['navigation_edge_counts'],
                    'full_platform_sweep_clear':True,'full_passenger_envelope_clear':True,
                    'lift_native_top_z':256,'lift_native_bottom_z':16,'lift_shaft_hull_samples':len(shaft_samples),
                    'teleporter_disc_support_samples':18,'teleporter_native_clearance_poses':tele_samples,
                    'ordinary_route_policy':'walk, step and bounded drop; lift, teleporter and jump edges excluded',
                    'runtime_activation_tested':False}
    ramps=None
    drops=None
    features=None
    interleaved=None
    if recipe=='talus':
        from tools.map_recipes.ramp_audit import audit_ramps
        assert layout['ordinary_path_checks']==264
        assert layout['ordinary_route_edge_kinds']==['walk','step','door']
        assert len(layout['stair_runs'])==4 and all(len(run)==8 for run in layout['stair_runs'])
        assert len(layout['ramps'])==2
        assert report['analysis']['visibility']['sampling']['player_spawn_eye_offset_z']==22
        ramps=audit_ramps(bsp,layout)
    if recipe=='undercut':
        from tools.map_recipes.drop_audit import audit_drops
        assert layout['ordinary_path_checks']==264 and layout['ordinary_route_edge_kinds']==['walk','step','door']
        assert len(layout['stair_runs'])==6 and all(len(run)==8 for run in layout['stair_runs'])
        assert not layout['ramps'] and len(layout['drops'])==2
        assert report['analysis']['visibility']['sampling']['player_spawn_eye_offset_z']==22
        drops=audit_drops(bsp,layout,report)
        drops['landing_eye_visibility']=[{'drop':d['id'],'visible_from_takeoff':bsp.trace(
            (d['from'][0],d['from'][1],d['from'][2]+46),(d['to'][0],d['to'][1],d['to'][2]+46))==1}
            for d in layout['drops']]
        assert all(d['visible_from_takeoff'] for d in drops['landing_eye_visibility'])
    if recipe=='fold':
        from tools.map_recipes.fold_audit import audit_fold
        feature_path=out/'features.json'
        assert feature_path.is_file(),'Fold needs its explicit stacked-route audit stations'
        features=audit_fold(bsp,layout,texture_physics,json.loads(feature_path.read_text(encoding='utf-8-sig')))
        mechanisms={'ordinary_path_checks':264,'edge_counts':layout['navigation_edge_counts'],
                    'teleporter_native_clearance_poses':features['teleporter_native_clearance_poses'],
                    'runtime_activation_tested':False}
    if recipe=='ravel':
        from tools.map_recipes.ravel_audit import audit_ravel
        from tools.map_recipes.ramp_audit import audit_ramps
        from tools.map_recipes.drop_audit import audit_drops
        from tools.map_recipes.gate_audit import audit_gates
        assert layout['ordinary_path_checks']==264 and layout['ordinary_route_edge_kinds']==['walk','step','door']
        assert len(layout['stair_runs'])==3 and all(len(run)==8 for run in layout['stair_runs'])
        assert len(layout['ramps'])==1 and len(layout['drops'])==1
        ramps=audit_ramps(bsp,layout)
        drops=audit_drops(bsp,layout,report)
        drops['landing_eye_visibility']=[{'drop':d['id'],'visible_from_takeoff':bsp.trace(
            (d['from'][0],d['from'][1],d['from'][2]+46),(d['to'][0],d['to'][1],d['to'][2]+46))==1}
            for d in layout['drops']]
        assert all(d['visible_from_takeoff'] for d in drops['landing_eye_visibility'])
        gate_dependencies=audit_gates(layout,report)
        assert not gate_dependencies['baseline_missing_pairs']
        save(out/'ordinary-gate-dependencies.json',gate_dependencies)
        interleaved=audit_ravel(bsp,layout,report)
    if recipe=='lockyard':
        from tools.map_recipes.lockyard_audit import audit_lockyard
        feature_path=out/'features.json'
        assert feature_path.is_file(),'Lockyard needs its courtyard and submerged-route contracts'
        features=audit_lockyard(bsp,layout,texture_physics,json.loads(feature_path.read_text(encoding='utf-8-sig')),report)
        from tools.map_recipes.gate_audit import audit_gates
        gate_dependencies=audit_gates(layout,report)
        assert not gate_dependencies['baseline_missing_pairs']
        winding_gate=next(p for p in gate_dependencies['passages'] if {p['from_room'],p['to_room']}=={2,5})
        assert winding_gate['unreachable_pairs']==0,'Winding needs a dry return independent of Boiler doorway'
        save(out/'ordinary-gate-dependencies.json',gate_dependencies)
    evidence={'map':map_name,'source_sha256':sha(out/(map_name+'.map')),'bsp_sha256':bsp.sha256,
        'recipe_sha256':sha(project/('tools/map_recipes/'+recipe+'.cpp')),
        'recipe_support_sha256':sha(project/'tools/map_recipes/recipe_support.hpp'),
        'core_sha256':sha(project/'build/windows-clang/qmg_core.lib'),
        'profile_sha256':comparison['profile_sha256'],'bsp_version':38,'sealed':True,'full_vis':True,
        'lighting':'extra4, one bounce, soft shadows; no empty lightmaps','path_checks':264,
        'standing_hull_vs_brush_checks':checks,'gameplay_anchors_checked':len(analysis['anchors']),
        'layout_analysis':report['analysis']['quality']['score'],'native_textures_verified':textures,
        'texture_physics':texture_physics,'ordinary_route_edge_kinds':layout['ordinary_route_edge_kinds'],
        'navigation_checks':analysis['navigation'],
        'compiled_spawn_los':analysis['spawn_los'],'runtime_playtested':False,'model_weights_trained':False,
        'archive_geometry_copied':False,'crossing_checks':crossing,'mechanism_checks':mechanisms,'ramp_checks':ramps,'drop_checks':drops,
        'compiled_world_collision_brushes':len(prisms),'collision_method':'exact convex polyhedron versus AABB, positive-volume overlap',
        'additional_compiled_traversal_hull_samples':len(traversal_samples),
        'timing_basis':'endpoint-aware Manhattan/node-anchor proxy v2','limits':before_after['limits']}
    if features is not None:
        evidence['feature_checks']=features
        evidence['features_sha256']=sha(out/'features.json')
    if interleaved is not None:
        evidence['interleaved_checks']=interleaved
    save(out/'verification.json',evidence)
    if recipe=='switchback':
        view_data={**layout,'anchors':analysis['anchors']}
        template=(project/'tools/map_recipes/layout-view.html').read_text(encoding='utf-8')
        fragment=template.replace('__SWITCHBACK_DATA__',json.dumps(view_data,separators=(',',':')).replace('<','\\u003c'))
        assert '__SWITCHBACK_DATA__' not in fragment and len(fragment.encode())<1_000_000
        (out/'switchback-layout.html').write_text(fragment,encoding='utf-8')
    with zipfile.ZipFile(out/(map_name+'.zip'),'w',zipfile.ZIP_DEFLATED) as archive:
        archive.write(bsp_path,'maps/'+map_name+'.bsp')
        archive.write(out/(map_name+'.map'),'maps/'+map_name+'.map')
        archive.writestr(map_name+'_README.txt',
            title.upper()+' - Quake II deathmatch for 4-6 players\n\n'
            'Extract into baseq2. Console: set deathmatch 1, then map '+map_name+'.\n'
            'Use windowed mode for tests. Requires stock Quake II assets.\n\n'
            +str(expected_rooms)+' asymmetric spaces; '+str(expected_connections)+' connections; '
            +str(len(layout.get('stair_runs',[])))+' stair routes; '+str(len(layout['levels']))+' declared floor heights.\n'
            +('One automatic lift, one one-way teleporter; all 264 ordinary routes also pass.\n' if recipe=='counterweight' else '')
            +('Two continuous 1:4 ramps with compiled three-lane hull sweeps; all 264 ordinary routes pass.\n' if recipe=='talus' else '')
            +('Two optional 128-unit descent candidates with clear compiled sweeps and ordinary typed returns.\n' if recipe=='undercut' else '')
            +('Compact adjoining rooms, raised walkways, a horseshoe room, optional teleporter and shallow water. Required routes remain dry and ordinary.\n' if recipe=='fold' else '')
            +('Outdoor court, partial gallery and optional deep culvert with stepped entry/exit. Required routes remain dry and ordinary.\n' if recipe=='lockyard' else '')
            +('Staggered compact rooms, one continuous ramp, a crosswalk and a local third floor with an optional drop and ordinary return.\n' if recipe=='ravel' else '')
            +'Eight respawns, seven weapons, two major items.\n'
            'Original Quake-MapGen experimental recipe; no archive geometry or game assets included.\n'
            'Sealed BSP, full VIS, final lighting and 264 static route checks passed.\n'
            'Engine traversal, rendering and multiplayer balance are not yet playtested.\n')
    with zipfile.ZipFile(out/(map_name+'.zip')) as archive:
        assert archive.testzip() is None
        assert hashlib.sha256(archive.read('maps/'+map_name+'.bsp')).hexdigest()==bsp.sha256
    print(json.dumps({'path_checks':264,'hull_checks':checks,'bsp_sha256':bsp.sha256,'metrics':analysis['metrics'],'spawn_los':analysis['spawn_los']},indent=2))

if __name__=='__main__':main()
