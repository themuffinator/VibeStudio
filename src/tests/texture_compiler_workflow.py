"""Optional generated-texture acceptance through VibeMap2 bsp and VibeMap3.

All assets are authored by VibeStudio CLI from a synthetic layered recipe.
Outputs and compiler logs stay under the repository's .agents/tmp directory.
No game launch, commercial assets, input control or screen capture is used.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import platform
import struct
import subprocess
import zipfile


def box(lo, hi, texture):
    lines = ['{']
    for axis in range(3):
        for positive in (False, True):
            point = list(lo)
            point[axis] = hi[axis] if positive else lo[axis]
            first, second = (axis + 1) % 3, (axis + 2) % 3
            if positive:
                first, second = second, first
            a, b = point.copy(), point.copy()
            a[first] += 1
            b[second] += 1
            plane = ' '.join('( ' + ' '.join(map(str, p)) + ' )' for p in (point, a, b))
            lines.append(f'{plane} {texture} 0 0 0 1 1')
    return '\n'.join(lines + ['}'])


def room(wad=None):
    bounds = [((-144, -144, -16), (144, 144, 0)), ((-144, -144, 128), (144, 144, 144)),
              ((-144, -144, 0), (-128, 144, 128)), ((128, -144, 0), (144, 144, 128)),
              ((-128, -144, 0), (128, -128, 128)), ((-128, 128, 0), (128, 144, 128))]
    text = '{\n"classname" "worldspawn"\n'
    if wad:
        text += f'"wad" "{wad.as_posix()}"\n'
    text += '\n'.join(box(lo, hi, 'old') for lo, hi in bounds) + '\n}\n'
    return text + '{\n"classname" "info_player_start"\n"origin" "0 0 32"\n}\n' + \
        '{\n"classname" "light"\n"origin" "0 0 96"\n"light" "300"\n}\n'


def lump(data, index, header=8):
    offset, size = struct.unpack_from('<2i', data, header + 8 * index)
    assert offset >= 0 and size >= 0 and offset + size <= len(data)
    return data[offset:offset + size]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--qbsp', type=Path, required=True)
    parser.add_argument('--q3map2', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    if not __debug__:
        parser.error('Run without -O; assertions verify the outputs.')
    repo = Path(__file__).resolve().parents[2]
    root = args.output_root.resolve()
    if not root.is_relative_to(repo / '.agents' / 'tmp'):
        parser.error('Output root must be within this repository/.agents/tmp.')
    root.mkdir(parents=True, exist_ok=True)
    runtime = root / 'runtime'
    runtime.mkdir(exist_ok=True)
    environment = dict(os.environ, TEMP=str(runtime), TMP=str(runtime), TMPDIR=str(runtime))
    steps, checks = [], []

    def run(label, words, cli=True):
        command = ([str(args.binary.resolve()), '--cli', '--settings-file', str(root / 'settings.ini')] + list(map(str, words)) + ['--json']
                   if cli else list(map(str, words)))
        result = subprocess.run(command, cwd=root, env=environment, capture_output=True, timeout=120)
        (root / f'{label}.stdout.txt').write_bytes(result.stdout)
        (root / f'{label}.stderr.txt').write_bytes(result.stderr)
        steps.append({'step': label, 'command': command, 'exitCode': result.returncode})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert result.returncode == 0, (label, result.stdout.decode(errors='replace'), result.stderr.decode(errors='replace'))
        print(label, 'PASS', flush=True)
        return json.loads(result.stdout) if cli else result.stdout.decode(errors='replace') + result.stderr.decode(errors='replace')

    project = root / 'authored.vtexture'
    recipe = root / 'layers.json'
    recipe.write_text(json.dumps([{'op': 'layer-add', 'name': 'Inlay'},
                                 {'op': 'rectangle', 'x': 8, 'y': 8, 'x2': 23, 'y2': 23, 'color': '#c0c0c0', 'filled': True}]), encoding='utf-8')
    run('author-project', ['texture', 'create', '--size', '32x32', '--color', '#606060', '--operations', recipe, '--output', project, '--overwrite'])
    palette = root / 'synthetic-palette.lmp'
    palette.write_bytes(bytes(value for i in range(256) for value in (i, i, i)))
    options = root / 'export.json'
    options.write_text(json.dumps({'name': 'authored'}), encoding='utf-8')

    # Native Quake: a staged miptexture, saved WAD2, and independently read BSP.
    empty = root / 'empty.wad'
    empty.write_bytes(b'WAD2' + struct.pack('<2i', 0, 12))
    draft = root / 'quake.vibepackage'
    run('quake-stage', ['texture', 'stage', project, '--profile', 'quake-miptex', '--palette-file', palette,
                        '--export-options', options, '--target-package', empty, '--target-entry', 'authored', '--output', draft, '--overwrite'])
    # qbsp adds a skip material internally, even for an all-authored room.
    skip_options = root / 'skip.json'
    skip_options.write_text(json.dumps({'name': 'skip'}), encoding='utf-8')
    run('quake-stage-skip', ['texture', 'stage', project, '--profile', 'quake-miptex', '--palette-file', palette,
                             '--export-options', skip_options, '--target-package', draft, '--target-entry', 'skip', '--output', draft, '--overwrite'])
    wad = root / 'authored.wad'
    run('quake-publish', ['package', 'save-as', draft, wad, '--format', 'wad', '--overwrite'])
    wad_bytes = wad.read_bytes()
    assert wad_bytes[:4] == b'WAD2'
    count, directory = struct.unpack_from('<2i', wad_bytes, 4)
    assert count == 2 and wad_bytes[directory + 12] == 0x44
    offset, size = struct.unpack_from('<2i', wad_bytes, directory)
    mip = wad_bytes[offset:offset + size]
    assert mip[:16].rstrip(b'\0') == b'authored' and struct.unpack_from('<2I', mip, 16) == (32, 32)
    expected = bytes(192 if 8 <= x < 24 and 8 <= y < 24 else 96 for y in range(32) for x in range(32))
    assert mip[40:40 + 1024] == expected
    source = root / 'quake-source.map'
    source.write_text(room(wad), encoding='utf-8')
    mapped = root / 'quake.map'
    run('quake-apply', ['map', 'apply-texture', source, '--object', 'entity:0', '--texture', 'authored', '--engine', 'idTech2', '--output', mapped, '--overwrite'])
    log = run('quake-compile', [args.qbsp.resolve(), '-threads', '2', mapped], cli=False)
    assert 'LEAK' not in log.upper() and 'WARNING' not in log.upper(), log
    bsp = mapped.with_suffix('.bsp').read_bytes()
    assert struct.unpack_from('<i', bsp)[0] == 29
    textures = lump(bsp, 2, 4)
    count = struct.unpack_from('<i', textures)[0]
    embedded = [struct.unpack_from('<i', textures, 4 + 4 * i)[0] for i in range(count)]
    assert any(at >= 0 and textures[at:at + len(mip)] == mip for at in embedded)
    checks.append({'game': 'quake', 'verified': 'all four native mip levels preserved exactly in BSP', 'bspSha256': hashlib.sha256(bsp).hexdigest()})

    # Quake II: authored WAL is consumed from the game's texture directory.
    q2base = root / 'quake2' / 'baseq2'
    q2maps = q2base / 'maps'
    q2maps.mkdir(parents=True, exist_ok=True)
    wal = q2base / 'textures' / 'handoff' / 'authored.wal'
    wal.parent.mkdir(parents=True, exist_ok=True)
    options.write_text(json.dumps({'name': 'handoff/authored'}), encoding='utf-8')
    run('quake2-export', ['texture', 'export', project, '--profile', 'quake2-wal', '--palette-file', palette, '--export-options', options, '--output', wal, '--overwrite'])
    run('quake2-export-skip', ['texture', 'export', project, '--profile', 'quake2-wal', '--palette-file', palette, '--export-options', skip_options,
                              '--output', q2base / 'textures' / 'skip.wal', '--overwrite'])
    wal_bytes = wal.read_bytes()
    assert wal_bytes[:32].rstrip(b'\0') == b'handoff/authored' and wal_bytes[100:1124] == expected
    source = q2maps / 'source.map'
    source.write_text(room(), encoding='utf-8')
    mapped = q2maps / 'authored.map'
    run('quake2-apply', ['map', 'apply-texture', source, '--object', 'entity:0', '--texture', 'handoff/authored', '--engine', 'idTech2', '--output', mapped, '--overwrite'])
    log = run('quake2-compile', [args.qbsp.resolve(), '-q2bsp', '-threads', '2', mapped], cli=False)
    assert 'LEAK' not in log.upper() and 'WARNING' not in log.upper(), log
    bsp = mapped.with_suffix('.bsp').read_bytes()
    assert bsp[:4] == b'IBSP' and struct.unpack_from('<i', bsp, 4)[0] == 38
    texinfo = lump(bsp, 5)
    names = [texinfo[at + 40:at + 72].split(b'\0', 1)[0].decode() for at in range(0, len(texinfo), 76)]
    faces = lump(bsp, 6)
    used = {names[struct.unpack_from('<h', faces, at + 10)[0]] for at in range(0, len(faces), 20)}
    assert used == {'handoff/authored'}, used
    run('quake2-dependencies', ['map', 'dependencies', mapped, '--package', q2base, '--engine', 'idTech2'])
    run('quake2-package', ['package', 'subset', q2base, root / 'quake2-assets.pak', '--map-input', mapped, '--engine', 'idTech2', '--format', 'pak', '--overwrite'])
    checks.append({'game': 'quake2', 'verified': 'native WAL pixels and compiled BSP texture references', 'bspSha256': hashlib.sha256(bsp).hexdigest()})

    # Quake III: two exports flow through a package draft, publication and shader.
    assets = root / 'q3-source'
    (assets / 'scripts').mkdir(parents=True, exist_ok=True)
    (assets / 'scripts' / 'shaderlist.txt').write_text('handoff\n', encoding='utf-8')
    (assets / 'scripts' / 'handoff.shader').write_text('textures/handoff/material\n{\n qer_editorimage textures/handoff/authored.png\n q3map_lightimage textures/handoff/authored.png\n { map textures/handoff/authored.tga }\n}\n', encoding='utf-8')
    draft = root / 'quake3.vibepackage'
    for profile, suffix in [('png', 'png'), ('tga', 'tga')]:
        run(f'quake3-stage-{suffix}', ['texture', 'stage', project, '--profile', profile, '--target-package', assets if suffix == 'png' else draft,
                                      '--target-entry', f'textures/handoff/authored.{suffix}', '--output', draft, '--overwrite'])
    package = root / 'quake3-assets.pk3'
    run('quake3-publish', ['package', 'save-as', draft, package, '--format', 'pk3', '--overwrite'])
    base = root / 'quake3' / 'baseq3'
    base.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(package) as archive:
        assert {item.filename for item in archive.infolist() if not item.is_dir()} == {'scripts/shaderlist.txt', 'scripts/handoff.shader', 'textures/handoff/authored.png', 'textures/handoff/authored.tga'}
        assert all((base / item.filename).resolve().is_relative_to(base.resolve()) for item in archive.infolist())
        archive.extractall(base)
    maps = base / 'maps'
    maps.mkdir(exist_ok=True)
    source = maps / 'source.map'
    source.write_text(room().replace('info_player_start', 'info_player_deathmatch'), encoding='utf-8')
    mapped = maps / 'authored.map'
    run('quake3-apply', ['map', 'apply-texture', source, '--object', 'entity:0', '--texture', 'handoff/material', '--engine', 'idTech3', '--output', mapped, '--overwrite'])
    home = root / 'q3-home'
    home.mkdir(exist_ok=True)
    command = [args.q3map2.resolve(), '-game', 'quake3', '-fs_basepath', root / 'quake3', '-fs_homepath', home, '-fs_game', 'baseq3', '-threads', '2']
    for label, flags in [('bsp', ['-meta']), ('vis', ['-vis', '-fast']), ('light', ['-light', '-fast'])]:
        log = run(f'quake3-{label}', command + flags + [mapped], cli=False)
        assert 'LEAKED' not in log and 'ERROR:' not in log and 'WARNING:' not in log, log
    bsp = mapped.with_suffix('.bsp').read_bytes()
    assert bsp[:4] == b'IBSP' and struct.unpack_from('<i', bsp, 4)[0] == 46
    shaders = lump(bsp, 1)
    names = {shaders[at:at + 64].split(b'\0', 1)[0].decode() for at in range(0, len(shaders), 72)}
    assert 'textures/handoff/material' in names and lump(bsp, 14), names
    run('quake3-dependencies', ['map', 'dependencies', mapped, '--package', base, '--engine', 'idTech3'])
    final = root / 'quake3-release.pk3'
    run('quake3-release', ['package', 'save-as', package, final, '--format', 'pk3', '--add-file', mapped.with_suffix('.bsp'), '--as', 'maps/authored.bsp', '--overwrite'])
    validation = run('quake3-validate', ['package', 'validate', final])['validation']
    assert validation['valid'] and validation['verifiedCount'] == 5, validation
    checks.append({'game': 'quake3', 'verified': 'PNG/TGA shader inputs, BSP/VIS/LIGHT and validated PK3', 'bspSha256': hashlib.sha256(bsp).hexdigest()})
    compilers = {}
    for name, executable, log_name in [('qbsp', args.qbsp, 'quake-compile'), ('q3map2', args.q3map2, 'quake3-bsp')]:
        version = next(line for line in (root / f'{log_name}.stdout.txt').read_text(encoding='utf-8', errors='replace').splitlines() if line.strip())
        compilers[name] = {'path': str(executable.resolve()), 'versionLine': version, 'sha256': hashlib.sha256(executable.read_bytes()).hexdigest()}
    report = {'stepsPassed': len(steps), 'checks': checks, 'compilers': compilers, 'platform': platform.platform(),
              'sourceProjectSha256': hashlib.sha256(project.read_bytes()).hexdigest()}
    (root / 'verified.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
