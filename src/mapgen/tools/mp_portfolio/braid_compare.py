"""Compare frozen construction intent and spawn-refinement stages separately.

This reads retained exports and compiled worlds. It never generates candidates,
changes selection, launches an engine, or treats geometric descriptors as play.
"""
import argparse
from collections import Counter
from copy import deepcopy
from fractions import Fraction
import hashlib
from itertools import combinations
import json
from pathlib import Path

from tools.map_study.binary import Bsp
from tools.map_recipes.packing_audit import packing
from tools.mp_portfolio.compare import world_signature
from tools.mp_portfolio.inspect import supported_anchor, validated_world, validate_report
from tools.mp_portfolio.ranking_compare import mixed_seed_key, validate_scores
from tools.mp_portfolio.topology_compare import full_span_room_gaps, room_graph, validate_trace

ROOT = Path(__file__).resolve().parents[2]
SPAWN_ROLE = 1
STRIDE = 0x9E3779B97F4A7C15


def require(value, message):
    if not value:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def json_hash(value):
    def exact_number(number):
        if isinstance(number, Fraction):
            return {'rational': [number.numerator, number.denominator]}
        raise TypeError('Unsupported canonical value: ' + type(number).__name__)
    return digest(json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False, default=exact_number).encode())


def bound_json(data, expected, label):
    require(digest(data) == expected, 'Changed frozen input: ' + label)
    return json.loads(data.decode('utf-8-sig'))


def json_normalized(value):
    """Use the retained JSON representation, including string object keys."""
    return json.loads(json.dumps(value, allow_nan=False))


class Evidence:
    def __init__(self, root):
        self.root = root.resolve()
        self.files = {}

    def bytes(self, path, expected=None):
        path = path.resolve(strict=True)
        require(path.is_relative_to(self.root) and path.is_file(), 'Evidence path outside project')
        data = path.read_bytes()
        actual = digest(data)
        name = path.relative_to(self.root).as_posix()
        require(expected is None or actual == expected, 'Changed frozen input: ' + name)
        require(name not in self.files or self.files[name] == actual, 'Evidence changed during comparison: ' + name)
        self.files[name] = actual
        return data

    def read(self, path, expected=None):
        data = self.bytes(path, expected)
        return json.loads(data.decode('utf-8-sig'))


def trace_contract(trace, current, request_seed):
    validate_trace(trace, current)
    validate_scores(trace)
    candidates = trace['candidates']
    require([c['index'] for c in candidates] == list(range(6)), 'Candidate indices changed')
    require([c['seed'] for c in candidates] == [(request_seed + i * STRIDE) % (1 << 64) for i in range(6)],
            'Candidate seed stream changed')
    require(trace['repair_passes'] == 0, 'Repairs require separate intervention attribution')
    best = max(c['rank_score'] for c in candidates)
    tied = [c for c in candidates if c['rank_score'] == best]
    chosen = next(c for c in candidates if c['selected'])
    require(chosen['index'] == trace['selected_index'] and chosen in tied, 'Selected index is not an exact maximum')
    require(trace['tied_best_indices'] == [c['index'] for c in tied], 'Exact tie diagnostics differ')
    require(trace['selection_policy'] == 'compact_q2_mp_adequacy_v1' and
            trace['tie_break_policy'] == 'minimum_splitmix64_seed_then_neutral_sha256_v1', 'Unexpected ranking policy')
    require(all(c['tie_key'] == mixed_seed_key(c['seed']) for c in candidates), 'Candidate seed tie keys differ')
    require(chosen['tie_key'] == min(c['tie_key'] for c in tied), 'Selection differs from declared tie policy')
    require(chosen['ranked_pre_repair_layout_sha256'] == trace['pipeline_layout_sha256'],
            'Unrepaired selected candidate differs from pipeline layout')


