"""Conservation controls for the same requested Span candidate population."""
from copy import deepcopy
import hashlib
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from tools.mp_portfolio.span_compare import bind_selected_export, compare_trace, verify_file_pin


def fixtures():
    candidate = {'index': 0, 'seed': 10, 'selected': True, 'layout_sha256': 'a'*64, 'rank_score': 0,
                 'rooms': [{'id': 1, 'bounds': [[0,0,0],[800,800,384]],
                            'tags': ['compact_mp','raised_crosswalk','motif_rotation_0']},
                           {'id': 2, 'bounds': [[848,0,0],[1648,800,384]],
                            'tags': ['compact_mp','offset_barrier']}],
                 'connections': [{'id': 1, 'from': 1, 'to': 2, 'volumes': [[[800,300,0],[848,460,192]]]}],
                 'solids': [{'id': 1, 'bounds': [[64,352,104],[736,512,128]], 'purpose_enum': 1},
                            {'id': 2, 'bounds': [[1100,200,0],[1200,300,112]], 'purpose_enum': 0}]}
    before = {'candidate_count': 6, 'pipeline_selected_seed': 10, 'feature_weights': [1,2],
              'selection_policy': 'test', 'tie_break_policy': 'seed', 'planned_verticality': .95, 'candidates': [],
              'selected_index': 0, 'pipeline_layout_sha256': 'a'*64, 'repair_passes': 0}
    for i in range(6):
        row = deepcopy(candidate); row.update(index=i, seed=10+i, selected=i==0)
        before['candidates'].append(row)
    after = deepcopy(before)
    after['pipeline_layout_sha256'] = 'b'*64
    for row in after['candidates']:
        row['rooms'][0]['tags'][1] = 'opposed_stair_bridge'
        row['layout_sha256'] = 'b'*64
    return before, after


def add_upper_link(before, after):
    for trace in (before, after):
        for row in trace['candidates']:
            row['solids'].append({'id': 3, 'bounds': [[912,352,104],[1584,512,128]], 'purpose_enum': 1})
    for row in after['candidates']:
        for room in row['rooms']:
            room['tags'].append('inter_room_upper_link')
        row['connections'].append({'id': 2, 'from': 1, 'to': 2, 'volumes': [[[800,352,128],[848,512,320]]]})
        for ident,bounds in ((4,[[736,352,104],[800,512,128]]), (5,[[848,352,104],[912,512,128]]),
                             (6,[[800,352,104],[848,512,128]])):
            row['solids'].append({'id': ident, 'bounds': bounds, 'purpose_enum': 1})


def selected_export(trace):
    c = trace['candidates'][trace['selected_index']]
    layout = {'layout_sha256': c['layout_sha256'], 'candidate_seed': c['seed'],
              'rooms': deepcopy(c['rooms']), 'connections': deepcopy(c['connections']),
              'solids': [{'id': s['id'], 'bounds': deepcopy(s['bounds']), 'purpose': s['purpose_enum']} for s in c['solids']]}
    report = {'layout_sha256': c['layout_sha256'], 'candidate_seed': str(c['seed']), 'repair_passes': []}
    return layout, report


