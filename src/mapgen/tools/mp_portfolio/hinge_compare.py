"""Compare upper-link construction without equating changed maps with quality.

The frozen traces expose96 room/edge observations, not96 complete geometries.
Only selected exports with matching candidate seeds support a structural A/B
preservation test. Compiled checks are separate from native movement trials.
"""
import argparse
from collections import Counter
from copy import deepcopy
import json
from pathlib import Path

from tools.map_study.binary import Bsp
from tools.map_study.features import anchors
from tools.map_recipes.gate_audit import audit_gates
from tools.map_recipes.packing_audit import packing
from tools.mp_portfolio.braid_compare import (
    Evidence, compiled_spawns, digest, json_hash, json_normalized, require, trace_contract,
)
from tools.mp_portfolio.compare import world_signature
from tools.mp_portfolio.inspect import supported_anchor, validated_world, validate_report
from tools.mp_portfolio.topology_compare import full_span_room_gaps, room_graph

ROOT = Path(__file__).resolve().parents[2]
LINK_TAG = 'inter_room_upper_link'
SOURCE_FILES = tuple('tools/mp_portfolio/'+n+'.py' for n in
                     ('hinge_compare', 'braid_compare', 'compare', 'inspect', 'ranking_compare', 'topology_compare')) + (
    'tools/map_study/binary.py', 'tools/map_study/features.py', 'tools/map_recipes/drop_audit.py',
    'tools/map_recipes/geometry.py', 'tools/map_recipes/packing_audit.py', 'tools/map_recipes/gate_audit.py',
    'tests/test_hinge_compare.py')


def indexed(rows, label):
    result = {r['id']: r for r in rows}
    require(len(result) == len(rows) and all(type(i) is int and i > 0 for i in result),
            'Duplicate or invalid ' + label + ' ID')
    return result


def trace_change(before, after):
    """Retain seed-matched observations; no unrecorded solid-preservation claim."""
    old, new = ({c['seed']: c for c in t['candidates']} for t in (before, after))
    require(len(old) == len(new) == 6 and old.keys() == new.keys(), 'Candidate identities differ or repeat')
    for key in ('feature_names', 'feature_weights', 'selection_policy', 'tie_break_policy', 'layout_intent'):
        require(before.get(key) == after.get(key), 'Unexpected policy/request change: ' + key)
    rows = []
    for seed, a in old.items():
        b = new[seed]
        require(a['index'] == b['index'] and a['family'] == b['family'] and a['rooms'] == b['rooms'],
                'Candidate identity, family or room IDs changed')
        require(a['connections'] == b['connections'][:len(a['connections'])],
                'Existing candidate passage endpoint records changed')
        added = b['connections'][len(a['connections']):]
        require(len(added) <= 1, 'More than one candidate passage was appended')
        if before.get('layout_intent') != 'wrapped_center':
            require(not added and a['rank_neutral_layout_sha256'] == b['rank_neutral_layout_sha256'],
                    'Unrequested candidate changed')
        old_edges = {tuple(sorted((e['from'], e['to']))) for e in a['connections']}
        graph = room_graph(b)
        rows.append({'index': a['index'], 'seed': seed, 'before_selected': a['selected'],
                     'after_selected': b['selected'], 'added_passage_endpoints': added,
                     'added_unique_adjacencies': sum(tuple(sorted((e['from'], e['to']))) not in old_edges for e in added),
                     'before_rank_neutral_sha256': a['rank_neutral_layout_sha256'],
                     'after_rank_neutral_sha256': b['rank_neutral_layout_sha256'],
                     'before_features': a['features'], 'after_features': b['features'],
                     'before_contributions': a['contributions'], 'after_contributions': b['contributions'],
                     'before_rank_score': a['rank_score'], 'after_rank_score': b['rank_score'],
                     'after_room_graph': graph})
    return {'matched_candidates': 6, 'candidates_with_appended_passage': sum(bool(r['added_passage_endpoints']) for r in rows),
            'candidates_without_appended_passage': sum(not r['added_passage_endpoints'] for r in rows),
            'selected_seed_changed': before['pipeline_selected_seed'] != after['pipeline_selected_seed'],
            'observations': rows,
            'scope': 'Room IDs and ordered passage endpoint records only. Frozen traces contain no solid/bounds snapshots; this is not96-candidate geometry preservation.'}


