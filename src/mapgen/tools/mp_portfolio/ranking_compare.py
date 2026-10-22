"""Bind before/after ranking to unchanged candidates; descriptors are not quality."""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path

from tools.mp_portfolio.compare import graph_signature
from tools.mp_portfolio.topology_compare import describe, read, validate_trace

FEATURE_NAMES = ['connection_count_ratio', 'cycle_rank_ratio', 'raised_platform_count_ratio',
                 'room_volume_cv_scaled', 'branching_room_count_ratio', 'spawn_count_ratio',
                 'weapon_count_ratio', 'connected_room_center_distance_score']


def validate_scores(trace):
    if trace['feature_names'] != FEATURE_NAMES:
        raise ValueError('Unexpected feature definitions')
    def vector(value):
        if not isinstance(value, list) or len(value) != 8 or any(
                not isinstance(v, (int, float)) or isinstance(v, bool) or not math.isfinite(v) for v in value):
            raise ValueError('Nonfinite or malformed feature vector')
    vector(trace['feature_weights'])
    for candidate in trace['candidates']:
        vector(candidate['features'])
        vector(candidate['contributions'])
        if any(abs(v*w-c) > 0.000001 for v, w, c in zip(candidate['features'], trace['feature_weights'], candidate['contributions'], strict=True)):
            raise ValueError('Feature contributions do not match products')
        score = candidate['rank_score']
        if not isinstance(score, (int, float)) or isinstance(score, bool) or not math.isfinite(score) or abs(sum(candidate['contributions'])-score) > 0.000002:
            raise ValueError('Score differs from contributions beyond float tolerance')


def verify_pool_record(output):
    root = output.resolve().parents[1]
    record = read(output/'pools/record.json')
    if len(record['rows']) != 16 or len({r['id'] for r in record['rows']}) != 16 or not record['all_parity_pass']:
        raise ValueError('Invalid frozen pool record')
    for row in record['rows']:
        path = root/row['trace']
        if hashlib.sha256(path.read_bytes()).hexdigest() != row['sha256']:
            raise ValueError('Frozen candidate trace changed')
        if len(row['parity']) != 2:
            raise ValueError('Missing diagnostic parity evidence')
        for check in row['parity']:
            if not check['identical'] or any(hashlib.sha256((root/check[key]).read_bytes()).hexdigest() != check['sha256'] for key in ('baseline', 'current')):
                raise ValueError('Frozen diagnostic MAP/report parity changed')
    return record


def mixed_seed_key(seed):
    mask = (1 << 64)-1
    value = (seed + 0x9E3779B97F4A7C15) & mask
    value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & mask
    value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & mask
    return value ^ (value >> 31)


