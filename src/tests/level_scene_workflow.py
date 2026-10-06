"""Original scene -> model -> q3map2 -> package proof. No game or input control."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess
import zipfile

from level_prefab_workflow import box, png


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
    for folder in ('maps', 'models/scene'):
        (base / folder).mkdir(parents=True, exist_ok=True)
    (root / 'home').mkdir(exist_ok=True)
    steps = []

    def run(label, words, cli=True):
        command = ([str(args.binary.resolve()), '--cli', '--json', '--settings-file', str(root / 'settings.ini')] + list(map(str, words))
                   if cli else list(map(str, words)))
        result = subprocess.run(command, cwd=repo, capture_output=True, timeout=120)
        (root / f'{label}.stdout.txt').write_bytes(result.stdout)
        (root / f'{label}.stderr.txt').write_bytes(result.stderr)
        steps.append({'step': label, 'command': command, 'exitCode': result.returncode})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert result.returncode == 0, (label, result.stdout, result.stderr)
        print(label, 'passed', flush=True)
        return json.loads(result.stdout) if cli else result.stdout.decode('utf-8', errors='replace')

    png(base / 'textures/scene/checker.png')
    design = root / 'prop.model.json'
    design.write_text(json.dumps({'schemaVersion': 2, 'name': 'scene_prop', 'parts': [{
        'name': 'prop', 'primitive': 'cylinder', 'size': [32, 32, 40], 'origin': [0, 0, 20],
        'roll': 0, 'pitch': 0, 'yaw': 0, 'segments': 8, 'material': 'textures/scene/checker',
        'uvScale': [1, 1], 'uvOffset': [0, 0], 'uvRotation': 0}]}), encoding='utf-8')
    run('model', ['model', 'build', design, '--output', base / 'models/scene/prop.md3', '--overwrite'])
    walls = [((-272, -272, -16), (272, 272, 0)), ((-272, -272, 256), (272, 272, 272)),
             ((-272, -272, 0), (-256, 272, 256)), ((256, -272, 0), (272, 272, 256)),
             ((-256, -272, 0), (256, -256, 256)), ((-256, 256, 0), (256, 272, 256))]
    source = base / 'maps/original.map'
    source.write_text('{\n"classname" "worldspawn"\n' + '\n'.join(box(lo, hi, 'textures/scene/checker') for lo, hi in walls)
                      + '\n}\n{\n"classname" "info_player_deathmatch"\n"origin" "0 0 32"\n}\n'
                      + '{\n"classname" "light"\n"origin" "0 0 180"\n"light" "350"\n}\n'
                      + '{\n"classname" "misc_model"\n"origin" "64 0 0"\n"model" "models/scene/prop.md3"\n}\n', encoding='utf-8')
    scene = base / 'maps/scene.map'
    created = run('scene-create', ['editor', 'scene', 'create', source, '--kind', 'layer', '--name', 'Editor hidden', '--output', scene, '--overwrite'])
    node = created['createdId']
    run('scene-assign', ['editor', 'scene', 'assign', scene, '--id', node, '--objects', 'brush:0,entity:3', '--output', scene, '--overwrite'])
    run('scene-hide', ['editor', 'scene', 'visibility', scene, '--id', node, '--visible', 'false', '--output', scene, '--overwrite'])
    run('scene-lock', ['editor', 'scene', 'lock', scene, '--id', node, '--locked', 'true', '--output', scene, '--overwrite'])
    assert scene.read_bytes().startswith(source.read_bytes())
    prefix = [args.compiler.resolve(), '-game', 'quake3', '-fs_basepath', root / 'game', '-fs_homepath', root / 'home', '-fs_game', 'baseq3', '-threads', '2']
    for name, path in [('baseline', source), ('scene', scene)]:
        output = run(name + '-bsp', prefix + ['-meta', path], cli=False)
        assert 'LEAKED' not in output and 'ERROR:' not in output, output
    original_bsp = source.with_suffix('.bsp').read_bytes()
    scene_bsp = scene.with_suffix('.bsp').read_bytes()

    def lump(data, index):
        offset, size = struct.unpack_from('<2i', data, 8 + 8 * index)
        return data[offset:offset + size]

    assert original_bsp[:8] == scene_bsp[:8] == b'IBSP' + struct.pack('<i', 46)
    for index in range(1, 14):
        assert lump(original_bsp, index) == lump(scene_bsp, index), ('compiled geometry differs', index)
    assert len(lump(scene_bsp, 8)) // 12 == 6
    surfaces = [struct.unpack_from('<12i', lump(scene_bsp, 13), at) for at in range(0, len(lump(scene_bsp, 13)), 104)]
    assert any(surface[2] == 3 for surface in surfaces), 'hidden placed model must still be baked'
    for stage, options in [('vis', ['-vis', '-fast']), ('light', ['-light', '-fast'])]:
        output = run(stage, prefix + options + [scene], cli=False)
        assert 'LEAKED' not in output and 'ERROR:' not in output, output
    library = root / 'assets.pk3'
    with zipfile.ZipFile(library, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name in ['textures/scene/checker.png', 'models/scene/prop.md3']:
            archive.write(base / name, name)
    dependencies = root / 'dependencies.pk3'
    run('dependencies', ['package', 'subset', library, dependencies, '--map-input', scene, '--engine', 'idTech3', '--overwrite'])
    package = root / 'scene.pk3'
    run('package', ['package', 'save-as', dependencies, package, '--format', 'pk3', '--add-file', scene, '--as', 'maps/scene.map',
                    '--add-file', scene.with_suffix('.bsp'), '--as', 'maps/scene.bsp', '--overwrite'])
    validation = run('validate', ['package', 'validate', package])['validation']
    assert validation['valid'] and validation['uncheckedCount'] == 0
    with zipfile.ZipFile(package) as archive:
        assert archive.testzip() is None and archive.read('maps/scene.map') == scene.read_bytes()
        assert archive.read('models/scene/prop.md3') == (base / 'models/scene/prop.md3').read_bytes()
    verified = {'stepsPassed': len(steps), 'identicalGeometryLumps': list(range(1, 14)), 'compiledBrushes': 6,
                'modelSurfaces': sum(surface[2] == 3 for surface in surfaces), 'validation': validation,
                'mapSha256': hashlib.sha256(scene.read_bytes()).hexdigest(), 'packageSha256': hashlib.sha256(package.read_bytes()).hexdigest()}
    (root / 'verified.json').write_text(json.dumps(verified, indent=2), encoding='utf-8')
    print('PASS: hidden, locked editor geometry and models compile and package unchanged.', flush=True)


if __name__ == '__main__':
    main()