def placement_changes(before, after):
    old, new = indexed(before, 'placement'), indexed(after, 'placement')
    return {'removed': [old[i] for i in sorted(old.keys()-new.keys())],
            'added': [new[i] for i in sorted(new.keys()-old.keys())],
            'changed': [{'id': i, 'fields': {k: {'before': old[i].get(k), 'after': new[i].get(k)}
                        for k in sorted(old[i].keys() | new[i].keys()) if old[i].get(k) != new[i].get(k)},
                         'before': old[i], 'after': new[i]}
                        for i in sorted(old.keys() & new.keys()) if old[i] != new[i]]}


def structural_change(before, after):
    """Prove the typed append-only contract only for a matching selected seed."""
    if before['candidate_seed'] != after['candidate_seed']:
        return {'comparable_same_seed': False, 'preservation_tested': False,
                'reason': 'Different selected candidates; A/B geometry and placements are descriptive only.'}
    old_rooms, new_rooms = indexed(before['rooms'], 'room'), indexed(after['rooms'], 'room')
    old_solids, new_solids = indexed(before['solids'], 'solid'), indexed(after['solids'], 'solid')
    old_edges, new_edges = indexed(before['connections'], 'passage'), indexed(after['connections'], 'passage')
    require(old_rooms.keys() == new_rooms.keys(), 'Selected room IDs changed')
    require(old_solids.keys() <= new_solids.keys() and all(new_solids[i] == v for i, v in old_solids.items()),
            'Existing selected solid geometry/material changed')
    require(old_edges.keys() <= new_edges.keys() and all(new_edges[i] == v for i, v in old_edges.items()),
            'Existing selected passage changed')
    added_edges = [new_edges[i] for i in sorted(new_edges.keys()-old_edges.keys())]
    added_solids = [new_solids[i] for i in sorted(new_solids.keys()-old_solids.keys())]
    require(len(added_edges) in (0, 1) and len(added_solids) == 3*len(added_edges),
            'Expected zero or one upper connection with exactly three slabs')
    endpoints = {added_edges[0][k] for k in ('from', 'to')} if added_edges else set()
    for i, old in old_rooms.items():
        expected = deepcopy(old)
        if i in endpoints:
            expected['tags'].append(LINK_TAG)
        require(new_rooms[i] == expected, 'Room envelope/name or unexpected provenance tag changed')
    for key in ('stair_runs', 'stair_run_solid_ids', 'ramps', 'drops', 'mechanisms', 'levels'):
        require(before[key] == after[key], 'Existing circulation representation changed: ' + key)
    for edge in added_edges:
        require(len(endpoints) == 2 and endpoints <= new_rooms.keys() and len(edge['volumes']) == 1 and
                edge['width'] == 160 and edge['height'] == 192, 'Malformed added upper passage')
        low, high = edge['volumes'][0]
        require(low[2] in (new_rooms[edge['from']]['bounds'][0][2]+128,
                           new_rooms[edge['from']]['bounds'][0][2]+192) and high[2]-low[2] == 192,
                'Added passage is not an existing upper level')
        axes = [axis for axis in (0, 1) if high[axis]-low[axis] == 48]
        require(len(axes) == 1 and high[1-axes[0]]-low[1-axes[0]] == 160,
                'Added passage has unsupported seam dimensions')
        axis = axes[0]
        portal_slab = [[low[0], low[1], low[2]-24], [high[0], high[1], low[2]]]
        require(sum(s['bounds'] == portal_slab for s in added_solids) == 1, 'Missing explicit upper portal slab')
        for solid in added_solids:
            lo, hi = solid['bounds']
            require(solid['purpose'] == 1 and solid['material'] == 3 and solid['ownership'] == 0 and
                    solid['owner'] is None and solid['contents'] == 'solid', 'Invalid upper slab material/ownership')
            require(lo[2] == low[2]-24 and hi[2] == low[2] and
                    lo[1-axis] == low[1-axis] and hi[1-axis] == high[1-axis] and lo[axis] < hi[axis],
                    'Upper slab is not a continuous aligned24-thick strip')
        spans = sorted((s['bounds'][0][axis], s['bounds'][1][axis]) for s in added_solids)
        require(spans[0][1] == spans[1][0] == low[axis] and spans[1][1] == spans[2][0] == high[axis],
                'Upper extension slabs leave a gap or overlap the portal')
        for end, room_id, face in ((spans[0][0], edge['from'], 1), (spans[-1][1], edge['to'], 0)):
            room = new_rooms[room_id]['bounds']
            require(room[face][axis] == (low[axis] if face else high[axis]), 'Upper connection room face differs')
            decks = [s for s in old_solids.values() if s['purpose'] == 1 and s['bounds'][1][2] == low[2] and
                     s['bounds'][face][axis] == end and
                     s['bounds'][0][1-axis] <= low[1-axis] and s['bounds'][1][1-axis] >= high[1-axis] and
                     all(room[0][a] <= s['bounds'][0][a] < s['bounds'][1][a] <= room[1][a] for a in range(3))]
            require(decks, 'Upper extension does not meet a full-width original deck in its room')
    return {'comparable_same_seed': True, 'preservation_tested': True, 'existing_geometry_preserved': True,
            'old_solids': len(old_solids), 'old_passages': len(old_edges),
            'added_connections': added_edges, 'added_solids': added_solids,
            'placement_changes': placement_changes(before['placements'], after['placements'])}


