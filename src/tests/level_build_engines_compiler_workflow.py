"""Generated Quake/Quake II prepared builds through VibeMap2.

Creates only synthetic content below .agents/tmp; never launches a game.
Independently reads BSP texture references and every published PAK payload.
"""
from pathlib import Path
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
import shutil
import struct
import subprocess
import time
import wave

from texture_compiler_workflow import room, lump


def read_pak(path):
    data = path.read_bytes()
    assert data[:4] == b'PACK'
    offset, size = struct.unpack_from('<2i', data, 4)
    assert offset >= 12 and size % 64 == 0 and offset + size == len(data)
    result = {}
    for at in range(offset, offset + size, 64):
        name = data[at:at + 56].split(b'\0', 1)[0].decode('ascii')
        start, count = struct.unpack_from('<2i', data, at + 56)
        assert name not in result and start >= 12 and start + count <= offset
        result[name] = data[start:start + count]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('binary', 'qbsp', 'vis', 'light', 'output-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--recorder', type=Path, help='Optional level_classic_deployment_smoke_test executable; never a game.')
    args = parser.parse_args()
    if not __debug__:
        parser.error('Assertions must remain enabled.')
    repo = Path(__file__).resolve().parents[2]
    if args.recorder and (args.recorder.resolve().stem != 'level_classic_deployment_smoke_test' or
                          not args.recorder.resolve().is_relative_to(repo / 'builddir')):
        parser.error('Use the built classic deployment recorder inside this repository/builddir.')
    root = args.output_root.resolve()
    if not root.is_relative_to(repo / '.agents' / 'tmp') or root.exists():
        parser.error('Use a new output directory within repository/.agents/tmp.')
    root.mkdir(parents=True)
    runtime = root / 'runtime'
    runtime.mkdir()
    env = dict(os.environ, TEMP=str(runtime), TMP=str(runtime), TMPDIR=str(runtime), PYTHONDONTWRITEBYTECODE='1')
    steps, checks = [], []
    binaries = {name: {'path': str(path.resolve()), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                for name, path in [('studio', args.binary), ('qbsp', args.qbsp), ('vis', args.vis), ('light', args.light)]}
    if args.recorder:
        binaries['recorder'] = {'path': str(args.recorder.resolve()), 'sha256': hashlib.sha256(args.recorder.read_bytes()).hexdigest()}
    (root / 'binaries.json').write_text(json.dumps({'startedUtc': datetime.now(timezone.utc).isoformat(), 'binaries': binaries}, indent=2), encoding='utf-8')

    def run(label, words, code=0):
        command = [str(args.binary.resolve()), '--cli', '--settings-file', str(root / 'settings.ini'), '--json'] + list(map(str, words))
        result = subprocess.run(command, cwd=root, env=env, capture_output=True, timeout=180)
        (root / f'{label}.stdout.txt').write_bytes(result.stdout)
        (root / f'{label}.stderr.txt').write_bytes(result.stderr)
        steps.append({'step': label, 'command': command, 'exitCode': result.returncode, 'expectedCode': code})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert result.returncode == code, (label, result.stdout.decode(errors='replace'), result.stderr.decode(errors='replace'))
        print(label, 'PASS', flush=True)
        return json.loads(result.stdout)

    palette = root / 'synthetic-palette.lmp'
    palette.write_bytes(bytes(value for i in range(256) for value in (i, i, i)))
    texture = root / 'authored.vtexture'
    run('texture-author', ['texture', 'create', '--size', '32x32', '--color', '#606060', '--output', texture])
    for target in ('quake', 'quake2'):
        assets = root / target / 'source assets'
        assets.mkdir(parents=True)
        texture_path = assets / ('textures.wad' if target == 'quake' else 'textures/authored.wal')
        texture_path.parent.mkdir(parents=True, exist_ok=True)
        options = root / f'{target}-export.json'
        options.write_text(json.dumps({'name': 'authored'}), encoding='utf-8')
        run(target + '-texture', ['texture', 'export', texture, '--profile', 'quake-wad2' if target == 'quake' else 'quake2-wal',
                                 '--palette-file', palette, '--export-options', options, '--output', texture_path])
        options.write_text(json.dumps({'name': 'skip'}), encoding='utf-8')
        run(target + '-compiler-skip', ['texture', 'export', texture, '--profile', 'quake-wad2' if target == 'quake' else 'quake2-wal',
                                       '--palette-file', palette, '--export-options', options, '--output', assets / ('skip.wad' if target == 'quake' else 'textures/skip.wal')])
        (assets / 'sound').mkdir()
        with wave.open(str(assets / 'sound/tone.wav'), 'wb') as output:
            output.setnchannels(1)
            output.setsampwidth(2)
            output.setframerate(11025)
            output.writeframes(b'\0\0' * 1103)
        if target == 'quake2':
            design = root / 'prop.model.json'
            design.write_text(json.dumps({'schemaVersion': 1, 'name': 'authored_prop', 'parts': [{'name': 'box', 'primitive': 'box', 'size': [16, 16, 16], 'origin': [0, 0, 0], 'yaw': 0, 'segments': 12, 'material': 'skins/authored.pcx'}]}), encoding='utf-8')
            (assets / 'models').mkdir()
            (assets / 'skins').mkdir()
            run('quake2-skin', ['texture', 'export', texture, '--profile', 'pcx', '--palette-file', palette, '--output', assets / 'skins/authored.pcx'])
            run('quake2-model', ['model', 'build', design, '--output', assets / 'models/authored.md2'])
        source = root / target / 'authored.map'
        map_text = room(Path('Z:/unavailable/foreign.wad') if target == 'quake' else None).replace(' old ', ' authored ')
        if target == 'quake2':
            map_text += '{\n\"classname\" \"misc_model\"\n\"origin\" \"32 32 16\"\n\"model\" \"models/authored.md2\"\n}\n'
        source.write_text(map_text, encoding='utf-8')
        original = source.read_bytes()
        workspace = root / target / 'build workspace'
        prepare = ['build', 'prepare', source, '--target', target, '--package', assets, '--output', workspace]
        run(target + '-dry-prepare', prepare + ['--dry-run'])
        assert not workspace.exists()
        run(target + '-prepare', prepare)
        assert source.read_bytes() == original
        base = workspace / 'game' / ('id1' if target == 'quake' else 'baseq2')
        manifest = json.loads((workspace / 'build-inputs.json').read_text(encoding='utf-8'))
        assert manifest['target'] == target
        run_words = ['build', 'run-prepared', workspace, '--tool', f'vibemap2-bsp={args.qbsp.resolve()}', '--tool', f'vibemap2-vis={args.vis.resolve()}',
                     '--tool', f'vibemap2-light={args.light.resolve()}', '--stage-args', 'qbsp=-threads 2', '--stage-args', 'vis=-threads 2',
                     '--stage-args', 'light=-threads 2' + (' -lit -lux' if target == 'quake' else '')]
        compiled = run(target + '-compile', run_words)
        assert all('does not match the selected compiler profile' not in warning for warning in compiled['pipeline']['warnings'])
        receipt = run(target + '-artifacts', ['build', 'artifacts', workspace])
        bsp = (base / 'maps/authored.bsp').read_bytes()
        if target == 'quake':
            assert struct.unpack_from('<i', bsp)[0] == 29
            textures = lump(bsp, 2, header=4)
            texture_offsets = {}
            for i in range(struct.unpack_from('<i', textures)[0]):
                at = struct.unpack_from('<i', textures, 4 + i * 4)[0]
                if at >= 0:
                    texture_offsets[textures[at:at + 16].split(b'\0', 1)[0].decode()] = at
            assert 'authored' in texture_offsets
            at = texture_offsets['authored']
            assert struct.unpack_from('<2I', textures, at + 16) == (32, 32)
            mip_offset = struct.unpack_from('<I', textures, at + 24)[0]
            assert textures[at + mip_offset:at + mip_offset + 1024] == bytes([96]) * 1024
        else:
            assert bsp[:4] == b'IBSP' and struct.unpack_from('<i', bsp, 4)[0] == 38
            texinfo = lump(bsp, 5)
            names = [texinfo[at + 40:at + 72].split(b'\0', 1)[0].decode() for at in range(0, len(texinfo), 76)]
            assert 'authored' in names
        outputs = json.loads((workspace / 'build-outputs.json').read_text(encoding='utf-8'))
        pak = root / target / 'pak0.pak'
        run(target + '-dry-publish', ['build', 'publish-prepared', workspace, '--output', pak, '--dry-run'])
        assert not pak.exists()
        run(target + '-publish', ['build', 'publish-prepared', workspace, '--output', pak])
        payload = read_pak(pak)
        expected = {item['path'].split('/', 2)[2] for item in manifest['inputs'] if item['path'] not in
                    [f'game/{base.name}/maps/authored.map', f'game/{base.name}/maps/authored.wad']}
        expected |= {item['path'].split('/', 2)[2] for item in outputs['outputs'] if item['role'] != 'diagnostic'}
        assert set(payload) == expected
        for name, data in payload.items():
            assert data == (base / name).read_bytes(), name
        assert payload['sound/tone.wav'] == (assets / 'sound/tone.wav').read_bytes()
        if target == 'quake':
            assert payload['maps/authored.lit'][:4] == b'QLIT' and payload['maps/authored.lux'][:4] == b'QLIT'
        else:
            assert payload['models/authored.md2'][:4] == b'IDP2'
        digest = hashlib.sha256(pak.read_bytes()).hexdigest()
        run(target + '-republish', ['build', 'publish-prepared', workspace, '--output', pak, '--overwrite'])
        assert hashlib.sha256(pak.read_bytes()).hexdigest() == digest and pak.with_suffix('.pak.bak').read_bytes() == pak.read_bytes()
        run(target + '-incremental', run_words + ['--disable-stage', 'qbsp'])
        if args.recorder:
            installation = root / target / 'disposable game installation'
            installation.mkdir()
            recorder = installation / args.recorder.name
            shutil.copy2(args.recorder, recorder)
            profile = run(target + '-install-recorder', ['install', 'add', installation, '--install-game', target,
                          '--install-engine', 'idTech2', '--install-executable', recorder,
                          '--install-name', target + ' deployment recorder', '--install-read-only', 'on'])['installation']
            common = [workspace, '--installation', profile['id'], '--mod', 'studio']
            plan = run(target + '-deploy-review', ['build', 'deploy-plan'] + common)['deploymentPlan']
            destination = Path(plan['packagePath'])
            assert plan['ready'] and plan['pakSlot'] == 0 and not destination.parent.exists()
            launch_args = plan['launch']['arguments']
            if target == 'quake':
                assert '-window' in launch_args and launch_args[launch_args.index('-basedir') + 1] == installation.as_posix()
            else:
                assert launch_args[launch_args.index('basedir') - 1] == '+set'
                assert launch_args[launch_args.index('basedir') + 1] == installation.as_posix()
                assert launch_args[launch_args.index('vid_fullscreen') + 1] == '0'
            reviewed = ['--expected-deployment-sha256', plan['reviewSha256']]
            run(target + '-deploy-dry', ['build', 'deploy-prepared'] + common + reviewed + ['--dry-run', '--launch'])
            assert not destination.parent.exists()
            run(target + '-deploy-readonly-refusal', ['build', 'deploy-prepared'] + common, code=4)
            deployed = run(target + '-deploy-record', ['build', 'deploy-prepared'] + common + reviewed +
                           ['--allow-test-assets', '--launch'])['deployment']
            assert deployed['succeeded'] and deployed['launched'] and deployed['publication']['committed']
            record = installation / 'classic-launch-record.json'
            deadline = time.monotonic() + 10
            while not record.exists() and time.monotonic() < deadline:
                time.sleep(0.05)
            captured = json.loads(record.read_text(encoding='utf-8'))
            assert captured['validPackageAtLaunch'] and captured['slot'] == 0
            installed_payload = read_pak(destination)
            assert set(installed_payload) == expected
            for name, data in installed_payload.items():
                assert data == (base / name).read_bytes(), name
            previous = destination.read_bytes()
            repeat = run(target + '-deploy-repeat-review', ['build', 'deploy-plan'] + common)['deploymentPlan']
            assert repeat['pakSlot'] == 0 and repeat['packageExists']
            run(target + '-deploy-replace', ['build', 'deploy-prepared'] + common + ['--allow-test-assets', '--overwrite',
                '--include-source', '--expected-deployment-sha256', repeat['reviewSha256']])
            assert Path(str(destination) + '.bak').read_bytes() == previous
            assert not (destination.parent / 'pak1.pak').exists()
            assert read_pak(destination)['maps/authored.map'] == (base / 'maps/authored.map').read_bytes()
            run(target + '-deploy-stale-refusal', ['build', 'deploy-prepared'] + common + ['--allow-test-assets', '--overwrite',
                '--expected-deployment-sha256', repeat['reviewSha256']], code=4)
            profiles = run(target + '-deploy-permission', ['install', 'list'])
            assert any(item['id'] == profile['id'] and item['readOnly'] for item in profiles['installations']['profiles'])
        capture = base / 'maps/authored.map'
        capture.write_bytes(capture.read_bytes() + b'\n// changed after capture\n')
        run(target + '-reject-modified-input', ['build', 'publish-prepared', workspace, '--output', pak, '--overwrite'], code=4)
        assert hashlib.sha256(pak.read_bytes()).hexdigest() == digest
        checks.append({'target': target, 'bspSha256': hashlib.sha256(bsp).hexdigest(), 'pakSha256': digest, 'entries': sorted(payload), 'verified': True})
    assert all(hashlib.sha256(Path(record['path']).read_bytes()).hexdigest() == record['sha256'] for record in binaries.values()), 'Compiler or studio binary changed during verification.'
    (root / 'verified.json').write_text(json.dumps({'checks': checks, 'steps': len(steps), 'binaries': binaries,
        'recorderDeploymentVerified': bool(args.recorder), 'realGameLaunched': False}, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
