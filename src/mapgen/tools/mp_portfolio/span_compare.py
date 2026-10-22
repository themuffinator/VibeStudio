"""Compare fixed Span populations without scoring a new shape as better gameplay."""
from collections import Counter
import argparse
import hashlib
import json
import math
from pathlib import Path
import re

from tools.mp_portfolio.compare import graph_signature, motif
from tools.map_recipes.gate_audit import audit_gates

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'out/span-portfolio'
TAG = 'opposed_stair_bridge'
MOTIFS = {'raised_crosswalk', 'horseshoe_gallery', TAG}


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_file_pin(file):
    path = ROOT / file['path']
    require(path.stat().st_size == file['bytes'] and sha(path) == file['sha256'], 'Changed selected export bytes')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def indexed(rows):
    result = {r['id']: r for r in rows}
    require(len(result) == len(rows), 'Duplicate IDs')
    return result


def inside(inner, outer):
    return all(outer[0][k] <= inner[0][k] < inner[1][k] <= outer[1][k] for k in range(3))


def solid_signatures(candidate, room, excluded=()):
    return Counter((tuple(s['bounds'][0]), tuple(s['bounds'][1]), s['purpose_enum'])
                   for s in candidate['solids'] if s['id'] not in excluded and inside(s['bounds'], room['bounds']))


def validate_trace(trace):
    candidates = trace['candidates']
    require(trace['candidate_count'] == len(candidates) == 6, 'Wrong candidate population')
    require([c['index'] for c in candidates] == list(range(6)), 'Candidate indices must be ordered0..5')
    seeds = [c['seed'] for c in candidates]
    require(all(type(seed) is int and 0 <= seed < 2**64 for seed in seeds) and len(set(seeds)) == 6,
            'Candidate seeds must be six distinct uint64 values')
    selected = trace['selected_index']
    require(type(selected) is int and selected in range(6) and
            all(type(c['selected']) is bool for c in candidates) and
            [c['index'] for c in candidates if c['selected']] == [selected], 'Selected candidate flags/index differ')
    require(trace['pipeline_selected_seed'] == candidates[selected]['seed'], 'Selected pipeline seed differs')
    require(trace['repair_passes'] == 0, 'This selected-source comparison is scoped to zero-repair exports')
    require(trace['pipeline_layout_sha256'] == candidates[selected]['layout_sha256'], 'Selected source/pipeline hash differs')
    for candidate in candidates:
        require(re.fullmatch('[0-9a-f]{64}', candidate['layout_sha256']) and math.isfinite(candidate['rank_score']),
                'Invalid candidate hash or rank score')
        indexed(candidate['rooms'])
        indexed(candidate['connections'])
        indexed(candidate['solids'])


def upper_link_parts(candidate):
    """Bind only the helper's appended three slabs; do not exempt entire linked rooms.

    This is a source representation check. Physical clearance, support and
    ordinary access remain separate compiled/native certificates.
    """
    elevated = [c for c in candidate['connections'] if any(v[0][2] > 0 for v in c['volumes'])]
    linked = {r['id'] for r in candidate['rooms'] if 'inter_room_upper_link' in r['tags']}
    if not elevated:
        require(not linked, 'Upper-link room tags lack an elevated passage')
        return {'slab_ids': [], 'connection': None, 'endpoint_rooms': []}
    require(len(elevated) == 1 and elevated[0] == candidate['connections'][-1] and
            len(elevated[0]['volumes']) == 1 and len(candidate['solids']) >= 4, 'Unexpected upper-link population/order')
    connection = elevated[0]
    rooms = indexed(candidate['rooms'])
    require(connection['from'] != connection['to'] and linked == {connection['from'], connection['to']},
            'Upper-link endpoint room tags differ')
    a, b = rooms[connection['from']], rooms[connection['to']]
    passage = connection['volumes'][0]
    low, high = passage
    lengths = [high[i] - low[i] for i in range(2)]
    require(sorted(lengths) == [48, 160] and low[2] in (128, 192) and high[2] == low[2] + 192,
            'Upper passage dimensions/elevation differ')
    axis = lengths.index(48)
    cross = 1 - axis
    require(a['bounds'][1][axis] == low[axis] and b['bounds'][0][axis] == high[axis], 'Upper passage does not face-join its endpoint rooms')
    original, slabs = candidate['solids'][:-3], candidate['solids'][-3:]
    maximum = max(s['id'] for s in original)
    require([s['id'] for s in slabs] == list(range(maximum + 1, maximum + 4)) and
            all(s['purpose_enum'] == 1 for s in slabs), 'Upper slabs are not the final three raised platforms')
    for slab in slabs:
        lo, hi = slab['bounds']
        require(lo[2] == low[2] - 24 and hi[2] == low[2] and
                lo[cross] == low[cross] and hi[cross] == high[cross], 'Upper slab thickness or full width differs')
    first, last, cap = [s['bounds'] for s in slabs]
    require(cap == [[*low[:2], low[2] - 24], [*high[:2], low[2]]], 'Upper portal cap differs from its passage footprint')
    require(inside(first, a['bounds']) and inside(last, b['bounds']) and
            first[1][axis] == low[axis] and last[0][axis] == high[axis], 'Upper branch does not join room to passage')
    deck_ids = []
    for room, branch, before in ((a, first, True), (b, last, False)):
        found = [s['id'] for s in original if s['purpose_enum'] == 1 and inside(s['bounds'], room['bounds']) and
                 s['bounds'][1][2] == low[2] and s['bounds'][0][2] == low[2] - 24 and
                 s['bounds'][0][cross] <= low[cross] and s['bounds'][1][cross] >= high[cross] and
                 s['bounds'][1][axis] - s['bounds'][0][axis] >= 32 and
                 (s['bounds'][1][axis] == branch[0][axis] if before else s['bounds'][0][axis] == branch[1][axis])]
        require(found, 'Upper branch does not butt-join an original full-width deck')
        deck_ids.append(found)
    return {'slab_ids': [s['id'] for s in slabs], 'connection': connection['id'],
            'endpoint_rooms': [a['id'], b['id']], 'supporting_original_deck_ids': deck_ids,
            'slab_bounds': [s['bounds'] for s in slabs]}