def validate_fixed_pool(before, after):
    validate_scores(before)
    validate_scores(after)
    old = {c['seed']: c for c in before['candidates']}
    new = {c['seed']: c for c in after['candidates']}
    if len(old) != 6 or len(new) != 6 or old.keys() != new.keys():
        raise ValueError('Candidate identities changed or duplicated')
    if before['feature_names'] != after['feature_names']:
        raise ValueError('Feature definitions changed')
    for seed in old:
        for field in ['rank_neutral_layout_sha256', 'features', 'family', 'rooms', 'connections']:
            if old[seed][field] != new[seed][field]:
                raise ValueError('Candidate pool changed: '+field)
    for i, (a, b) in enumerate(zip(before['feature_weights'], after['feature_weights'], strict=True)):
        if (i in (3, 7) and b != 0) or (i not in (3, 7) and a != b):
            raise ValueError('Unexpected weight intervention')
    if before['repair_passes'] or after['repair_passes']:
        raise ValueError('Repair intervention requires separate attribution')
    if after['selection_policy'] != 'compact_q2_mp_adequacy_v1' or after['tie_break_policy'] != 'minimum_splitmix64_seed_then_neutral_sha256_v1':
        raise ValueError('Unexpected selection policy')
    if any(c['tie_key'] != mixed_seed_key(c['seed']) for c in after['candidates']):
        raise ValueError('Tie keys differ from seed mixer')
    best = max(c['rank_score'] for c in after['candidates'])
    tied = [c for c in after['candidates'] if c['rank_score'] == best]
    selected = [c for c in after['candidates'] if c['selected']]
    if len(selected) != 1 or selected[0] not in tied:
        raise ValueError('Selected candidate is not a unique marked maximum')
    if set(after['tied_best_indices']) != {c['index'] for c in tied}:
        raise ValueError('Tie diagnostics differ from scores')
    if selected[0]['tie_key'] != min(c['tie_key'] for c in tied):
        raise ValueError('Selection differs from recorded tie policy')
    if selected[0]['seed'] != after['pipeline_selected_seed']:
        raise ValueError('Selected seed differs from pipeline')
    return {'unchanged_candidates': 6, 'exact_best_ties': len(tied),
            'selected_seed_changed': before['pipeline_selected_seed'] != after['pipeline_selected_seed'],
            'candidate_families': dict(Counter(c['family'] for c in after['candidates'])),
            'candidate_graphs': len({graph_signature(c) for c in after['candidates']})}


def compare(output):
    frozen = verify_pool_record(output)
    rows = []
    for group, cases in [('primary', read(output/'cases.json')), ('holdout', read(output/'holdouts.json')),
                         ('example', [read(output/'hero.json')])]:
        for case in cases:
            ident = case['id']
            binding = [r for r in frozen['rows'] if r['id'] == ident and r['group'] == group]
            if len(binding) != 1 or Path(binding[0]['trace']).parts[-2:] != (ident, 'candidate-trace.json'):
                raise ValueError('Frozen request/group binding differs')
            folder = output.parent/'lattice' if group == 'example' else output/'generated'/ident
            old, new = describe(output/'baseline'/ident), describe(folder)
            before, after = read(output/'pools'/ident/'candidate-trace.json'), read(folder/'candidate-trace.json')
            validate_trace(before, old)
            validate_trace(after, new)
            intervention = validate_fixed_pool(before, after)
            rows.append({'id': ident, 'group': group, 'before': old, 'after': new,
                         'intervention': intervention,
                         'envelope_change_fraction': new['compiled_xy_envelope_area']/old['compiled_xy_envelope_area']-1})
    variety = []
    for group in ('primary', 'holdout'):
        for count in sorted({r['before']['room_graph']['room_count'] for r in rows if r['group'] == group}):
            subset = [r for r in rows if r['group'] == group and r['before']['room_graph']['room_count'] == count]
            variety.append({'group': group, 'rooms': count, 'requests': len(subset),
                            **{phase: {'selected_graphs': len({r[phase]['room_graph']['canonical_multigraph'] for r in subset}),
                                       'selected_families': dict(Counter(r[phase]['family'] for r in subset))}
                               for phase in ('before', 'after')}})
    result = {'schema_version': 1, 'scope': 'Fixed candidate geometry, changed scoped ranking only. Primary and predeclared holdout requests are reported separately; all holdout seeds are odd. No learned weights, family quotas or candidate replacement.',
              'cases': rows, 'selected_variety': variety, 'unchanged_candidate_count': sum(r['intervention']['unchanged_candidates'] for r in rows),
              'limitations': ['Remaining count features are adequacy heuristics, not candidate quality gates.',
                              'Only selected candidates are compiled and inspected; this does not certify every offered candidate.',
                              'Canonical graph variety and occupied envelope are descriptors, not balance or combat depth.',
                              'The small seed set is a reproducibility check, not a statistical generalization claim.']}
    (output/'comparison.json').write_text(json.dumps(result, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    print(json.dumps({'unchanged_candidates': result['unchanged_candidate_count'], 'variety': variety}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    compare(parser.parse_args().output)
