"""Generated draft -> prepared workspace -> VibeMap3 -> verified PK3 integration proof.

No game launch, native input or screen capture. Output stays in .agents/tmp.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct
import subprocess
import wave
import zipfile

from level_compiler_test_helpers import png, box


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    root = args.output_root.resolve()
    assert root.is_relative_to(repo / '.agents' / 'tmp')
    root.mkdir(parents=True, exist_ok=True)
    assets = root / 'assets'
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

    png(assets / 'textures/workspace/wall.png')
    png(assets / 'textures/workspace/model.png')
    (assets / 'sound/workspace').mkdir(parents=True)
    with wave.open(str(assets / 'sound/workspace/tone.wav'), 'wb') as audio:
        audio.setparams((1, 2, 22050, 0, 'NONE', 'not compressed'))
        audio.writeframes(b''.join(struct.pack('<h', int(1000 * math.sin(2*math.pi*220*i/22050))) for i in range(2205)))
    (assets / 'scripts').mkdir()
    (assets / 'scripts/workspace.shader').write_text('textures/workspace/wall\n{\n qer_editorimage textures/workspace/wall.png\n { map textures/workspace/wall.png }\n}\n', encoding='utf-8')
    (assets / 'scripts/shaderlist.txt').write_text('workspace\n', encoding='utf-8')
    design = root / 'prop.model.json'
    design.write_text(json.dumps({'schemaVersion': 2, 'name': 'workspace_prop', 'parts': [{
        'name': 'prop', 'primitive': 'cylinder', 'size': [32, 32, 40], 'origin': [0, 0, 20],
        'roll': 0, 'pitch': 0, 'yaw': 0, 'segments': 8, 'material': 'textures/workspace/model',
        'uvScale': [1, 1], 'uvOffset': [0, 0], 'uvRotation': 0}]}), encoding='utf-8')
    model = root / 'prop.md3'
    run('model-design', ['model', 'build', design, '--output', model, '--overwrite'])
    draft = root / 'assets.vibepackage'
    run('draft-stage-model', ['package', 'draft-save', assets, draft, '--add-file', model, '--as', 'models/workspace/prop.md3'])
    room = [((-272, -272, -16), (272, 272, 0)), ((-272, -272, 256), (272, 272, 272)),
            ((-272, -272, 0), (-256, 272, 256)), ((256, -272, 0), (272, 272, 256)),
            ((-256, -272, 0), (256, -256, 256)), ((-256, 256, 0), (256, 272, 256))]
    source = root / 'arena.map'
    source.write_text('{\n"classname" "worldspawn"\n' + '\n'.join(box(lo, hi, 'classic', 'workspace/wall') for lo, hi in room)
                      + '\n}\n{\n"classname" "info_player_deathmatch"\n"origin" "0 -100 64"\n}\n'
                      + '{\n"classname" "light"\n"origin" "0 0 220"\n"light" "500"\n}\n'
                      + '{\n"classname" "misc_model"\n"origin" "32 48 0"\n"model" "models/workspace/prop.md3"\n}\n'
                      + '{\n"classname" "target_speaker"\n"origin" "0 96 48"\n"noise" "sound/workspace/tone.wav"\n}\n', encoding='utf-8')
    def inventory(path):
        return {p.relative_to(path).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in path.rglob('*') if p.is_file()}
    original = source.read_bytes()
    draft_before = inventory(draft)
    workspace = root / 'prepared'
    arguments = ['build', 'prepare', source, '--package', draft, '--output', workspace, '--name', 'arena']
    run('dry-prepare', arguments + ['--dry-run'])
    assert not workspace.exists()
    prepared = run('prepare', arguments)['workspace']
    assert prepared['prepared']
    for record in prepared['inputs']:
        data = (workspace / record['path']).read_bytes()
        assert len(data) == record['bytes'] and hashlib.sha256(data).hexdigest() == record['sha256']
    pipeline = ['build', 'run-prepared', workspace, '--tool', 'vibemap3=' + str(args.compiler.resolve()),
                '--stage-args', 'bsp=-threads 2 -meta', '--stage-args', 'vis=-threads 2 -fast', '--stage-args', 'light=-threads 2 -fast']
    run('plan', pipeline + ['--dry-run'])
    result = run('compile', pipeline)['pipeline']
    assert result['succeeded'] and len(result['stages']) == 3
    base = workspace / 'game/baseq3'
    bsp = (base / 'maps/arena.bsp').read_bytes()
    assert bsp[:8] == b'IBSP' + struct.pack('<i', 46)
    def lump(index):
        offset, size = struct.unpack_from('<2i', bsp, 8 + 8*index)
        return bsp[offset:offset+size]
    assert b'"noise" "sound/workspace/tone.wav"' in lump(0)
    shaders = [lump(1)[i:i+64].split(b'\0', 1)[0].decode() for i in range(0, len(lump(1)), 72)]
    model_surfaces = sum(shaders[struct.unpack_from('<i', lump(13), i)[0]] == 'textures/workspace/model' for i in range(0, len(lump(13)), 104))
    assert model_surfaces > 0, 'compiler must bake the model present only in the package draft'
    output = root / 'arena.pk3'
    run('package', ['package', 'save-as', base, output, '--format', 'pk3'])
    validation = run('validate-package', ['package', 'validate', output])['validation']
    assert validation['valid'] and validation['uncheckedCount'] == 0
    with zipfile.ZipFile(output) as archive:
        assert archive.testzip() is None and archive.read('maps/arena.bsp') == bsp
        for record in prepared['inputs']:
            assert archive.read(record['path'].removeprefix('game/baseq3/')) == (workspace / record['path']).read_bytes()
    assert source.read_bytes() == original and inventory(draft) == draft_before
    asset = base / 'models/workspace/prop.md3'
    saved = asset.read_bytes()
    asset.write_bytes(bytes([saved[0] ^ 1]) + saved[1:])
    refused = run('tamper-refused', pipeline + ['--dry-run'], code=4)
    assert not refused['pipeline']['stages']
    asset.write_bytes(saved)
    (root / 'verified.json').write_text(json.dumps({'stepsPassed': len(steps), 'inputs': len(prepared['inputs']),
        'modelSurfaces': model_surfaces, 'bspSha256': hashlib.sha256(bsp).hexdigest(), 'packageValidation': validation,
        'sourceAndDraftUnchanged': True}, indent=2), encoding='utf-8')
    print('PASS: staged model, current map, shaders, sound, three compiler stages, PK3 and tamper guard.', flush=True)


if __name__ == '__main__':
    main()
