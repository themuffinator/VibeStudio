"""Synthetic face clipboard -> save -> BSP/VIS/LIGHT -> PK3 acceptance.

Output must live under this project's .agents/tmp. No game or native input.
The shared box/PNG helper generates original fixtures. BSP layout references
and licences are recorded in docs/CREDITS.md under Model collision.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct
import subprocess
from level_compiler_test_helpers import box, png, primitive_basis, dot


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    binary_hash = hashlib.sha256(args.binary.read_bytes()).hexdigest()
    compiler_hash = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    repo = Path(__file__).resolve().parents[2]
    root = args.output_root.resolve()
    assert root.is_relative_to(repo / '.agents' / 'tmp')
    base = root / 'game' / 'baseq3'
    maps = base / 'maps'
    maps.mkdir(parents=True, exist_ok=True)
    (root / 'home').mkdir(exist_ok=True)
    steps, variants = [], []

    def run(label, words, cli=True, expected=0):
        command = ([str(args.binary.resolve()), '--cli', '--settings-file', str(root / 'settings.ini')] + list(map(str, words))
                   if cli else list(map(str, words)))
        result = subprocess.run(command, cwd=repo, capture_output=True, timeout=180)
        (root / f'{label}.stdout.txt').write_bytes(result.stdout)
        (root / f'{label}.stderr.txt').write_bytes(result.stderr)
        steps.append({'step': label, 'arguments': command, 'exitCode': result.returncode, 'expectedExitCode': expected})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert result.returncode == expected, (label, result.stdout, result.stderr)
        print(label, result.returncode, flush=True)
        return json.loads(result.stdout) if cli else result.stdout.decode('utf-8', errors='replace')

    room = [((-528, -528, -16), (528, 528, 0)), ((-528, -528, 384), (528, 528, 400)),
            ((-528, -528, 0), (-512, 528, 384)), ((512, -528, 0), (528, 528, 384)),
            ((-512, -528, 0), (512, -512, 384)), ((-512, 512, 0), (512, 528, 384))]
    for name in ['wall', 'copied'] + [f'box/face{i}' for i in range(6)]:
        png(base / 'textures' / 'attributes' / f'{name}.png', 64, 64)
    for kind in ('classic', 'valve', 'primitive'):
        source = maps / f'{kind}-source.map'
        brushes = [box(lo, hi, kind, 'attributes/wall') for lo, hi in room]
        brushes.append(box((-32, 16, 24), (96, 80, 88), kind, 'attributes/box', True))
        source.write_text('{\n"classname" "worldspawn"\n' + '\n'.join(brushes) + '\n}\n'
                          + '{\n"classname" "info_player_deathmatch"\n"origin" "256 0 96"\n}\n'
                          + '{\n"classname" "light"\n"origin" "0 0 320"\n"light" "600"\n}\n', encoding='utf-8')
        original = source.read_bytes()
        raw = root / f'{kind}-faces.map'
        run(f'{kind}-copy', ['map', 'copy-surface', source, '--target', 'face:6:2', '--target', 'face:6:6',
                            '--format', 'map', '--output', raw, '--overwrite', '--json'])
        records = raw.read_text(encoding='utf-8').splitlines()
        assert len(records) == 2
        # Preserve geometry but give the last record deliberately different
        # values and contents. Face paste must retain target contents (zero).
        line = records[-1].replace('attributes/box/face5', 'attributes/copied')
        if kind == 'primitive':
            line = line[:line.rfind(' 0 0 0')] + ' 1024 0 0'
        elif kind == 'valve':
            line = line[:line.rfind(']') + 1] + ' 90 -2 3 1024 0 0'
        else:
            line = line[:line.index('attributes/copied') + len('attributes/copied')] + ' 7 9 90 -2 3 1024 0 0'
        raw.write_text(records[0] + '\n' + line + '\n', encoding='utf-8')
        definition = raw.read_bytes()
        output = maps / f'{kind}-attributes.map'
        command = ['map', 'paste-surface', source, '--clipboard', raw, '--target', 'face:6:2', '--target', 'face:6:6', '--output', output, '--json']
        run(f'{kind}-dry-run', command + ['--dry-run', '--overwrite'])
        result = run(f'{kind}-paste', command + ['--overwrite'])
        assert result['mode'] == 'attributes' and result['changedFaces'] == 2 and result['convertedFaces'] == 0
        saved = output.read_bytes()
        run(f'{kind}-existing-output', command, expected=4)
        assert output.read_bytes() == saved and source.read_bytes() == original and raw.read_bytes() == definition
        prefix = [args.compiler.resolve(), '-game', 'quake3', '-fs_basepath', root / 'game', '-fs_homepath', root / 'home', '-fs_game', 'baseq3', '-threads', '2']
        for stage, flags in [('bsp', ['-meta']), ('vis', ['-vis', '-fast']), ('light', ['-light', '-fast'])]:
            log = run(f'{kind}-{stage}', prefix + flags + [output], cli=False)
            assert 'LEAKED' not in log and 'ERROR:' not in log, log
        bsp = output.with_suffix('.bsp').read_bytes()
        assert bsp[:4] == b'IBSP' and struct.unpack_from('<i', bsp, 4)[0] == 46

        def lump(index):
            at, size = struct.unpack_from('<2i', bsp, 8 + 8 * index)
            assert 0 <= at <= len(bsp) and 0 <= size <= len(bsp) - at
            return bsp[at:at + size]

        shaders = [lump(1)[i:i + 64].split(b'\0', 1)[0].decode() for i in range(0, len(lump(1)), 72)]
        seen, samples = set(), 0
        for at in range(0, len(lump(13)), 104):
            surface = struct.unpack_from('<12i', lump(13), at)
            if shaders[surface[0]] != 'textures/attributes/copied':
                continue
            tile = None
            for index in range(surface[3], surface[3] + surface[4]):
                v = struct.unpack_from('<10f4B', lump(10), index * 44)
                axis = 2 if v[9] > .99 else 0
                assert abs(v[axis] - (88 if axis == 2 else 96)) < 1e-4 and v[7 + axis] > .99
                seen.add(axis)
                first, second = (v[0], v[1]) if axis == 2 else (v[1], v[2])
                if kind == 'classic':
                    # Classic axial basis: a positive 90 degree angle sends
                    # U along second and V along first before signed scales.
                    expected = ((second / -2 + 7) / 64, (first / 3 + 9) / 64)
                elif kind == 'valve':
                    # Fixture U/V have a 15-degree basis with unequal lengths.
                    # +90 about cross(U,V) rotates within that existing basis.
                    c, s = math.cos(math.radians(15)), math.sin(math.radians(15))
                    expected = (((2*s*first - 2*c*second) / -2 + 7) / 64,
                                ((.5*c*first + .5*s*second) / 3 + 9) / 64)
                else:
                    u, w = primitive_basis(axis, 1)
                    x, y = dot(v[:3], u), dot(v[:3], w)
                    expected = (.03125*x + .0078125*y + .125, -.015625*x + .0625*y - .75)
                delta = tuple(v[3 + i] - expected[i] for i in range(2))
                if tile is None:
                    tile = delta
                assert all(abs(delta[i] - tile[i]) < 3e-5 and abs(delta[i] - round(delta[i])) < 3e-5 for i in range(2)), (kind, axis, v, expected, delta)
                samples += 1
        assert seen == {0, 2} and samples >= 8, (kind, seen, samples)
        run(f'{kind}-dependencies', ['map', 'dependencies', output, '--package', base, '--engine', 'idTech3', '--json'])
        subset, package = root / f'{kind}-assets.pk3', root / f'{kind}-level.pk3'
        run(f'{kind}-subset', ['package', 'subset', base, subset, '--map-input', output, '--engine', 'idTech3', '--overwrite', '--json'])
        run(f'{kind}-package', ['package', 'save-as', subset, package, '--format', 'pk3', '--add-file', output.with_suffix('.bsp'),
                              '--as', f'maps/{kind}-attributes.bsp', '--overwrite', '--json'])
        validation = run(f'{kind}-validate', ['package', 'validate', package, '--json'])['validation']
        assert validation['valid'] and validation['verifiedCount'] >= 7
        variants.append({'dialect': kind, 'verifiedUvSamples': samples, 'packageValidation': validation, 'bspSha256': hashlib.sha256(bsp).hexdigest()})
    assert hashlib.sha256(args.binary.read_bytes()).hexdigest() == binary_hash, 'Application changed during the workflow; rerun against one build.'
    assert hashlib.sha256(args.compiler.read_bytes()).hexdigest() == compiler_hash
    evidence = {'ok': True, 'stepsPassed': len(steps), 'variants': variants,
                'binarySha256': binary_hash, 'compilerSha256': compiler_hash,
                'scope': 'Synthetic face clipboard BSP/VIS/LIGHT, independent compiled UVs and PK3 validation. No game launched.'}
    (root / 'verified.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    print(f'PASS: {len(steps)} steps, {sum(v["verifiedUvSamples"] for v in variants)} compiled UV samples.', flush=True)


if __name__ == '__main__':
    main()
