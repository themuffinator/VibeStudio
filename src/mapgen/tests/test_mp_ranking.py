import copy
import hashlib
from pathlib import Path
import unittest
from unittest.mock import patch
from tools.mp_portfolio.ranking_compare import FEATURE_NAMES, mixed_seed_key, validate_fixed_pool, verify_pool_record


def fixture():
    before = {'feature_names': FEATURE_NAMES.copy(), 'feature_weights': [1]*8, 'repair_passes': 0,
              'pipeline_selected_seed': 5, 'candidates': [
                  {'seed': i, 'index': i, 'rank_neutral_layout_sha256': str(i), 'features': [1]*8,
                   'family': 'rows', 'rooms': [{'id': 0}, {'id': 1}], 'connections': [{'from': 0, 'to': 1}],
                   'rank_score': 8, 'contributions': [1]*8, 'selected': i == 5} for i in range(6)]}
    after = copy.deepcopy(before)
    after['feature_weights'][3] = after['feature_weights'][7] = 0
    after['pipeline_selected_seed'] = 0
    after['tied_best_indices'] = list(range(6))
    after['selection_policy'] = 'compact_q2_mp_adequacy_v1'
    after['tie_break_policy'] = 'minimum_splitmix64_seed_then_neutral_sha256_v1'
    winner = min(range(6), key=mixed_seed_key)
    after['pipeline_selected_seed'] = winner
    for c in after['candidates']:
        c.update(rank_score=6, tie_key=mixed_seed_key(c['seed']), selected=c['seed'] == winner)
        c['contributions'][3] = c['contributions'][7] = 0
    return before, after


class FixedPoolControls(unittest.TestCase):
    def test_rank_change_without_geometry_change(self):
        self.assertEqual(validate_fixed_pool(*fixture())['unchanged_candidates'], 6)

    def test_geometry_change_rejected(self):
        a, b = fixture()
        b['candidates'][0]['rank_neutral_layout_sha256'] = 'changed'
        with self.assertRaisesRegex(ValueError, 'pool changed'):
            validate_fixed_pool(a, b)

    def test_feature_change_rejected_even_if_hash_unchanged(self):
        a, b = fixture()
        b['candidates'][0]['features'][0] = 0
        with self.assertRaisesRegex(ValueError, 'products'):
            validate_fixed_pool(a, b)

    def test_duplicate_candidate_rejected(self):
        a, b = fixture()
        b['candidates'][0]['seed'] = 1
        with self.assertRaisesRegex(ValueError, 'identities'):
            validate_fixed_pool(a, b)

    def test_changed_adequacy_weight_rejected(self):
        a, b = fixture()
        b['feature_weights'][1] = 0
        with self.assertRaisesRegex(ValueError, 'products'):
            validate_fixed_pool(a, b)

    def test_post_ranking_repair_is_not_rank_only(self):
        a, b = fixture()
        b['repair_passes'] = 1
        with self.assertRaisesRegex(ValueError, 'Repair intervention'):
            validate_fixed_pool(a, b)

    def test_wrong_tie_selection_rejected(self):
        a, b = fixture()
        winner = next(c for c in b['candidates'] if c['selected'])
        winner['selected'] = False
        b['candidates'][(winner['index']+1)%6]['selected'] = True
        with self.assertRaisesRegex(ValueError, 'tie policy'):
            validate_fixed_pool(a, b)

    def test_forged_tie_key_rejected(self):
        a, b = fixture()
        b['candidates'][0]['tie_key'] = 0
        with self.assertRaisesRegex(ValueError, 'seed mixer'):
            validate_fixed_pool(a, b)

    def test_forged_scores_rejected(self):
        a, b = fixture()
        for c in b['candidates']:
            c['rank_score'] = 999
        with self.assertRaisesRegex(ValueError, 'Score differs'):
            validate_fixed_pool(a, b)

    def test_forged_products_rejected(self):
        a, b = fixture()
        b['candidates'][0]['contributions'] = [999]*8
        with self.assertRaisesRegex(ValueError, 'products'):
            validate_fixed_pool(a, b)

    def test_reordered_feature_semantics_rejected(self):
        a, b = fixture()
        a['feature_names'].reverse()
        b['feature_names'].reverse()
        with self.assertRaisesRegex(ValueError, 'definitions'):
            validate_fixed_pool(a, b)

    def test_nonfinite_feature_rejected(self):
        a, b = fixture()
        b['candidates'][0]['features'][0] = float('nan')
        with self.assertRaisesRegex(ValueError, 'Nonfinite'):
            validate_fixed_pool(a, b)

    def test_frozen_trace_and_parity_pins(self):
        blob = b'fixed bytes'
        sha = hashlib.sha256(blob).hexdigest()
        record = {'all_parity_pass': True, 'rows': [
            {'id': str(i), 'trace': 'trace'+str(i), 'sha256': sha,
             'parity': [{'baseline': 'baseline'+str(i)+s, 'current': 'current'+str(i)+s, 'sha256': sha, 'identical': True}
                        for s in ('.map', '.qmg.json')]} for i in range(16)]}
        output = Path(__file__).resolve().parents[1]/'out/lattice-portfolio'
        with patch('tools.mp_portfolio.ranking_compare.read', return_value=record), patch.object(Path, 'read_bytes', return_value=blob):
            self.assertEqual(len(verify_pool_record(output)['rows']), 16)
        with patch('tools.mp_portfolio.ranking_compare.read', return_value=record), patch.object(Path, 'read_bytes', return_value=b'changed'):
            with self.assertRaisesRegex(ValueError, 'trace changed'):
                verify_pool_record(output)
        def altered_parity(path):
            return b'changed' if path.name.startswith('baseline') else blob
        with patch('tools.mp_portfolio.ranking_compare.read', return_value=record), patch.object(Path, 'read_bytes', altered_parity):
            with self.assertRaisesRegex(ValueError, 'parity changed'):
                verify_pool_record(output)


if __name__ == '__main__':
    unittest.main()