def lower_connections(candidate):
    # Actual physical free volumes, with IDs ignored because appended upper links
    # may differ. Endpoint IDs and every dimension remain bound.
    return Counter((c['from'], c['to'], tuple(v[0]), tuple(v[1]))
                   for c in candidate['connections'] for v in c['volumes'] if v[0][2] == 0)


def compare_trace(before, after, control=False):
    validate_trace(before)
    validate_trace(after)
    for key in ('feature_weights', 'selection_policy', 'tie_break_policy', 'planned_verticality'):
        require(before[key] == after[key], 'Unexpected ranking/request change: ' + key)
    require([c['seed'] for c in before['candidates']] == [c['seed'] for c in after['candidates']], 'Candidate order/seeds differ')
    observations = []
    for old, new in zip(before['candidates'], after['candidates']):
        require(old['index'] == new['index'], 'Candidate index changed')
        a, b = indexed(old['rooms']), indexed(new['rooms'])
        require(a.keys() == b.keys(), 'Room population changed')
        require(lower_connections(old) == lower_connections(new), 'Ordinary lower passage geometry changed')
        old_link, new_link = upper_link_parts(old), upper_link_parts(new)
        replaced = []
        for room_id, prior in a.items():
            current = b[room_id]
            require(prior['bounds'] == current['bounds'], 'Room envelope changed')
            old_tags, new_tags = set(prior['tags']), set(current['tags'])
            require((old_tags - MOTIFS - {'inter_room_upper_link'}) == (new_tags - MOTIFS - {'inter_room_upper_link'}),
                    'Room role, family or rotation changed')
            if TAG in new_tags:
                require(TAG not in old_tags and len(old_tags & MOTIFS) == 1, 'Bridge replaced a non-elevated room')
                old_heights = {s['bounds'][1][2] for s in old['solids'] if s['purpose_enum'] == 1 and
                               s['id'] not in old_link['slab_ids'] and inside(s['bounds'], prior['bounds'])}
                new_heights = {s['bounds'][1][2] for s in new['solids'] if s['purpose_enum'] == 1 and
                               s['id'] not in new_link['slab_ids'] and inside(s['bounds'], current['bounds'])}
                require(old_heights == new_heights and len(new_heights) == 1, 'Motif elevation changed')
                replaced.append(room_id)
            else:
                require((old_tags & MOTIFS) == (new_tags & MOTIFS), 'Unexpected other motif replacement')
                require(solid_signatures(old, prior, old_link['slab_ids']) == solid_signatures(new, current, new_link['slab_ids']),
                        'Unreplaced room physical solids changed')
        if control:
            require(not replaced and old['layout_sha256'] == new['layout_sha256'], 'Flat control candidate changed')
        observations.append({'index': old['index'], 'seed': old['seed'], 'replaced_rooms': replaced,
                             'before_rank_score': old['rank_score'], 'after_rank_score': new['rank_score'],
                             'before_selected': old['selected'], 'after_selected': new['selected'],
                             'before_upper_link': old_link, 'after_upper_link': new_link,
                             'before_upper_passages': sum(v[0][2] > 0 for c in old['connections'] for v in c['volumes']),
                             'after_upper_passages': sum(v[0][2] > 0 for c in new['connections'] for v in c['volumes'])})
    return {'candidate_count': 6, 'geometry_conservation_passed': True,
            'offered_bridge_candidates': sum(bool(c['replaced_rooms']) for c in observations),
            'offered_bridge_rooms': sum(len(c['replaced_rooms']) for c in observations),
            'selected_seed_changed': before['pipeline_selected_seed'] != after['pipeline_selected_seed'],
            'observations': observations}


