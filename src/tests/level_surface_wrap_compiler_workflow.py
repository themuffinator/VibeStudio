"""Optional synthetic seamless-UV proof through VibeMap3 and PK3 publication.

Requires --binary, --compiler and --output-root under this project's .agents/tmp.
Uses original generated fixtures. No game launch, native input or screen capture.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess
from level_compiler_test_helpers import box, png


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    parser.add_argument('--mapping-only', action='store_true', help='Retain target materials and verify unequal source/target image sizes.')
    parser.add_argument('--radiant-project', action='store_true', help='Verify native projected brush/patch paste (requires --mapping-only).')
    parser.add_argument('--stroke', action='store_true', help='Verify ordered wrapping around two corners (requires --mapping-only).')
    args = parser.parse_args()
    if args.radiant_project and not args.mapping_only:
        parser.error('--radiant-project requires --mapping-only to identify compiled target surfaces')
    if args.stroke and (not args.mapping_only or args.radiant_project):
        parser.error('--stroke requires --mapping-only and seamless mode')
    repo = Path(__file__).resolve().parents[2]
    root = args.output_root.resolve()
    assert root.is_relative_to(repo / '.agents' / 'tmp')
    base = root / 'game' / 'baseq3'
    maps = base / 'maps'
    maps.mkdir(parents=True, exist_ok=True)
    (root / 'home').mkdir(exist_ok=True)
    steps, reports = [], []

    def run(label, words, cli=True, expected=0):
        command = ([str(args.binary.resolve()), '--cli', '--settings-file', str(root / 'settings.ini')] + list(map(str, words))
                   if cli else list(map(str, words)))
        result = subprocess.run(command, cwd=repo, capture_output=True, timeout=120)
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
    for name in ['wall', 'seam', 'patch'] + [f'box/face{face}' for face in range(6)]:
        size = (128, 64) if name == 'seam' else (64, 256)
        if args.stroke and name == 'box/face3':
            size = (256, 32)
        png(base / 'textures' / 'wrap' / f'{name}.png', *(size if args.mapping_only else (64, 64)))
    for kind in ('classic', 'valve', 'primitive'):
        source = maps / f'{kind}-source.map'
        brushes = [box(lo, hi, kind, 'wrap/wall') for lo, hi in room]
        brushes.append(box((-32, 16, 24), (96, 80, 88), kind, 'wrap/box', True))
        if args.radiant_project:
            columns = ['( ' + ' '.join(f'( {x} {y} {224 if x == y == 0 else 160} 0 0 )' for y in (-96, 0, 96)) + ' )' for x in (-96, 0, 96)]
            brushes.append('{\npatchDef2\n{\nwrap/patch\n( 3 3 0 0 0 )\n(\n' + '\n'.join(columns) + '\n)\n}\n}')
        source.write_text('{\n"classname" "worldspawn"\n' + '\n'.join(brushes)
                          + '\n}\n{\n"classname" "info_player_deathmatch"\n"origin" "256 0 96"\n}\n'
                          + '{\n"classname" "light"\n"origin" "0 0 320"\n"light" "600"\n}\n', encoding='utf-8')
        original = source.read_bytes()
        clipboard = root / f'{kind}.surface.json'
        run(f'{kind}-copy', ['map', 'copy-surface', source, '--target', 'face:6:6', '--output', clipboard, '--overwrite', '--json'])
        definition = json.loads(clipboard.read_text())
        definition['material'] = 'wrap/seam'
        # Genuine shear with mirrored handedness. For classic targets the
        # seamless batch itself must request and perform map-wide conversion.
        definition['mapping'] = ({'kind': 'matrix', 'matrix': [0.03125, 0.015625, 0.125, -0.015625, 0.0625, -0.75]}
                                 if kind == 'primitive' else {'kind': 'valve220', 'rotation': 0, 'scale': [1, 1],
                                                              'u': [2, 0, 0, 17], 'v': [0.5, -3, 0, -9]})
        if args.radiant_project and kind == 'classic':
            definition['mapping'] = {'kind': 'classic', 'shift': [17, -9], 'rotation': 0, 'scale': [0.5, 1 / 3]}
        clipboard.write_text(json.dumps(definition, indent=2), encoding='utf-8')
        copied = clipboard.read_bytes()
        output = maps / f'{kind}-wrapped.map'
        paste = ['map', 'paste-surface', source, '--clipboard', clipboard, '--target', 'face:6:6', '--target', 'face:6:2',
                 '--mode', 'radiant-project' if args.radiant_project else 'seamless', '--output', output, '--overwrite', '--json']
        if args.mapping_only:
            paste += ['--mapping-only', '--texture-size', '128,64', '--material-size', 'wrap/box/face5=64,256',
                      '--material-size', 'wrap/box/face1=64,256']
        if args.radiant_project:
            paste += ['--target', 'patch:0', '--material-size', 'wrap/patch=64,256']
        if args.stroke:
            paste += ['--stroke', '--target', 'face:6:4', '--material-size', 'wrap/box/face3=256,32']
        if kind == 'classic' and not args.radiant_project:
            refusal = run(f'{kind}-consent', paste, expected=4)
            assert 'Valve 220' in json.dumps(refusal)
        report = run(f'{kind}-wrap', paste + ([] if args.radiant_project else ['--allow-valve220']))
        assert report['convertedFaces'] == (42 if kind == 'classic' and not args.radiant_project else 0), report
        if args.radiant_project:
            assert report['changedPatches'] == 1 and report['edgeOnFaces'] == (0 if kind == 'classic' else 1), report
        if args.stroke:
            assert report['strokeHits'] == 3 and report['sourceAdvanced'] and report['finalSource']['material'] == 'wrap/box/face3', report
        assert original == source.read_bytes() and copied == clipboard.read_bytes()
        prefix = [args.compiler.resolve(), '-game', 'quake3', '-fs_basepath', root / 'game', '-fs_homepath', root / 'home',
                  '-fs_game', 'baseq3', '-threads', '2']
        for stage, options in [('bsp', ['-meta']), ('vis', ['-vis', '-fast']), ('light', ['-light', '-fast'])]:
            log = run(f'{kind}-{stage}', prefix + options + [output], cli=False)
            assert 'LEAKED' not in log and 'ERROR:' not in log, log
        bsp = output.with_suffix('.bsp').read_bytes()
        assert bsp[:4] == b'IBSP' and struct.unpack_from('<i', bsp, 4)[0] == 46

        def lump(index):
            offset, size = struct.unpack_from('<2i', bsp, 8 + 8 * index)
            assert 0 <= offset <= len(bsp) and 0 <= size <= len(bsp) - offset
            return bsp[offset:offset + size]

        shaders = [lump(1)[p:p + 64].split(b'\0', 1)[0].decode() for p in range(0, len(lump(1)), 72)]
        seen, vertices, offsets = set(), 0, {}
        for at in range(0, len(lump(13)), 104):
            header = struct.unpack_from('<12i', lump(13), at)
            wanted = {'textures/wrap/box/face1', 'textures/wrap/box/face5'} if args.mapping_only else {'textures/wrap/seam'}
            if args.radiant_project:
                wanted.add('textures/wrap/patch')
            if args.stroke:
                wanted.add('textures/wrap/box/face3')
            if shaders[header[0]] not in wanted:
                continue
            shift = None
            for index in range(header[3], header[3] + header[4]):
                v = struct.unpack_from('<10f4B', lump(10), index * 44)
                face = ('patch' if shaders[header[0]] == 'textures/wrap/patch' else 'corner' if args.stroke and v[8] > 0.99
                        else 'top' if v[9] > 0.99 else 'side')
                seen.add(face)
                if face == 'patch':
                    assert 159.99 <= v[2] <= 224.01 and abs(v[0]) <= 96.01 and abs(v[1]) <= 96.01
                elif face == 'corner':
                    assert abs(v[1] - 80) < 1e-4 and v[8] > 0.99
                else:
                    assert abs(v[2] - 88) < 1e-4 if face == 'top' else abs(v[0] - 96) < 1e-4 and v[7] > 0.99
                # Independent inverse of a +90-degree Y turn about x=96,z=88.
                x, y = (v[0], v[1]) if args.radiant_project or face == 'top' else (184 - v[2], v[1])
                if face == 'corner':
                    # Undo +90 degrees about Z at x=96,y=80, then undo the
                    # first hinge. This proves the second wrap uses the new
                    # source plane rather than projecting every hit from top.
                    x, y = 184 - v[2], 176 - v[0]
                expected = ((0.03125 * y + 0.015625 * x + 0.125, -0.015625 * y + 0.0625 * x - 0.75)
                            if kind == 'primitive' else ((2 * x + 17) / 64, (0.5 * x - 3 * y - 9) / 64))
                if args.radiant_project and kind == 'classic':
                    first, second = (v[1], v[2]) if face == 'side' else (v[0], v[1])
                    expected = ((2 * first + 17) / 64, (-3 * second - 9) / 64)
                if args.mapping_only:
                    width, height = (256, 32) if face == 'corner' else (64, 256)
                    expected = (expected[0] * (128 if kind == 'primitive' else 64) / width, expected[1] * 64 / height)
                delta = tuple(v[3 + i] - expected[i] for i in range(2))
                if shift is None:
                    shift = delta
                # q3map2 may remove whole texture repeats per draw surface.
                assert all(abs(delta[i] - shift[i]) < 2e-5 and abs(delta[i] - round(delta[i])) < 2e-5 for i in range(2)), (kind, face, delta)
                vertices += 1
                offsets[face] = shift
        expected_faces = {'top', 'side', 'patch'} if args.radiant_project else {'top', 'side', 'corner'} if args.stroke else {'top', 'side'}
        assert seen == expected_faces and vertices >= (17 if args.radiant_project else 12 if args.stroke else 8), (kind, shaders, seen, vertices)
        run(f'{kind}-dependencies', ['map', 'dependencies', output, '--package', base, '--engine', 'idTech3', '--json'])
        subset = root / f'{kind}-assets.pk3'
        run(f'{kind}-subset', ['package', 'subset', base, subset, '--map-input', output, '--engine', 'idTech3', '--overwrite', '--json'])
        package = root / f'{kind}-level.pk3'
        run(f'{kind}-package', ['package', 'save-as', subset, package, '--format', 'pk3', '--add-file', output.with_suffix('.bsp'),
                              '--as', f'maps/{kind}-wrapped.bsp', '--overwrite', '--json'])
        validation = run(f'{kind}-validate', ['package', 'validate', package, '--json'])['validation']
        assert validation['valid'] and validation['verifiedCount'] >= 7, validation
        reports.append({'dialect': kind, 'verifiedUvSamples': vertices, 'tileOffsets': offsets, 'convertedFaces': report['convertedFaces'],
                        'bspSha256': hashlib.sha256(bsp).hexdigest(), 'packageValidation': validation})
    evidence = {'ok': True, 'mappingOnly': args.mapping_only, 'radiantProject': args.radiant_project, 'stroke': args.stroke, 'stepsPassed': len(steps), 'variants': reports, 'scope': 'Synthetic q3map2 BSP/VIS/LIGHT, compiled UVs and PK3 validation; no game launched.',
                'binarySha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(), 'compilerSha256': hashlib.sha256(args.compiler.read_bytes()).hexdigest()}
    (root / 'verified.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    print(f'PASS: {len(steps)} steps, {sum(r["verifiedUvSamples"] for r in reports)} compiled UV samples and three verified PK3s.', flush=True)


if __name__ == '__main__':
    main()
