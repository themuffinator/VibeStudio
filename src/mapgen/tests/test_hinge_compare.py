"""Original analytical controls; these do not substitute for compiled/native tests."""
from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest

from tools.mp_portfolio.hinge_compare import (
    Evidence, ROOT, digest, entity_changes, indexed, placement_changes, retain,
    stage_bindings, structural_change, trace_change, verify_guard,
)
from tools.mp_portfolio.topology_compare import room_graph


def pool():
    return {'feature_names': ['fixture'], 'feature_weights': [1], 'selection_policy': 'fixture',
            'tie_break_policy': 'fixture', 'layout_intent': 'wrapped_center', 'pipeline_selected_seed': 10,
            'candidates': [{'seed': 10+i, 'index': i, 'family': 'pinwheel', 'selected': i == 0,
                'rooms': [{'id': 1}, {'id': 2}, {'id': 3}],
                'connections': [{'from': 1, 'to': 2}, {'from': 2, 'to': 3}, {'from': 3, 'to': 1}],
                'rank_neutral_layout_sha256': str(i)*64, 'features': [1], 'contributions': [1],
                'rank_score': 1} for i in range(6)]}


def box(ident, bounds):
    return {'id': ident, 'bounds': bounds, 'purpose': 1, 'material': 3,
            'ownership': 0, 'owner': None, 'contents': 'solid'}


def layout_pair():
    old = {'candidate_seed': 10,
           'rooms': [{'id': 1, 'name': 'west', 'bounds': [[0, 0, 0], [768, 768, 448]], 'tags': ['compact_mp']},
                     {'id': 2, 'name': 'east', 'bounds': [[816, 0, 0], [1584, 768, 448]], 'tags': ['compact_mp']}],
           'connections': [{'id': 1, 'from': 1, 'to': 2, 'width': 160, 'height': 192,
                            'volumes': [[[768, 352, 0], [816, 512, 192]]]}],
           'solids': [box(1, [[64, 352, 104], [704, 512, 128]]),
                      box(2, [[880, 352, 104], [1520, 512, 128]])],
           'placements': [{'id': 1, 'role': 1, 'origin': [100, 100, 24]},
                          {'id': 2, 'role': 4, 'origin': [120, 400, 152]},
                          {'id': 3, 'role': 9, 'origin': [100, 100, 200], 'light_intensity': 300}],
           'stair_runs': [], 'stair_run_solid_ids': [], 'ramps': [], 'drops': [], 'mechanisms': [], 'levels': [0, 128]}
    new = deepcopy(old)
    for room in new['rooms']:
        room['tags'].append('inter_room_upper_link')
    new['connections'].append({'id': 2, 'from': 1, 'to': 2, 'width': 160, 'height': 192,
                              'volumes': [[[768, 352, 128], [816, 512, 320]]]})
    new['solids'] += [box(3, [[704, 352, 104], [768, 512, 128]]),
                      box(4, [[816, 352, 104], [880, 512, 128]]),
                      box(5, [[768, 352, 104], [816, 512, 128]])]
    return old, new


