"""Optional VibeMap3 seam check using original generated fixtures only.

Run from the repository with --binary, --compiler and an explicit disposable
--output-root under .agents/tmp. Writes exact commands, logs and measured artifacts.
Does not launch a game. Requires the built application's Qt runtime on PATH.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess
import zlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    root = args.output_root.resolve()
    assert root.is_relative_to(repo / '.agents' / 'tmp'), 'Use a project .agents/tmp output directory.'
    binary, compiler = args.binary.resolve(), args.compiler.resolve()
    base = root / 'game' / 'baseq3'
    maps = base / 'maps'
    maps.mkdir(parents=True, exist_ok=True)
    (root / 'home').mkdir(exist_ok=True)
    commands, steps = [], []

    def run(label, words, cli=True, expected=0):
        cmd = ([str(binary), '--cli', '--settings-file', str(root / 'settings.ini')] + list(map(str, words))
               if cli else list(map(str, words)))
        commands.append(cmd)
        (root / 'commands.json').write_text(json.dumps(commands, indent=2), encoding='utf-8')
        result = subprocess.run(cmd, cwd=repo, capture_output=True, timeout=120)
        (root / f'{label}.stdout.txt').write_bytes(result.stdout)
        (root / f'{label}.stderr.txt').write_bytes(result.stderr)
        steps.append({'step': label, 'exitCode': result.returncode, 'expectedExitCode': expected})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert result.returncode == expected, (label, result.stdout, result.stderr)
        print(label, result.returncode, flush=True)
        return json.loads(result.stdout) if cli else result.stdout.decode('utf-8', errors='replace')

    def png(path, color):
        def chunk(kind, data):
            return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
        raw = b''.join(b'\0' + b''.join(bytes(color if (x // 8 + y // 8) % 2 else (28, 36, 47)) for x in range(64)) for y in range(64))
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', 64, 64, 8, 2, 0, 0, 0))
                         + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))

    for name, color in [('wall', (106, 116, 126)), ('first', (44, 182, 210)), ('second', (204, 74, 114))]:
        png(base / 'textures' / 'stitch' / f'{name}.png', color)
    run('01-room', ['map', 'new', '--game', 'quake3', '--preset', 'room', '--texture', 'stitch/wall',
                    '--output', maps / 'room.map', '--overwrite', '--json'])
    previous = maps / 'room.map'
    for index, (name, x) in enumerate([('first', -64), ('second', 68)]):
        output = maps / f'{name}.map'
        run(f'02-{name}', ['map', 'add-patch', previous, '--shape', 'plane', '--size', '128,128,1',
                           '--origin', f'{x},64,64', '--texture', f'stitch/{name}', '--output', output, '--overwrite', '--json'])
        previous = output
        output = maps / f'{name}-curved.map'
        run(f'03-{name}-curve', ['map', 'edit-patch', previous, '--patch', index, '--point', '1,0', '--point', '1,1',
                                '--point', '1,2', '--delta', '0,0,48', '--output', output, '--overwrite', '--json'])
        previous = output
        output = maps / f'{name}-handle.map'
        run(f'04-{name}-handle', ['map', 'edit-patch', previous, '--patch', index, '--point', '0,1', '--point', '1,1',
                                 '--point', '2,1', '--delta', f'0,0,{8 if index == 0 else 24}',
                                 '--output', output, '--overwrite', '--json'])
        previous = output
    run('05-refine', ['map', 'edit-patch', previous, '--patch', '1', '--subdivide', 'rows', '--output', maps / 'pair.map', '--overwrite', '--json'])
    common = ['map', 'stitch-patches', maps / 'pair.map', '--first', '0:last-column', '--second', '1:first-column',
              '--match-tangents', '--uv', 'first', '--output', maps / 'stitched.map', '--overwrite', '--json']
    run('06-gap-refusal', common + ['--max-gap', '1'], expected=4)
    merge = run('07-stitch', common)['stitch']
    assert merge['boundaryPoints'] == 5 and merge['maximumGap'] == 4
    source = run('08-inspect', ['map', 'inspect', maps / 'stitched.map', '--json'])['map']['patches']
    assert len(source) == 2 and all(p['width'] == 3 and p['height'] == 5 for p in source)
    for row in range(5):
        a, b = source[0]['controlPoints'][row * 3 + 2], source[1]['controlPoints'][row * 3]
        ha, hb = source[0]['controlPoints'][row * 3 + 1], source[1]['controlPoints'][row * 3 + 1]
        assert all(abs(a[k] - b[k]) < 1e-10 for k in ('x', 'y', 'z', 'u', 'v'))
        assert all(abs(ha[k] + hb[k] - 2 * a[k]) < 1e-10 for k in ('x', 'y', 'z'))
    prefix = [compiler, '-game', 'quake3', '-fs_basepath', root / 'game', '-fs_homepath', root / 'home', '-fs_game', 'baseq3', '-threads', '2']
    for name, options in [('09-bsp', ['-meta']), ('10-vis', ['-vis', '-fast']), ('11-light', ['-light', '-fast'])]:
        text = run(name, prefix + options + [maps / 'stitched.map'], cli=False)
        assert 'LEAKED' not in text and 'ERROR:' not in text
    bsp = (maps / 'stitched.bsp').read_bytes()
    assert bsp[:4] == b'IBSP' and struct.unpack_from('<i', bsp, 4)[0] == 46

    def lump(index):
        offset, size = struct.unpack_from('<2i', bsp, 8 + index * 8)
        return bsp[offset:offset + size]

    shaders = [lump(1)[p:p + 64].split(b'\0', 1)[0].decode() for p in range(0, len(lump(1)), 72)]
    matched, vertices, worst, shifts = {}, 0, 0.0, {}
    for at in range(0, len(lump(13)), 104):
        header = struct.unpack_from('<12i', lump(13), at)
        shader = shaders[header[0]]
        if shader not in ('textures/stitch/first', 'textures/stitch/second'):
            continue
        assert header[2] == 2, 'Expected an emitted quadratic patch, not a brush or triangle soup.'
        authored = next(p for p in source if shader == 'textures/' + p['textureName'])
        points = authored['controlPoints']
        assert header[4] == len(points) == 15
        compiled = [struct.unpack_from('<10f4B', lump(10), index * 44) for index in range(header[3], header[3] + header[4])]
        shift = None
        for v in compiled:
            candidates = [p for p in points if max(abs(v[i] - p[k]) for i, k in enumerate(('x', 'y', 'z'))) < 1e-5]
            assert len(candidates) == 1, (shader, v, candidates)
            p = candidates[0]
            delta = (v[3] - p['u'], v[4] - p['v'])
            if shift is None:
                shift = delta
            deviation = max(abs(delta[i] - shift[i]) for i in range(2))
            # q3map2 rebases each surface by a uniform whole-tile offset.
            assert deviation < 1e-6 and max(abs(d - round(d)) for d in delta) < 1e-6, (shader, v, p, shift)
            worst = max(worst, deviation)
            vertices += 1
        shifts[shader] = shift
        matched[shader] = [v[:3] + (v[3] - shift[0], v[4] - shift[1]) + v[5:] for v in compiled]
    assert len(matched) == 2 and vertices == 30
    a = [v for v in matched['textures/stitch/first'] if abs(v[0] - 2) < 1e-6]
    b = [v for v in matched['textures/stitch/second'] if abs(v[0] - 2) < 1e-6]
    assert len(a) == len(b) == 5 and sorted(v[:5] for v in a) == sorted(v[:5] for v in b)
    run('12-dependencies', ['map', 'dependencies', maps / 'stitched.map', '--package', base, '--engine', 'idTech3', '--json'])
    run('13-subset', ['package', 'subset', base, root / 'stitch-assets.pk3', '--map-input', maps / 'stitched.map', '--engine', 'idTech3', '--overwrite', '--json'])
    run('14-package', ['package', 'save-as', root / 'stitch-assets.pk3', root / 'stitched-level.pk3', '--format', 'pk3',
                       '--add-file', maps / 'stitched.bsp', '--as', 'maps/stitched.bsp', '--overwrite', '--json'])
    validation = run('15-validate', ['package', 'validate', root / 'stitched-level.pk3', '--json'])
    assert validation['validation']['valid'] and validation['validation']['verifiedCount'] == 4
    report = {'stepsPassed': len(steps), 'compiledPatches': len(matched), 'verifiedControls': vertices,
              'sharedSeamControls': len(a), 'worstUvError': worst, 'compilerUvTileOffsets': shifts,
              'bspSha256': hashlib.sha256(bsp).hexdigest(),
              'stitch': merge, 'validation': validation}
    (root / 'verified.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'Verified {vertices} compiled controls and {len(a)} shared seam controls.', flush=True)


if __name__ == '__main__':
    main()
