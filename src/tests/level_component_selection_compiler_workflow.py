"""Optional shared-corner authoring proof through VibeMap3 and PK3 publication.

Uses synthetic adjacent brushes, generated textures and no installed game data.
The output root must be inside this repository's .agents/tmp directory.
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
    parser.add_argument('--selection-topology', action='store_true',
                        help='Grow/shrink corner picks, then convert complete top-face perimeters into the transform selectors.')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    root = args.output_root.resolve()
    assert root.is_relative_to(repo / '.agents' / 'tmp')
    base = root / 'game' / 'baseq3'
    maps = base / 'maps'
    maps.mkdir(parents=True, exist_ok=True)
    (root / 'home').mkdir(exist_ok=True)
    steps, reports = [], []

    def run(label, words, cli=True):
        command = ([str(args.binary.resolve()), '--cli', '--settings-file', str(root / 'settings.ini')]
                   + list(map(str, words)) if cli else list(map(str, words)))
        result = subprocess.run(command, cwd=repo, capture_output=True, timeout=120)
        (root / f'{label}.stdout.txt').write_bytes(result.stdout)
        (root / f'{label}.stderr.txt').write_bytes(result.stderr)
        steps.append({'step': label, 'command': command, 'exitCode': result.returncode})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert result.returncode == 0, (label, result.stdout, result.stderr)
        print(label, flush=True)
        return json.loads(result.stdout) if cli else result.stdout.decode('utf-8', errors='replace')

    room = [((-528, -528, -16), (528, 528, 0)), ((-528, -528, 384), (528, 528, 400)),
            ((-528, -528, 0), (-512, 528, 384)), ((512, -528, 0), (528, 528, 384)),
            ((-512, -528, 0), (512, -512, 384)), ((-512, 512, 0), (512, 528, 384))]
    for material in ('wall', 'left', 'right'):
        png(base / 'textures' / 'seam' / f'{material}.png')
    old, moved = (0, -16, 56), (0, -16, 64)
    for kind in ('classic', 'valve', 'primitive'):
        source = maps / f'{kind}-source.map'
        brushes = [box(lo, hi, kind, 'seam/wall') for lo, hi in room]
        brushes += [box((-32, -16, 24), (0, 16, 56), kind, 'seam/left'),
                    box((0, -16, 24), (32, 16, 56), kind, 'seam/right')]
        source.write_text('{\n"classname" "worldspawn"\n' + '\n'.join(brushes)
                          + '\n}\n{\n"classname" "info_player_deathmatch"\n"origin" "256 0 96"\n}\n'
                          + '{\n"classname" "light"\n"origin" "0 0 320"\n"light" "600"\n}\n', encoding='utf-8')
        before, selected_points, selectors = {}, {}, []
        for brush_id in (6, 7):
            topology = run(f'{kind}-inspect-{brush_id}', ['map', 'brush-components', source, '--brush', brush_id, '--json'])['brush']
            points = {p['id']: tuple(p[a] for a in ('x', 'y', 'z')) for p in topology['vertices']}
            vertex = next(i for i, point in points.items() if point == old)
            before[brush_id] = set(points.values())
            seed = f'brush:{brush_id}/vertex/{vertex}'
            selected_points[brush_id] = {old}
            if args.selection_topology:
                grown = run(f'{kind}-grow-{brush_id}', ['map', 'select-components', source,
                            '--set', seed, '--operation', 'grow', '--json'])
                assert grown['componentCount'] == 4
                shrunk = run(f'{kind}-shrink-{brush_id}', ['map', 'select-components', source,
                             '--set', grown['selectors'][0], '--operation', 'shrink', '--json'])
                assert shrunk['selectors'] == [seed]
                top = next(face['id'] for face in topology['faces']
                           if all(points[i][2] == 56 for i in face['vertices']))
                edges = run(f'{kind}-face-edges-{brush_id}', ['map', 'select-components', source,
                            '--set', f'brush:{brush_id}/face/{top}', '--operation', 'convert', '--to', 'edge', '--json'])
                vertices = run(f'{kind}-edge-vertices-{brush_id}', ['map', 'select-components', source,
                               '--set', edges['selectors'][0], '--operation', 'convert', '--to', 'vertex', '--json'])
                assert edges['componentCount'] == vertices['componentCount'] == 4
                selected_points[brush_id] = {points[i] for i in vertices['sets'][0]['components']}
                assert selected_points[brush_id] == {p for p in before[brush_id] if p[2] == 56}
                seed = vertices['selectors'][0]
            selectors += ['--set', seed]
        output = maps / f'{kind}-seam.map'
        run(f'{kind}-move', ['map', 'transform-components', source, *selectors,
                            '--operation', 'move', '--delta', '0,0,8', '--output', output, '--overwrite', '--json'])
        for brush_id in (6, 7):
            topology = run(f'{kind}-verify-{brush_id}', ['map', 'brush-components', output, '--brush', brush_id, '--json'])['brush']
            actual = {tuple(p[a] for a in ('x', 'y', 'z')) for p in topology['vertices']}
            changed = selected_points[brush_id]
            assert actual == before[brush_id] - changed | {(p[0], p[1], p[2]+8) for p in changed}, (kind, brush_id, actual)
        prefix = [args.compiler.resolve(), '-game', 'quake3', '-fs_basepath', root / 'game',
                  '-fs_homepath', root / 'home', '-fs_game', 'baseq3', '-threads', '2']
        for stage, options in [('bsp', ['-meta']), ('vis', ['-vis', '-fast']), ('light', ['-light', '-fast'])]:
            compiler_log = run(f'{kind}-{stage}', prefix + options + [output], cli=False)
            assert 'LEAKED' not in compiler_log and 'ERROR:' not in compiler_log
        bsp = output.with_suffix('.bsp').read_bytes()
        assert bsp[:4] == b'IBSP' and struct.unpack_from('<i', bsp, 4)[0] == 46

        def lump(index):
            offset, length = struct.unpack_from('<2i', bsp, 8 + index * 8)
            return bsp[offset:offset + length]

        shaders = [lump(1)[at:at + 64].split(b'\0', 1)[0].decode() for at in range(0, len(lump(1)), 72)]
        found = set()
        checked_vertices = 0
        for at in range(0, len(lump(13)), 104):
            surface = struct.unpack_from('<12i', lump(13), at)
            material = shaders[surface[0]]
            if material not in ('textures/seam/left', 'textures/seam/right'):
                continue
            for vertex in range(surface[3], surface[3] + surface[4]):
                point = struct.unpack_from('<10f4B', lump(10), vertex * 44)
                assert all(math.isfinite(value) for value in point[:10])
                if all(abs(point[i] - moved[i]) < 1e-4 for i in range(3)):
                    found.add(material)
                checked_vertices += 1
        assert found == {'textures/seam/left', 'textures/seam/right'}, (kind, found)
        run(f'{kind}-dependencies', ['map', 'dependencies', output, '--package', base, '--engine', 'idTech3', '--json'])
        subset, package = root / f'{kind}-assets.pk3', root / f'{kind}-level.pk3'
        run(f'{kind}-subset', ['package', 'subset', base, subset, '--map-input', output, '--engine', 'idTech3', '--overwrite', '--json'])
        run(f'{kind}-package', ['package', 'save-as', subset, package, '--format', 'pk3', '--add-file', output.with_suffix('.bsp'),
                               '--as', f'maps/{kind}-seam.bsp', '--add-file', output, '--as', f'maps/{kind}-seam.map', '--overwrite', '--json'])
        validation = run(f'{kind}-validate', ['package', 'validate', package, '--json'])['validation']
        assert validation['valid'] and validation['verifiedCount'] == 5, validation
        with zipfile.ZipFile(package) as archive:
            assert archive.testzip() is None and archive.read(f'maps/{kind}-seam.bsp') == bsp
            assert archive.read(f'maps/{kind}-seam.map') == output.read_bytes()
        reports.append({'dialect': kind, 'sharedCorner': moved,
                        'selectionMode': 'top-face-conversion' if args.selection_topology else 'shared-corner',
                        'checkedCompiledVertices': checked_vertices,
                        'bspSha256': hashlib.sha256(bsp).hexdigest(), 'packageValidation': validation})
    (root / 'verified.json').write_text(json.dumps({'stepsPassed': len(steps), 'variants': reports,
        'applicationSha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        'compilerSha256': hashlib.sha256(args.compiler.read_bytes()).hexdigest()}, indent=2), encoding='utf-8')
    print(f'PASS: {len(steps)} steps; both brushes retain the compiled seam in three dialects; three verified PK3s.', flush=True)


if __name__ == '__main__':
    main()