def entity_changes(before, after):
    """Full compiled entity property multisets; duplicates cannot disappear."""
    old, new = Counter(json_hash(e) for e in before), Counter(json_hash(e) for e in after)
    values = {json_hash(e): e for e in before + after}
    return {'removed': [{'count': count, 'entity': values[key]} for key, count in sorted((old-new).items())],
            'added': [{'count': count, 'entity': values[key]} for key, count in sorted((new-old).items())]}


def verify_guard(evidence, output):
    guard = evidence.read(output/'guard-only-parity/record.json')
    require(guard['byte_identical_files'] == len(guard['files']) == 32 and
            len({p['before'] for p in guard['files']}) == 32 and
            len({p['after'] for p in guard['files']}) == 32, 'Expected32 distinct guard-only file pairs')
    for pin in guard['files']:
        for side in ('before', 'after'):
            evidence.bytes(evidence.root/pin[side], pin['sha256'])
    return {'verified_byte_pairs': 32, 'scope': guard['scope']}


def stage_bindings(record, cases, output, stage, root=ROOT):
    require(len(record['rows']) == len(cases) and {r['id'] for r in record['rows']} == {c['id'] for c in cases},
            'Stage case set differs')
    rows = {r['id']: r for r in record['rows']}
    for case in cases:
        row = rows[case['id']]
        expected_name = 'qmg_hinge' if case['group'] == 'example' else 'qmg_'+case['id']
        require(row['group'] == case['group'] and row['map_name'] == expected_name and
                (root/row['folder']).resolve() == (output/stage/case['id']).resolve(),
                'Stage folder/name/group points to another artifact')
    return rows


