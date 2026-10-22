"""Verify and bind retained native experiment inputs, analysis and outputs."""
import json
from pathlib import Path

from tools.native_movement.run import BUILD_RECORD, EVIDENCE, NATIVE, PROJECT, require, save, sha, verify_build


def main():
    build = verify_build(NATIVE)
    trials = []
    for name in ('fold', 'lockyard'):
        comparison = json.loads((EVIDENCE/(name+'-comparison.json')).read_text(encoding='utf-8'))
        proof = comparison['control_proof']
        for path_key, hash_key in (('source_map', 'source_map_sha256'), ('control_map', 'control_map_sha256'),
                                   ('dry_bsp', 'dry_bsp_sha256')):
            require(sha(Path(proof[path_key])) == proof[hash_key], 'Control source artifact changed')
        for pair in comparison['pairs']:
            for state in ('wet', 'dry'):
                record = pair[state]
                require(sha(Path(record['bsp'])) == record['bsp_sha256'], 'Current trial BSP changed')
                require(record['native_executable_sha256'] == build['executable_sha256'], 'Trial uses superseded native executable')
                require(record['native_build_record_sha256'] == sha(BUILD_RECORD), 'Trial build record changed')
                require(record['runner_sha256'] == sha(PROJECT/'tools/native_movement/run.py'), 'Trial runner changed')
                for name, hash_key in (('trial.json', 'trial_sha256'), ('commands.txt', 'commands_sha256'),
                                       ('frames.ndjson', 'frames_sha256')):
                    require(sha(EVIDENCE/record['id']/name) == record[hash_key], 'Retained trial data changed')
                require(json.loads((EVIDENCE/record['id']/'result.json').read_text(encoding='utf-8')) == record,
                        'Standalone result differs from paired comparison')
                trials.append({'id': record['id'], 'success': record['success'], 'bsp_sha256': record['bsp_sha256']})
    paths = sorted(p for p in EVIDENCE.rglob('*') if p.is_file() and p.name != 'artifact-manifest.json')
    require(not any(p.suffix.lower() in {'.exe', '.dll', '.obj', '.lib', '.pdb'} for p in paths),
            'Private native build product found in retained map/data outputs')
    sources = sorted(p for p in (PROJECT/'tools/native_movement').iterdir() if p.is_file())
    sources += [PROJECT/p for p in ('tests/test_native_movement.py', 'tools/map_study/binary.py',
                                    'tools/map_recipes/geometry.py', 'tools/map_recipes/ramp_stair_comparison.py')]
    manifest = {'schema_version': 1, 'id': 'native-q2-319-water-comparison-v1',
                'status': 'final_current_trials_bound_private_binary_not_packaged',
                'current_trials': trials, 'current_trial_count': len(trials),
                'successful_current_trials': sum(t['success'] for t in trials),
                'failed_current_trials_retained': sum(not t['success'] for t in trials),
                'native_executable_sha256': build['executable_sha256'],
                'build_provenance_sha256': sha(EVIDENCE/'build-provenance.json'),
                'source_files': [{'path': str(p.relative_to(PROJECT)), 'sha256': sha(p)} for p in sources],
                'retained_artifact_files': [{'path': str(p.relative_to(EVIDENCE)), 'bytes': p.stat().st_size, 'sha256': sha(p)} for p in paths],
                'superseded_attempts': 'superseded-attempts.json', 'cleanup_status': 'cleanup-status.json',
                'limits': 'Manifest binds data identity and recorded control outcomes; it does not enlarge the native world-only movement contract or certify full-game equivalence.'}
    save(EVIDENCE/'artifact-manifest.json', manifest)
    print(json.dumps({'current_trials': len(trials), 'successes': manifest['successful_current_trials'],
                      'retained_failures': manifest['failed_current_trials_retained'], 'artifact_files': len(paths),
                      'manifest_sha256': sha(EVIDENCE/'artifact-manifest.json')}))


if __name__ == '__main__':
    main()