def candidate_intervention(before, after):
    """Retain any failed B→C invariant instead of silently dropping its case."""
    old = {c['seed']: c for c in before['candidates']}
    new = {c['seed']: c for c in after['candidates']}
    require(len(old) == len(new) == 6 and old.keys() == new.keys(), 'Candidate identities differ or repeat')
    violations = []
    for field in ('candidate_count', 'selected_index', 'pipeline_selected_seed', 'repair_passes', 'layout_intent',
                  'feature_names', 'feature_weights', 'selection_policy', 'tie_break_policy', 'tied_best_indices'):
        if before.get(field) != after.get(field):
            violations.append({'field': field})
    neutral_matches = 0
    for seed in old:
        for field in ('spawn_origin_neutral_layout_sha256', 'features', 'contributions', 'rank_score', 'family',
                      'rooms', 'connections', 'selected', 'index', 'tie_key', 'tie_hash'):
            if old[seed][field] != new[seed][field]:
                violations.append({'seed': seed, 'field': field})
        neutral_matches += old[seed]['spawn_origin_neutral_layout_sha256'] == new[seed]['spawn_origin_neutral_layout_sha256']
    return {'ok': not violations, 'candidates': 6, 'spawn_neutral_hash_matches': neutral_matches,
            'selected_seed_unchanged': before['pipeline_selected_seed'] == after['pipeline_selected_seed'],
            'violations': violations}


def authored_spawn_neutral(layout):
    keys = ('players', 'candidate_seed', 'layout_intent', 'rooms', 'connections', 'solids', 'placements', 'levels',
            'stair_runs', 'stair_run_solid_ids', 'ramps', 'drops', 'mechanisms')
    result = {key: deepcopy(layout[key]) for key in keys if key in layout}
    for placement in result['placements']:
        if placement['role'] == SPAWN_ROLE:
            placement['origin'] = [0, 0, 0]
    return result


def pair_changes(before, after):
    old = {tuple(row['ids']): row for row in before}
    new = {tuple(row['ids']): row for row in after}
    require(len(old) == len(before) and len(new) == len(after) and old.keys() == new.keys(), 'Spawn pair identities differ')
    rows = [{'ids': list(key), 'before_visible': old[key]['visible'], 'after_visible': new[key]['visible'],
             'before_distance_squared': old[key]['distance_squared'], 'after_distance_squared': new[key]['distance_squared']}
            for key in sorted(old)]
    newly_visible = [row['ids'] for row in rows if not row['before_visible'] and row['after_visible']]
    objectives = {stage: [sum(row[stage+'_visible'] for row in rows),
                          sum(row[stage+'_distance_squared'] < 256 ** 2 for row in rows),
                          sum(max(0, 256 ** 2-row[stage+'_distance_squared']) for row in rows)]
                  for stage in ('before', 'after')}
    return {'pairs': rows, 'newly_visible_pairs': newly_visible, 'blocked_pairs_preserved': not newly_visible,
            'before_objective': objectives['before'], 'after_objective': objectives['after'],
            'before_close_pairs': sum(r['before_distance_squared'] < 256 ** 2 for r in rows),
            'after_close_pairs': sum(r['after_distance_squared'] < 256 ** 2 for r in rows),
            'before_squared_deficit': sum(max(0, 256 ** 2-r['before_distance_squared']) for r in rows),
            'after_squared_deficit': sum(max(0, 256 ** 2-r['after_distance_squared']) for r in rows)}