def load_stage(evidence, case, binding):
    folder = evidence.root/binding['folder']
    name = binding['map_name']
    require(binding['id'] == case['id'] and binding['group'] == case['group'], 'Stage case/group mismatch')
    layout = evidence.read(folder/'layout.json')
    report = evidence.read(folder/(name+'.qmg.json'), binding['report_sha256'])
    evidence.bytes(folder/(name+'.map'), binding['map_sha256'])
    trace = evidence.read(folder/'candidate-trace.json', binding['trace_sha256'])
    check = evidence.read(folder/'verification.json')
    analysis = evidence.read(folder/'compiled-analysis.json')
    validate_report(layout, report)
    require(report['prompt'] == case['prompt'] and int(report['request_seed']) == case['seed'], 'Request changed')
    require(layout['map_name'] == check['map'] == name and
            check['layout_sha256'] == layout['layout_sha256'] == report['layout_sha256'] and
            check['source_sha256'] == binding['map_sha256'], 'Stale selected geometry binding')
    require(check['ok'] and check['sealed'] and check['full_vis'] and check['extra4_one_bounce_lighting'] and
            not check['anchor_failures'], 'Incomplete compiled evidence')
    expected = 'wrapped_center' if case['layout_intent_requested'] else None
    require(all(v.get('layout_intent') == expected for v in (layout, report, trace)), 'Typed intent changed')
    family = 'pinwheel' if all('compact_family_pinwheel' in r['tags'] for r in layout['rooms']) else 'rows'
    current = {'candidate_seed': layout['candidate_seed'], 'layout_sha256': layout['layout_sha256'], 'family': family}
    trace_contract(trace, current, case['seed'])
    require(binding['selected_seed'] == current['candidate_seed'], 'Selected seed binding changed')
    bsp = Bsp(evidence.bytes(folder/(name+'.bsp'), check['bsp_sha256']))
    require(not bsp.file_issues and not bsp.entity_issues and len(bsp.models) == 1, 'Invalid BSP or unscoped brush entity')
    world = validated_world(bsp)
    for suffix in ('-qbsp.log', '-vis.log', '-light.log', '.prt'):
        evidence.bytes(folder/(name+suffix))
    spawns = compiled_spawns(layout, bsp, world)
    require(not analysis['spawn_los']['invalid_eye_entities'] and
            analysis['spawn_los']['tested_pairs'] == len(spawns['pairs']) and
            sorted(sorted(p) for p in analysis['spawn_los']['visible_pairs']) ==
            sorted(sorted(p['entities']) for p in spawns['pairs'] if p['visible']), 'Stored spawn rays differ')
    actual_anchors, excluded = anchors(bsp.entities)
    require(json_normalized(actual_anchors) == analysis['anchors'] and excluded == analysis['excluded_not_deathmatch'],
            'Stored compiled anchor inventory differs')
    anchor_checks = [supported_anchor(world, a['origin']) for a in analysis['anchors']]
    require(anchor_checks == check['authored_anchor_hulls_and_settling_candidates'], 'Stored anchor support differs')
    bounds = [[min(s.bounds[0][a] for _, _, s in world) for a in range(3)],
              [max(s.bounds[1][a] for _, _, s in world) for a in range(3)]]
    area = (bounds[1][0]-bounds[0][0])*(bounds[1][1]-bounds[0][1])
    require(area.denominator == 1 and int(area) == check['compiled_world_xy_envelope_area'], 'Stored envelope differs')
    require([[float(v) for v in p] for p in bounds] == check['compiled_world_bounds'] and
            len(world) == check['compiled_world_brushes'], 'Stored compiled world bounds/count differs')
    rectangles = [r['bounds'][0][:2]+r['bounds'][1][:2] for r in layout['rooms']]
    rectangles += [v[0][:2]+v[1][:2] for c in layout['connections'] for v in c['volumes']]
    actual_packing = packing(rectangles)
    require(actual_packing == check['authored_room_passage_packing'], 'Stored packing differs')
    gates = json_normalized(audit_gates(layout, report))
    require(gates == check['ordinary_gate_checks'], 'Stored ordinary graph checks differ')
    for key in ('navigation', 'visibility', 'lighting', 'quality'):
        actual = report['analysis']['navigation']['player']['findings'] if key == 'navigation' else report['analysis'][key]['findings']
        require(actual == check['analysis_findings'][key], 'Stored analysis findings differ: '+key)
    require(report['analysis']['quality']['score'] == check['quality_score'], 'Stored quality score differs')
    textures = {p['texture'] for p in bsp.polygons()}
    require(textures == check['texture_physics'].keys(), 'Compiled material set differs')
    for p in bsp.polygons():
        physics = check['texture_physics'][p['texture']]
        require(p['flags'] == physics['flags'] and physics['contents'] == 0 and not p['flags'] & (8|16|32),
                'Compiled material physics differs or is outside solid scope')
    elevated = [c for c in layout['connections'] if c['volumes'][0][0][2] > min(r['bounds'][0][2] for r in layout['rooms'])]
    summary = {**current, 'map_sha256': binding['map_sha256'], 'bsp_sha256': check['bsp_sha256'],
               'room_graph': room_graph(layout), 'full_span_room_gaps': full_span_room_gaps(layout),
               'compiled_xy_envelope_area': int(area), 'authored_room_passage_packing': actual_packing,
               'ordinary_pairs': gates['baseline_pairs'], 'ordinary_missing_pairs': gates['baseline_missing_pairs'],
               'passage_closures_losing_pairs': sum(p['unreachable_pairs'] > 0 for p in gates['passages']),
               'findings': check['analysis_findings'], 'quality_score': check['quality_score'],
               'levels': layout['levels'], 'stair_runs': len(layout['stair_runs']),
               'treads': sum(map(len, layout['stair_runs'])), 'elevated_connections': elevated,
               'compiled_spawn_checks': spawns, 'compiled_anchors': analysis['anchors'],
               'compiled_anchor_support': anchor_checks, 'compiled_materials': check['texture_physics'],
               'compiled_entity_classes': dict(Counter(e.get('classname', '') for e in bsp.entities)),
               'compiled_world_planes_sha256': json_hash(world_signature(bsp)),
               'compiled_world_brushes': len(world)}
    return {'summary': summary, 'trace': trace, 'layout': layout, 'entities': bsp.entities}


