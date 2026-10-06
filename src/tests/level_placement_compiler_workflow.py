"""Optional snap/duplicate/paste -> q3map2 UV -> model/audio dependency -> PK3 proof.

Uses generated assets only. No game launch, native input or screen capture.
All output must stay under this project's .agents/tmp directory.
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

from level_compiler_test_helpers import png, box, axes, primitive_basis, dot


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    root = args.output_root.resolve()
    assert root.is_relative_to(repo / '.agents' / 'tmp')
    base = root / 'game' / 'baseq3'
    maps = base / 'maps'
    for folder in ('maps', 'models/placement', 'sound/placement'):
        (base / folder).mkdir(parents=True, exist_ok=True)
    (root / 'home').mkdir(exist_ok=True)
    steps, variants = [], []

    def run(label, words, cli=True):
        command = ([str(args.binary.resolve()), '--cli', '--json', '--settings-file', str(root / 'settings.ini')] + list(map(str, words))
                   if cli else list(map(str, words)))
        result = subprocess.run(command, cwd=repo, capture_output=True, timeout=120)
        (root / f'{label}.stdout.txt').write_bytes(result.stdout)
        (root / f'{label}.stderr.txt').write_bytes(result.stderr)
        steps.append({'step': label, 'command': command, 'exitCode': result.returncode})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert result.returncode == 0, (label, result.stdout, result.stderr)
        print(label, 'passed', flush=True)
        return json.loads(result.stdout) if cli else result.stdout.decode('utf-8', errors='replace')

    png(base / 'textures/placement/model.png')
    design = root / 'prop.model.json'
    design.write_text(json.dumps({'schemaVersion': 2, 'name': 'placement_prop', 'parts': [{
        'name': 'prop', 'primitive': 'cylinder', 'size': [32, 32, 40], 'origin': [0, 0, 20],
        'roll': 0, 'pitch': 0, 'yaw': 0, 'segments': 8, 'material': 'textures/placement/model',
        'uvScale': [1, 1], 'uvOffset': [0, 0], 'uvRotation': 0}]}), encoding='utf-8')
    run('model', ['model', 'build', design, '--output', base / 'models/placement/prop.md3', '--overwrite'])
    with wave.open(str(base / 'sound/placement/tone.wav'), 'wb') as audio:
        audio.setparams((1, 2, 22050, 0, 'NONE', 'not compressed'))
        audio.writeframes(b''.join(struct.pack('<h', int(1000 * math.sin(2*math.pi*220*i/22050))) for i in range(2205)))
    room = [((-528, -528, -16), (528, 528, 0)), ((-528, -528, 384), (528, 528, 400)),
            ((-528, -528, 0), (-512, 528, 384)), ((512, -528, 0), (528, 528, 384)),
            ((-512, -528, 0), (512, -512, 384)), ((-512, 512, 0), (512, 528, 384))]
    png(base / 'textures/placement/wall.png')
    prefix = [args.compiler.resolve(), '-game', 'quake3', '-fs_basepath', root / 'game', '-fs_homepath', root / 'home',
              '-fs_game', 'baseq3', '-threads', '2']
    for kind in ('classic', 'valve', 'primitive'):
        for face in range(6):
            png(base / f'textures/placement/{kind}/face{face}.png')
        brush = box((-29, 19, 27), (99, 83, 91), kind, f'placement/{kind}', True)
        source = maps / f'{kind}-source.map'
        source.write_text('{\n"classname" "worldspawn"\n' + '\n'.join(box(lo, hi, kind, 'placement/wall') for lo, hi in room)
                          + '\n' + brush + '\n}\n{\n"classname" "info_player_deathmatch"\n"origin" "0 -200 96"\n}\n'
                          + '{\n"classname" "light"\n"origin" "0 0 320"\n"light" "600"\n}\n', encoding='utf-8')
        snippet = root / f'{kind}-clipboard.map'
        snippet.write_text(brush + '\n{\n"classname" "misc_model"\n"origin" "32 150 24"\n"model" "models/placement/prop.md3"\n}\n'
                           + '{\n"classname" "target_speaker"\n"origin" "32 180 48"\n"noise" "sound/placement/tone.wav"\n}\n', encoding='utf-8')
        current = source
        for action, options in [('snap', ['--object', 'brush:6', '--grid', '16']),
                                ('duplicate', ['--object', 'brush:6', '--delta', '192,0,0']),
                                ('paste', ['--from', snippet, '--delta', '-192,0,0'])]:
            output = maps / f'{kind}-{action}.map'
            report = run(f'{kind}-{action}', ['map', action, current] + options + ['--output', output, '--overwrite'])
            assert report['textureLockPolicy'] == 'locked'
            current = output
        for stage, options in [('bsp', ['-meta']), ('vis', ['-vis', '-fast']), ('light', ['-light', '-fast'])]:
            output = run(f'{kind}-{stage}', prefix + options + [current], cli=False)
            assert 'LEAKED' not in output and 'ERROR:' not in output, output
        bsp = current.with_suffix('.bsp').read_bytes()
        assert bsp[:8] == b'IBSP' + struct.pack('<i', 46)

        def lump(index):
            offset, size = struct.unpack_from('<2i', bsp, 8+8*index)
            return bsp[offset:offset+size]

        assert len(lump(8)) // 12 == 9, 'six room brushes and three placed boxes'
        assert b'"noise" "sound/placement/tone.wav"' in lump(0), 'speaker keeps its sound dependency'
        assert b'"origin" "-160 180 48"' in lump(0), 'speaker shares the paste translation'
        shaders = [lump(1)[i:i+64].split(b'\0', 1)[0].decode() for i in range(0, len(lump(1)), 72)]
        seen, samples, models = set(), 0, 0
        for at in range(0, len(lump(13)), 104):
            header = struct.unpack_from('<12i', lump(13), at)
            name = shaders[header[0]]
            if name == 'textures/placement/model':
                models += 1
            if not name.startswith(f'textures/placement/{kind}/face'):
                continue
            face = int(name[-1])
            axis, sign = face//2, 1 if face % 2 else -1
            shift = None
            for index in range(header[3], header[3]+header[4]):
                vertex = struct.unpack_from('<10f4B', lump(10), index*44)
                copy = 'paste' if vertex[0] < -64 else 'duplicate' if vertex[0] > 128 else 'snap'
                delta = {'snap': (-3, -3, 5), 'duplicate': (189, -3, 5), 'paste': (-192, 0, 0)}[copy]
                point = tuple(vertex[i]-delta[i] for i in range(3))
                assert abs(point[axis] - ((99, 83, 91) if sign > 0 else (-29, 19, 27))[axis]) < 1e-4
                if kind == 'primitive':
                    s, t = primitive_basis(axis, sign)
                    uv = (0.03125*dot(point, s)+0.0078125*dot(point, t)+0.125,
                          -0.015625*dot(point, s)+0.0625*dot(point, t)-0.75)
                else:
                    s, t = axes(axis)
                    c, sn = math.cos(math.radians(15)), math.sin(math.radians(15))
                    uv = (((c*dot(point, s)+sn*dot(point, t))/-0.5+7)/64, ((sn*dot(point, s)-c*dot(point, t))/2+9)/64)
                difference = (vertex[3]-uv[0], vertex[4]-uv[1])
                if shift is None:
                    shift = difference
                assert max(abs(difference[i]-shift[i]) for i in range(2)) < 2e-5
                assert all(abs(x-round(x)) < 2e-5 for x in difference), (kind, copy, face, difference)
                seen.add((copy, face))
                samples += 1
        assert seen == {(operation, face) for operation in ('snap', 'duplicate', 'paste') for face in range(6)}
        assert models > 0, 'pasted generated model must be baked'
        subset = root / f'{kind}-assets.pk3'
        run(f'{kind}-subset', ['package', 'subset', base, subset, '--map-input', current, '--engine', 'idTech3', '--overwrite'])
        package = root / f'{kind}-level.pk3'
        run(f'{kind}-package', ['package', 'save-as', subset, package, '--format', 'pk3', '--add-file', current,
                              '--as', f'maps/{kind}.map', '--add-file', current.with_suffix('.bsp'), '--as', f'maps/{kind}.bsp', '--overwrite'])
        validation = run(f'{kind}-validate', ['package', 'validate', package])['validation']
        assert validation['valid'] and validation['uncheckedCount'] == 0
        with zipfile.ZipFile(package) as archive:
            assert archive.testzip() is None and archive.read(f'maps/{kind}.map') == current.read_bytes()
            for name in ('models/placement/prop.md3', 'textures/placement/model.png', 'sound/placement/tone.wav'):
                assert archive.read(name) == (base / name).read_bytes()
        variants.append({'dialect': kind, 'verifiedUvSamples': samples, 'verifiedFaces': len(seen), 'modelSurfaces': models,
                         'validation': validation, 'mapSha256': hashlib.sha256(current.read_bytes()).hexdigest(),
                         'bspSha256': hashlib.sha256(bsp).hexdigest()})
    (root / 'verified.json').write_text(json.dumps({'stepsPassed': len(steps), 'variants': variants}, indent=2), encoding='utf-8')
    print('PASS: three dialects, all placement UVs, generated model/audio dependencies and verified PK3 payloads.', flush=True)


if __name__ == '__main__':
    main()