def compiled_spawns(layout, bsp, world, *, direct_support=True):
    """Bind typed IDs to actual compiled entities before comparing pair identity."""
    entities = {i: e for i, e in enumerate(bsp.entities) if e.get('classname') == 'info_player_deathmatch'}
    stations = []
    used = set()
    for placement in layout['placements']:
        if placement['role'] != SPAWN_ROLE:
            continue
        origin = placement['origin']
        matches = [i for i, e in entities.items() if [float(v) for v in e.get('origin', '').split()] == origin and
                   float(e.get('angle', '0')) == placement.get('angle_degrees', 0)]
        require(len(matches) == 1 and matches[0] not in used, 'Typed/compiled spawn identity is ambiguous')
        entity = matches[0]
        used.add(entity)
        eye = [origin[0], origin[1], origin[2] + 22]
        require(bsp.trace(eye, eye) >= 1, 'Compiled spawn eye is invalid')
        support = supported_anchor(world, origin)
        directly_supported = support['supported_feet'][2] == origin[2] - 24
        require(not direct_support or directly_supported, 'Refined spawn requires support at authored feet')
        rooms = [room for room in layout['rooms'] if all(room['bounds'][0][axis] <= origin[axis] <= room['bounds'][1][axis]
                                                       for axis in range(3))]
        require(len(rooms) == 1, 'Spawn room binding is ambiguous')
        room = rooms[0]
        hull_low = [origin[0]-16, origin[1]-16, origin[2]-24]
        hull_high = [origin[0]+16, origin[1]+16, origin[2]+32]
        require(all(room['bounds'][0][axis] <= hull_low[axis] and hull_high[axis] <= room['bounds'][1][axis]
                    for axis in range(3)), 'Spawn standing hull leaves its assigned room')
        stations.append({'id': placement['id'], 'entity': entity, 'room': room['id'], 'origin': origin,
                         'eye': eye, 'support': support, 'directly_supported': directly_supported})
    require(len(stations) >= 2 and used == set(entities), 'Compiled spawn count differs from export')
    stations.sort(key=lambda row: row['id'])
    pairs = []
    for a, b in combinations(stations, 2):
        pairs.append({'ids': [a['id'], b['id']], 'entities': [a['entity'], b['entity']],
                      'visible': bsp.trace(a['eye'], b['eye']) >= 1,
                      'distance_squared': sum((x-y) ** 2 for x, y in zip(a['origin'], b['origin'], strict=True))})
    return {'stations': stations, 'pairs': pairs, 'visible_pairs': sum(row['visible'] for row in pairs),
            'close_pairs': sum(row['distance_squared'] < 256 ** 2 for row in pairs)}


