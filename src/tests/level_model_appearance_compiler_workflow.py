"""Compare synthetic MD3 instance appearances with a real NRC q3map2 BSP.

All generated files remain in --output-root under this repository's .agents/tmp.
No game, commercial assets, native input or screen capture. The MD3/BSP layouts
are the credited id Software/q3map2 public formats; fixtures/readers are original.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from level_compiler_test_helpers import box, png


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def native_model():
    surfaces = []
    positions = [((-12, -12, 0), (12, -12, 0), (0, 12, 0)), ((36, -12, 6), (60, -12, 6), (48, 12, 6))]
    for index, name in enumerate(('Body', 'Head')):
        materials = [f'textures/appearance/{name.lower()}.tga']
        if index == 0:
            materials.append('textures/appearance/unused.tga')
        triangles = struct.pack('<3i', 0, 2, 1)
        shaders = b''.join(struct.pack('<64si', name.encode(), slot) for slot, name in enumerate(materials))
        uv = struct.pack('<6f', 0, 0, 1, 0, .5, 1)
        vertices = b''.join(struct.pack('<3hH', int(x*64), int(y*64), int((z+frame*(40 if index == 0 else 20))*64), 0)
                            for frame in range(2) for x, y, z in positions[index])
        offset = 108
        surfaces.append(struct.pack('<4s64s10i', b'IDP3', name.encode(), 0, 2, len(materials), 3, 1,
                                    offset, offset+len(triangles), offset+len(triangles)+len(shaders),
                                    offset+len(triangles)+len(shaders)+len(uv), offset+len(triangles+shaders+uv+vertices))
                        + triangles+shaders+uv+vertices)
    frames = b''.join(struct.pack('<10f16s', -12, -12, 0, 60, 12, 46, 0, 0, 0, 90, f'pose{frame}'.encode()) for frame in range(2))
    data = b''.join(surfaces)
    return struct.pack('<4si64s9i', b'IDP3', 15, b'fixture', 0, 2, 0, 2, 0, 108, 108+len(frames), 108+len(frames), 108+len(frames+data)) + frames + data


def read_bsp(path):
    data = path.read_bytes()
    assert data[:8] == b'IBSP' + struct.pack('<i', 46)
    def lump(index):
        offset, size = struct.unpack_from('<2i', data, 8+index*8)
        assert offset >= 0 and size >= 0 and offset+size <= len(data)
        return data[offset:offset+size]
    shader_lump, vertex_lump, face_lump, index_lump = lump(1), lump(10), lump(13), lump(11)
    shaders = [shader_lump[i:i+64].split(b'\0')[0].decode() for i in range(0, len(shader_lump), 72)]
    found = []
    for offset in range(0, len(face_lump), 104):
        header = struct.unpack_from('<12i', face_lump, offset)
        material = shaders[header[0]]
        if not material.startswith('textures/appearance/') or material.endswith('/wall'):
            continue
        assert header[6] % 3 == 0
        for at in range(header[5], header[5]+header[6], 3):
            indexes = struct.unpack_from('<3i', index_lump, at*4)
            points = [struct.unpack_from('<3f', vertex_lump, (header[3]+index)*44) for index in indexes]
            found.append((material, tuple(sorted(tuple(round(c, 4) for c in p) for p in points))))
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    root = args.output_root.resolve()
    root.relative_to(repo / '.agents/tmp')
    assert not root.exists(), 'Use a fresh output directory.'
    root.mkdir(parents=True)
    steps = []
    source = native_model()

    def run(name, command, cwd=root, expected_code=0):
        result = subprocess.run(list(map(str, command)), cwd=cwd, capture_output=True, timeout=120)
        (root / (name+'.stdout.txt')).write_bytes(result.stdout)
        (root / (name+'.stderr.txt')).write_bytes(result.stderr)
        steps.append({'name': name, 'command': list(map(str, command)), 'exitCode': result.returncode})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert result.returncode == expected_code, (name, result.stdout.decode(errors='replace'), result.stderr.decode(errors='replace'))
        return result.stdout

    materials = ['body', 'head', 'unused', 'blue', 'green', 'red', 'fallback', 'equal', 'wall']
    scenarios = [
        ('default', {}, {}, [('body', 0), ('head', 0)]),
        ('frame', {'_frame': '1'}, {}, [('body', 0), ('head', 0)]),
        ('baked-frame', {}, {}, [('body', 1), ('head', 1)]),
        ('named', {'_skin': 'blue'}, {'prop_blue.skin': 'textures/appearance/body,textures/appearance/blue\ntextures/appearance/head,textures/appearance/green\n'}, [('blue', 0), ('green', 0)]),
        ('omission', {'skin': 'body'}, {'prop_body.skin': 'replace textures/appearance/body textures/appearance/red\n'}, [('red', 0), None]),
        ('numeric', {'_skin': '14', 'skin': 'body', '_frame': '0', 'frame': '1'}, {'prop.md3_14.skin': 'textures/appearance/body,textures/appearance/blue\n'}, [('blue', 0), None]),
        ('remaps', {'_skin': 'blue', '_remap0': '*;textures/appearance/fallback', '_remap1': 'blue;textures/appearance/red', '_remap2': 'appearance/blue;textures/appearance/green', '_remap3': 'appearance/blue;textures/appearance/equal', '_remap4': '*;textures/appearance/head'}, {'prop_blue.skin': 'textures/appearance/body,textures/appearance/blue\ntextures/appearance/head,textures/appearance/blue2\n'}, [('green', 0), ('head', 0)]),
        ('implicit', {}, {'prop_default.skin': 'Body,textures/appearance/blue\nHead,textures/appearance/green\ntag_mount,\n'}, [('blue', 0), ('green', 0)]),
        ('implicit-explicit', {'_skin': 'final'}, {'prop_default.skin': 'Body,textures/appearance/blue\nHead,textures/appearance/green\n', 'prop_final.skin': 'textures/appearance/blue,textures/appearance/red\n'}, [('red', 0), None]),
        ('native-surface-keys', {'_skin': 'native'}, {'prop_native.skin': 'Body,textures/appearance/blue\nHead,textures/appearance/green\n'}, [None, None]),
    ]
    verified = []
    original_positions = [((-12, -12, 0), (12, -12, 0), (0, 12, 0)), ((36, -12, 6), (60, -12, 6), (48, 12, 6))]
    for label, properties, skins, expected in scenarios:
        case = root / label
        base = case / 'game/baseq3'
        for directory in ('models/appearance', 'maps', 'scripts'):
            (base / directory).mkdir(parents=True)
        (case / 'home').mkdir()
        model = base / 'models/appearance/prop.md3'
        model.write_bytes(source)
        cli = [args.binary.resolve(), '--cli', '--json', '--settings-file', case / 'settings.ini']
        model_name = 'prop.md3'
        if label == 'baked-frame':
            recipe = case / 'pose.assembly.json'
            run(label+'-recipe', cli + ['model', 'assembly', '--new', '--part', 'prop', '--model', model,
                                      '--first-frame', '1', '--last-frame', '1', '--output', recipe])
            model_name = 'prop_pose.md3'
            run(label+'-bake', cli + ['model', 'assembly', recipe, '--operation', 'bake', '--time', '0', '--output', model.parent/model_name])
            assert struct.unpack_from('<i', (model.parent/model_name).read_bytes(), 76)[0] == 1
        for name, content in skins.items():
            (model.parent / name).write_text(content, encoding='ascii')
        for name in materials:
            png(base / f'textures/appearance/{name}.png')
        room = [((-144, -144, -16), (144, 144, 0)), ((-144, -144, 192), (144, 144, 208)),
                ((-144, -144, 0), (-128, 144, 192)), ((128, -144, 0), (144, 144, 192)),
                ((-128, -144, 0), (128, -128, 192)), ((-128, 128, 0), (128, 144, 192))]
        mapfile = base / 'maps/appearance.map'
        mapfile.write_text('{\n"classname" "worldspawn"\n' + '\n'.join(box(lo, hi, 'classic', 'appearance/wall') for lo, hi in room)
                           + '\n}\n{\n"classname" "info_player_deathmatch"\n"origin" "0 -64 64"\n}\n'
                           + '{\n"classname" "misc_model"\n"model" "models/appearance/' + model_name + '"\n"origin" "0 0 32"\n'
                           + ''.join(f'"{key}" "{value}"\n' for key, value in properties.items()) + '}\n', encoding='utf-8')
        blocked_frame = label == 'frame'
        preview = json.loads(run(label+'-preview', cli + ['map', 'materials', mapfile, '--package', base, '--engine', 'idTech3', '--geometry'], expected_code=4 if blocked_frame else 0))
        receipt = preview['materials']['modelAppearances'][0]
        if blocked_frame:
            assert receipt['status'] == 'unavailable' and 'static MD3' in receipt['error']
        else:
            retained = [(s['material'].split('/')[-1], receipt['frame']) if s['retained'] else None for s in receipt['surfaces']]
            expected_receipt = [(item[0], 0) if item else None for item in expected]
            assert retained == expected_receipt, (label, retained, expected_receipt)
        compiler_output = run(label+'-bsp', [args.compiler.resolve(), '-game', 'quake3', '-fs_basepath', case/'game',
                    '-fs_homepath', case/'home', '-threads', '2', '-v', '-meta', mapfile])
        assert b'LEAKED' not in compiler_output and b'ERROR:' not in compiler_output, compiler_output
        actual = read_bsp(mapfile.with_suffix('.bsp'))
        triangles = []
        for index, item in enumerate(expected):
            if item is None:
                continue
            name, frame = item
            points = tuple(sorted((float(x), float(y), float(z+32+frame*(40 if index == 0 else 20))) for x, y, z in original_positions[index]))
            triangles.append(('textures/appearance/'+name, points))
        assert Counter(actual) == Counter(triangles), (label, actual, triangles)
        dependencies = json.loads(run(label+'-dependencies', cli + ['map', 'dependencies', mapfile, '--package', base, '--engine', 'idTech3'], expected_code=4 if blocked_frame else 0))
        report = dependencies['dependencies']
        assert report['canExport'] != blocked_frame, report
        assert set(f'models/appearance/{name}' for name in skins) <= set(report['files'])
        assert 'textures/appearance/unused.png' not in report['files']
        assert model.read_bytes() == source
        verified.append({'scenario': label, 'triangles': len(actual), 'appearance': receipt, 'bspSha256': digest(mapfile.with_suffix('.bsp'))})
        print(label, 'compiler geometry/materials/dependencies passed', flush=True)
    (root / 'verified.json').write_text(json.dumps({'status': 'passed', 'binary': str(args.binary.resolve()), 'binarySha256': digest(args.binary),
        'compiler': str(args.compiler.resolve()), 'compilerSha256': digest(args.compiler), 'modelSha256': hashlib.sha256(source).hexdigest(),
        'scenarios': verified, 'steps': len(steps), 'noGameLaunch': True, 'noNativeInputOrCapture': True}, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