def bind_selected_export(layout, report, trace):
    validate_trace(trace)
    selected = trace['candidates'][trace['selected_index']]
    require(report['repair_passes'] == [], 'This export binding requires zero repair passes')
    require(layout['layout_sha256'] == report['layout_sha256'] == trace['pipeline_layout_sha256'] and
            layout['candidate_seed'] == trace['pipeline_selected_seed'] and
            report['candidate_seed'] == str(trace['pipeline_selected_seed']), 'Selected export identity differs')
    captured = {
        'rooms': [{key: room[key] for key in ('id', 'bounds', 'tags')} for room in layout['rooms']],
        'connections': [{key: c[key] for key in ('id', 'from', 'to', 'volumes')} for c in layout['connections']],
        'solids': [{'id': s['id'], 'bounds': s['bounds'], 'purpose_enum': s['purpose']} for s in layout['solids']],
    }
    for key, rows in captured.items():
        require(rows == selected[key], 'Selected export captured ' + key + ' differ from trace')


def verify_export_bindings(stage):
    path = OUT / ('export-bindings.json' if stage == 'generated' else 'export-bindings-' + stage + '.json')
    binding = read(path)
    require(binding['protocol_sha256'] == sha(OUT / 'protocol.json'), 'Export binding declaration changed')
    require(set(binding['stage_records']) == {'baseline', stage} and
            all(digest == sha(OUT / name / 'record.json') for name, digest in binding['stage_records'].items()),
            'Export binding stage record changed')
    expected = {(name, row['id']) for name in ('baseline', stage) for row in read(OUT / name / 'record.json')['rows']}
    require(len(binding['exports']) == len(expected) == 28 and
            {(r['stage'], r['id']) for r in binding['exports']} == expected, 'Wrong export binding population')
    paths = set()
    for export in binding['exports']:
        require(len(export['files']) == 2, 'Export needs both layout and report byte pins')
        row = next(r for r in read(OUT / export['stage'] / 'record.json')['rows'] if r['id'] == export['id'])
        folder = ROOT / row['folder']
        wanted = {folder / 'layout.json', folder / (row['map_name'] + '.qmg.json')}
        require({ROOT / r['path'] for r in export['files']} == wanted, 'Export pins refer to other files')
        for file in export['files']:
            target = ROOT / file['path']
            require(target not in paths, 'Duplicate selected export file')
            verify_file_pin(file)
            paths.add(target)
    return {'path': path.relative_to(ROOT).as_posix(), 'sha256': sha(path), 'exports': 28, 'files': len(paths)}


