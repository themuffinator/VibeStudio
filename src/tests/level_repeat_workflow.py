"""Original Repeat CLI, save/reopen and optional VibeMap3/PK3 acceptance fixture.

All geometry and textures are generated locally. No game, OS input or screen
capture is used. Output must stay in the project's designated task area.
"""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import subprocess
import zipfile

from level_compiler_test_helpers import box, png
from level_linked_extraction_workflow import compiled_geometry


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
        assert (result.returncode == 0) == success, (label, result.stdout[-1200:], result.stderr[-1200:])
        print(label, 'OK', flush=True)
        return json.loads(result.stdout) if cli else result.stdout.decode(errors='replace')

    room = [((-528, -528, -16), (528, 528, 0)), ((-528, -528, 384), (528, 528, 400)),
            ((-528, -528, 0), (-512, 528, 384)), ((512, -528, 0), (528, 528, 384)),
            ((-512, -528, 0), (512, -512, 384)), ((-512, 512, 0), (512, 528, 384))]
    png(base / 'textures' / 'repeat' / 'wall.png')
    for kind in ('classic', 'valve', 'primitive'):
        for face in range(6):
            png(base / 'textures' / 'repeat' / kind / f'face{face}.png')
        source = maps / f'{kind}-source.map'
        source.write_text('{\n"classname" "worldspawn"\n'
                          + '\n'.join(box(lo, hi, kind, 'repeat/wall') for lo, hi in room)
                          + '\n}\n{\n"classname" "info_player_deathmatch"\n"origin" "0 128 48"\n}\n'
                          + '{\n"classname" "light"\n"origin" "0 0 320"\n"light" "600"\n}\n'
                          + '{\n"classname" "func_door"\n"speed" "123"\n'
                          + box((-192, -32, 0), (-160, 0, 96), kind, f'repeat/{kind}', True)
                          + '\n}\n', encoding='utf-8')
        unchanged = source.read_bytes()
        moved, rotated, direct, repeated = [maps / f'{kind}-{part}.map' for part in ('moved', 'rotated', 'direct', 'repeated')]
        recipe = {'version': 1, 'actions': []}
        operations = [('move', source, moved, ['--delta', '32,0,0']),
                      ('rotate', moved, rotated, ['--axis', 'z', '--degrees', '90', '--pivot', '0,0,0']),
                      ('duplicate', rotated, direct, ['--delta', '128,0,0'])]
        for operation, input_map, output, options in operations:
            result = run(f'{kind}-record-{operation}', ['map', operation, input_map, '--object', 'brush:6']
                         + options + ['--output', output, '--overwrite'])
            captured = result['map']['repeatRecipe']
            assert captured['version'] == 1 and len(captured['actions']) == 1
            recipe['actions'] += captured['actions']
        recipe_file = root / f'{kind}-recipe.json'
        recipe_file.write_text(json.dumps(recipe), encoding='utf-8')
        words = ['map', 'repeat', source, '--recipe', recipe_file, '--object', 'brush:6', '--output', repeated]
        # Dry run preserves an existing output and never creates a new one.
        old_output = repeated.read_bytes() if repeated.exists() else None
        run(f'{kind}-dry-run', words + ['--dry-run', '--overwrite'])
        assert (repeated.read_bytes() if repeated.exists() else None) == old_output
        result = run(f'{kind}-repeat', words + ['--overwrite'])
        assert result['map']['repeatRecipe'] == recipe
        assert source.read_bytes() == unchanged
        reopened = run(f'{kind}-reopen', ['map', 'inspect', repeated])
        assert reopened['map']['repeatRecipe']['actions'] == []
        protected_output = repeated.read_bytes()
        run(f'{kind}-no-overwrite', words, success=False)
        assert repeated.read_bytes() == protected_output
        run(f'{kind}-dry-run-existing', words + ['--dry-run', '--overwrite'])
        assert repeated.read_bytes() == protected_output
        # Compare semantic payloads independently of source line numbers and
        # equivalent floating-point serialisation spellings.
        direct_map = run(f'{kind}-direct-reopen', ['map', 'inspect', direct])['map']
        assert direct_map['statistics'] == reopened['map']['statistics']
        if kind == 'classic':
            malformed = []
            for field, value in [('version', 2), ('unexpected', True), ('actions', [])]:
                bad = copy.deepcopy(recipe); bad[field] = value; malformed.append(bad)
            for field, value in [('kind', 'execute'), ('rotationAxis', 0.5), ('matrix', [0]*12), ('unexpected', True)]:
                bad = copy.deepcopy(recipe); bad['actions'][0][field] = value; malformed.append(bad)
            bad = copy.deepcopy(recipe); bad['actions'][1]['rotationDegrees'] = 45; malformed.append(bad)
            bad = copy.deepcopy(recipe); bad['actions'][2]['transformYaw'] = True; malformed.append(bad)
            bad = copy.deepcopy(recipe); bad['actions'][0]['matrix'][0] = 1e-310; malformed.append(bad)
            for index, bad in enumerate(malformed):
                invalid = root / f'invalid-{index}.json'; invalid.write_text(json.dumps(bad), encoding='utf-8')
                run(f'invalid-{index}', ['map', 'repeat', source, '--recipe', invalid, '--object', 'brush:6',
                                       '--output', repeated, '--overwrite'], success=False)
                assert repeated.read_bytes() == protected_output
            for index, extra in enumerate([['--recipe', recipe_file], ['--dry-run=false'], ['--unknown'], ['extra-map']]):
                run(f'invalid-option-{index}', words + extra, success=False)
                assert repeated.read_bytes() == protected_output
            oversized = root / 'oversized.json'; oversized.write_bytes(b' ' * (4*1024*1024+1))
            run('oversized', ['map', 'repeat', source, '--recipe', oversized, '--object', 'brush:6', '--output', repeated,
                             '--overwrite'], success=False)
            assert repeated.read_bytes() == protected_output
            oversized.unlink()  # This fixture alone created this ordinary file.
        variant = {'dialect': kind, 'recipeActions': len(recipe['actions'])}
        if args.compiler:
            prefix = [args.compiler.resolve(), '-game', 'quake3', '-fs_basepath', root / 'game',
                      '-fs_homepath', root / 'home', '-fs_game', 'baseq3', '-threads', '2']
            for label, path in [('direct', direct), ('repeated', repeated)]:
                for stage, options in [('bsp', ['-meta']), ('vis', ['-vis', '-fast']), ('light', ['-light', '-fast'])]:
                    output = run(f'{kind}-{label}-{stage}', prefix + options + [path], cli=False)
                    assert 'LEAKED' not in output and 'ERROR:' not in output
            expected, expected_entities, expected_models = compiled_geometry(direct.with_suffix('.bsp').read_bytes())
            actual, actual_entities, actual_models = compiled_geometry(repeated.with_suffix('.bsp').read_bytes())
            assert actual == expected, (kind, 'compiled position, UV or normal mismatch')
            assert actual_entities == expected_entities and actual_models == expected_models
            subset, package = root / f'{kind}-assets.pk3', root / f'{kind}-level.pk3'
            run(f'{kind}-dependencies', ['map', 'dependencies', repeated, '--package', base, '--engine', 'idTech3'])
            run(f'{kind}-subset', ['package', 'subset', base, subset, '--map-input', repeated, '--engine', 'idTech3', '--overwrite'])
            run(f'{kind}-package', ['package', 'save-as', subset, package, '--format', 'pk3', '--overwrite',
                                   '--add-file', repeated, '--as', f'maps/{kind}.map',
                                   '--add-file', repeated.with_suffix('.bsp'), '--as', f'maps/{kind}.bsp'])
            validation = run(f'{kind}-validate', ['package', 'validate', package])['validation']
            assert validation['valid'] and validation['verifiedCount'] == 9
            with zipfile.ZipFile(package) as archive:
                assert archive.testzip() is None
                assert archive.read(f'maps/{kind}.map') == repeated.read_bytes()
                assert archive.read(f'maps/{kind}.bsp') == repeated.with_suffix('.bsp').read_bytes()
            variant.update(identicalSurfaces=sum(actual.values()), models=actual_models, validation=validation,
                           packageSha256=hashlib.sha256(package.read_bytes()).hexdigest())
        variants.append(variant)
    report = {'stepsPassed': len(steps), 'variants': variants,
              'applicationSha256': hashlib.sha256(args.binary.read_bytes()).hexdigest()}
    if args.compiler:
        report['compilerSha256'] = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    (root / 'verified.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'PASS: {len(steps)} repeat CLI/workflow steps across three brush dialects.')


if __name__ == '__main__':
    main()
