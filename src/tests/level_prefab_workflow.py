"""Original prefab -> Models -> q3map2 -> package integration proof.

Use --output-root below the project's .agents/tmp. --compiler is optional;
without it, this still exercises CLI validation, dependencies and PK3 round trips.
No game is launched, and no native input or screen capture is used.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess
import wave
import zipfile
import zlib


def box(lo, hi, material):
    lines = ['{']
    for axis in range(3):
        for positive in (False, True):
            p = list(lo)
            p[axis] = hi[axis] if positive else lo[axis]
            a, b = p.copy(), p.copy()
            first, second = (axis+1) % 3, (axis+2) % 3
            if positive:
                first, second = second, first
            a[first] += 1
            b[second] += 1
            geometry = ' '.join('( ' + ' '.join(map(str, v)) + ' )' for v in (p, a, b))
            lines.append(geometry + f' {material} 0 0 0 0.5 0.5 0 0 0')
    return '\n'.join(lines + ['}'])


def png(path):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind+data) & 0xffffffff)
    raw = b''.join(b'\0' + b''.join(bytes((50, 180, 205) if (x//8+y//8) % 2 else (35, 45, 55)) for x in range(64)) for y in range(64))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', 64, 64, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--compiler', type=Path)
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    root = args.output_root.resolve()
    assert root.is_relative_to(repo / '.agents' / 'tmp')
    base = root / 'game' / 'baseq3'
    for folder in ('maps', 'models/prefab', 'sound/prefab', 'prefabs'):
        (base / folder).mkdir(parents=True, exist_ok=True)
    (root / 'home').mkdir(exist_ok=True)
    steps = []

    def run(label, words, expected=0, cli=True):
        command = ([str(args.binary.resolve()), '--cli', '--settings-file', str(root / 'settings.ini')] + list(map(str, words))
                   if cli else list(map(str, words)))
        result = subprocess.run(command, cwd=repo, capture_output=True, timeout=120)
        (root / f'{label}.stdout.txt').write_bytes(result.stdout)
        (root / f'{label}.stderr.txt').write_bytes(result.stderr)
        steps.append({'step': label, 'command': command, 'exitCode': result.returncode, 'expectedExitCode': expected})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert result.returncode == expected, (label, result.returncode, result.stdout, result.stderr)
        print(label, result.returncode, flush=True)
        return json.loads(result.stdout) if cli else result.stdout.decode('utf-8', errors='replace')

    png(base / 'textures' / 'prefab' / 'checker.png')
    model_source = root / 'fixture.model.json'
    model_source.write_text(json.dumps({'schemaVersion': 2, 'name': 'prefab_fixture', 'parts': [{
        'name': 'prop', 'primitive': 'cylinder', 'size': [32, 32, 40], 'origin': [0, 0, 20],
        'roll': 0, 'pitch': 0, 'yaw': 15, 'segments': 8, 'material': 'textures/prefab/checker',
        'uvScale': [1, 1], 'uvOffset': [0, 0], 'uvRotation': 0}]}), encoding='utf-8')
    run('model-build', ['model', 'build', model_source, '--output', base / 'models/prefab/prop.md3', '--overwrite', '--json'])
    with wave.open(str(base / 'sound/prefab/tone.wav'), 'wb') as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(22050)
        wav.writeframes(b'\0\0' * 2205)
    panel = '{\npatchDef2\n{\nprefab/checker\n( 3 3 0 0 0 )\n(\n' + '\n'.join(
        '( ' + ' '.join(f'( {x} {y} {56 + (12 if i == 1 and j == 1 else 0)} {i/2} {j/2} )'
                       for j, y in enumerate((-16, 0, 16))) + ' )' for i, x in enumerate((-48, 0, 48))) + '\n)\n}\n}\n'
    source = root / 'assembly.map'
    source.write_text('{\n"classname" "worldspawn"\n"message" "Project world settings"\n' + panel + '}\n'
                      + '{\n"classname" "func_door"\n"targetname" "gate"\n' + box((-24, -8, 0), (24, 8, 48), 'prefab/checker') + '\n}\n'
                      + '{\n"classname" "target_delay"\n"origin" "0 0 64"\n"targetname" "relay"\n"target" "gate"\n"killtarget" "outside"\n}\n'
                      + '{\n"classname" "misc_model"\n"origin" "0 0 72"\n"model" "models/prefab/prop.md3"\n"angles" "0 30 0"\n}\n'
                      + '{\n"classname" "target_speaker"\n"origin" "0 0 64"\n"noise" "sound/prefab/tone.wav"\n}\n', encoding='utf-8')
    prefab = base / 'prefabs' / 'door.vprefab'
    export_args = ['map', 'export-prefab', source, '--engine', 'idTech3', '--name', 'Door assembly', '--description', 'Original reusable assembly',
                   '--object', 'brush:0', '--object', 'patch:0', '--object', 'entity:2', '--object', 'entity:3', '--object', 'entity:4',
                   '--package', base, '--output', prefab, '--json']
    dry_output = root / 'dry.vprefab'
    dry_args = export_args.copy()
    dry_args[dry_args.index('--output')+1] = dry_output
    dry = run('export-dry', dry_args + ['--dry-run'])
    assert dry['save']['dryRun'] and not dry['save']['written'] and not dry_output.exists()
    captured = run('export', export_args + ['--overwrite'])
    assert captured['prefab']['entities'] == 4 and captured['prefab']['brushes'] == 1 and captured['prefab']['patches'] == 1
    assert captured['prefab']['externalTargets'] == ['outside']
    assert 'Project world settings' not in json.loads(prefab.read_text(encoding='utf-8'))['map']
    first_bytes = prefab.read_bytes()
    run('export-no-overwrite', export_args, expected=4)
    replaced = run('export-backup', export_args + ['--overwrite'])
    assert Path(replaced['save']['backupPath']).read_bytes() == first_bytes
    info = run('inspect', ['map', 'inspect-prefab', prefab, '--package', base, '--json'])['prefab']
    assert info['dependencies']['missing'] == 0 and info['dependencies']['complete']
    run('duplicate-name', export_args + ['--name', 'duplicate'], expected=2)
    run('unknown-option', ['map', 'inspect-prefab', prefab, '--unknown', '--json'], expected=2)
    run('extra-input', ['map', 'inspect-prefab', prefab, 'extra', '--json'], expected=2)
    malformed = root / 'invalid.vprefab'
    malformed.write_text('{"format":"VibeStudioPrefab","version":2}', encoding='utf-8')
    run('invalid-version', ['map', 'inspect-prefab', malformed, '--json'], expected=4)
    run('missing-file', ['map', 'inspect-prefab', root / 'missing.vprefab', '--json'], expected=3)
    room = base / 'maps' / 'room.map'
    bounds = [((-304, -304, -16), (304, 304, 0)), ((-304, -304, 256), (304, 304, 272)),
              ((-304, -304, 0), (-288, 304, 256)), ((288, -304, 0), (304, 304, 256)),
              ((-288, -304, 0), (288, -288, 256)), ((-288, 288, 0), (288, 304, 256))]
    room.write_text('{\n"classname" "worldspawn"\n' + '\n'.join(box(lo, hi, 'prefab/checker') for lo, hi in bounds)
                    + '\n}\n{\n"classname" "info_player_deathmatch"\n"origin" "0 -192 32"\n}\n'
                    + '{\n"classname" "light"\n"origin" "0 0 224"\n"light" "500"\n}\n', encoding='utf-8')
    library = root / 'library.pk3'
    run('library', ['package', 'save-as', base, library, '--format', 'pk3', '--overwrite', '--json'])
    packed = run('inspect-package-entry', ['map', 'inspect-prefab', library, '--entry', 'prefabs/door.vprefab', '--json'])['prefab']
    assert packed['dependencies']['missing'] == 0 and packed['name'] == info['name']
    first, second = base / 'maps/first.map', base / 'maps/placed.map'
    insert_args = ['map', 'insert-prefab', room, '--engine', 'idTech3', '--prefab', library, '--entry', 'prefabs/door.vprefab',
                   '--position=-128,0,0', '--rotation', '0,0,90', '--output', first, '--overwrite', '--json']
    one = run('insert-package', insert_args)['prefab']
    assert one['targetPrefix'] == 'prefab1_' and one['renamedTargets']['gate'] == 'prefab1_gate'
    two = run('insert-second', ['map', 'insert-prefab', first, '--prefab', prefab, '--position', '128,0,0', '--rotation', '0,0,-90',
                                '--package', library, '--output', second, '--overwrite', '--json'])['prefab']
    assert two['targetPrefix'] == 'prefab2_'
    output_bytes = second.read_bytes()
    run('prefix-collision', ['map', 'insert-prefab', second, '--prefab', prefab, '--position', '0,0,0', '--target-prefix', 'prefab1_',
                            '--output', second, '--overwrite', '--json'], expected=4)
    assert second.read_bytes() == output_bytes
    run('bad-position', ['map', 'insert-prefab', first, '--prefab', prefab, '--position', 'nan,0,0', '--output', second, '--json'], expected=2)
    run('bad-lock', insert_args + ['--texture-lock', 'maybe'], expected=2)
    dry_map = root / 'dry.map'
    run('insert-dry', ['map', 'insert-prefab', room, '--engine', 'idTech3', '--prefab', prefab, '--position', '0,0,0', '--output', dry_map, '--dry-run', '--json'])
    assert not dry_map.exists()
    run('dependencies', ['map', 'dependencies', second, '--package', library, '--json'])
    compiled = {}
    if args.compiler:
        prefix = [args.compiler.resolve(), '-game', 'quake3', '-fs_basepath', root / 'game', '-fs_homepath', root / 'home', '-fs_game', 'baseq3', '-threads', '2']
        for stage, options in [('bsp', ['-meta']), ('vis', ['-vis', '-fast']), ('light', ['-light', '-fast'])]:
            output = run(stage, prefix + options + [second], cli=False)
            assert 'LEAKED' not in output and 'ERROR:' not in output, output
        bsp = second.with_suffix('.bsp').read_bytes()
        assert bsp[:4] == b'IBSP' and struct.unpack_from('<i', bsp, 4)[0] == 46
        def lump(index):
            offset, size = struct.unpack_from('<2i', bsp, 8+8*index)
            return bsp[offset:offset+size]
        entities = lump(0).decode(errors='replace')
        for name in ('prefab1_gate', 'prefab2_gate', 'prefab1_relay', 'prefab2_relay', 'sound/prefab/tone.wav'):
            assert name in entities, (name, entities)
        surfaces = [struct.unpack_from('<12i', lump(13), at) for at in range(0, len(lump(13)), 104)]
        assert sum(s[2] == 2 for s in surfaces) == 2  # Both authored curves survive.
        assert any(s[2] == 3 for s in surfaces)  # Generated MD3 triangles are baked.
        compiled = {'bspSha256': hashlib.sha256(bsp).hexdigest(), 'surfaces': len(surfaces), 'patches': 2}
    subset = root / 'dependencies.pk3'
    run('subset', ['package', 'subset', library, subset, '--map-input', second, '--engine', 'idTech3', '--overwrite', '--json'])
    package = root / 'level.pk3'
    final = ['package', 'save-as', subset, package, '--format', 'pk3', '--add-file', prefab, '--as', 'prefabs/door.vprefab',
             '--add-file', second, '--as', 'maps/placed.map', '--overwrite', '--json']
    if args.compiler:
        final += ['--add-file', second.with_suffix('.bsp'), '--as', 'maps/placed.bsp']
    run('publish', final)
    validation = run('validate', ['package', 'validate', package, '--json'])['validation']
    assert validation['valid'] and validation['uncheckedCount'] == 0
    with zipfile.ZipFile(package) as archive:
        assert archive.testzip() is None
        assert archive.read('prefabs/door.vprefab') == first_bytes
        assert archive.read('models/prefab/prop.md3') == (base / 'models/prefab/prop.md3').read_bytes()
        assert archive.read('maps/placed.map') == output_bytes
    run('final-prefab', ['map', 'inspect-prefab', package, '--entry', 'prefabs/door.vprefab', '--json'])
    verified = {'stepsPassed': len(steps), 'compiled': compiled, 'validation': validation,
                'prefabSha256': hashlib.sha256(first_bytes).hexdigest(), 'packageSha256': hashlib.sha256(package.read_bytes()).hexdigest()}
    (root / 'verified.json').write_text(json.dumps(verified, indent=2), encoding='utf-8')
    print(f'PASS: {len(steps)} steps, linked model/patch/brush/sound assemblies, verified PK3.', flush=True)


if __name__ == '__main__':
    main()