def load_stage(evidence, output, stage, case, binding):
    group = case['group']
    name = 'qmg_braid' if group == 'example' and stage != 'baseline' else 'qmg_' + case['id']
    folder = evidence.root / binding.get('folder', (output / stage / case['id']).relative_to(evidence.root).as_posix())
    require(binding['group'] == group and binding.get('map_name', name) == name, 'Stage request/group changed')
    report = evidence.read(folder / (name + '.qmg.json'), binding['report_sha256'])
    evidence.bytes(folder / (name + '.map'), binding['map_sha256'])
    trace = evidence.read(folder / 'candidate-trace.json', binding['trace_sha256'])
    layout = evidence.read(folder / 'layout.json')
    verification = evidence.read(folder / 'verification.json')
    analysis = evidence.read(folder / 'compiled-analysis.json')
    validate_report(layout, report)
    require(report['prompt'] == case['prompt'] and int(report['request_seed']) == case['seed'], 'Normalized request changed')
    require(verification['layout_sha256'] == layout['layout_sha256'] == report['layout_sha256'] and
            layout['map_name'] == name and verification['map'] == name and
            verification['source_sha256'] == binding['map_sha256'], 'Stage geometry binding differs')
    require(verification['ok'] and verification['sealed'] and verification['full_vis'] and
            verification['extra4_one_bounce_lighting'] and not verification['anchor_failures'], 'Incomplete compiled stage')
    bsp = Bsp(evidence.bytes(folder / (name + '.bsp'), verification['bsp_sha256']))
    require(not bsp.file_issues and not bsp.entity_issues, 'Malformed compiled BSP')
    world = validated_world(bsp)
    for suffix in ('-qbsp.log', '-vis.log', '-light.log', '.prt'):
        evidence.bytes(folder / (name + suffix))
    graph = room_graph(layout)
    family = 'pinwheel' if all('compact_family_pinwheel' in r.get('tags', []) for r in layout['rooms']) else 'rows'
    current = {'family': family, 'candidate_seed': layout['candidate_seed'], 'layout_sha256': layout['layout_sha256']}
    trace_contract(trace, current, case['seed'])
    require(binding.get('selected_seed', current['candidate_seed']) == current['candidate_seed'], 'Stage selected seed differs')
    if 'prompt_sha256' in binding:
        evidence.bytes(evidence.root / case['prompt_file'], binding['prompt_sha256'])
    expected_intent = 'wrapped_center' if stage != 'baseline' and case['layout_intent_requested'] else None
    require(all(value.get('layout_intent') == expected_intent for value in (layout, report, trace)), 'Stage typed intent differs')
    if expected_intent:
        require(all(c['family'] == 'pinwheel' for c in trace['candidates']), 'Explicit intent lost a constrained candidate')
    spawns = compiled_spawns(layout, bsp, world, direct_support=stage != 'baseline')
    visible_entities = sorted(sorted(row['entities']) for row in spawns['pairs'] if row['visible'])
    require(not analysis['spawn_los']['invalid_eye_entities'] and
            analysis['spawn_los']['tested_pairs'] == len(spawns['pairs']) and
            sorted(sorted(row) for row in analysis['spawn_los']['visible_pairs']) == visible_entities,
            'Stored compiled spawn rays differ from independent BSP queries')
    require(abs(verification['metrics']['spawn_pair_world_los_fraction'] -
                spawns['visible_pairs']/len(spawns['pairs'])) < 1e-12, 'Stored spawn LOS metric differs')
    bounds = [[min(shape.bounds[0][axis] for _, _, shape in world) for axis in range(3)],
              [max(shape.bounds[1][axis] for _, _, shape in world) for axis in range(3)]]
    area = (bounds[1][0]-bounds[0][0]) * (bounds[1][1]-bounds[0][1])
    require(area.denominator == 1, 'Compiled envelope area is not integral in the current AABB contract')
    area = int(area)
    require(area == verification['compiled_world_xy_envelope_area'], 'Stored compiled footprint differs')
    gates = verification['ordinary_gate_checks']
    rectangles = [room['bounds'][0][:2] + room['bounds'][1][:2] for room in layout['rooms']]
    rectangles += [box[0][:2] + box[1][:2] for connection in layout['connections'] for box in connection['volumes']]
    actual_packing = packing(rectangles)
    require(actual_packing == verification['authored_room_passage_packing'], 'Stored packing differs from exported geometry')
    summary = {**current, 'room_graph': graph, 'full_span_room_gaps': full_span_room_gaps(layout),
               'compiled_xy_envelope_area': area, 'authored_room_passage_packing': actual_packing,
               'candidate_families': dict(Counter(c['family'] for c in trace['candidates'])),
               'rank_score': report['rank_score'], 'ordinary_pairs': gates['baseline_pairs'],
               'ordinary_missing_pairs': gates['baseline_missing_pairs'],
               'passage_closures_losing_pairs': sum(row['unreachable_pairs'] > 0 for row in gates['passages']),
               'findings': verification['analysis_findings'], 'levels': layout['levels'],
               'compiled_spawn_checks': spawns, 'map_sha256': binding['map_sha256'], 'bsp_sha256': verification['bsp_sha256']}
    return {'summary': summary, 'trace': trace, 'layout': layout, 'world_signature': json_hash(world_signature(bsp)),
            'nonspawn_entities': [entity for entity in bsp.entities if entity.get('classname') != 'info_player_deathmatch']}


