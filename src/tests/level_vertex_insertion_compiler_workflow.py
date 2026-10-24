"""Original inserted-corner fixtures through VibeMap3 and PK3 publication.

No game data, installation or launch. Quake III BSP layout references and
licence review are recorded under Shared clipper in docs/CREDITS.md.
"""
from pathlib import Path
import argparse
import hashlib
import itertools
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
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    root = args.output_root.resolve()
    assert root.is_relative_to(repo / '.agents' / 'tmp')
    base = root / 'game' / 'baseq3'
    maps = base / 'maps'
    maps.mkdir(parents=True, exist_ok=True)
    (root / 'home').mkdir(exist_ok=True)
    for material in ('wall', 'object'):
        png(base / 'textures' / 'insertion' / f'{material}.png')
    steps, reports = [], []

    def run(label, words, cli=True):
        command = ([str(args.binary.resolve()), '--cli', '--settings-file', str(root / 'settings.ini')]
                   + list(map(str, words)) if cli else list(map(str, words)))
        process = subprocess.run(command, cwd=repo, capture_output=True, timeout=120)
        (root / f'{label}.stdout.txt').write_bytes(process.stdout)
        (root / f'{label}.stderr.txt').write_bytes(process.stderr)
        steps.append({'step': label, 'command': command, 'exitCode': process.returncode})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert process.returncode == 0, (label, process.stdout, process.stderr)
        print(label, flush=True)
        return json.loads(process.stdout) if cli else process.stdout.decode('utf-8', errors='replace')

    room = [((-528, -528, -16), (528, 528, 0)), ((-528, -528, 384), (528, 528, 400)),
            ((-528, -528, 0), (-512, 528, 384)), ((512, -528, 0), (528, 528, 384)),
            ((-512, -528, 0), (512, -512, 384)), ((-512, 512, 0), (512, 528, 384))]
    for dialect, kind in itertools.product(('classic', 'valve', 'primitive'), ('face', 'shared-edge')):
        name = f'{dialect}-{kind}'
        source, output = maps / f'{name}-source.map', maps / f'{name}.map'
        brushes = [box(lo, hi, dialect, 'insertion/wall') for lo, hi in room]
        brushes.append(box((-64, -32, 16), (64, 32, 96), dialect, 'insertion/object'))
        if kind == 'shared-edge':
            brushes.append(box((-64, -32, 96), (64, 32, 176), dialect, 'insertion/object'))
        source.write_text('{\n"classname" "worldspawn"\n' + '\n'.join(brushes)
                          + '\n}\n{\n"classname" "info_player_deathmatch"\n"origin" "256 0 96"\n}\n'
                          + '{\n"classname" "light"\n"origin" "0 0 320"\n"light" "600"\n}\n', encoding='utf-8')
        position = (-96, 8, 96) if kind == 'shared-edge' else (16, 8, 128)
        selected = ['--brush', '6', '--brush', '7'] if kind == 'shared-edge' else ['--brush', '6']
        count = len(selected) // 2
        inserted = run(f'{name}-insert', ['map', 'insert-vertices', source, *selected, '--position', ','.join(map(str, position)), '--output', output, '--overwrite', '--json'])
        assert inserted['brushes'] == count and inserted['insertedVertices'] == count and inserted['collapsedVertices'] == 0
        for brush in range(6, 6 + count):
            topology = run(f'{name}-topology-{brush}', ['map', 'brush-components', output, '--brush', brush, '--json'])['brush']
            assert len(topology['vertices']) == 9
        prefix = [args.compiler.resolve(), '-game', 'quake3', '-fs_basepath', root / 'game',
                  '-fs_homepath', root / 'home', '-fs_game', 'baseq3', '-threads', '2']
        for stage, options in [('bsp', ['-meta']), ('vis', ['-vis', '-fast']), ('light', ['-light', '-fast'])]:
            log = run(f'{name}-{stage}', prefix + options + [output], cli=False)
            assert 'LEAKED' not in log and 'ERROR:' not in log, name
        bsp = output.with_suffix('.bsp').read_bytes()
        assert bsp[:4] == b'IBSP' and struct.unpack_from('<i', bsp, 4)[0] == 46

        def lump(index):
            offset, length = struct.unpack_from('<2i', bsp, 8 + index * 8)
            assert offset >= 0 and length >= 0 and offset + length <= len(bsp)
            return bsp[offset:offset + length]

        shader_data, planes, solids, sides, vertices = (lump(i) for i in (1, 2, 8, 9, 10))
        shaders = [shader_data[i:i+64].split(b'\0', 1)[0].decode() for i in range(0, len(shader_data), 72)]
        shader = shaders.index('textures/insertion/object')
        point_vertices = sum(math.dist(struct.unpack_from('<3f', vertices, i), position) < .01 for i in range(0, len(vertices), 44))
        assert point_vertices > 0, (name, 'inserted vertex missing from compiled draw geometry')
        corners = []
        for at in range(0, len(solids), 12):
            start, number, material = struct.unpack_from('<3i', solids, at)
            if material != shader:
                continue
            boundary = [struct.unpack_from('<4f', planes, struct.unpack_from('<i', sides, i*8)[0]*16) for i in range(start, start+number)]
            distances = [sum(p[i]*position[i] for i in range(3))-p[3] for p in boundary]
            assert max(distances) < .02 and number >= 7, (name, number, distances)
            incident = [p for p, distance in zip(boundary, distances) if abs(distance) < .02]
            # Three independent supporting planes prove a retained convex
            # collision corner, rather than just a tessellated flat point.
            def determinant(a, b, c):
                return a[0]*(b[1]*c[2]-b[2]*c[1])-a[1]*(b[0]*c[2]-b[2]*c[0])+a[2]*(b[0]*c[1]-b[1]*c[0])
            assert any(abs(determinant(*triple)) > .001 for triple in itertools.combinations(incident, 3)), (name, incident)
            corners.append({'sides': number, 'incidentPlanes': incident})
        assert len(corners) == count, (name, corners)
        run(f'{name}-dependencies', ['map', 'dependencies', output, '--package', base, '--engine', 'idTech3', '--json'])
        subset, package = root / f'{name}-assets.pk3', root / f'{name}-level.pk3'
        run(f'{name}-subset', ['package', 'subset', base, subset, '--map-input', output, '--engine', 'idTech3', '--overwrite', '--json'])
        run(f'{name}-package', ['package', 'save-as', subset, package, '--format', 'pk3', '--add-file', output.with_suffix('.bsp'),
                              '--as', f'maps/{name}.bsp', '--add-file', output, '--as', f'maps/{name}.map', '--overwrite', '--json'])
        validation = run(f'{name}-validate', ['package', 'validate', package, '--json'])['validation']
        assert validation['valid'], validation
        with zipfile.ZipFile(package) as archive:
            assert archive.testzip() is None and archive.read(f'maps/{name}.bsp') == bsp and archive.read(f'maps/{name}.map') == output.read_bytes()
            assert 'textures/insertion/object.png' in archive.namelist() and 'textures/insertion/wall.png' in archive.namelist()
        reports.append({'dialect': dialect, 'kind': kind, 'position': position, 'compiledCorners': corners,
                        'drawVerticesAtCorner': point_vertices, 'bspSha256': hashlib.sha256(bsp).hexdigest(), 'packageValidation': validation})
    (root / 'verified.json').write_text(json.dumps({'stepsPassed': len(steps), 'variants': reports,
        'applicationSha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        'compilerSha256': hashlib.sha256(args.compiler.read_bytes()).hexdigest()}, indent=2), encoding='utf-8')
    print(f'PASS: {len(steps)} steps; face/shared-edge corners survive three dialects and six validated PK3s.', flush=True)


if __name__ == '__main__':
    main()