def summarize(rows):
    result = []
    for group in ('primary', 'holdout', 'example'):
        for rooms in sorted({r['rooms'] for r in rows if r['group'] == group}):
            subset = [r for r in rows if r['group'] == group and r['rooms'] == rooms]
            result.append({'group': group, 'rooms': rooms, 'cases': len(subset),
                'matched_candidate_observations': sum(r['trace_comparison']['matched_candidates'] for r in subset),
                'candidates_with_appended_passage': sum(r['trace_comparison']['candidates_with_appended_passage'] for r in subset),
                'selected_seed_changes': sum(r['trace_comparison']['selected_seed_changed'] for r in subset),
                'envelope_changes': [r['compiled_envelope_change_fraction'] for r in subset],
                'stages': {stage: {'selected_elevated_links': sum(len(r[stage]['elevated_connections']) for r in subset),
                    'distinct_selected_multigraphs': len({r[stage]['room_graph']['canonical_multigraph'] for r in subset}),
                    'ordinary_pairs': sum(r[stage]['ordinary_pairs'] for r in subset),
                    'ordinary_missing_pairs': sum(len(r[stage]['ordinary_missing_pairs']) for r in subset),
                    'passage_closures_losing_pairs': sum(r[stage]['passage_closures_losing_pairs'] for r in subset),
                    'visible_spawn_pairs': sum(r[stage]['compiled_spawn_checks']['visible_pairs'] for r in subset),
                    'close_spawn_pairs': sum(r[stage]['compiled_spawn_checks']['close_pairs'] for r in subset),
                    'findings': {k: sum(len(r[stage]['findings'][k]) for r in subset)
                                 for k in ('navigation', 'visibility', 'lighting', 'quality')}} for stage in ('before', 'after')}})
    return result


