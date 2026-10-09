"""Generated model/draft -> VibeMap3 -> complete installation PK3 -> recorder.

The recorder is level_build_deployment_smoke_test, never a game executable.
No native input or screen capture. All files stay in the project .agents/tmp.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys
import time
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--recorder', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    root = args.output_root.resolve()
    recorder = args.recorder.resolve()
    assert recorder.stem == 'level_build_deployment_smoke_test' and recorder.is_relative_to(repo / 'builddir')
    assert root.is_relative_to(repo / '.agents' / 'tmp') and not root.exists()
    root.mkdir(parents=True)
    baseline = root / 'baseline'
    fixture = subprocess.run([sys.executable, str(Path(__file__).with_name('level_build_artifacts_compiler_workflow.py')),
                              '--binary', str(args.binary.resolve()), '--compiler', str(args.compiler.resolve()),
                              '--output-root', str(baseline)], cwd=repo, capture_output=True, timeout=360)
    (root / 'fixture.stdout.txt').write_bytes(fixture.stdout)
    (root / 'fixture.stderr.txt').write_bytes(fixture.stderr)
    assert fixture.returncode == 0, fixture.stderr
    steps = []

    def run(label, words, code=0):
        command = [str(args.binary.resolve()), '--cli', '--json', '--settings-file', str(root / 'settings.ini')] + list(map(str, words))
        result = subprocess.run(command, cwd=repo, capture_output=True, timeout=180)
        (root / f'{label}.stdout.txt').write_bytes(result.stdout)
        (root / f'{label}.stderr.txt').write_bytes(result.stderr)
        steps.append({'step': label, 'command': command, 'exitCode': result.returncode})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert result.returncode == code, (label, result.stdout, result.stderr)
        print(label, 'passed', flush=True)
        return json.loads(result.stdout)

    workspace = baseline / 'prepared'
    # Return the proven workspace to generated shaders and external lightmaps.
    run('external-build', ['build', 'run-prepared', workspace, '--tool', 'vibemap3=' + str(args.compiler.resolve()),
                          '--stage-args', 'bsp=-threads 2 -meta', '--stage-args', 'vis=-threads 2 -fast',
                          '--stage-args', 'light=-threads 2 -fast -extlmhacksize 256'])
    installation = root / 'disposable game installation'
    installation.mkdir()
    profile = run('add-recorder-installation', ['install', 'add', installation, '--install-game', 'quake3', '--install-engine', 'idTech3',
                                               '--install-executable', recorder, '--install-name', 'Deployment recorder', '--install-read-only', 'on'])['installation']
    common = [workspace, '--installation', profile['id'], '--mod', 'studio']
    plan = run('deployment-review', ['build', 'deploy-plan'] + common)['deploymentPlan']
    package = Path(plan['packagePath'])
    assert plan['ready'] and plan['readOnly'] and not package.parent.exists()
    launch_args = plan['launch']['arguments']
    assert launch_args[launch_args.index('r_fullscreen') + 1] == '0'
    assert launch_args[launch_args.index('fs_homepath') + 1] == str(installation).replace('\\', '/')
    receipt = plan['artifacts']['recordSha256']
    reviewed = ['--expected-output-sha256', receipt, '--expected-package-sha256', 'missing']
    run('deployment-dry', ['build', 'deploy-prepared'] + common + reviewed + ['--dry-run', '--launch'])
    assert not package.parent.exists() and not (installation / 'launch-record.json').exists()
    run('read-only-refusal', ['build', 'deploy-prepared'] + common, code=4)
    result = run('deploy-and-record', ['build', 'deploy-prepared'] + common + reviewed + ['--allow-test-assets', '--launch'])['deployment']
    assert result['succeeded'] and result['launched'] and result['publication']['committed']
    timeout = time.monotonic() + 10
    record = installation / 'launch-record.json'
    while not record.exists() and time.monotonic() < timeout:
        time.sleep(0.05)
    captured = json.loads(record.read_text(encoding='utf-8'))
    assert captured['validPackageAtLaunch']
    original = package.read_bytes()
    with zipfile.ZipFile(package) as archive:
        assert archive.testzip() is None
        required = {'maps/arena.bsp', 'models/workspace/prop.md3', 'sound/workspace/tone.wav', 'scripts/q3map2_arena.shader'}
        assert required <= set(archive.namelist())
        assert any(path.startswith('maps/arena/lm_') for path in archive.namelist())
        assert 'maps/arena.prt' not in archive.namelist() and 'maps/arena.map' not in archive.namelist()
        for path in archive.namelist():
            assert archive.read(path) == (workspace / 'game/baseq3' / path).read_bytes()
    run('stale-missing-refusal', ['build', 'deploy-prepared'] + common + reviewed + ['--allow-test-assets', '--overwrite'], code=4)
    revised = run('existing-review', ['build', 'deploy-plan'] + common)['deploymentPlan']
    assert revised['existingPackageSha256'] == hashlib.sha256(original).hexdigest()
    source = run('deploy-source-backup', ['build', 'deploy-prepared'] + common + ['--allow-test-assets', '--overwrite', '--include-source',
                 '--expected-package-sha256', revised['existingPackageSha256']])['deployment']
    assert source['succeeded'] and not source['launched'] and Path(str(package) + '.bak').read_bytes() == original
    with zipfile.ZipFile(package) as archive:
        assert archive.read('maps/arena.map') == (workspace / 'game/baseq3/maps/arena.map').read_bytes()
    # Review-token refusal preserves an outside edit in the installation.
    changed = package.read_bytes()[:-1] + bytes([package.read_bytes()[-1] ^ 1])
    package.write_bytes(changed)
    run('changed-package-refusal', ['build', 'deploy-prepared'] + common + ['--allow-test-assets', '--overwrite',
                 '--expected-package-sha256', source['publication']['sha256']], code=4)
    assert package.read_bytes() == changed
    profiles = run('permission-still-read-only', ['install', 'list'])
    assert any(item['id'] == profile['id'] and item['readOnly'] for item in profiles['installations']['profiles'])
    (root / 'verified.json').write_text(json.dumps({'stepsPassed': len(steps), 'baselineStepsPassed': 24,
        'fullRuntimeArchiveVerified': True, 'windowedArgumentsVerified': True, 'recorderSawValidPackage': True,
        'permissionUnchanged': True, 'verifiedBackup': True, 'staleReviewPreservedOutsideEdit': True,
        'deployedSha256': hashlib.sha256(original).hexdigest(), 'realGameLaunched': False}, indent=2), encoding='utf-8')
    print('PASS: real compiler outputs and complete assets deployed; recorder launched only after verified publication.', flush=True)


if __name__ == '__main__':
    main()