def compare_case(case, stages):
    a, b, c = [stages[key]['summary'] for key in ('A', 'B', 'C')]
    candidates = candidate_intervention(stages['B']['trace'], stages['C']['trace'])
    pairs = pair_changes(b['compiled_spawn_checks']['pairs'], c['compiled_spawn_checks']['pairs'])
    invariant = {'candidate_pool': candidates['ok'],
                 'compiled_world_solids_equal': stages['B']['world_signature'] == stages['C']['world_signature'],
                 'nonspawn_compiled_entities_equal': stages['B']['nonspawn_entities'] == stages['C']['nonspawn_entities'],
                 'authored_fields_except_spawn_origins_equal': authored_spawn_neutral(stages['B']['layout']) == authored_spawn_neutral(stages['C']['layout']),
                 'blocked_compiled_pairs_preserved': pairs['blocked_pairs_preserved'],
                 'ordinary_pair_budget_unchanged': b['ordinary_pairs'] == c['ordinary_pairs'],
                 'ordinary_routes_complete': not c['ordinary_missing_pairs'] and c['passage_closures_losing_pairs'] == 0}
    before_stations = {s['id']: s for s in b['compiled_spawn_checks']['stations']}
    after_stations = {s['id']: s for s in c['compiled_spawn_checks']['stations']}
    moved = [ident for ident in sorted(before_stations) if before_stations[ident]['origin'] != after_stations[ident]['origin']]
    invariant['spawn_room_and_z_unchanged'] = all(before_stations[i]['room'] == after_stations[i]['room'] and
                                                before_stations[i]['origin'][2] == after_stations[i]['origin'][2]
                                                for i in before_stations)
    invariant['objective_nonworsening'] = pairs['after_objective'] <= pairs['before_objective']
    invariant['strict_objective_improvement_when_moved'] = not moved or pairs['after_objective'] < pairs['before_objective']
    return {'id': case['id'], 'group': case['group'], 'rooms': case['rooms'], 'seed': case['seed'],
            'layout_intent_requested': case['layout_intent_requested'], 'stages': {'A': a, 'B': b, 'C': c},
            'construction_A_to_B': {'selected_room_graph_changed': a['room_graph']['canonical_multigraph'] != b['room_graph']['canonical_multigraph'],
                                    'selected_seed_changed': a['candidate_seed'] != b['candidate_seed'],
                                    'compiled_envelope_change_fraction': b['compiled_xy_envelope_area']/a['compiled_xy_envelope_area']-1,
                                    'maximum_room_hops_change': b['room_graph']['maximum_room_hops']-a['room_graph']['maximum_room_hops']},
            'spawn_B_to_C': {'ok': all(invariant.values()), 'invariants': invariant, 'candidate_comparison': candidates,
                             'moved_spawn_ids': moved, **pairs}}


def summarize(rows):
    groups = []
    for group in ('primary', 'holdout', 'example'):
        for count in sorted({row['rooms'] for row in rows if row['group'] == group}):
            subset = [row for row in rows if row['group'] == group and row['rooms'] == count]
            groups.append({'group': group, 'rooms': count, 'requests': len(subset),
                           'stages': {stage: {'selected_families': dict(Counter(r['stages'][stage]['family'] for r in subset)),
                               'distinct_selected_room_graphs': len({r['stages'][stage]['room_graph']['canonical_multigraph'] for r in subset}),
                               'compiled_visible_spawn_pairs': sum(r['stages'][stage]['compiled_spawn_checks']['visible_pairs'] for r in subset),
                               'close_spawn_pairs': sum(r['stages'][stage]['compiled_spawn_checks']['close_pairs'] for r in subset),
                               'lighting_findings': sum(len(r['stages'][stage]['findings']['lighting']) for r in subset)} for stage in ('A', 'B', 'C')},
                           'A_to_B_footprint_changes': [r['construction_A_to_B']['compiled_envelope_change_fraction'] for r in subset],
                           'B_to_C_moved_spawns': sum(len(r['spawn_B_to_C']['moved_spawn_ids']) for r in subset)})
    return groups


