import copy
from fractions import Fraction
import json
import unittest
from unittest.mock import patch

from tools.mp_portfolio.braid_compare import (
    authored_spawn_neutral, bound_json, candidate_intervention, compiled_spawns, digest, json_hash,
    json_normalized, pair_changes, summarize,
)


def pool():
    return {'candidate_count': 6, 'selected_index': 0, 'pipeline_selected_seed': 7, 'repair_passes': 0,
            'feature_names': ['count'], 'feature_weights': [1], 'selection_policy': 'policy',
            'tie_break_policy': 'tie', 'tied_best_indices': list(range(6)), 'layout_intent': 'wrapped_center',
            'candidates': [{'seed': i+7, 'index': i, 'selected': i == 0,
                'spawn_origin_neutral_layout_sha256': str(i)*64, 'rank_neutral_layout_sha256': 'original',
                'features': [1], 'contributions': [1], 'rank_score': 1, 'family': 'pinwheel',
                'rooms': [{'id': 1}], 'connections': [], 'tie_key': i, 'tie_hash': ''} for i in range(6)]}


class BraidComparisonControls(unittest.TestCase):
    def test_retained_json_roundtrip_preserves_verification_equality(self):
        result = {'room_graph': {'passage_degree': {1: 4, 2: 2}}, 'tuple': (1, 2)}
        normalized = json_normalized(result)
        self.assertEqual(normalized, json.loads(json.dumps(normalized)))
        self.assertEqual(normalized['room_graph']['passage_degree'], {'1': 4, '2': 2})

    def test_settling_below_origin_is_not_direct_refined_support(self):
        class World:
            entities = [{'classname': 'info_player_deathmatch', 'origin': '100 100 24'}]

            @staticmethod
            def trace(a, b):
                return 1
        layout = {'placements': [{'id': 1, 'role': 1, 'origin': [100, 100, 24]}]}
        with patch('tools.mp_portfolio.braid_compare.supported_anchor', return_value={'supported_feet': [100, 100, -16]}):
            with self.assertRaisesRegex(ValueError, 'authored feet'):
                compiled_spawns(layout, World(), [])

    def test_exact_world_planes_do_not_round_away_rational_differences(self):
        self.assertEqual(json_hash(Fraction(1, 2)), json_hash(Fraction(2, 4)))
        self.assertNotEqual(json_hash(Fraction(1, 2)), json_hash(Fraction(2**60+1, 2**61)))

    def test_stale_bound_bytes_rejected_even_when_valid_json(self):
        raw = json.dumps({'score': 1}).encode()
        self.assertEqual(bound_json(raw, digest(raw), 'fixture'), {'score': 1})
        with self.assertRaisesRegex(ValueError, 'Changed frozen input'):
            bound_json(json.dumps({'score': 2}).encode(), digest(raw), 'fixture')

    def test_changed_full_hash_is_allowed_when_spawn_neutral_hash_matches(self):
        before, after = pool(), pool()
        after['candidates'][0]['rank_neutral_layout_sha256'] = 'spawn moved'
        result = candidate_intervention(before, after)
        self.assertTrue(result['ok'])
        self.assertEqual(result['spawn_neutral_hash_matches'], 6)

    def test_changed_candidate_geometry_retained_as_failed_invariant(self):
        before, after = pool(), pool()
        after['candidates'][3]['spawn_origin_neutral_layout_sha256'] = 'changed'
        result = candidate_intervention(before, after)
        self.assertFalse(result['ok'])
        self.assertEqual(result['spawn_neutral_hash_matches'], 5)
        self.assertIn({'seed': 10, 'field': 'spawn_origin_neutral_layout_sha256'}, result['violations'])

    def test_same_selection_does_not_hide_changed_features_or_scores(self):
        for field, value in [('features', [2]), ('contributions', [2]), ('rank_score', 2)]:
            before, after = pool(), pool()
            after['candidates'][2][field] = value
            self.assertFalse(candidate_intervention(before, after)['ok'])

    def test_changed_selector_is_not_spawn_only(self):
        before, after = pool(), pool()
        after['pipeline_selected_seed'] = 8
        result = candidate_intervention(before, after)
        self.assertFalse(result['ok'])
        self.assertFalse(result['selected_seed_unchanged'])

    def test_duplicate_or_missing_candidate_identity_rejected(self):
        before, after = pool(), pool()
        after['candidates'][1]['seed'] = 7
        with self.assertRaisesRegex(ValueError, 'identities'):
            candidate_intervention(before, after)

    def test_equal_visible_totals_cannot_hide_pair_exchange(self):
        before = [{'ids': [1, 2], 'visible': False, 'distance_squared': 10000},
                  {'ids': [1, 3], 'visible': True, 'distance_squared': 90000}]
        after = copy.deepcopy(before)
        after[0]['visible'], after[1]['visible'] = True, False
        result = pair_changes(before, after)
        self.assertFalse(result['blocked_pairs_preserved'])
        self.assertEqual(result['newly_visible_pairs'], [[1, 2]])

    def test_exact_separation_boundary_and_regression_are_retained(self):
        before = [{'ids': [1, 2], 'visible': False, 'distance_squared': 256**2}]
        after = [{'ids': [1, 2], 'visible': False, 'distance_squared': 256**2-1}]
        result = pair_changes(before, after)
        self.assertEqual((result['before_close_pairs'], result['after_close_pairs']), (0, 1))
        self.assertEqual(result['after_squared_deficit'], 1)
        self.assertGreater(result['after_objective'], result['before_objective'])

    def test_duplicate_or_relabelled_pairs_rejected(self):
        pair = {'ids': [1, 2], 'visible': False, 'distance_squared': 10000}
        with self.assertRaisesRegex(ValueError, 'identities'):
            pair_changes([pair, pair], [pair])
        with self.assertRaisesRegex(ValueError, 'identities'):
            pair_changes([pair], [{**pair, 'ids': [1, 3]}])

    def test_spawn_neutral_export_preserves_nonspawn_fields_and_angle(self):
        before = {'placements': [{'id': 1, 'role': 1, 'origin': [0, 0, 24], 'angle_degrees': 90},
                                 {'id': 2, 'role': 4, 'origin': [200, 0, 24]}], 'rooms': []}
        after = copy.deepcopy(before)
        after['placements'][0]['origin'] = [100, 0, 24]
        self.assertEqual(authored_spawn_neutral(before), authored_spawn_neutral(after))
        after['placements'][0]['angle_degrees'] = 180
        self.assertNotEqual(authored_spawn_neutral(before), authored_spawn_neutral(after))
        after = copy.deepcopy(before)
        after['placements'][1]['origin'] = [300, 0, 24]
        self.assertNotEqual(authored_spawn_neutral(before), authored_spawn_neutral(after))
        self.assertEqual(before['placements'][0]['origin'], [0, 0, 24])

    def test_primary_holdout_and_hero_are_not_pooled(self):
        rows = []
        for group, graph in [('primary', 'a'), ('holdout', 'b'), ('example', 'c')]:
            stage = {'family': 'pinwheel', 'room_graph': {'canonical_multigraph': graph},
                     'compiled_spawn_checks': {'visible_pairs': 0, 'close_pairs': 1}, 'findings': {'lighting': [1]}}
            rows.append({'group': group, 'rooms': 8, 'stages': {key: stage for key in ('A', 'B', 'C')},
                         'construction_A_to_B': {'compiled_envelope_change_fraction': .1},
                         'spawn_B_to_C': {'moved_spawn_ids': []}})
        result = summarize(rows)
        self.assertEqual([r['group'] for r in result], ['primary', 'holdout', 'example'])
        self.assertTrue(all(r['requests'] == 1 and r['A_to_B_footprint_changes'] == [.1] for r in result))


if __name__ == '__main__':
    unittest.main()
