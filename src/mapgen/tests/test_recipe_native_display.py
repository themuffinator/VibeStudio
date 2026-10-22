"""Synthetic stale-evidence controls for optional native drop status.

The in-memory filesystem preserves complete hash relationships without creating
scratch files or treating a native trajectory as the drawn geometric candidate.
"""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.map_recipes.render_layout import _native_drop_evidence


class RecipeNativeDisplayTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(__file__).resolve().parents[1] / '.agents' / 'tmp' / 'native-display-memory-fixture'
        self.files = {}
        self.layout = {'map_name': 'qmg_synthetic', 'layout_sha256': 'a' * 64, 'drops': [
            {'id': 1, 'from': [176,16,256], 'departure': [112,16,256], 'to': [112,16,128]}]}
        self.put('layout.json', self.layout)
        self.files[self.root / 'qmg_synthetic.map'] = b'original source bytes'
        self.files[self.root / 'qmg_synthetic.bsp'] = b'original compiled bytes'
        self.summary = {'schema_version': 1, 'layout_sha256': self.layout['layout_sha256'],
                        'world_only': True, 'gameplay_tested': False,
                        'layout_file_sha256': self.sha('layout.json'),
                        'map_sha256': self.sha('qmg_synthetic.map'),
                        'bsp_sha256': self.sha('qmg_synthetic.bsp'),
                        'drop': {'id': 1, 'geometric_candidate_departure': [112,16,256],
                                 'geometric_candidate_landing': [112,16,128], 'trials': []}}
        for forward in (200, 400):
            trial_id = f'probe-forward{forward}'
            prefix = 'native-routes/' + trial_id + '/'
            goal = {'frame': 20, 'origin': [0,16,152.125], 'feet_z':128.125,
                    'grounded':True, 'waterlevel':0}
            spec = {'id':trial_id, 'start':{'feet':[176,16,256], 'clearance_z':.125, 'velocity':[0,0,0]},
                    'commands':[{'frames':400, 'forward':forward, 'side':0, 'up':0, 'pitch':0}]}
            self.put(prefix + 'trial.json', spec)
            self.files[self.root / (prefix + 'commands.txt')] = f'commands-{forward}'.encode()
            self.files[self.root / (prefix + 'frames.ndjson')] = f'original-native-frames-{forward}'.encode()
            result = {'id':trial_id, 'success':True, 'failure_reason':None,
                      'bsp_sha256':self.summary['bsp_sha256'], 'stable_grounded_settle_verified':True,
                      'initial_native_state':{'world_only_collision':True}, 'movement_time_ms':192,
                      'final_observed_frame':deepcopy(goal)}
            for filename, key in [('trial.json','trial_sha256'),('commands.txt','commands_sha256'),('frames.ndjson','frames_sha256')]:
                result[key] = self.sha(prefix + filename)
            self.put(prefix + 'result.json', result)
            audit = {'success':True, 'failure_reason':None, 'independent_first_goal_matches_runner':True,
                     'stable_standing_dry_start_verified':True, 'standing_dry_through_observation':True,
                     'static_drop_candidate':{'departure_feet':[176,16,256], 'clearance_feet':[112,16,256],
                                              'landing_feet':[112,16,128]},
                     'bounded_goal_time_ms':192, 'first_grounded_station_goal':deepcopy(goal)}
            self.put(prefix + 'route-audit.json', audit)
            self.summary['drop']['trials'].append({'id':trial_id, 'route':'drop', 'forwardmove':forward,
                'success':True, 'failure_reason':None, 'source_feet':[176,16,256],
                'explicit_launch_clearance_z':.125, 'bounded_goal_time_ms':192, 'actual_goal_feet':[0,16,128.125],
                'result_sha256':self.sha(prefix+'result.json'), 'route_audit_sha256':self.sha(prefix+'route-audit.json')})
        self.put('native-routes/summary.json', self.summary)
        self.addCleanup(patch.stopall)
        patch.object(Path, 'exists', lambda path: path in self.files).start()
        patch.object(Path, 'is_file', lambda path: path in self.files).start()
        patch.object(Path, 'read_bytes', lambda path: self.files[path]).start()
        patch('tools.map_recipes.render_layout._is_link', return_value=False).start()

    def sha(self, name):
        return hashlib.sha256(self.files[self.root / name]).hexdigest()

    def put(self, name, value):
        self.files[self.root / name] = json.dumps(value, sort_keys=True).encode()

    def get(self, name):
        return json.loads(self.files[self.root / name])

    def run_audit(self):
        self.put('native-routes/summary.json', self.summary)
        return _native_drop_evidence(self.root, self.layout)

    def rebind(self, index=0):
        row = self.summary['drop']['trials'][index]
        prefix = 'native-routes/' + row['id'] + '/'
        result = self.get(prefix+'result.json')
        for name,key in [('trial.json','trial_sha256'),('commands.txt','commands_sha256'),('frames.ndjson','frames_sha256')]:
            result[key] = self.sha(prefix+name)
        self.put(prefix+'result.json',result)
        row['result_sha256'] = self.sha(prefix+'result.json')
        row['route_audit_sha256'] = self.sha(prefix+'route-audit.json')

    def test_optional_absence_keeps_historical_scope(self):
        del self.files[self.root/'native-routes/summary.json']
        self.assertIsNone(_native_drop_evidence(self.root,self.layout))

    def test_compact_status_keeps_geometric_candidate_unchanged(self):
        before = deepcopy(self.layout)
        evidence = self.run_audit()
        self.assertEqual(self.layout,before)
        self.assertEqual(evidence['drops'],[{'id':1,'probe_count':2,'forward_inputs':[200,400]}])
        self.assertFalse(evidence['diagram_is_native_trajectory'])
        self.assertFalse(evidence['gameplay_tested'])
        self.assertNotIn('actual_goal_feet',json.dumps(evidence))

    def test_current_source_bsp_and_layout_bytes_are_bound(self):
        for name in ('layout.json','qmg_synthetic.map','qmg_synthetic.bsp'):
            original = self.files[self.root/name]
            self.files[self.root/name] += b' changed'
            with self.subTest(name=name), self.assertRaisesRegex(ValueError,'stale'):
                self.run_audit()
            self.files[self.root/name] = original

    def test_canonical_layout_is_bound_separately(self):
        self.summary['layout_sha256'] = 'b'*64
        with self.assertRaisesRegex(ValueError,'canonical layout'):
            self.run_audit()

    def test_scope_cannot_claim_gameplay_or_nonworld_collision(self):
        for key,value in (('world_only',False),('gameplay_tested',True)):
            original = self.summary[key]; self.summary[key] = value
            with self.subTest(key=key),self.assertRaisesRegex(ValueError,'scope'):
                self.run_audit()
            self.summary[key] = original

    def test_geometric_departure_and_landing_must_match(self):
        for key in ('geometric_candidate_departure','geometric_candidate_landing'):
            original = self.summary['drop'][key]
            self.summary['drop'][key] = [0,16,128]
            with self.subTest(key=key),self.assertRaisesRegex(ValueError,'candidate endpoints'):
                self.run_audit()
            self.summary['drop'][key] = original

    def test_duplicate_or_missing_input_profiles_are_rejected(self):
        self.summary['drop']['trials'][1] = deepcopy(self.summary['drop']['trials'][0])
        with self.assertRaisesRegex(ValueError,'two distinct'):
            self.run_audit()

    def test_summary_failure_cannot_be_displayed_as_success(self):
        self.summary['drop']['trials'][0]['success'] = False
        with self.assertRaisesRegex(ValueError,'failed probe'):
            self.run_audit()

    def test_trial_ids_cannot_escape_evidence_directory(self):
        self.summary['drop']['trials'][0]['id'] = '../different-output'
        with self.assertRaisesRegex(ValueError,'invalid trial ID'):
            self.run_audit()

    def test_result_audit_commands_and_frames_hashes_are_bound(self):
        prefix='native-routes/probe-forward200/'
        for name in ('result.json','route-audit.json','trial.json','commands.txt','frames.ndjson'):
            original=self.files[self.root/(prefix+name)]
            self.files[self.root/(prefix+name)] += b' stale'
            with self.subTest(name=name),self.assertRaisesRegex(ValueError,'stale'):
                self.run_audit()
            self.files[self.root/(prefix+name)] = original

    def test_rehashed_failed_result_is_still_rejected(self):
        path='native-routes/probe-forward200/result.json'; value=self.get(path)
        value['success']=False; self.put(path,value); self.rebind()
        with self.assertRaisesRegex(ValueError,'result lacks'):
            self.run_audit()

    def test_different_start_or_jump_input_is_not_accepted(self):
        path='native-routes/probe-forward200/trial.json'; original=self.get(path)
        for changed in ('start','jump'):
            value=deepcopy(original)
            if changed=='start':value['start']['feet'][0]=192
            else:value['commands'][0]['up']=400
            self.put(path,value); self.rebind()
            with self.subTest(changed=changed),self.assertRaisesRegex(ValueError,'native start|no-jump input'):
                self.run_audit()

    def test_rehashed_airborne_or_different_arrival_is_rejected(self):
        path='native-routes/probe-forward200/route-audit.json'; value=self.get(path)
        value['first_grounded_station_goal']['grounded']=False; self.put(path,value); self.rebind()
        with self.assertRaisesRegex(ValueError,'declared dry lower floor'):
            self.run_audit()

    def test_linked_evidence_is_rejected(self):
        with patch('tools.map_recipes.render_layout._is_link',side_effect=lambda p:p.name=='probe-forward200'), \
             self.assertRaisesRegex(ValueError,'traverses a link'):
            self.run_audit()


if __name__=='__main__':
    unittest.main()