class SpanConservationTests(unittest.TestCase):
    def test_source_conservation_is_separate_from_bridge_geometry_certification(self):
        before, after = fixtures()
        result = compare_trace(before, after)
        self.assertEqual(result['offered_bridge_candidates'], 6)
        self.assertFalse(result['selected_seed_changed'])

    def test_room_envelope_change_is_not_a_motif_intervention(self):
        before, after = fixtures(); after['candidates'][0]['rooms'][0]['bounds'][1][0] += 16
        with self.assertRaisesRegex(ValueError, 'envelope'):
            compare_trace(before, after)

    def test_lower_doorway_shift_is_not_hidden_by_same_adjacency(self):
        before, after = fixtures(); after['candidates'][2]['connections'][0]['volumes'][0][0][1] += 16
        with self.assertRaisesRegex(ValueError, 'lower passage'):
            compare_trace(before, after)

    def test_unreplaced_room_solid_cannot_change(self):
        before, after = fixtures(); after['candidates'][1]['solids'][1]['bounds'][0][0] += 8
        with self.assertRaisesRegex(ValueError, 'Unreplaced room'):
            compare_trace(before, after)

    def test_new_upper_links_are_recorded_separately(self):
        before, after = fixtures()
        add_upper_link(before, after)
        result = compare_trace(before, after)
        self.assertEqual(result['observations'][0]['before_upper_passages'], 0)
        self.assertEqual(result['observations'][0]['after_upper_passages'], 1)
        self.assertEqual(result['observations'][0]['after_upper_link']['slab_ids'], [4,5,6])

    def test_link_exclusion_cannot_hide_original_room_barrier_change(self):
        before, after = fixtures(); add_upper_link(before, after)
        after['candidates'][0]['solids'][1]['bounds'][1][0] += 8
        with self.assertRaisesRegex(ValueError, 'Unreplaced room'):
            compare_trace(before, after)

    def test_link_exclusion_requires_exact_cap_and_original_deck_contact(self):
        for which in ('cap', 'branch', 'purpose', 'endpoint'):
            with self.subTest(which=which):
                before, after = fixtures(); add_upper_link(before, after)
                row = after['candidates'][0]
                if which == 'cap': row['solids'][-1]['bounds'][0][0] += 1
                elif which == 'branch': row['solids'][-3]['bounds'][0][0] -= 8
                elif which == 'purpose': row['solids'][-3]['purpose_enum'] = 2
                else: row['rooms'][1]['tags'].remove('inter_room_upper_link')
                with self.assertRaises(ValueError): compare_trace(before, after)

    def test_upper_link_cannot_be_represented_by_passage_only(self):
        before, after = fixtures()
        after['candidates'][0]['connections'].append({'id':2,'from':1,'to':2,'volumes':[[[800,352,128],[848,512,320]]]})
        with self.assertRaisesRegex(ValueError, 'upper-link population'):
            compare_trace(before, after)

    def test_seed_permutation_cannot_substitute_for_paired_observations(self):
        before, after = fixtures(); after['candidates'][0], after['candidates'][1] = after['candidates'][1], after['candidates'][0]
        with self.assertRaisesRegex(ValueError, 'indices'):
            compare_trace(before, after)

    def test_claimed_six_candidates_cannot_hide_truncation(self):
        before, after = fixtures()
        before['candidates'].pop(); after['candidates'].pop()
        with self.assertRaisesRegex(ValueError, 'population'): compare_trace(before, after)

    def test_duplicate_candidate_seed_or_index_is_rejected(self):
        for field in ('seed', 'index'):
            before, after = fixtures()
            after['candidates'][1][field] = after['candidates'][0][field]
            with self.assertRaises(ValueError): compare_trace(before, after)

    def test_selected_flags_index_seed_and_hash_must_agree(self):
        for defect in ('none', 'multiple', 'index', 'seed', 'hash', 'repair'):
            with self.subTest(defect=defect):
                before, after = fixtures()
                if defect == 'none': after['candidates'][0]['selected'] = False
                elif defect == 'multiple': after['candidates'][1]['selected'] = True
                elif defect == 'index': after['selected_index'] = 1
                elif defect == 'seed': after['pipeline_selected_seed'] = 11
                elif defect == 'hash': after['pipeline_layout_sha256'] = 'c'*64
                else: after['repair_passes'] = 1
                with self.assertRaises(ValueError): compare_trace(before, after)

    def test_selected_export_binds_captured_geometry_not_just_hash_string(self):
        _, trace = fixtures()
        layout, report = selected_export(trace)
        bind_selected_export(layout, report, trace)
        for field in ('rooms', 'connections', 'solids'):
            changed = deepcopy(layout)
            if field == 'rooms': changed[field][0]['bounds'][1][0] += 8
            elif field == 'connections': changed[field][0]['volumes'][0][0][1] += 8
            else: changed[field][0]['bounds'][0][0] += 8
            with self.assertRaisesRegex(ValueError, 'captured ' + field): bind_selected_export(changed, report, trace)

    def test_selected_export_with_repairs_is_outside_comparison_scope(self):
        _, trace = fixtures(); layout, report = selected_export(trace)
        report['repair_passes'] = [{'kind': 'test'}]
        with self.assertRaisesRegex(ValueError, 'zero repair'): bind_selected_export(layout, report, trace)

    def test_report_seed_uses_exact_decimal_string_schema(self):
        _, trace = fixtures(); layout, report = selected_export(trace)
        bind_selected_export(layout, report, trace)
        for value in (10, '010', '11'):
            report['candidate_seed'] = value
            with self.assertRaisesRegex(ValueError, 'identity'): bind_selected_export(layout, report, trace)

    def test_byte_pin_rejects_same_length_changed_export(self):
        row = {'path': 'synthetic-layout.json', 'bytes': 3, 'sha256': hashlib.sha256(b'old').hexdigest()}
        with patch.object(Path, 'stat', return_value=SimpleNamespace(st_size=3)):
            with patch.object(Path, 'read_bytes', return_value=b'old'): verify_file_pin(row)
            with patch.object(Path, 'read_bytes', return_value=b'new'):
                with self.assertRaisesRegex(ValueError, 'Changed selected export bytes'): verify_file_pin(row)

    def test_rotation_and_elevation_are_bound(self):
        before, after = fixtures(); after['candidates'][0]['rooms'][0]['tags'][-1] = 'motif_rotation_2'
        with self.assertRaisesRegex(ValueError, 'rotation'):
            compare_trace(before, after)
        before, after = fixtures(); after['candidates'][0]['solids'][0]['bounds'][1][2] = 192
        with self.assertRaisesRegex(ValueError, 'elevation'):
            compare_trace(before, after)

    def test_flat_control_requires_identical_candidate_geometry(self):
        before, after = fixtures()
        with self.assertRaisesRegex(ValueError, 'Flat control'):
            compare_trace(before, after, control=True)
        self.assertEqual(compare_trace(before, deepcopy(before), control=True)['offered_bridge_candidates'], 0)


if __name__ == '__main__':
    unittest.main()
