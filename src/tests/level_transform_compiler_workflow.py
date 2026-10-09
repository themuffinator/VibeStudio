"""Optional generated-map UV proof through VibeMap3 and PK3 publication.

Requires --binary, --compiler and --output-root under this project's .agents/tmp.
No game assets, game launch, native input or screen capture are used.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct
import subprocess
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
    maps.mkdir(parents=True, exist_ok=True)
    (root / 'home').mkdir(exist_ok=True)
    steps, reports = [], []

    def run(label, words, cli=True, expected=0):
        cmd = ([str(args.binary.resolve()), '--cli', '--settings-file', str(root / 'settings.ini')] + list(map(str, words))
               if cli else list(map(str, words)))
        result = subprocess.run(cmd, cwd=repo, capture_output=True, timeout=120)
        (root / f'{label}.stdout.txt').write_bytes(result.stdout)
        (root / f'{label}.stderr.txt').write_bytes(result.stderr)
        steps.append({'step': label, 'command': cmd, 'exitCode': result.returncode, 'expectedExitCode': expected})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert result.returncode == expected, (label, result.stdout, result.stderr)
        print(label, result.returncode, flush=True)
        return json.loads(result.stdout) if cli else result.stdout.decode('utf-8', errors='replace')

    room = [((-528, -528, -16), (528, 528, 0)), ((-528, -528, 384), (528, 528, 400)),
            ((-528, -528, 0), (-512, 528, 384)), ((512, -528, 0), (528, 528, 384)),
            ((-512, -528, 0), (512, -512, 384)), ((-512, 512, 0), (512, 528, 384))]
    png(base / 'textures' / 'lock' / 'wall.png')
    for kind in ('classic', 'valve', 'primitive'):
        for face in range(6):
            png(base / 'textures' / 'lock' / kind / f'face{face}.png')
        source = maps / f'{kind}-source.map'
        brushes = [box(lo, hi, kind, 'lock/wall') for lo, hi in room]
        brushes.append(box((-32, 16, 24), (96, 80, 88), kind, f'lock/{kind}', True))
        source.write_text('{\n"classname" "worldspawn"\n' + '\n'.join(brushes)
                          + '\n}\n{\n"classname" "info_player_deathmatch"\n"origin" "256 0 96"\n}\n'
                          + '{\n"classname" "light"\n"origin" "0 0 320"\n"light" "600"\n}\n', encoding='utf-8')
        current = source
        sequence = [('move', ['--delta', '19.25,-8.5,3.75']), ('rotate', ['--degrees', '90', '--pivot', '0,0,0']),
                    ('flip', ['--axis', 'x']), ('resize', ['--mins', '-128,-64,48', '--maxs', '128,0,128', '--allow-valve220'])]
        for command, options in sequence:
            output = maps / f'{kind}-{command}.map'
            run(f'{kind}-{command}', ['map', command, current, '--object', 'brush:6', '--texture-lock', 'on']
                + options + ['--output', output, '--overwrite', '--json'])
            current = output
        prefix = [args.compiler.resolve(), '-game', 'quake3', '-fs_basepath', root / 'game', '-fs_homepath', root / 'home', '-fs_game', 'baseq3', '-threads', '2']
        for stage, options in [('bsp', ['-meta']), ('vis', ['-vis', '-fast']), ('light', ['-light', '-fast'])]:
            text = run(f'{kind}-{stage}', prefix + options + [current], cli=False)
            assert 'LEAKED' not in text and 'ERROR:' not in text, text
        bsp = current.with_suffix('.bsp').read_bytes()
        assert bsp[:4] == b'IBSP' and struct.unpack_from('<i', bsp, 4)[0] == 46

        def lump(index):
            offset, size = struct.unpack_from('<2i', bsp, 8+8*index)
            return bsp[offset:offset+size]

        shaders = [lump(1)[p:p+64].split(b'\0', 1)[0].decode() for p in range(0, len(lump(1)), 72)]
        vertices, seen, tile_offsets = 0, set(), {}
        for at in range(0, len(lump(13)), 104):
            header = struct.unpack_from('<12i', lump(13), at)
            name = shaders[header[0]]
            if not name.startswith(f'textures/lock/{kind}/face'):
                continue
            face = int(name[-1])
            axis, sign = face//2, 1 if face % 2 else -1
            seen.add(face)
            shift = None
            for index in range(header[3], header[3]+header[4]):
                v = struct.unpack_from('<10f4B', lump(10), index*44)
                # Independent inverse of move -> Z turn -> X mirror -> resize.
                p = ((v[1]+64)*2-32, (v[0]+128)/4+16, (v[2]-48)/1.25+24)
                assert abs(p[axis] - ((96, 80, 88) if sign > 0 else (-32, 16, 24))[axis]) < 1e-4
                if kind == 'primitive':
                    s, t = primitive_basis(axis, sign)
                    u_expected = 0.03125*dot(p, s)+0.0078125*dot(p, t)+0.125
                    v_expected = -0.015625*dot(p, s)+0.0625*dot(p, t)-0.75
                else:
                    s, t = axes(axis)
                    c, sn = math.cos(math.radians(15)), math.sin(math.radians(15))
                    u_expected = ((c*dot(p, s)+sn*dot(p, t)) / -0.5+7) / 64
                    v_expected = ((sn*dot(p, s)-c*dot(p, t)) / 2+9) / 64
                delta = (v[3]-u_expected, v[4]-v_expected)
                if shift is None:
                    shift = delta
                assert max(abs(delta[i]-shift[i]) for i in range(2)) < 2e-5, (kind, face, delta, shift)
                assert all(abs(x-round(x)) < 2e-5 for x in delta), (kind, face, delta)
                vertices += 1
            tile_offsets[str(face)] = shift
        assert seen == set(range(6)) and vertices >= 24, (kind, shaders, seen, vertices)
        run(f'{kind}-dependencies', ['map', 'dependencies', current, '--package', base, '--engine', 'idTech3', '--json'])
        subset = root / f'{kind}-assets.pk3'
        run(f'{kind}-subset', ['package', 'subset', base, subset, '--map-input', current, '--engine', 'idTech3', '--overwrite', '--json'])
        package = root / f'{kind}-level.pk3'
        run(f'{kind}-package', ['package', 'save-as', subset, package, '--format', 'pk3', '--add-file', current.with_suffix('.bsp'),
                              '--as', f'maps/{kind}-resize.bsp', '--overwrite', '--json'])
        validation = run(f'{kind}-validate', ['package', 'validate', package, '--json'])['validation']
        assert validation['valid'] and validation['verifiedCount'] == 8, validation
        reports.append({'dialect': kind, 'verifiedVertices': vertices, 'tileOffsets': tile_offsets,
                        'bspSha256': hashlib.sha256(bsp).hexdigest(), 'packageValidation': validation})
    (root / 'verified.json').write_text(json.dumps({'stepsPassed': len(steps), 'variants': reports}, indent=2), encoding='utf-8')
    print(f'PASS: {len(steps)} steps, three compiled dialects, {sum(r["verifiedVertices"] for r in reports)} UV samples, three verified PK3s.', flush=True)


if __name__ == '__main__':
    main()
