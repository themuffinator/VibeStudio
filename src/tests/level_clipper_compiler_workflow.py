"""Original clip-material fixtures through VibeMap3, dependencies and PK3 export.

No game installation or game launch is used. Output stays under .agents/tmp.
BSP layout reference is credited under Shared clipper in docs/CREDITS.md.
"""
from pathlib import Path
import argparse
import hashlib
import json
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
    (base / 'scripts').mkdir(exist_ok=True)
    (base / 'scripts' / 'shaderlist.txt').write_text('clipper\n', encoding='utf-8')
    (base / 'scripts' / 'clipper.shader').write_text(
        'textures/clip/caulk\n{\nqer_editorimage textures/clip/cut.png\n'
        'surfaceparm nodraw\nsurfaceparm nolightmap\n}\n', encoding='utf-8')
    for material in ('wall', 'original', 'cut'):
        png(base / 'textures' / 'clip' / f'{material}.png')
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
    for dialect in ('classic', 'valve', 'primitive'):
        source = maps / f'{dialect}-source.map'
        brushes = [box(lo, hi, dialect, 'clip/wall') for lo, hi in room]
        brushes.append(box((-64, -32, 16), (64, 32, 96), dialect, 'clip/original'))
        source.write_text('{\n"classname" "worldspawn"\n' + '\n'.join(brushes)
                          + '\n}\n{\n"classname" "info_player_deathmatch"\n"origin" "256 0 96"\n}\n'
                          + '{\n"classname" "light"\n"origin" "0 0 320"\n"light" "600"\n}\n', encoding='utf-8')
        for material in ('cut', 'caulk'):
            name = f'{dialect}-{material}'
            output = maps / f'{name}.map'
            cut = run(f'{name}-clip', ['map', 'clip', source, '--object', 'brush:6', '--axis', 'x', '--at', '0',
                                      '--cut-texture', f'clip/{material}', '--output', output, '--overwrite', '--json'])
            assert cut['clipped'] == 1 and cut['cutTexture'] == f'clip/{material}'
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

            shader_data, planes, sides, surfaces = lump(1), lump(2), lump(9), lump(13)
            shaders = [shader_data[i:i+64].split(b'\0', 1)[0].decode() for i in range(0, len(shader_data), 72)]
            shader = shaders.index(f'textures/clip/{material}')
            flags = struct.unpack_from('<i', shader_data, shader*72+64)[0]
            assert bool(flags & 0x80) == (material == 'caulk')  # Quake III SURF_NODRAW
            caps = []
            for at in range(0, len(sides), 8):
                plane, side_shader = struct.unpack_from('<2i', sides, at)
                if side_shader == shader:
                    caps.append(struct.unpack_from('<4f', planes, plane*16))
            assert caps and all(abs(p[0]-1) < 1e-5 and all(abs(v) < 1e-5 for v in p[1:]) for p in caps), (name, caps)
            rendered = sum(struct.unpack_from('<i', surfaces, at)[0] == shader for at in range(0, len(surfaces), 104))
            assert (rendered > 0) == (material == 'cut'), (name, rendered)
            run(f'{name}-dependencies', ['map', 'dependencies', output, '--package', base, '--engine', 'idTech3', '--json'])
            subset, package = root / f'{name}-assets.pk3', root / f'{name}-level.pk3'
            run(f'{name}-subset', ['package', 'subset', base, subset, '--map-input', output, '--engine', 'idTech3', '--overwrite', '--json'])
            run(f'{name}-package', ['package', 'save-as', subset, package, '--format', 'pk3', '--add-file', output.with_suffix('.bsp'),
                                  '--as', f'maps/{name}.bsp', '--add-file', output, '--as', f'maps/{name}.map', '--overwrite', '--json'])
            validation = run(f'{name}-validate', ['package', 'validate', package, '--json'])['validation']
            assert validation['valid'], validation
            with zipfile.ZipFile(package) as archive:
                assert archive.testzip() is None and archive.read(f'maps/{name}.bsp') == bsp
                assert archive.read(f'maps/{name}.map') == output.read_bytes()
                assert 'textures/clip/cut.png' in archive.namelist()
                if material == 'caulk':
                    assert b'textures/clip/caulk' in archive.read('scripts/clipper.shader')
            reports.append({'dialect': dialect, 'material': material, 'compiledCutPlanes': caps,
                            'renderedCutSurfaces': rendered, 'surfaceFlags': flags,
                            'bspSha256': hashlib.sha256(bsp).hexdigest(), 'packageValidation': validation})
    (root / 'verified.json').write_text(json.dumps({'stepsPassed': len(steps), 'variants': reports,
        'applicationSha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        'compilerSha256': hashlib.sha256(args.compiler.read_bytes()).hexdigest()}, indent=2), encoding='utf-8')
    print(f'PASS: {len(steps)} steps; visible and caulk cuts compile in three dialects; six verified PK3s.', flush=True)


if __name__ == '__main__':
    main()