def development_observations(evidence, cases, baseline, development):
    """Retain the first construction attempt without requiring intermediate BSPs."""
    rows = []
    for case in cases:
        binding = development[case['id']]
        folder, name = evidence.root/binding['folder'], binding['map_name']
        evidence.bytes(folder/(name+'.map'), binding['map_sha256'])
        report = evidence.read(folder/(name+'.qmg.json'), binding['report_sha256'])
        trace = evidence.read(folder/'candidate-trace.json', binding['trace_sha256'])
        layout = evidence.read(folder/'layout.json')
        validate_report(layout, report)
        require(report['prompt'] == case['prompt'] and int(report['request_seed']) == case['seed'] and
                binding['group'] == case['group'], 'Development request/group changed')
        family = 'pinwheel' if all('compact_family_pinwheel' in r['tags'] for r in layout['rooms']) else 'rows'
        trace_contract(trace, {'candidate_seed': layout['candidate_seed'], 'layout_sha256': layout['layout_sha256'],
                               'family': family}, case['seed'])
        old = baseline[case['id']]
        before = evidence.read(evidence.root/old['folder']/'candidate-trace.json', old['trace_sha256'])
        change = trace_change(before, trace)
        selected = next(r for r in change['observations'] if r['after_selected'])
        rows.append({'id': case['id'], 'group': case['group'], 'candidate_observations': 6,
                     'candidates_with_appended_passage': change['candidates_with_appended_passage'],
                     'selected_candidate_seed': layout['candidate_seed'],
                     'selected_seed_changed': change['selected_seed_changed'],
                     'selected_candidate_appended_passages': selected['added_passage_endpoints'],
                     'selected_elevated_connections': [c for c in layout['connections'] if c['volumes'][0][0][2] > 0],
                     'source_map_equals_baseline': binding['map_sha256'] == old['map_sha256']})
    return {'stage': 'generated', 'cases': rows,
            'scope': 'First globally aligned16-unit strip search, retained before the feasible-boundary phase correction. Same16 fixed requests and seeds; intermediate maps are not all compiled here. No failed physical traversal is inferred from a no-link construction observation.'}