def comparison(output):
    output = output.resolve(strict=True)
    evidence = Evidence(output.parents[1])
    cases = []
    for group, file in (('primary', 'cases.json'), ('holdout', 'holdouts.json'), ('example', 'hero.json')):
        value = evidence.read(output / file)
        cases += [{**case, 'group': group} for case in (value if isinstance(value, list) else [value])]
    require(len(cases) == 16 and len({c['id'] for c in cases}) == 16 and
            Counter(c['group'] for c in cases) == {'primary': 9, 'holdout': 6, 'example': 1}, 'Predeclared strata changed')
    for case in cases:
        text = evidence.bytes(evidence.root / case['prompt_file']).decode('utf-8-sig')
        require(text == case['prompt'], 'Predeclared prompt bytes differ from request')
    frozen = evidence.read(output / 'compiled-stages-before-comparison.json')
    require(len({p['path'] for p in frozen['files']}) == len(frozen['files']), 'Duplicate supplemental evidence binding')
    for pin in frozen['files']:
        evidence.bytes(evidence.root / pin['path'], pin['sha256'])
    records = {}
    for stage in ('baseline', 'intent-only', 'generated'):
        record = evidence.read(output / stage / 'record.json')
        require(len(record['rows']) == 16 and {r['id'] for r in record['rows']} == {c['id'] for c in cases}, 'Stage case set differs')
        records[stage] = {r['id']: r for r in record['rows']}
        for pin in record['inputs']:
            if 'snapshot' in pin:
                evidence.bytes(evidence.root / pin['snapshot'], pin['sha256'])
    rows = []
    for case in cases:
        stages = {letter: load_stage(evidence, output, stage, case, records[stage][case['id']])
                  for letter, stage in (('A', 'baseline'), ('B', 'intent-only'), ('C', 'generated'))}
        rows.append(compare_case(case, stages))
    for name in ('braid_compare.py', 'compare.py', 'topology_compare.py', 'ranking_compare.py', 'inspect.py'):
        evidence.bytes(evidence.root / 'tools/mp_portfolio' / name)
    for name in ('tools/map_study/binary.py', 'tools/map_recipes/drop_audit.py', 'tools/map_recipes/geometry.py',
                 'tools/map_recipes/packing_audit.py', 'tests/test_braid_compare.py',
                 'out/braid-portfolio/comparison-development.log'):
        evidence.bytes(evidence.root / name)
    result = {'schema_version': 1, 'ok': all(row['spawn_B_to_C']['ok'] for row in rows),
            'stage_definitions': {'A': 'Exact requested phrase, before typed construction intent was implemented.',
                                  'B': 'Typed wrapped construction constraint; original spawn refinement retained.',
                                  'C': 'Same constrained candidates; bounded spawn separation appended after original refinement.'},
            'cases': rows, 'strata': summarize(rows),
            'spawn_neutral_candidate_hash_matches': sum(r['spawn_B_to_C']['candidate_comparison']['spawn_neutral_hash_matches'] for r in rows),
            'candidate_count': 96,
            'inputs': [{'path': path, 'sha256': sha} for path, sha in sorted(evidence.files.items())],
            'limits': ['A→B changes construction and resulting placements together; graph and packing changes are descriptive, not quality gains.',
                       'B→C full candidate invariants use frozen C++ canonical hashes; only selected candidates are independently compiled here.',
                       'Fixed eye rays, standing hulls and support are local static world checks; no runtime spawn selection or multiplayer balance is tested.',
                       'Close-pair counts use the existing 256-unit Euclidean warning threshold; this is a procedural policy, not learned quality.',
                       'Ordinary path graphs and room hops do not certify continuous native journeys or movement time.',
                       'The predeclared hero and holdouts are separate from the nine primary requests; no regression is removed from these strata.']}
    return json_normalized(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'out/braid-portfolio')
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    result = comparison(args.output)
    path = args.output / 'comparison.json'
    if args.verify:
        require(json.loads(path.read_text(encoding='utf-8')) == result, 'Stored comparison differs from re-derived evidence')
    else:
        if path.exists():
            require(json.loads(path.read_text(encoding='utf-8')) == result, 'Refusing to replace different retained comparison')
        else:
            path.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    print(json.dumps({'ok': result['ok'], 'cases': len(result['cases']),
                      'spawn_neutral_candidate_hash_matches': result['spawn_neutral_candidate_hash_matches'],
                      'bound_files': len(result['inputs'])}))


if __name__ == '__main__':
    main()