class HingeComparisonControls(unittest.TestCase):
    def test_parallel_upper_passage_is_not_new_unique_adjacency(self):
        a, b = pool(), pool()
        b['candidates'][0]['connections'].append({'from': 2, 'to': 1})
        result = trace_change(a, b)
        row = result['observations'][0]
        self.assertEqual(result['matched_candidates'], 6)
        self.assertEqual(row['added_unique_adjacencies'], 0)
        self.assertEqual(row['after_room_graph']['passages'], 4)
        self.assertEqual(row['after_room_graph']['simple_edges'], 3)
        self.assertEqual(row['after_room_graph']['maximum_room_hops'], 1)
        self.assertIn('not96-candidate geometry preservation', result['scope'])

    def test_duplicate_seed_and_changed_original_edge_rejected(self):
        a, b = pool(), pool()
        b['candidates'][1]['seed'] = 10
        with self.assertRaisesRegex(ValueError, 'identities'):
            trace_change(a, b)
        b = pool()
        b['candidates'][0]['connections'][0]['to'] = 3
        with self.assertRaisesRegex(ValueError, 'endpoint'):
            trace_change(a, b)

    def test_rank_changes_are_recorded_without_assuming_geometry_fixed(self):
        a, b = pool(), pool()
        b['candidates'][0]['features'] = [2]
        b['candidates'][0]['contributions'] = [2]
        b['candidates'][0]['rank_score'] = 2
        row = trace_change(a, b)['observations'][0]
        self.assertEqual((row['before_rank_score'], row['after_rank_score']), (1, 2))
        b['feature_weights'] = [2]
        with self.assertRaisesRegex(ValueError, 'policy/request'):
            trace_change(a, b)

    def test_unrequested_candidate_change_rejected(self):
        a, b = pool(), pool()
        a.pop('layout_intent')
        b.pop('layout_intent')
        b['candidates'][0]['rank_neutral_layout_sha256'] = 'changed'
        with self.assertRaisesRegex(ValueError, 'Unrequested'):
            trace_change(a, b)

    def test_same_seed_append_only_contract_and_parallel_graph(self):
        a, b = layout_pair()
        result = structural_change(a, b)
        self.assertTrue(result['existing_geometry_preserved'])
        self.assertEqual(len(result['added_solids']), 3)
        old, new = room_graph(a), room_graph(b)
        self.assertEqual(old['simple_edges'], new['simple_edges'])
        self.assertEqual(old['maximum_room_hops'], new['maximum_room_hops'])
        self.assertNotEqual(old['canonical_multigraph'], new['canonical_multigraph'])

    def test_changed_selection_does_not_claim_structural_preservation(self):
        a, b = layout_pair()
        b['candidate_seed'] = 11
        b['rooms'][0]['bounds'][0][0] = -100
        result = structural_change(a, b)
        self.assertFalse(result['preservation_tested'])
        self.assertNotIn('existing_geometry_preserved', result)

    def test_old_material_geometry_room_and_stair_mutations_rejected(self):
        for mutate in (
                lambda b: b['solids'][0].update(material=4),
                lambda b: b['solids'][0]['bounds'][0].__setitem__(0, 65),
                lambda b: b['rooms'][0].update(name='changed'),
                lambda b: b['connections'][0].update(height=128),
                lambda b: b.update(stair_run_solid_ids=[[1]])):
            a, b = layout_pair()
            mutate(b)
            with self.assertRaises(ValueError):
                structural_change(a, b)

    def test_missing_portal_slab_and_small_support_gap_rejected(self):
        a, b = layout_pair()
        b['solids'][-1]['bounds'][0][2] = 105
        with self.assertRaisesRegex(ValueError, 'explicit upper portal slab'):
            structural_change(a, b)
        a, b = layout_pair()
        b['solids'][-3]['bounds'][1][0] -= 1
        with self.assertRaisesRegex(ValueError, 'gap'):
            structural_change(a, b)

    def test_aligned_strip_without_original_deck_attachment_rejected(self):
        a, b = layout_pair()
        b['solids'][-3]['bounds'][0][0] = 720
        with self.assertRaisesRegex(ValueError, 'original deck'):
            structural_change(a, b)

    def test_all_placement_fields_and_lights_retained(self):
        a, b = layout_pair()
        b['placements'][0]['origin'][2] = 152
        b['placements'][1].update(variant=4)
        b['placements'][2]['light_intensity'] = 500
        changes = placement_changes(a['placements'], b['placements'])
        self.assertEqual(len(changes['changed']), 3)
        self.assertIn('light_intensity', changes['changed'][2]['fields'])
        self.assertEqual(structural_change(a, b)['placement_changes'], changes)

    def test_compiled_entity_multiset_retains_duplicate_removal_and_items(self):
        light = {'classname': 'light', 'origin': '0 0 100'}
        item = {'classname': 'item_health', 'origin': '10 10 24'}
        changes = entity_changes([light, light, item], [light, {**item, 'origin': '10 10 152'}])
        self.assertEqual(sum(r['count'] for r in changes['removed']), 2)
        self.assertEqual(sum(r['count'] for r in changes['added']), 1)

    def test_duplicate_ids_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            indexed([{'id': 1}, {'id': 1}], 'fixture')

    def test_stale_evidence_and_immutable_output(self):
        # Test temporaries stay within the project and are removed by the context.
        parent = ROOT/'.agents/tmp/hinge-comparison-tests'
        parent.mkdir(parents=True, exist_ok=True)
        folder = Path(tempfile.mkdtemp(dir=parent)).resolve(strict=True)
        self.assertTrue(folder.is_relative_to(parent.resolve(strict=True)))
        try:
            file = folder/'input.json'
            file.write_text('{}', encoding='utf-8')
            evidence = Evidence(ROOT)
            evidence.bytes(file, digest(b'{}'))
            file.write_text('{"changed":true}', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'Changed frozen input'):
                evidence.bytes(file, digest(b'{}'))
            output = folder/'comparison.json'
            retain(output, {'count': 1})
            retain(output, {'count': 1}, verify=True)
            with self.assertRaisesRegex(ValueError, 'changed retained'):
                retain(output, {'count': 2})
            self.assertEqual(json.loads(output.read_text()), {'count': 1})
        finally:
            # Only these two exact task-created files; no recursive cleanup.
            for name in ('input.json', 'comparison.json'):
                target = folder/name
                self.assertFalse(target.is_symlink())
                if target.exists():
                    target.unlink()
            folder.rmdir()

    def test_guard_pairs_require_unique_artifacts_and_expected_hashes(self):
        class FakeEvidence:
            root = Path('.')
            def read(self, path):
                return {'scope': 'fixture', 'byte_identical_files': 32,
                        'files': [{'before': f'old/{i}', 'after': f'new/{i}', 'sha256': str(i)} for i in range(32)]}
            def bytes(self, path, expected):
                if path.as_posix() == 'new/17':
                    raise ValueError('Changed frozen input')
        with self.assertRaisesRegex(ValueError, 'Changed frozen input'):
            verify_guard(FakeEvidence(), Path('.'))
        fake = FakeEvidence()
        original = fake.read(Path('.'))
        original['files'][1] = original['files'][0]
        fake.read = lambda path: original
        with self.assertRaisesRegex(ValueError, 'distinct'):
            verify_guard(fake, Path('.'))

    def test_stage_cannot_substitute_another_retained_folder(self):
        output = ROOT/'out/hinge-portfolio'
        cases = [{'id': 'rooms8-seed607', 'group': 'example'}]
        record = {'rows': [{'id': 'rooms8-seed607', 'group': 'example', 'map_name': 'qmg_hinge',
                           'folder': 'out/hinge-portfolio/generated-refined/rooms8-seed607'}]}
        self.assertIn('rooms8-seed607', stage_bindings(record, cases, output, 'generated-refined'))
        record['rows'][0]['folder'] = 'out/hinge-portfolio/baseline/rooms8-seed607'
        with self.assertRaisesRegex(ValueError, 'another artifact'):
            stage_bindings(record, cases, output, 'generated-refined')


if __name__ == '__main__':
    unittest.main()
