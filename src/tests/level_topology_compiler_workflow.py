"""Generated brush topology edits or clipping -> VibeMap3 -> verified PK3.

No game launch, input injection or screen capture. Uses original fixtures;
all generated output must stay inside this project's .agents/tmp directory.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct
import subprocess
import zipfile

from level_compiler_test_helpers import box, png


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    parser.add_argument('--operation', choices=('components', 'clip', 'camera-clip', 'split', 'extrude', 'snap', 'wrap', 'component-wrap'), default='components')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    root = args.output_root.resolve()
    assert root.is_relative_to((repo / '.agents/tmp').resolve())
    base = root / 'game/baseq3'
    maps = base / 'maps'
    maps.mkdir(parents=True, exist_ok=True)
    (root / 'home').mkdir(exist_ok=True)
    steps, variants = [], []

    def run(label, words, cli=True, expected_exit=0):
        command = ([str(args.binary.resolve()), '--cli', '--json', '--settings-file', str(root / 'settings.ini')]
                   + list(map(str, words)) if cli else list(map(str, words)))
        result = subprocess.run(command, cwd=repo, capture_output=True, timeout=180)
        (root / f'{label}.stdout.txt').write_bytes(result.stdout)
        (root / f'{label}.stderr.txt').write_bytes(result.stderr)
        steps.append({'step': label, 'command': command, 'exitCode': result.returncode})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert result.returncode == expected_exit, (label, result.stdout, result.stderr)
        print(label, 'passed', flush=True)
        return json.loads(result.stdout) if cli else result.stdout.decode('utf-8', errors='replace')

    room = [((-272, -272, -16), (272, 272, 0)), ((-272, -272, 256), (272, 272, 272)),
            ((-272, -272, 0), (-256, 272, 256)), ((256, -272, 0), (272, 272, 256)),
            ((-256, -272, 0), (256, -256, 256)), ((-256, 256, 0), (256, 272, 256))]
    png(base / 'textures/topology/wall.png')
    prefix = [args.compiler.resolve(), '-game', 'quake3', '-fs_basepath', root / 'game',
              '-fs_homepath', root / 'home', '-fs_game', 'baseq3', '-threads', '2']
    for kind in ('classic', 'valve', 'primitive'):
        for face in range(6):
            png(base / f'textures/topology/{kind}/face{face}.png')
        if args.operation in ('wrap', 'component-wrap'):
            png(base / f'textures/topology/{kind}/wrap.png')
        source = maps / f'{kind}-source.map'
        source.write_text('{\n"classname" "worldspawn"\n'
                          + '\n'.join(box(lo, hi, kind, 'topology/wall') for lo, hi in room)
                          + '\n' + box((-64, -64, 16), (64, 64, 80), kind, f'topology/{kind}', True)
                          + '\n}\n{\n"classname" "info_player_deathmatch"\n"origin" "0 -160 64"\n}\n'
                          + '{\n"classname" "light"\n"origin" "0 0 224"\n"light" "600"\n}\n', encoding='utf-8')
        inserted, edited = maps / f'{kind}-inserted.map', maps / f'{kind}-edited.map'
        if args.operation == 'components':
            run(f'{kind}-insert', ['map', 'insert-vertices', source, '--brush', '6', '--position', '0,0,112',
                                  '--output', inserted, '--overwrite'])
            run(f'{kind}-remove', ['map', 'remove-components', inserted, '--brush', '6', '--kind', 'vertex',
                                  '--component', '0', '--output', edited, '--overwrite'])
            topology = run(f'{kind}-inspect', ['map', 'brush-components', edited, '--brush', '6'])['brush']
            expected = [tuple(v[axis] for axis in ('x', 'y', 'z')) for v in topology['vertices']]
            faces = len(topology['faces'])
            assert len(expected) == 8 and faces == 10
            assert (0, 0, 112) in expected and (-64, -64, 16) not in expected
        elif args.operation == 'split':
            original = run(f'{kind}-inspect-source', ['map', 'brush-components', source, '--brush', '6'])['brush']
            vertices = {v['id']: tuple(v[axis] for axis in ('x', 'y', 'z')) for v in original['vertices']}
            top = next(f['id'] for f in original['faces'] if all(vertices[i][2] == 80 for i in f['vertices']))
            run(f'{kind}-split-face', ['map', 'split-components', source, '--brush', '6', '--kind', 'face',
                                      '--component', top, '--delta', '0,0,32', '--output', inserted, '--overwrite'])
            intermediate = run(f'{kind}-inspect-face', ['map', 'brush-components', inserted, '--brush', '6'])['brush']
            vertices = {v['id']: tuple(v[axis] for axis in ('x', 'y', 'z')) for v in intermediate['vertices']}
            edge = next(e['id'] for e in intermediate['edges'] if all(vertices[i][0] == -64 and vertices[i][1] == -64 for i in e['vertices']))
            run(f'{kind}-split-edge', ['map', 'split-components', inserted, '--brush', '6', '--kind', 'edge',
                                      '--component', edge, '--delta', '-16,-16,0', '--output', edited, '--overwrite'])
            topology = run(f'{kind}-inspect-result', ['map', 'brush-components', edited, '--brush', '6'])['brush']
            expected = [tuple(v[axis] for axis in ('x', 'y', 'z')) for v in topology['vertices']]
            faces = len(topology['faces'])
            assert len(expected) == 10 and faces > 9
            assert (0, 0, 112) in expected and (-80, -80, 48) in expected
        elif args.operation == 'wrap':
            report = run(f'{kind}-wrap', ['map', 'clip', source, '--object', 'brush:6', '--points', '128,0,48',
                                        '--wrap-merge', '--wrap-texture', f'topology/{kind}/wrap',
                                        '--cut-texture', 'unused/cut', '--output', edited, '--overwrite'])
            assert report['operation'] == 'wrap-merge' and report['newFaces'] == 4 and report['inheritedFaces'] == 5
            topology = run(f'{kind}-inspect-result', ['map', 'brush-components', edited, '--brush', '6'])['brush']
            expected = [tuple(v[axis] for axis in ('x', 'y', 'z')) for v in topology['vertices']]
            faces = len(topology['faces'])
            assert len(expected) == 9 and faces == 9 and (128, 0, 48) in expected
        elif args.operation == 'component-wrap':
            original = run(f'{kind}-inspect-source', ['map', 'brush-components', source, '--brush', '6'])['brush']
            vertices = {v['id']: tuple(v[axis] for axis in ('x', 'y', 'z')) for v in original['vertices']}
            top = next(f['id'] for f in original['faces'] if all(vertices[i][2] == 80 for i in f['vertices']))
            report = run(f'{kind}-wrap-components', ['map', 'wrap-merge', source, '--set', f'brush:6/face/{top}',
                         '--point', '0,0,112', '--texture', f'topology/{kind}/wrap', '--output', edited, '--overwrite'])
            assert report['sourcesRetained'] and report['sourceBrushes'] == 1 and report['brushId'] == 7
            assert report['newFaces'] == 5 and report['inheritedFaces'] == 0
            retained = run(f'{kind}-inspect-retained', ['map', 'brush-components', edited, '--brush', '6'])['brush']
            assert retained == original, 'original topology and surfaces must survive unchanged'
            topology = run(f'{kind}-inspect-result', ['map', 'brush-components', edited, '--brush', '7'])['brush']
            expected = [tuple(v[axis] for axis in ('x', 'y', 'z')) for v in topology['vertices']]
            faces = len(topology['faces'])
            assert len(expected) == 5 and faces == 5 and (0, 0, 112) in expected
            assert min(v[2] for v in expected) == 80, 'unpicked lower corners cannot enter the new hull'
        elif args.operation == 'snap':
            original = run(f'{kind}-inspect-source', ['map', 'brush-components', source, '--brush', '6'])['brush']
            vertices = {v['id']: tuple(v[axis] for axis in ('x', 'y', 'z')) for v in original['vertices']}
            source_corner, target_corner = (-64, -64, 16), (-64, -64, 80)
            source_id = next(i for i, p in vertices.items() if p == source_corner)
            target_id = next(i for i, p in vertices.items() if p == target_corner)
            report = run(f'{kind}-snap', ['map', 'transform-components', source, '--set', f'brush:6/vertex/{source_id}',
                                        '--operation', 'snap', '--target', f'brush:6/vertex/{target_id}', '--output', edited, '--overwrite'])
            assert report['brushes'] == 1 and report['collapsedVertices'] == 1
            topology = run(f'{kind}-inspect-result', ['map', 'brush-components', edited, '--brush', '6'])['brush']
            expected = [tuple(v[axis] for axis in ('x', 'y', 'z')) for v in topology['vertices']]
            faces = len(topology['faces'])
            assert len(expected) == 7 and set(expected) == set(vertices.values()) - {source_corner}
        elif args.operation == 'extrude':
            def upper_face(path, brush, label):
                topology = run(label, ['map', 'brush-components', path, '--brush', brush])['brush']
                vertices = {v['id']: tuple(v[axis] for axis in ('x', 'y', 'z')) for v in topology['vertices']}
                top = max(v[2] for v in vertices.values())
                return next(f['id'] for f in topology['faces'] if all(vertices[i][2] == top for i in f['vertices']))

            top = upper_face(source, 6, f'{kind}-inspect-source')
            run(f'{kind}-extrude', ['map', 'extrude-faces', source, '--face', f'6:{top}', '--distance', 16,
                                  '--output', inserted, '--overwrite'])
            outward = maps / f'{kind}-outward.map'
            top = upper_face(inserted, 6, f'{kind}-inspect-grown')
            run(f'{kind}-extrude-outward', ['map', 'extrude-faces', inserted, '--face', f'6:{top}', '--distance', 32,
                                          '--split', '--output', outward, '--overwrite'])
            top = upper_face(outward, 7, f'{kind}-inspect-outward')
            run(f'{kind}-extrude-inward', ['map', 'extrude-faces', outward, '--face', f'7:{top}', '--distance', -16,
                                         '--split', '--output', edited, '--overwrite'])
            expected, faces = [], 0
            for brush, heights in ((6, (16, 96)), (7, (96, 112)), (8, (112, 128))):
                topology = run(f'{kind}-inspect-{brush}', ['map', 'brush-components', edited, '--brush', brush])['brush']
                points = [tuple(v[axis] for axis in ('x', 'y', 'z')) for v in topology['vertices']]
                assert len(points) == 8 and len(topology['faces']) == 6
                assert (min(v[2] for v in points), max(v[2] for v in points)) == heights
                expected += points
                faces += len(topology['faces'])
            rejected = root / f'{kind}-must-not-exist.map'
            common = ['map', 'extrude-faces', source, '--face', f'6:{top}', '--distance', 16, '--output', rejected]
            for index, options in enumerate((['--typo'], ['--distance', 32], ['--grid'], ['--split=true'])):
                run(f'{kind}-reject-{index}', common + options, expected_exit=2)
                assert not rejected.exists()
        else:
            plane = (['--points', '0,-48,80 0,48,80', '--camera-eye', '-200,0,120', '--camera-forward', '1,0,0']
                     if args.operation == 'camera-clip' else ['--points', '0,0,0 0,64,0 0,0,64'])
            report = run(f'{kind}-clip', ['map', 'clip', source, '--object', 'brush:6'] + plane
                         + ['--keep', 'both', '--output', edited, '--overwrite'])
            if args.operation == 'camera-clip':
                assert report['cameraPlane'] and report['planeAxis'] == 'z' and report['planeDirection'] == 1
            expected, faces = [], 0
            # Native text maps number brushes by source order on reopen.
            for brush_id in (6, 7):
                topology = run(f'{kind}-inspect-{brush_id}', ['map', 'brush-components', edited, '--brush', brush_id])['brush']
                corners = [tuple(v[axis] for axis in ('x', 'y', 'z')) for v in topology['vertices']]
                assert len(corners) == 8 and len(topology['faces']) == 6
                assert all(p[0] <= 0 for p in corners) if brush_id == 6 else all(p[0] >= 0 for p in corners)
                expected += corners
                faces += len(topology['faces'])
            # Reject ambiguous, misspelled, repeated and incomplete arguments
            # before writing any output, including a non-finite axis value.
            rejected = root / f'{kind}-must-not-exist.map'
            common = ['map', 'clip', source, '--object', 'brush:6', '--output', rejected]
            for index, options in enumerate((['--axis', 'x', '--at', '0', '--points', '0,0,0 0,1,0 0,0,1'],
                                            ['--axis', 'x', '--at', '0', '--kepp', 'front'],
                                            ['--axis', 'x', '--axis', 'y', '--at', '0'],
                                            ['--axis', 'x', '--at'], ['--axis', 'x', '--at', 'nan'])):
                run(f'{kind}-reject-{index}', common + options, expected_exit=2)
                assert not rejected.exists()
        for stage, options in [('bsp', ['-meta']), ('vis', ['-vis', '-fast']), ('light', ['-light', '-fast'])]:
            output = run(f'{kind}-{stage}', prefix + options + [edited], cli=False)
            assert 'LEAKED' not in output and 'ERROR:' not in output, output
        bsp = edited.with_suffix('.bsp').read_bytes()
        assert bsp[:8] == b'IBSP' + struct.pack('<i', 46)

        def lump(index):
            offset, size = struct.unpack_from('<2i', bsp, 8 + 8 * index)
            assert offset >= 0 and size >= 0 and offset + size <= len(bsp)
            return bsp[offset:offset + size]

        assert len(lump(8)) // 12 == (9 if args.operation == 'extrude' else 8 if args.operation in ('clip', 'camera-clip', 'component-wrap') else 7), 'room and edited brush count'
        shaders = [lump(1)[i:i + 64].split(b'\0', 1)[0].decode() for i in range(0, len(lump(1)), 72)]
        if args.operation in ('snap', 'wrap', 'component-wrap'):
            collision_brushes = 0
            for at in range(0, len(lump(8)), 12):
                first, count, shader = struct.unpack_from('<3i', lump(8), at)
                if not shaders[shader].startswith(f'textures/topology/{kind}/'):
                    continue
                if args.operation == 'component-wrap' and shaders[shader] != f'textures/topology/{kind}/wrap':
                    continue
                boundary = [struct.unpack_from('<4f', lump(2), struct.unpack_from('<i', lump(9), i*8)[0]*16)
                            for i in range(first, first+count)]
                def outside(point):
                    return max(sum(p[i]*point[i] for i in range(3))-p[3] for p in boundary)
                if args.operation == 'snap':
                    assert outside(source_corner) > 1, 'removed corner must also leave the collision hull'
                elif args.operation == 'component-wrap':
                    assert outside((0, 0, 120)) > 1 and outside((0, 0, 64)) > 1, 'component collision hull excludes original lower volume and stops at apex'
                else:
                    assert outside((136, 0, 48)) > 1, 'collision hull must stop at the new apex'
                assert all(outside(point) < .02 for point in expected), 'retained corners remain inside collision hull'
                collision_brushes += 1
            assert collision_brushes == 1
        compiled = []
        materials = set()
        for at in range(0, len(lump(13)), 104):
            header = struct.unpack_from('<12i', lump(13), at)
            name = shaders[header[0]]
            if name.startswith(f'textures/topology/{kind}/'):
                materials.add(name)
                for index in range(header[3], header[3] + header[4]):
                    vertex = struct.unpack_from('<10f4B', lump(10), index * 44)
                    assert all(math.isfinite(value) for value in vertex[:10])
                    compiled.append(vertex[:3])
        assert len(materials) == 6, 'wrap hides one original surface and adds one material; six original materials otherwise'
        if args.operation in ('wrap', 'component-wrap'):
            assert f'textures/topology/{kind}/wrap' in materials and not any('unused/cut' in shader for shader in shaders)
        for point in expected:
            assert any(max(abs(point[i] - other[i]) for i in range(3)) < 1e-3 for other in compiled), point
        if args.operation == 'component-wrap':
            for point in vertices.values():
                assert any(max(abs(point[i] - other[i]) for i in range(3)) < 1e-3 for other in compiled), ('retained source corner', point)
        assets = root / f'{kind}-assets.pk3'
        run(f'{kind}-dependencies', ['package', 'subset', base, assets, '--map-input', edited,
                                    '--engine', 'idTech3', '--overwrite'])
        package = root / f'{kind}-level.pk3'
        run(f'{kind}-package', ['package', 'save-as', assets, package, '--format', 'pk3', '--add-file', edited,
                               '--as', f'maps/{kind}.map', '--add-file', edited.with_suffix('.bsp'),
                               '--as', f'maps/{kind}.bsp', '--overwrite'])
        validation = run(f'{kind}-validate', ['package', 'validate', package])['validation']
        assert validation['valid'] and validation['uncheckedCount'] == 0
        with zipfile.ZipFile(package) as archive:
            assert archive.testzip() is None and archive.read(f'maps/{kind}.map') == edited.read_bytes()
            assert archive.read(f'maps/{kind}.bsp') == bsp
            for material in materials:
                assert archive.read(material + '.png') == (base / (material + '.png')).read_bytes()
            if args.operation == 'component-wrap':
                for face in range(6):
                    name = f'textures/topology/{kind}/face{face}.png'
                    assert archive.read(name) == (base / name).read_bytes(), 'map dependencies include hidden original surfaces'
        variants.append({'dialect': kind, 'authoredVertices': len(expected), 'authoredFaces': faces,
                         'compiledVertexSamples': len(compiled),
                         'inheritedMaterials': sorted(m for m in materials if '/face' in m),
                         'generatedMaterials': sorted(m for m in materials if '/face' not in m),
                         'validation': validation, 'mapSha256': hashlib.sha256(edited.read_bytes()).hexdigest(),
                         'bspSha256': hashlib.sha256(bsp).hexdigest()})
    (root / 'verified.json').write_text(json.dumps({'operation': args.operation, 'stepsPassed': len(steps), 'variants': variants,
        'applicationSha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        'compiler': str(args.compiler.resolve()), 'compilerSha256': hashlib.sha256(args.compiler.read_bytes()).hexdigest()}, indent=2), encoding='utf-8')
    print('PASS: three brush dialects, compiled authored corners, inherited materials and verified PK3 payloads.', flush=True)


if __name__ == '__main__':
    main()