def compare(stage='generated', require_compiled=True):
    protocol = read(OUT / 'protocol.json')
    records = [read(OUT / name / 'record.json') for name in ('baseline', stage)]
    bindings = verify_export_bindings(stage)
    require(all(record['protocol_sha256'] == sha(OUT / 'protocol.json') for record in records), 'Stage declaration changed')
    rows = []
    for case, a, b in zip(protocol['cases'], records[0]['rows'], records[1]['rows']):
        require(a['id'] == b['id'] == case['id'], 'Case order differs')
        folders = [ROOT / r['folder'] for r in (a,b)]
        layouts = [read(f / 'layout.json') for f in folders]
        reports = [read(f / (case['map_name'] + '.qmg.json')) for f in folders]
        traces = [read(f / 'candidate-trace.json') for f in folders]
        for folder, record, layout, report, trace in zip(folders, (a,b), layouts, reports, traces):
            require(sha(folder / (case['map_name'] + '.map')) == record['map_sha256'] and
                    sha(folder / 'candidate-trace.json') == record['trace_sha256'], 'Stage artifact changed')
            require(layout['layout_sha256'] == trace['pipeline_layout_sha256'] == report['layout_sha256'], 'Selected identities differ')
            require(record['layout_sha256'] == layout['layout_sha256'] and record['selected_seed'] == trace['pipeline_selected_seed'],
                    'Stage selected identity differs')
            bind_selected_export(layout, report, trace)
            require([c['seed'] for c in trace['candidates']] ==
                    [(case['requested_seed'] + i * 0x9E3779B97F4A7C15) % 2**64 for i in range(6)],
                    'Trace seeds differ from declared request sequence')
        trace_change = compare_trace(*traces, control=case['group'] == 'low_verticality_control')
        gates = [audit_gates(layout, report) for layout, report in zip(layouts, reports)]
        require(all(not g['baseline_missing_pairs'] for g in gates), 'Ordinary selected anchor graph disconnected')
        row = {'id': case['id'], 'group': case['group'], 'hero': case['hero'], 'trace': trace_change,
               'before_map_sha256': a['map_sha256'], 'after_map_sha256': b['map_sha256'],
               'selected_bridge_rooms': [r['id'] for r in layouts[1]['rooms'] if TAG in r['tags']],
               'before_motifs': dict(Counter(motif(r) for r in layouts[0]['rooms'])),
               'after_motifs': dict(Counter(motif(r) for r in layouts[1]['rooms'])),
               'before_graph': graph_signature(layouts[0]), 'after_graph': graph_signature(layouts[1]),
               'before_colored_graph': graph_signature(layouts[0], colored=True), 'after_colored_graph': graph_signature(layouts[1], colored=True),
               'before_quality': reports[0]['analysis']['quality']['score'], 'after_quality': reports[1]['analysis']['quality']['score'],
               'ordinary_pairs': [g['baseline_pairs'] for g in gates],
               'single_passage_missing_pair_counts': [[x['unreachable_pairs'] for x in g['passages']] for g in gates]}
        if case['group'] == 'low_verticality_control':
            require(a['map_sha256'] == b['map_sha256'], 'Flat selected MAP changed')
        if require_compiled:
            verifications = [read(f / 'verification.json') for f in folders]
            require(all(v['ok'] and v['sealed'] and v['full_vis'] and v['extra4_one_bounce_lighting'] for v in verifications), 'Selected compiler gates failed')
            for folder, record, verification in zip(folders, (a,b), verifications):
                require(verification['bsp_sha256'] == sha(folder / (case['map_name'] + '.bsp')) and
                        verification['source_sha256'] == record['map_sha256'] and
                        verification['layout_sha256'] == record['layout_sha256'], 'Compiled source/BSP binding differs')
            row['compiled'] = {'both_verified': True,
                'before_packing': verifications[0]['authored_room_passage_packing'],
                'after_packing': verifications[1]['authored_room_passage_packing'],
                'before_world_bounds': verifications[0]['compiled_world_bounds'],
                'after_world_bounds': verifications[1]['compiled_world_bounds']}
        rows.append(row)
    require(len(rows) == len(protocol['cases']) == len(records[0]['rows']) == len(records[1]['rows']) == 14,
            'Incomplete declared portfolio')
    require(len({r['id'] for r in rows}) == 14 and Counter(r['group'] for r in rows) ==
            {'primary': 9, 'holdout': 2, 'low_verticality_control': 3} and
            [r['id'] for r in rows if r['hero']] == ['rooms6-seed701'], 'Wrong declared strata or hero population')
    groups = {}
    for name in ('primary', 'holdout', 'low_verticality_control'):
        group = [r for r in rows if r['group'] == name]
        groups[name] = {'cases': len(group), 'candidate_observations': 6*len(group),
            'offered_bridge_candidates': sum(r['trace']['offered_bridge_candidates'] for r in group),
            'selected_maps_with_bridge': sum(bool(r['selected_bridge_rooms']) for r in group),
            'selected_seed_changes': sum(r['trace']['selected_seed_changed'] for r in group),
            'same_selected_map_bytes': sum(r['before_map_sha256'] == r['after_map_sha256'] for r in group),
            'before_warnings': sum(r['before_quality']['warnings'] for r in group),
            'after_warnings': sum(r['after_quality']['warnings'] for r in group)}
    return {'schema_version': 1, 'stage': stage, 'ok': True, 'compiled_checks_required': require_compiled,
            'protocol_sha256': sha(OUT / 'protocol.json'), 'export_byte_bindings': bindings, 'groups': groups, 'cases': rows,
            'scope': 'Matched per-candidate original room envelopes, rotations and ordinary lower passage volumes; unreplaced room-local solids retain captured bounds/purpose after excluding only structurally bound final upper-link slabs. Zero-repair selected exports match captured trace fields and frozen layout/report bytes. Materials, ownership, placements and names are not captured by this trace conservation check. Optional upper links and selection are reported as downstream effects. No improved gameplay or native journey claim.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', default='generated', choices=['generated', 'generated-v2'])
    parser.add_argument('--source-only', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = compare(args.stage, not args.source_only)
    with args.output.open('x', encoding='utf-8') as stream:
        stream.write(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'ok': True, 'groups': result['groups']}))
