"""Original linked extraction -> VibeMap3 -> PK3 acceptance fixture.

Generates all geometry and textures locally. No game, input control or screen
capture is used. Requires an explicit compiler and project-local output root.
"""
from collections import Counter
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
import subprocess
import zipfile

from level_compiler_test_helpers import box, png


def compiled_geometry(data):
    assert data[:8] == b'IBSP' + struct.pack('<i', 46)

    def lump(index):
        offset, size = struct.unpack_from('<2i', data, 8 + 8 * index)
        return data[offset:offset + size]

    shaders = [lump(1)[p:p + 64].split(b'\0', 1)[0].decode() for p in range(0, len(lump(1)), 72)]
    surfaces = []
    for at in range(0, len(lump(13)), 104):
        header = struct.unpack_from('<12i', lump(13), at)
        vertices = []
        for index in range(header[3], header[3] + header[4]):
            vertex = struct.unpack_from('<10f4B', lump(10), index * 44)
            # World position, texture coordinates and normals; lightmap packing
            # and model/surface indices may change when ownership is split.
            vertices.append(tuple(round(x, 6) for x in vertex[:5] + vertex[7:10]))
        surfaces.append((shaders[header[0]], header[2], tuple(sorted(vertices))))
    entities = [dict(re.findall(r'"([^"\n]*)"\s*"([^"\n]*)"', block))
                for block in re.findall(r'\{([^{}]*)\}', lump(0).decode())]
    return Counter(surfaces), entities, len(lump(7)) // 40


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
    steps, variants = [], []

    def run(label, words, cli=True):
        command = ([str(args.binary.resolve()), '--cli', '--json', '--settings-file', str(root / 'settings.ini')]
                   + list(map(str, words)) if cli else list(map(str, words)))
        result = subprocess.run(command, cwd=repo, capture_output=True, timeout=120)
        (root / f'{label}.stdout.txt').write_bytes(result.stdout)
        (root / f'{label}.stderr.txt').write_bytes(result.stderr)
        steps.append({'step': label, 'command': command, 'exitCode': result.returncode})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert result.returncode == 0, (label, result.stdout[-1600:], result.stderr[-1600:])
        print(label, 'OK', flush=True)
        return json.loads(result.stdout) if cli else result.stdout.decode(errors='replace')

    room = [((-528, -528, -16), (528, 528, 0)), ((-528, -528, 384), (528, 528, 400)),
            ((-528, -528, 0), (-512, 528, 384)), ((512, -528, 0), (528, 528, 384)),
            ((-512, -528, 0), (512, -512, 384)), ((-512, 512, 0), (512, 528, 384))]
    png(base / 'textures' / 'extract' / 'wall.png')
    for kind in ('classic', 'valve', 'primitive'):
        for face in range(6):
            png(base / 'textures' / 'extract' / kind / f'face{face}.png')
        current = maps / f'{kind}-linked.map'
        door = '\n'.join(box(lo, hi, kind, f'extract/{kind}', True) for lo, hi in
                         [((-192, -32, 0), (-160, 0, 96)), ((-152, -32, 0), (-120, 0, 96))])
        current.write_text('{\n"classname" "worldspawn"\n' + '\n'.join(box(lo, hi, kind, 'extract/wall') for lo, hi in room)
                           + '\n}\n{\n"classname" "info_player_deathmatch"\n"origin" "0 128 48"\n}\n'
                           + '{\n"classname" "light"\n"origin" "0 0 320"\n"light" "600"\n}\n'
                           + '{\n"classname" "func_door"\n"speed" "111"\n"targetname" "gate"\n' + door + '\n}\n'
                           + '{\n"classname" "target_relay"\n"origin" "-128 64 32"\n"target" "gate"\n}\n', encoding='utf-8')

        def scene(label, operation, options):
            return run(f'{kind}-{label}', ['editor', 'scene', operation, current] + options
                       + ['--output', current, '--overwrite'])

        group = scene('group', 'create', ['--kind', 'group', '--name', 'Assembly'])['createdId']
        scene('assign', 'assign', ['--id', group, '--objects', 'entity:3,entity:4'])
        linked = scene('link', 'link', ['--id', group, '--offset', '256,0,0'])
        nodes = {node['id']: node for node in linked['scene']['nodes']}
        owner_identity = nodes[group]['linkObjects']['entity:3']
        brush_identity = nodes[group]['linkObjects']['brush:6']
        relay_identity = nodes[group]['linkObjects']['entity:4']
        # Native selectors may be renumbered by serialization. Resolve durable
        # correspondence against the saved map before issuing the next edit.
        nodes = {node['id']: node for node in run(f'{kind}-linked-reopen', ['editor', 'scene', 'list', current])['scene']['nodes']}
        peer = nodes[linked['createdId']]
        peer_owner = next(key for key, value in peer['linkObjects'].items() if value == owner_identity)
        scene('local', 'set-protected-property', ['--objects', peer_owner, '--key', 'speed', '--value', '222'])
        before = current
        after = maps / f'{kind}-extracted.map'
        nodes = {node['id']: node for node in run(f'{kind}-local-reopen', ['editor', 'scene', 'list', current])['scene']['nodes']}
        chosen = ','.join(key for key, value in nodes[group]['linkObjects'].items() if value in (brush_identity, relay_identity))
        result = run(f'{kind}-extract', ['editor', 'scene', 'extract-linked', current, '--context', group,
                                        '--objects', chosen, '--output', after, '--overwrite'])
        assert result['extraction']['instances'] == 2 and result['extraction']['splitEntities'] == 2
        reopened = run(f'{kind}-reopen', ['editor', 'scene', 'list', after])['scene']
        assert len(reopened['nodes']) == 4
        families = Counter(node['link'] for node in reopened['nodes'])
        assert sorted(families.values()) == [2, 2]

        prefix = [args.compiler.resolve(), '-game', 'quake3', '-fs_basepath', root / 'game',
                  '-fs_homepath', root / 'home', '-fs_game', 'baseq3', '-threads', '2']
        for label, path in [('before', before), ('after', after)]:
            for stage, options in [('bsp', ['-meta']), ('vis', ['-vis', '-fast']), ('light', ['-light', '-fast'])]:
                text = run(f'{kind}-{label}-{stage}', prefix + options + [path], cli=False)
                assert 'LEAKED' not in text and 'ERROR:' not in text, (kind, label, stage)
        original, entities_before, models_before = compiled_geometry(before.with_suffix('.bsp').read_bytes())
        extracted, entities_after, models_after = compiled_geometry(after.with_suffix('.bsp').read_bytes())
        assert extracted == original, ('compiled geometry, UV or normal changed', kind)
        speeds = lambda entities: Counter(entity.get('speed') for entity in entities if entity.get('classname') == 'func_door')
        assert speeds(entities_before) == Counter({'111': 1, '222': 1})
        assert speeds(entities_after) == Counter({'111': 2, '222': 2})
        assert (models_before, models_after) == (3, 5)
        points = lambda entities: Counter(tuple(sorted(entity.items())) for entity in entities if entity.get('classname') == 'target_relay')
        assert points(entities_before) == points(entities_after)
        run(f'{kind}-dependencies', ['map', 'dependencies', after, '--package', base, '--engine', 'idTech3'])
        subset, package = root / f'{kind}-assets.pk3', root / f'{kind}-level.pk3'
        run(f'{kind}-subset', ['package', 'subset', base, subset, '--map-input', after, '--engine', 'idTech3', '--overwrite'])
        run(f'{kind}-package', ['package', 'save-as', subset, package, '--format', 'pk3', '--overwrite',
                               '--add-file', after, '--as', f'maps/{kind}.map',
                               '--add-file', after.with_suffix('.bsp'), '--as', f'maps/{kind}.bsp'])
        validation = run(f'{kind}-validate', ['package', 'validate', package])['validation']
        assert validation['valid'] and validation['verifiedCount'] == 9
        with zipfile.ZipFile(package) as archive:
            assert archive.testzip() is None
            assert archive.read(f'maps/{kind}.map') == after.read_bytes()
            assert archive.read(f'maps/{kind}.bsp') == after.with_suffix('.bsp').read_bytes()
        variants.append({'dialect': kind, 'identicalSurfaces': sum(extracted.values()), 'modelsBefore': models_before,
                         'modelsAfter': models_after, 'validation': validation, 'packageSha256': hashlib.sha256(package.read_bytes()).hexdigest()})
    report = {'stepsPassed': len(steps), 'variants': variants,
              'applicationSha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(),
              'compilerSha256': hashlib.sha256(args.compiler.read_bytes()).hexdigest()}
    (root / 'verified.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'PASS: {len(steps)} steps; three dialects preserve compiled geometry, UVs and local properties; three verified PK3s.')


if __name__ == '__main__':
    main()
