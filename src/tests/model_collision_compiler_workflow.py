"""Compile generated collision through VibeMap2 and VibeMap3; inspect BSP data.

Optional integration proof: pass --binary, --qbsp, --q3map2 and a new
--output-root below this repository's .agents/tmp. No game data or game launch.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys

from level_compiler_test_helpers import png
from texture_compiler_workflow import box, lump

# Original readers of GPL-2.0-or-later public layout facts, reviewed 2026-10-05:
# ericw-tools include/common/bspfile_q1.hh and bspfile_q2.hh at f80b1e2;
# NetRadiant Custom tools/quake3/q3map2/q3map2.h and bspfile_ibsp.cpp at 68ecbed.
# Full pinned source links and compatible licenses are in docs/CREDITS.md.


def quake_contents(data, hull, point):
    planes, nodes, leaves = lump(data, 1, 4), lump(data, 5 if hull == 0 else 9, 4), lump(data, 10, 4)
    node = struct.unpack_from('<i', lump(data, 14, 4), 36 + hull * 4)[0]
    stride = 24 if hull == 0 else 8
    for _ in range(len(nodes) // stride + 1):
        if node < 0:
            return struct.unpack_from('<i', leaves, (-1 - node) * 28)[0] if hull == 0 else node
        plane, front, back = struct.unpack_from('<ihh', nodes, node * stride)
        normal = struct.unpack_from('<4f', planes, plane * 20)
        node = front if sum(point[i] * normal[i] for i in range(3)) >= normal[3] else back
    raise AssertionError('Cyclic compiled collision hull')


def rotated(point, angles, centre):
    # Independent scalar X/Y/Z rotations for the BSP membership oracle.
    x, y, z = point
    rx, ry, rz = map(math.radians, angles)
    y, z = y * math.cos(rx) - z * math.sin(rx), y * math.sin(rx) + z * math.cos(rx)
    x, z = x * math.cos(ry) + z * math.sin(ry), -x * math.sin(ry) + z * math.cos(ry)
    x, y = x * math.cos(rz) - y * math.sin(rz), x * math.sin(rz) + y * math.cos(rz)
    return tuple(a + b for a, b in zip((x, y, z), centre))


def verify_brushes(data, q3, volumes):
    planes = lump(data, 2 if q3 else 1)
    brushes = lump(data, 8 if q3 else 14)
    sides = lump(data, 9 if q3 else 15)
    shaders = lump(data, 1) if q3 else b''
    leaves = lump(data, 4 if q3 else 8)
    leaf_brushes = lump(data, 6 if q3 else 10)
    referenced = set()
    for at in range(0, len(leaves), 48 if q3 else 28):
        first, count = struct.unpack_from('<2i' if q3 else '<2H', leaves, at + (40 if q3 else 24))
        for index in range(first, first + count):
            referenced.add(struct.unpack_from('<i' if q3 else '<H', leaf_brushes, index * (4 if q3 else 2))[0])
    clips, clip_shaders = [], set()
    for at in range(0, len(brushes), 12):
        first, count, contents = struct.unpack_from('<3i', brushes, at)
        shader = contents
        if q3:
            flags, contents = struct.unpack_from('<2i', shaders, shader * 72 + 64)
        if not contents & 65536:
            continue
        assert at // 12 in referenced and not contents & 1, (at, contents)
        if q3:
            assert flags & 128, 'Clip shader must suppress draw surfaces'
            clip_shaders.add(shader)
        brush_planes = []
        for index in range(first, first + count):
            plane = struct.unpack_from('<i' if q3 else '<H', sides, index * (8 if q3 else 4))[0]
            brush_planes.append(struct.unpack_from('<4f', planes, plane * (16 if q3 else 20)))
        clips.append(brush_planes)
    assert len(clips) == len(volumes), len(clips)

    def contains(planes, point):
        return all(sum(p[i] * point[i] for i in range(3)) <= p[3] + .02 for p in planes)

    matched, samples = set(), 0
    for volume in volumes:
        centre, size, angles = volume
        candidates = [i for i, planes in enumerate(clips) if contains(planes, centre)]
        assert len(candidates) == 1 and candidates[0] not in matched
        selected = candidates[0]
        matched.add(selected)
        for axis in range(3):
            for sign in (-1, 1):
                for inside in (True, False):
                    point = [0., 0., 0.]
                    point[axis] = sign * (size[axis] * .45 if inside else size[axis] * .5 + 5)
                    assert contains(clips[selected], rotated(point, angles, centre)) == inside, (volume, point)
                    samples += 1
    if q3:
        faces = lump(data, 13)
        assert all(struct.unpack_from('<i', faces, at)[0] not in clip_shaders for at in range(0, len(faces), 104))
    else:
        faces, texinfo = lump(data, 6), lump(data, 5)
        for at in range(0, len(faces), 20):
            texture = struct.unpack_from('<h', faces, at + 10)[0]
            assert texinfo[texture * 76 + 40:texture * 76 + 72].split(b'\0', 1)[0] != b'clip'
    return {'clipBrushes': len(clips), 'membershipSamples': samples, 'visibleClipFaces': 0}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('binary', 'qbsp', 'q3map2', 'output-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if not __debug__:
        parser.error('Assertions must remain enabled.')
    repo = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(repo / 'scripts'))
    from source_companion_common import reject_links
    root = args.output_root.absolute()
    reject_links(root)
    root = root.resolve()
    if not root.is_relative_to(repo / '.agents/tmp') or root.exists():
        parser.error('Use a new output directory inside this repository/.agents/tmp.')
    root.mkdir(parents=True)
    runtime = root / 'runtime'
    runtime.mkdir()
    env = dict(os.environ, TEMP=str(runtime), TMP=str(runtime), TMPDIR=str(runtime), PYTHONDONTWRITEBYTECODE='1')
    binaries = {name: {'path': str(path.resolve()), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                for name, path in [('studio', args.binary), ('qbsp', args.qbsp), ('q3map2', args.q3map2)]}
    steps, checks = [], []

    def run(label, words, cli=True):
        command = ([str(args.binary.resolve()), '--cli', '--settings-file', str(root / 'settings.ini')] + list(map(str, words)) + ['--json']
                   if cli else list(map(str, words)))
        result = subprocess.run(command, cwd=root, env=env, capture_output=True, timeout=120)
        (root / (label + '.stdout.txt')).write_bytes(result.stdout)
        (root / (label + '.stderr.txt')).write_bytes(result.stderr)
        steps.append({'step': label, 'command': command, 'exitCode': result.returncode})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert result.returncode == 0, (label, result.stdout.decode(errors='replace'), result.stderr.decode(errors='replace'))
        print(label, 'PASS', flush=True)
        return json.loads(result.stdout) if cli else (result.stdout + result.stderr).decode(errors='replace')

    obj = root / 'synthetic.obj'
    obj.write_text('v 0 0 0\nv 4 0 0\nv 0 4 0\nf 1 2 3\n', encoding='ascii')
    mesh = root / 'collision.mesh.json'
    run('import', ['model', 'import', obj, '--output', mesh])
    volumes = [((0, 0, 64), (32, 40, 48), (15, 25, 35)), ((80, 0, 64), (16, 24, 32), (0, 0, 90))]
    for index, (centre, size, angles) in enumerate(volumes):
        run(f'box-{index}', ['model', 'collision', mesh, '--operation', 'add', '--name', f'box_{index}',
                            '--centre', ','.join(map(str, centre)), '--size', ','.join(map(str, size)),
                            '--rotation', ','.join(map(str, angles)), '--output', mesh])
    run('box-transform', ['model', 'collision', mesh, '--operation', 'transform', '--box', 'box_1',
                         '--offset', '-8,16,0', '--rotate', '0,0,-45', '--scale', '2,1,0.5', '--output', mesh])
    # Independent expected geometry: resize in the original 90-degree local
    # basis, rotate about the centre, then translate in model coordinates.
    volumes[1] = ((72, 16, 64), (32, 24, 16), (0, 0, 45))
    mesh_hash = hashlib.sha256(mesh.read_bytes()).hexdigest()
    palette = root / 'synthetic-palette.lmp'
    palette.write_bytes(bytes(value for i in range(256) for value in (i, i, i)))
    texture = root / 'wall.vtexture'
    run('texture', ['texture', 'create', '--size', '32x32', '--color', '#808080', '--output', texture])
    for target in ('quake', 'quake2', 'quake3'):
        game = root / target
        base = game / ('id1' if target == 'quake' else 'baseq2' if target == 'quake2' else 'baseq3')
        maps = base / 'maps'
        maps.mkdir(parents=True)
        (game / 'home').mkdir()
        wad_paths = []
        if target != 'quake3':
            for name in ('wall', 'skip', 'clip'):
                options = root / f'{target}-{name}.json'
                options.write_text(json.dumps({'name': name}), encoding='utf-8')
                asset = base / (f'{name}.wad' if target == 'quake' else f'textures/{name}.wal')
                asset.parent.mkdir(parents=True, exist_ok=True)
                run(f'{target}-{name}', ['texture', 'export', texture, '--profile', 'quake-wad2' if target == 'quake' else 'quake2-wal',
                                        '--palette-file', palette, '--export-options', options, '--output', asset])
                wad_paths.append('../' + asset.name)
            if target == 'quake2':
                (base / 'pics').mkdir()
                run('quake2-palette', ['texture', 'export', texture, '--profile', 'pcx', '--palette-file', palette,
                                       '--output', base / 'pics/colormap.pcx'])
        else:
            png(base / 'textures/collision/wall.png')
            (base / 'scripts').mkdir()
            (base / 'scripts/shaderlist.txt').write_text('collision\n', encoding='ascii')
            (base / 'scripts/collision.shader').write_text('textures/collision/playerclip\n{\n'
                ' qer_editorimage textures/collision/wall.png\n surfaceparm playerclip\n surfaceparm nodraw\n}\n', encoding='ascii')
        bounds = [((-272, -272, -16), (272, 272, 0)), ((-272, -272, 256), (272, 272, 272)),
                  ((-272, -272, 0), (-256, 272, 256)), ((256, -272, 0), (272, 272, 256)),
                  ((-256, -272, 0), (256, -256, 256)), ((-256, 256, 0), (256, 272, 256))]
        world = '{\n"classname" "worldspawn"\n'
        if target == 'quake':
            world += '"wad" "' + ';'.join(wad_paths) + '"\n'
        world += '\n'.join(box(lo, hi, 'collision/wall' if target == 'quake3' else 'wall') for lo, hi in bounds) + '\n}\n'
        player = 'info_player_deathmatch' if target == 'quake3' else 'info_player_start'
        world += '{\n"classname" "' + player + '"\n"origin" "180 0 64"\n}\n'
        source = maps / 'room.map'
        source.write_text(world, encoding='ascii')
        mapped = maps / 'collision.map'
        options = ['--target', target] + (['--material', 'collision/playerclip'] if target == 'quake3' else [])
        run(target + '-export', ['model', 'collision', mesh, '--operation', 'export-map', *options, '--output', maps / 'fragment.map'])
        run(target + '-place', ['model', 'collision', mesh, '--operation', 'place', *options, '--map', source, '--output', mapped])
        if target == 'quake3':
            command = [args.q3map2.resolve(), '-game', 'quake3', '-fs_basepath', game, '-fs_homepath', game / 'home',
                       '-fs_game', 'baseq3', '-threads', '2', '-meta', mapped]
        else:
            command = [args.qbsp.resolve(), '-threads', '2'] + (['-q2bsp'] if target == 'quake2' else []) + [mapped]
        log = run(target + '-compile', command, cli=False)
        assert all(word not in log.upper() for word in ('LEAK', 'ERROR:', 'WARNING', 'FAILED TO LOAD')), log
        data = mapped.with_suffix('.bsp').read_bytes()
        if target == 'quake':
            assert struct.unpack_from('<i', data)[0] == 29
            samples = []
            for hull in (0, 1, 2):
                values = [quake_contents(data, hull, volume[0]) for volume in volumes]
                assert values == ([-1, -1] if hull == 0 else [-2, -2]), (hull, values)
                assert quake_contents(data, hull, (180, 0, 128)) == -1
                samples.append({'hull': hull, 'boxCentres': values, 'outside': -1})
            verified = {'hullSamples': samples}
        else:
            assert data[:4] == b'IBSP' and struct.unpack_from('<i', data, 4)[0] == (38 if target == 'quake2' else 46)
            verified = verify_brushes(data, target == 'quake3', volumes)
        assert source.read_text(encoding='ascii') == world and hashlib.sha256(mesh.read_bytes()).hexdigest() == mesh_hash
        checks.append({'target': target, 'bspSha256': hashlib.sha256(data).hexdigest(), **verified})
    for binary in binaries.values():
        assert hashlib.sha256(Path(binary['path']).read_bytes()).hexdigest() == binary['sha256']
    record = {'recordedAtUtc': datetime.now(timezone.utc).isoformat(), 'binaries': binaries,
              'stepsPassed': len(steps), 'checks': checks, 'gameLaunched': False,
              'scope': 'Compiled BSP collision and draw-surface data; engine movement and packaged release acceptance remain open.'}
    (root / 'verified.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
    print(f'PASS: {len(steps)} steps; collision verified in BSP29, IBSP38 and IBSP46.', flush=True)


if __name__ == '__main__':
    main()
