"""Original object copy/cut -> paste -> VibeMap3 -> PK3 acceptance fixtures.

Generated geometry and textures only; no game launch, OS input or screen capture.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import zipfile

from level_compiler_test_helpers import box, png
from level_linked_extraction_workflow import compiled_geometry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--compiler', required=True, type=Path)
    parser.add_argument('--output-root', required=True, type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    root = args.output_root.resolve()
    assert root.is_relative_to(repo / '.agents' / 'tmp')
    base = root / 'game' / 'baseq3'
    maps = base / 'maps'
    maps.mkdir(parents=True, exist_ok=True)
    (root / 'home').mkdir(exist_ok=True)
    steps, variants = [], []

    def run(label, words, cli=True, success=True):
        command = ([str(args.binary.resolve()), '--cli', '--json', '--settings-file', str(root / 'settings.ini')]
                   + list(map(str, words)) if cli else list(map(str, words)))
        result = subprocess.run(command, cwd=repo, capture_output=True, timeout=120)
        (root / f'{label}.stdout.txt').write_bytes(result.stdout)
        (root / f'{label}.stderr.txt').write_bytes(result.stderr)
        steps.append({'step': label, 'command': command, 'exitCode': result.returncode, 'expectedSuccess': success})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert (result.returncode == 0) == success, (label, result.stdout[-1600:], result.stderr[-800:])
        print(label, 'OK', flush=True)
        return json.loads(result.stdout) if cli else result.stdout.decode(errors='replace')

    room = [((-528, -528, -16), (528, 528, 0)), ((-528, -528, 384), (528, 528, 400)),
            ((-528, -528, 0), (-512, 528, 384)), ((512, -528, 0), (528, 528, 384)),
            ((-512, -528, 0), (512, -512, 384)), ((-512, 512, 0), (512, 528, 384))]
    png(base / 'textures' / 'clipboard' / 'wall.png')
    for kind in ('classic', 'valve', 'primitive'):
        for face in range(6):
            png(base / 'textures' / 'clipboard' / kind / f'face{face}.png')
        world = ('{\n"classname" "worldspawn"\n'
                 + '\n'.join(box(lo, hi, kind, 'clipboard/wall') for lo, hi in room)
                 + '\n}\n{\n"classname" "info_player_deathmatch"\n"origin" "0 128 48"\n}\n'
                 + '{\n"classname" "light"\n"origin" "0 0 320"\n"light" "600"\n}\n')
        properties = '{\n"classname" "func_door"\n"targetname" "gate"\n"speed" "123"\n'
        first = box((-32, -16, 0), (32, 16, 96), kind, f'clipboard/{kind}', True)
        sibling = box((224, -16, 0), (288, 16, 96), kind, f'clipboard/{kind}', True)
        source = maps / f'{kind}-source.map'
        source.write_text(world + properties + first + '\n' + sibling + '\n}\n', encoding='utf-8')
        original = source.read_bytes()
        copied, cut_clip = root / f'{kind}-copy.map', root / f'{kind}-cut.map'
        remaining, pasted = maps / f'{kind}-remaining.map', maps / f'{kind}-pasted.map'
        result = run(f'{kind}-copy', ['map', 'copy', source, '--object', 'brush:6', '--output', copied, '--overwrite'])
        assert result['clipboardWritten'] and not result['mapWritten']
        result = run(f'{kind}-cut-dry', ['map', 'cut', source, '--object', 'brush:6', '--clipboard', cut_clip,
                                       '--output', remaining, '--dry-run', '--overwrite'])
        assert not result['clipboardWritten'] and not result['mapWritten']
        result = run(f'{kind}-cut', ['map', 'cut', source, '--object', 'brush:6', '--clipboard', cut_clip,
                                   '--output', remaining, '--overwrite'])
        assert result['clipboardWritten'] and result['mapWritten']
        assert cut_clip.read_bytes() == copied.read_bytes() and source.read_bytes() == original
        fragment = cut_clip.read_text(encoding='utf-8')
        assert fragment.count('"classname" "func_door"') == 1 and '"speed" "123"' in fragment
        run(f'{kind}-paste', ['map', 'paste', remaining, '--from', cut_clip, '--delta', '0,0,0', '--output', pasted, '--overwrite'])
        run(f'{kind}-reopen', ['map', 'inspect', pasted])
        # Independently written expected text: selected child in a fresh owner,
        # sibling in the original owner. No clipboard/parser code creates it.
        expected = maps / f'{kind}-expected.map'
        expected.write_text(world + properties + sibling + '\n}\n' + properties + first + '\n}\n', encoding='utf-8')
        prefix = [args.compiler.resolve(), '-game', 'quake3', '-fs_basepath', base.parent, '-fs_homepath', root / 'home', '-threads', '2']
        for label, path in [('expected', expected), ('pasted', pasted)]:
            for stage, options in [('bsp', ['-meta']), ('vis', ['-vis', '-fast']), ('light', ['-light', '-fast'])]:
                output = run(f'{kind}-{label}-{stage}', prefix + options + [path], cli=False)
                assert 'LEAKED' not in output and 'ERROR:' not in output
        want, want_entities, want_models = compiled_geometry(expected.with_suffix('.bsp').read_bytes())
        actual, entities, models = compiled_geometry(pasted.with_suffix('.bsp').read_bytes())
        assert actual == want and entities == want_entities and models == want_models == 3
        doors = [entity for entity in entities if entity.get('classname') == 'func_door']
        assert len(doors) == 2 and all(entity.get('speed') == '123' and entity.get('targetname') == 'gate' for entity in doors)
        subset, package = root / f'{kind}-assets.pk3', root / f'{kind}-level.pk3'
        run(f'{kind}-dependencies', ['map', 'dependencies', pasted, '--package', base, '--engine', 'idTech3'])
        run(f'{kind}-subset', ['package', 'subset', base, subset, '--map-input', pasted, '--engine', 'idTech3', '--overwrite'])
        run(f'{kind}-package', ['package', 'save-as', subset, package, '--format', 'pk3', '--overwrite',
                              '--add-file', pasted, '--as', f'maps/{kind}.map',
                              '--add-file', pasted.with_suffix('.bsp'), '--as', f'maps/{kind}.bsp'])
        validation = run(f'{kind}-validate', ['package', 'validate', package])['validation']
        assert validation['valid'] and validation['verifiedCount'] == 9
        with zipfile.ZipFile(package) as archive:
            assert archive.testzip() is None and archive.read(f'maps/{kind}.map') == pasted.read_bytes()
            assert archive.read(f'maps/{kind}.bsp') == pasted.with_suffix('.bsp').read_bytes()
        variants.append({'dialect': kind, 'identicalSurfaces': sum(actual.values()), 'models': models, 'validation': validation,
                         'packageSha256': hashlib.sha256(package.read_bytes()).hexdigest()})
    report = {'stepsPassed': len(steps), 'variants': variants,
              'applicationSha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(),
              'compilerSha256': hashlib.sha256(args.compiler.read_bytes()).hexdigest()}
    (root / 'verified.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'PASS: {len(steps)} object clipboard CLI/compiler/package steps.')


if __name__ == '__main__':
    main()