def comparison(output, stage='generated-refined'):
    output = output.resolve(strict=True)
    evidence = Evidence(ROOT)
    require(output.is_relative_to(ROOT/'out'), 'Comparison output must be inside project out')
    require(stage not in ('', '.', '..', 'baseline') and Path(stage).name == stage and
            '/' not in stage and '\\' not in stage, 'Expected one final-stage directory name')
    for source in SOURCE_FILES:
        evidence.bytes(ROOT/source)
    cases = []
    for group, name in (('primary', 'cases.json'), ('holdout', 'holdouts.json'), ('example', 'hero.json')):
        value = evidence.read(output/name)
        cases += [{**c, 'group': group} for c in (value if isinstance(value, list) else [value])]
    require(len(cases) == len({c['id'] for c in cases}) == 16 and
            Counter(c['group'] for c in cases) == {'primary': 9, 'holdout': 6, 'example': 1}, 'Predeclared strata differ')
    for case in cases:
        require(evidence.bytes(ROOT/case['prompt_file']).decode('utf-8-sig') == case['prompt'], 'Prompt bytes changed')
    frozen = evidence.read(output/'compiled-baseline-record.json')
    require(len(frozen['files']) == len({p['path'] for p in frozen['files']}) == 128, 'Expected128 frozen compiled baseline pins')
    for pin in frozen['files']:
        evidence.bytes(ROOT/pin['path'], pin['sha256'])
    guard = verify_guard(evidence, output)
    records = {}
    for stage_name in dict.fromkeys(('baseline', 'generated', stage)):
        record = evidence.read(output/stage_name/'record.json')
        records[stage_name] = stage_bindings(record, cases, output, stage_name)
        for pin in record['inputs']:
            if 'snapshot' in pin:
                evidence.bytes(ROOT/pin['snapshot'], pin['sha256'])
            elif stage_name == stage:
                evidence.bytes(ROOT/pin['path'], pin['sha256'])
    development = development_observations(evidence, cases, records['baseline'], records['generated'])
    rows = []
    for case in cases:
        old, new = [load_stage(evidence, case, records[name][case['id']]) for name in ('baseline', stage)]
        a, b = old['summary'], new['summary']
        rows.append({'id': case['id'], 'group': case['group'], 'rooms': case['rooms'], 'seed': case['seed'],
                     'before': a, 'after': b, 'trace_comparison': trace_change(old['trace'], new['trace']),
                     'selected_structural_comparison': structural_change(old['layout'], new['layout']),
                     'all_selected_placement_changes': placement_changes(old['layout']['placements'], new['layout']['placements']),
                     'compiled_entity_changes': entity_changes(old['entities'], new['entities']),
                     'compiled_world_planes_equal': a['compiled_world_planes_sha256'] == b['compiled_world_planes_sha256'],
                     'compiled_envelope_change_fraction': b['compiled_xy_envelope_area']/a['compiled_xy_envelope_area']-1})
    # Recheck pins before returning. Explicit dependencies also keep imported
    # and command-line rederivations independent of the caller's module cache.
    for path, expected in list(evidence.files.items()):
        evidence.bytes(ROOT/path, expected)
    return json_normalized({'schema_version': 1, 'ok': True, 'final_stage': stage,
        'ok_scope': 'Evidence consistency and bounded typed construction checks; no overall map quality verdict.',
        'cases': rows, 'strata': summarize(rows),
        'matched_candidate_observations': sum(r['trace_comparison']['matched_candidates'] for r in rows),
        'same_selected_seed_structural_comparisons': sum(r['selected_structural_comparison']['preservation_tested'] for r in rows),
        'retained_development_observation': development,
        'baseline_compiled_pins': 128, 'guard_only_parity': guard,
        'inputs': [{'path': p, 'sha256': s} for p, s in sorted(evidence.files.items())],
        'limits': ['The96 frozen trace rows contain room IDs and passage endpoints, not full solid geometry. Only same-selected-seed exports support structural preservation comparisons.',
                   'An appended passage may be parallel to an old lower passage. Multigraph edge/cycle counts differ from unique adjacency, room hops and physical route identity.',
                   'Geometry and allocated positions can change together. A changed selected seed compares different candidates and cannot isolate the upper-link effect.',
                   'Compiled full-hull anchor support is a local pose/vertical settling candidate. Ordinary graph edges and node closures do not certify continuous native journeys.',
                   'Static eye rays and room/passage packing describe selected geometry; no safety, balance, movement-time or universal quality score is inferred.',
                   'Material WAL hashes are retained inspector evidence; compiled face flags and SOLID world contents are independently checked here.',
                   'The nine primary, six holdout and separate predeclared hero cases are not pooled into a quality or generalization claim.']})


def retain(path, result, *, verify=False):
    if path.exists():
        require(json.loads(path.read_text(encoding='utf-8')) == result, 'Refusing changed retained comparison')
    else:
        require(not verify, 'Missing retained comparison')
        path.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'out/hinge-portfolio')
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--stage', default='generated-refined', help='Retained final stage directory; baseline is fixed.')
    args = parser.parse_args()
    result = comparison(args.output, args.stage)
    retain(args.output/'comparison.json', result, verify=args.verify)
    print(json.dumps({'ok': result['ok'], 'cases': len(result['cases']),
                      'candidate_observations': result['matched_candidate_observations'],
                      'bound_files': len(result['inputs'])}))


if __name__ == '__main__':
    main()
