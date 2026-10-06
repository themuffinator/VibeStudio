"""Optional real q3map2 cap, persistence and PK3 workflow with generated assets.

Requires --binary, --compiler and --output-root below this project's .agents/tmp.
Uses no game data or game launch. Put the built application's Qt runtime on PATH.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
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
    assert root.is_relative_to(repo / '.agents' / 'tmp'), 'Use the project .agents/tmp area.'
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

    for name, color in [('wall', (106, 116, 126)), ('side', (44, 182, 210)), ('end', (204, 74, 114)),
                        ('arch-side', (112, 168, 62)), ('arch-end', (204, 151, 42))]:
        png(base / 'textures' / 'caps' / f'{name}.png', color)
    run('01-room', ['map', 'new', '--game', 'quake3', '--preset', 'room', '--texture', 'caps/wall',
                    '--output', maps / 'room.map', '--overwrite', '--json'])
    run('02-cylinder', ['map', 'add-patch', maps / 'room.map', '--shape', 'cylinder', '--size', '128,128,128',
                        '--origin', '0,0,128', '--texture', 'caps/side', '--output', maps / 'cylinder.map', '--overwrite', '--json'])
    original = run('03-inspect-source', ['map', 'inspect', maps / 'cylinder.map', '--json'])['map']['patches'][0]
    common = ['map', 'cap-patch', maps / 'cylinder.map', '--patch', '0', '--texture', 'caps/end',
              '--output', maps / 'capped.map', '--overwrite', '--json']
    run('04-straight-refusal', common + ['--boundary', 'first-column'], expected=4)
    caps = run('05-caps', common + ['--boundary', 'first-row', '--boundary', 'last-row'])['cap']
    assert len(caps['caps']) == 2
    source = run('06-inspect-capped', ['map', 'inspect', maps / 'capped.map', '--json'])['map']['patches']
    assert len(source) == 3 and source[0]['controlPoints'] == original['controlPoints']
    for cap in source[1:]:
        assert (cap['width'], cap['height']) == (9, 3)
        for p in cap['controlPoints']:
            assert abs(p['u'] - (0.5 + p['x'] / 128)) < 1e-10
            assert abs(p['v'] - (0.5 + p['y'] / 128)) < 1e-10
    run('06a-arch', ['map', 'add-patch', maps / 'capped.map', '--shape', 'plane', '--plane', 'xz', '--size', '128,128,128',
                     '--origin', '192,0,128', '--texture', 'caps/arch-side', '--output', maps / 'arch.map', '--overwrite', '--json'])
    run('06b-curve', ['map', 'edit-patch', maps / 'arch.map', '--patch', '3', '--point', '0,1', '--point', '1,1', '--point', '2,1',
                      '--delta', '0,64,0', '--invert', '--output', maps / 'curved-arch.map', '--overwrite', '--json'])
    arch_caps = run('06c-cap-arch', ['map', 'cap-patch', maps / 'curved-arch.map', '--patch', '3', '--boundary', 'first-row',
                                    '--boundary', 'last-row', '--texture', 'caps/arch-end', '--output', maps / 'complete.map', '--overwrite', '--json'])['cap']
    assert len(arch_caps['caps']) == 2 and not any(c['closed'] for c in arch_caps['caps'])
    source = run('06d-inspect-complete', ['map', 'inspect', maps / 'complete.map', '--json'])['map']['patches']
    assert len(source) == 6 and source[0]['controlPoints'] == original['controlPoints']
    for cap in source[4:]:
        assert (cap['width'], cap['height']) == (3, 3)
        for p in cap['controlPoints']:
            assert abs(p['u'] - (0.5 + (p['x'] - 192) / 128)) < 1e-10
            assert abs(p['v'] - (0.5 + p['y'] / 128)) < 1e-10
    prefix = [compiler, '-game', 'quake3', '-fs_basepath', root / 'game', '-fs_homepath', root / 'home', '-fs_game', 'baseq3', '-threads', '2']
    for label, options in [('07-bsp', ['-meta']), ('08-vis', ['-vis', '-fast']), ('09-light', ['-light', '-fast'])]:
        text = run(label, prefix + options + [maps / 'complete.map'], cli=False)
        assert 'LEAKED' not in text and 'ERROR:' not in text, (label, text)
    bsp = (maps / 'complete.bsp').read_bytes()
    assert bsp[:4] == b'IBSP' and struct.unpack_from('<i', bsp, 4)[0] == 46

    def lump(index):
        offset, size = struct.unpack_from('<2i', bsp, 8 + index * 8)
        return bsp[offset:offset + size]

    shaders = [lump(1)[p:p + 64].split(b'\0', 1)[0].decode() for p in range(0, len(lump(1)), 72)]
    controls, seam_points, seen, shifts = 0, 0, set(), {}
    for at in range(0, len(lump(13)), 104):
        header = struct.unpack_from('<12i', lump(13), at)
        shader = shaders[header[0]]
        if shader not in ('textures/caps/end', 'textures/caps/arch-end'):
            continue
        assert header[2] == 2, 'Expected quadratic cap controls.'
        compiled = [struct.unpack_from('<10f4B', lump(10), i * 44) for i in range(header[3], header[3] + header[4])]
        height = round(compiled[0][2])
        identity = (shader, height)
        assert height in (64, 192) and identity not in seen
        seen.add(identity)
        authored = next(p for p in source if 'textures/' + p['textureName'] == shader and abs(p['controlPoints'][0]['z'] - height) < 1e-6)
        expected = authored['controlPoints']
        assert len(compiled) == len(expected) == (27 if shader.endswith('/end') else 9)
        shift = None
        for v in compiled:
            candidates = [p for p in expected if max(abs(v[i] - p[k]) for i, k in enumerate(('x', 'y', 'z'))) < 1e-5]
            assert candidates, (v, height)
            p = candidates[0]
            delta = (v[3] - p['u'], v[4] - p['v'])
            if shift is None:
                shift = delta
            assert max(abs(delta[i] - shift[i]) for i in range(2)) < 1e-6
            assert all(abs(x - round(x)) < 1e-6 for x in delta)
            assert all(math.isfinite(x) for x in v[:10])
            assert v[9] < -0.99 if height == 64 else v[9] > 0.99, (height, v)
            controls += 1
        original_surface = original if shader.endswith('/end') else source[3]
        for p in authored['controlPoints'][-authored['width']:]:
            assert any(max(abs(v[i] - p[k]) for i, k in enumerate(('x', 'y', 'z'))) < 1e-5 for v in compiled)
            assert any(max(abs(q[k] - p[k]) for k in ('x', 'y', 'z')) < 1e-8 for q in original_surface['controlPoints'])
            seam_points += 1
        shifts[f'{shader}:{height}'] = shift
    assert len(seen) == 4 and controls == 72 and seam_points == 24
    run('10-dependencies', ['map', 'dependencies', maps / 'complete.map', '--package', base, '--engine', 'idTech3', '--json'])
    run('11-subset', ['package', 'subset', base, root / 'cap-assets.pk3', '--map-input', maps / 'complete.map', '--engine', 'idTech3', '--overwrite', '--json'])
    run('12-package', ['package', 'save-as', root / 'cap-assets.pk3', root / 'capped-level.pk3', '--format', 'pk3',
                       '--add-file', maps / 'complete.bsp', '--as', 'maps/complete.bsp', '--overwrite', '--json'])
    validation = run('13-validate', ['package', 'validate', root / 'capped-level.pk3', '--json'])
    assert validation['validation']['valid'] and validation['validation']['verifiedCount'] == 6
    report = {'stepsPassed': len(steps), 'compiledCaps': len(seen), 'verifiedControls': controls,
              'boundaryControls': seam_points, 'normals': 'bottom -Z, top +Z', 'uvTileOffsets': shifts,
              'bspSha256': hashlib.sha256(bsp).hexdigest(), 'caps': caps, 'archCaps': arch_caps, 'validation': validation}
    (root / 'verified.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'Verified {controls} compiled cap controls, {seam_points} boundary controls, facing, UVs and six package payloads.', flush=True)


if __name__ == '__main__':
    main()
