"""Original volume-selection CLI, compiler and package acceptance fixtures.

Generated geometry/textures only. No game launch, OS input or screen capture.
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
    parser.add_argument('--compiler', type=Path)
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
        steps.append({'step': label, 'exitCode': result.returncode, 'expectedSuccess': success, 'command': command})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert (result.returncode == 0) == success, (label, result.stdout[-1800:], result.stderr[-800:])
        print(label, 'OK', flush=True)
        return json.loads(result.stdout) if cli else result.stdout.decode(errors='replace')

    room = [((-528, -528, -16), (528, 528, 0)), ((-528, -528, 384), (528, 528, 400)),
            ((-528, -528, 0), (-512, 528, 384)), ((512, -528, 0), (528, 528, 384)),
            ((-512, -528, 0), (512, -512, 384)), ((-512, 512, 0), (512, 528, 384))]
    png(base / 'textures' / 'region' / 'wall.png')
    for kind in ('classic', 'valve', 'primitive'):
        for face in range(6):
            png(base / 'textures' / 'region' / kind / f'face{face}.png')
        world = '{\n"classname" "worldspawn"\n'
        walls = '\n'.join(box(lo, hi, kind, 'region/wall') for lo, hi in room)
        tail = ('\n}\n{\n"classname" "info_player_deathmatch"\n"origin" "0 128 48"\n}\n'
                + '{\n"classname" "light"\n"origin" "0 0 320"\n"light" "600"\n}\n'
                + '{\n"classname" "func_door"\n"targetname" "gate"\n"speed" "123"\n'
                + box((-32, -16, 0), (32, 16, 96), kind, f'region/{kind}', True) + '\n}\n')
        source = maps / f'{kind}-source.map'
        source.write_text(world + box((-100, -100, -8), (100, 100, 144), kind, 'region/volume') + '\n' + walls + tail, encoding='utf-8')
        original = source.read_bytes()
        expected = maps / f'{kind}-expected.map'
        expected.write_text(world + walls + tail, encoding='utf-8')
        outputs = []
        for policy in ('trenchbroom', 'radiant'):
            prefix = ['map', 'select-region', source, '--object', 'brush:0', '--policy', policy]
            query = run(f'{kind}-{policy}-query', prefix)
            assert query['queryOnly'] and not query['mapWritten'] and query['objects'] == ['brush:7'] and query['removedObjects'] == 1
            output = maps / f'{kind}-{policy}.map'
            before = output.read_bytes() if output.exists() else None
            dry = run(f'{kind}-{policy}-dry', prefix + ['--output', output, '--dry-run', '--overwrite'])
            assert dry['dryRun'] and not dry['mapWritten'] and (output.read_bytes() if output.exists() else None) == before
            saved = run(f'{kind}-{policy}-save', prefix + ['--output', output, '--overwrite'])
            assert saved['mapWritten'] and source.read_bytes() == original
            run(f'{kind}-{policy}-reopen', ['map', 'inspect', output])
            outputs.append((policy, output))
        legacy = run(f'{kind}-legacy', ['map', 'select-region', source, '--object', 'brush:0'])
        assert legacy['policy'] == 'bounds' and legacy['removedObjects'] == 0 and source.read_bytes() == original
        touching = run(f'{kind}-touching', ['map', 'select-region', source, '--object', 'brush:0', '--policy', 'radiant', '--mode', 'touching'])
        assert touching['removedObjects'] == 0 and 'brush:0' in touching['selection'] and 'brush:7' in touching['selection']
        if kind == 'classic':
            prefix = ['map', 'select-region', source, '--object', 'brush:0']
            for index, flags in enumerate([
                    ['--axis', 'w'], ['--axis'], ['--axis', 'x', '--axis', 'y'], ['--mode', 'miss'],
                    ['--policy', 'miss'], ['--output'], ['--dry-run'], ['--overwrite'], ['--dry-run=true'],
                    ['--typo', '1'], ['extra'], ['--object', 'brush:999'], ['--definitions', root / 'missing.def']]):
                run(f'invalid-{index}', prefix + flags, success=False)
            run('missing-selector', ['map', 'select-region', source], success=False)
            run('output-directory', prefix + ['--policy', 'trenchbroom', '--output', maps, '--overwrite'], success=False)
            run('existing-output', prefix + ['--output', outputs[0][1]], success=False)
            in_place = maps / 'in-place.map'
            in_place.write_bytes(original)
            saved = run('in-place', ['map', 'select-region', in_place, '--object', 'brush:0', '--policy', 'trenchbroom', '--output', in_place, '--overwrite'])
            assert saved['mapWritten'] and Path(saved['backup']).read_bytes() == original and in_place.read_bytes() != original
            custom = maps / 'custom.map'
            custom.write_text(world + box((-16, -16, -16), (16, 16, 16), kind, 'region/wall')
                              + '\n}\n{\n"classname" "custom_test"\n"origin" "12 0 0"\n}\n', encoding='utf-8')
            definitions = root / 'custom.fgd'
            definitions.write_text('@PointClass size(-2 -2 -2, 2 2 2) = custom_test : "Custom test" []\n', encoding='utf-8')
            prefix = ['map', 'select-region', custom, '--object', 'brush:0', '--policy', 'trenchbroom']
            assert run('fallback-definition', prefix)['objects'] == []
            assert run('custom-definition', prefix + ['--definitions', definitions])['objects'] == ['entity:1']
            grouped = maps / 'grouped.map'
            group = run('group-create', ['editor', 'scene', 'create', source, '--kind', 'group', '--name', 'Volume and door',
                                         '--output', grouped, '--overwrite'])['createdId']
            run('group-assign', ['editor', 'scene', 'assign', grouped, '--id', group, '--objects', 'brush:0,entity:3',
                                  '--output', grouped, '--overwrite'])
            prefix = ['map', 'select-region', grouped, '--object', 'brush:0', '--policy', 'trenchbroom']
            run('closed-source-group', prefix, success=False)
            assert run('open-source-group', prefix + ['--context', group])['objects'] == ['brush:7']
            run('missing-source-context', prefix + ['--context', 'not-a-group'], success=False)
        if not args.compiler:
            continue
        compiler_prefix = [args.compiler.resolve(), '-game', 'quake3', '-fs_basepath', base.parent, '-fs_homepath', root / 'home', '-threads', '2']
        for label, path in [('expected', expected)] + outputs:
            for stage, options in [('bsp', ['-meta']), ('vis', ['-vis', '-fast']), ('light', ['-light', '-fast'])]:
                log = run(f'{kind}-{label}-{stage}', compiler_prefix + options + [path], cli=False)
                assert 'LEAKED' not in log and 'ERROR:' not in log
        want, want_entities, want_models = compiled_geometry(expected.with_suffix('.bsp').read_bytes())
        for policy, output in outputs:
            actual, entities, models = compiled_geometry(output.with_suffix('.bsp').read_bytes())
            assert actual == want and entities == want_entities and models == want_models == 2
            door = next(entity for entity in entities if entity.get('classname') == 'func_door')
            assert door['speed'] == '123' and door['targetname'] == 'gate'
            variants.append({'dialect': kind, 'policy': policy, 'identicalSurfaces': sum(actual.values()), 'models': models})
        output = outputs[0][1]
        subset, package = root / f'{kind}-assets.pk3', root / f'{kind}-level.pk3'
        run(f'{kind}-dependencies', ['map', 'dependencies', output, '--package', base, '--engine', 'idTech3'])
        run(f'{kind}-subset', ['package', 'subset', base, subset, '--map-input', output, '--engine', 'idTech3', '--overwrite'])
        run(f'{kind}-package', ['package', 'save-as', subset, package, '--format', 'pk3', '--overwrite',
                              '--add-file', output, '--as', f'maps/{kind}.map', '--add-file', output.with_suffix('.bsp'), '--as', f'maps/{kind}.bsp'])
        validation = run(f'{kind}-validate', ['package', 'validate', package])['validation']
        assert validation['valid'] and validation['verifiedCount'] == 9
        with zipfile.ZipFile(package) as archive:
            assert archive.testzip() is None and archive.read(f'maps/{kind}.map') == output.read_bytes()
            assert archive.read(f'maps/{kind}.bsp') == output.with_suffix('.bsp').read_bytes()
    report = {'stepsPassed': len(steps), 'variants': variants, 'applicationSha256': hashlib.sha256(args.binary.read_bytes()).hexdigest()}
    if args.compiler:
        report['compilerSha256'] = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    (root / 'verified.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'PASS: {len(steps)} region CLI/compiler/package steps.')


if __name__ == '__main__':
    main()
