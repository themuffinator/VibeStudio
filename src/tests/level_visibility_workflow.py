"""Session visibility CLI acceptance on original map fixtures; no game launch."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
from level_compiler_test_helpers import box


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    root = args.output_root.resolve()
    assert root.is_relative_to(repo / '.agents' / 'tmp')
    root.mkdir(parents=True, exist_ok=True)
    steps = []

    def run(label, words, code=0):
        command = [str(args.binary.resolve()), '--cli', '--json', '--settings-file', str(root / 'settings.ini'), *map(str, words)]
        result = subprocess.run(command, cwd=repo, capture_output=True, timeout=60)
        (root / f'{label}.stdout.json').write_bytes(result.stdout)
        (root / f'{label}.stderr.txt').write_bytes(result.stderr)
        steps.append({'step': label, 'expectedCode': code, 'exitCode': result.returncode, 'command': command})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert result.returncode == code, (label, result.stdout[-1500:], result.stderr[-500:])
        print(label, 'OK', flush=True)
        return json.loads(result.stdout)

    for dialect in ('classic', 'valve', 'primitive'):
        source = root / f'{dialect}.map'
        source.write_text('{\n"classname" "worldspawn"\n' + box((0, 0, 0), (16, 16, 16), dialect, 'visibility/test') + '\n'
                          + box((32, 0, 0), (48, 16, 16), dialect, 'visibility/test') + '\n}\n{\n"classname" "func_door"\n'
                          + box((64, 0, 0), (80, 16, 16), dialect, 'visibility/test') + '\n'
                          + box((96, 0, 0), (112, 16, 16), dialect, 'visibility/test') + '\n'
                          + '}\n{\n"classname" "light"\n"origin" "0 0 96"\n}\n', encoding='utf-8')
        original = source.read_bytes()

        def query(label, options, hidden, selected=None):
            result = run(f'{dialect}-{label}', ['map', 'visibility', source, *options])
            assert set(result['hidden']) == set(hidden), (label, result)
            if selected is not None:
                assert set(result['selection']) == set(selected), (label, result)
            assert result['queryOnly'] and not result['mapWritten']
            assert result['sourceSha256'] == hashlib.sha256(original).hexdigest() and source.read_bytes() == original
            return result

        query('hide', ['--mode', 'hide', '--object', 'brush:0'], ['brush:0'], [])
        query('multiple', ['--mode=hide', '--object', 'brush:0', '--object', 'brush:2', '--object', 'brush:0'], ['brush:0', 'brush:2'], [])
        result = query('owner', ['--mode', 'hide', '--object', 'entity:1'], ['entity:1'], [])
        assert set(result['effectiveHidden']) == {'entity:1', 'brush:2', 'brush:3'}
        result = query('label', ['--mode', 'hide', '--object', 'brush:2', '--object', 'brush:3'], ['brush:2', 'brush:3'], [])
        assert 'entity:1' in result['effectiveHidden']
        query('isolate-child', ['--mode', 'isolate', '--object', 'brush:2', '--hidden', 'entity:1'], ['brush:0', 'brush:1', 'brush:3', 'entity:2'], ['brush:2'])
        query('isolate-owner', ['--mode', 'isolate', '--object', 'entity:1'], ['brush:0', 'brush:1', 'entity:2'], ['entity:1'])
        query('show-all', ['--mode', 'show-all', '--hidden', 'brush:0', '--hidden', 'entity:1'], [], [])
        assert not query('show-noop', ['--mode', 'show-all'], [], [])['changed']
        for number, options in enumerate([
            [], ['--mode', 'wrong'], ['--mode', 'hide'], ['--mode', 'show-all', '--object', 'brush:0'],
            ['--mode', 'hide', '--object', 'brush:999'], ['--mode', 'hide', '--object', 'entity:0'],
            ['--mode', 'show-all', '--hidden', 'invalid'], ['--mode', 'show-all', '--output', root / 'forbidden.map'],
            ['--mode', 'show-all', '--mode', 'show-all'], ['--mode', 'show-all', '--dry-run'],
            ['--mode', 'show-all', '--hidden'], ['--mode', 'show-all', '--unknown'],
        ]):
            result = run(f'{dialect}-invalid-{number}', ['map', 'visibility', source, *options], 2)
            assert not result['mapWritten'] and source.read_bytes() == original
        # Visibility selectors feed the shared selection query without modifying
        # the source, then the selected visible object feeds ordinary authoring.
        result = query('handoff', ['--mode', 'isolate', '--object', 'brush:0'], ['brush:1', 'brush:2', 'brush:3', 'entity:1', 'entity:2'], ['brush:0'])
        excludes = [word for selector in result['hidden'] for word in ('--exclude', selector)]
        selected = run(f'{dialect}-visible-query', ['map', 'select-siblings', source, '--object', 'brush:0', *excludes])['selection']
        assert selected == ['brush:0']
        run(f'{dialect}-move', ['map', 'move', source, '--object', selected[0], '--delta', '16,0,0', '--output', root / f'{dialect}-moved.map', '--overwrite'])
        assert source.read_bytes() == original

    grouped = root / 'grouped.map'
    group = run('group-create', ['editor', 'scene', 'create', source, '--kind', 'group', '--name', 'Pair', '--output', grouped, '--overwrite'])['createdId']
    run('group-assign', ['editor', 'scene', 'assign', grouped, '--id', group, '--objects', 'brush:0,brush:1', '--output', grouped, '--overwrite'])
    assert set(run('group-hide', ['map', 'visibility', grouped, '--mode', 'hide', '--object', 'brush:0'])['hidden']) == {'brush:0', 'brush:1'}
    assert run('group-open', ['map', 'visibility', grouped, '--mode', 'hide', '--object', 'brush:0', '--context', group])['hidden'] == ['brush:0']
    run('group-outside', ['map', 'visibility', grouped, '--mode', 'hide', '--object', 'brush:2', '--context', group], 4)

    original_grouped = grouped.read_bytes()
    state = f'group:{group}'
    result = run('group-inherited-hide', ['map', 'visibility', grouped, '--policy', 'inherited', '--mode', 'hide', '--object', 'brush:0'])
    assert result['hidden'] == [state] and set(result['effectiveHidden']) == {'brush:0', 'brush:1'} and result['shown'] == []
    result = run('group-shown-child', ['map', 'visibility', grouped, '--mode', 'hide', '--object', 'brush:2', '--hidden', state, '--shown', 'brush:0'])
    assert 'brush:0' not in result['effectiveHidden'] and 'brush:1' in result['effectiveHidden'] and result['shown'] == ['brush:0']
    result = run('group-rehide-shown', ['map', 'visibility', grouped, '--policy', 'inherited', '--mode', 'hide', '--object', 'brush:0', '--hidden', state, '--shown', 'brush:0'])
    assert result['shown'] == [] and set(result['effectiveHidden']) == {'brush:0', 'brush:1'}
    result = run('group-isolate-inherited', ['map', 'visibility', grouped, '--policy', 'inherited', '--mode', 'isolate', '--object', 'brush:0'])
    assert {state, 'brush:0', 'brush:1'} <= set(result['shown']) and 'brush:0' not in result['effectiveHidden']
    result = run('group-reset-shown', ['map', 'visibility', grouped, '--mode', 'show-all', '--hidden', state, '--shown', 'brush:0'])
    assert result['hidden'] == result['shown'] == [] and result['changed']
    result = run('group-reset-shown-only', ['map', 'visibility', grouped, '--mode', 'show-all', '--shown', state])
    assert result['hidden'] == result['shown'] == [] and result['changed']
    for label, extra in [('bad-policy', ['--policy', 'wrong']), ('missing-group', ['--hidden', 'group:missing']),
                         ('conflict', ['--hidden', state, '--shown', state]), ('world-shown', ['--shown', 'entity:0']),
                         ('policy-repeat', ['--policy', 'flat', '--policy', 'inherited'])]:
        run(label, ['map', 'visibility', grouped, '--mode', 'show-all', *extra], 2)
    assert grouped.read_bytes() == original_grouped

    layer = run('transfer-layer', ['editor', 'scene', 'create', grouped, '--kind', 'layer', '--name', 'Destination', '--output', grouped, '--overwrite'])['createdId']
    original_grouped = grouped.read_bytes()
    result = run('transfer-inspect', ['map', 'visibility', grouped, '--mode', 'inspect', '--hidden', state, '--shown', 'brush:0'])
    assert result['shown'] == ['brush:0'] and 'brush:0' not in result['effectiveHidden'] and not result['changed']
    result = run('transfer-move', ['map', 'visibility', grouped, '--mode', 'move', '--node', group, '--parent', layer, '--hidden', state, '--shown', 'brush:0'])
    assert result['shown'] == [] and {'brush:0', 'brush:1'} <= set(result['effectiveHidden']) and result['changed']
    result = run('transfer-same-layer', ['map', 'visibility', grouped, '--mode', 'move', '--node', group, '--parent', 'default', '--hidden', state, '--shown', 'brush:0'])
    assert result['shown'] == ['brush:0'] and not result['changed']
    result = run('transfer-assign', ['map', 'visibility', grouped, '--mode', 'assign', '--object', 'brush:0', '--parent', layer, '--shown', 'brush:0'])
    assert result['shown'] == [] and result['changed']
    result = run('transfer-duplicate', ['map', 'visibility', grouped, '--mode', 'duplicate', '--object', 'brush:0', '--object', 'brush:1', '--offset', '128,0,0', '--shown', state, '--hidden', 'brush:1'])
    new_groups = [node for node in result['scene']['nodes'] if node['kind'] == 'group' and node['id'] != group]
    assert len(new_groups) == 1 and f"group:{new_groups[0]['id']}" in result['shown']
    assert len(set(new_groups[0]['objects']) & set(result['hidden'])) == 1
    assert grouped.read_bytes() == original_grouped and not result['mapWritten']
    for profile, fresh in [('trenchbroom', False), ('netradiant-custom', True), ('vibestudio-default', False)]:
        result = run(f'{profile}-clone-policy', ['map', 'visibility', grouped, '--profile', profile, '--mode', 'duplicate',
                     '--object', 'brush:0', '--object', 'brush:1', '--offset', '-128,0,0', '--hidden', 'brush:1', '--shown', state])
        assert result['profile'] == profile and result['insertionVisibility'] == ('fresh-inherited' if fresh else 'preserve-and-reveal')
        assert len(result['hidden']) == (1 if fresh else 2) and len(result['shown']) == (1 if fresh else 2)
        assert result['policy'] == ('inherited' if profile == 'trenchbroom' else 'flat')
        assert grouped.read_bytes() == original_grouped and not result['mapWritten']
        result = run(f'{profile}-hidden-destination', ['map', 'visibility', grouped, '--profile', profile, '--mode', 'duplicate',
                     '--context', group, '--object', 'brush:0', '--hidden', state, '--shown', 'brush:0'])
        assert len(result['selection']) == 1 and (result['selection'][0] in result['effectiveHidden']) == fresh
        assert 'brush:0' not in result['effectiveHidden'] and grouped.read_bytes() == original_grouped
    result = run('profile-policy-override', ['map', 'visibility', grouped, '--profile', 'netradiant-custom', '--policy', 'inherited',
                 '--mode', 'hide', '--object', 'brush:0'])
    assert result['hidden'] == [state] and result['insertionVisibility'] == 'fresh-inherited'
    for label, extra, code in [
        ('move-missing-node', ['--mode', 'move'], 2), ('move-bad-node', ['--mode', 'move', '--node', 'missing'], 4),
        ('offset-on-inspect', ['--mode', 'inspect', '--offset', '1,2,3'], 2),
        ('duplicate-bad-offset', ['--mode', 'duplicate', '--object', 'brush:0', '--offset', 'nan,1,2'], 2),
        ('duplicate-short-offset', ['--mode', 'duplicate', '--object', 'brush:0', '--offset', '1,2'], 2),
        ('assign-bad-parent', ['--mode', 'assign', '--object', 'brush:0', '--parent', 'missing'], 4),
        ('missing-profile', ['--mode', 'inspect', '--profile'], 2),
        ('invalid-profile', ['--mode', 'inspect', '--profile', 'invalid'], 2),
        ('repeated-profile', ['--mode', 'inspect', '--profile', 'trenchbroom', '--profile', 'netradiant-custom'], 2),
    ]:
        run(label, ['map', 'visibility', grouped, *extra], code)
        assert grouped.read_bytes() == original_grouped

    for game in ('doom', 'hexen', 'quake', 'quake2', 'quake3'):
        doom = game in ('doom', 'hexen')
        source = root / (game + ('.wad' if doom else '.map'))
        map_name = ['--map', 'MAP02'] if doom else []
        run(f'{game}-new', ['map', 'new', '--game', game, '--preset', 'room', *map_name, '--output', source, '--overwrite'])
        original = source.read_bytes()
        selector = 'linedef:0' if doom else 'brush:0'
        result = run(f'{game}-hide', ['map', 'visibility', source, *map_name, '--mode', 'hide', '--object', selector])
        assert result['hidden'] == [selector] and result['selection'] == []
        result = run(f'{game}-isolate', ['map', 'visibility', source, *map_name, '--mode', 'isolate', '--object', selector])
        assert selector not in result['effectiveHidden'] and result['selection'] == [selector]
        result = run(f'{game}-show', ['map', 'visibility', source, *map_name, '--mode', 'show-all', '--hidden', selector])
        assert result['hidden'] == [] and not result['mapWritten'] and source.read_bytes() == original
        if doom:
            for kind in ('vertex', 'sector', 'thing'):
                selected = f'{kind}:0'
                result = run(f'{game}-isolate-{kind}', ['map', 'visibility', source, *map_name, '--mode', 'isolate', '--object', selected])
                assert selected not in result['effectiveHidden'] and result['selection'] == [selected]
                assert not result['mapWritten'] and source.read_bytes() == original
            run(f'{game}-missing-map', ['map', 'visibility', source, '--map', 'MAP99', '--mode', 'show-all'], 4)
    report = {'stepsPassed': len(steps), 'dialects': ['classic', 'valve220', 'brushDef'], 'sourceFilesUnchangedByQueries': True,
              'nativeGames': ['doom', 'hexen', 'quake', 'quake2', 'quake3'],
              'visibilityToAuthoringHandoffs': 3, 'applicationSha256': hashlib.sha256(args.binary.read_bytes()).hexdigest()}
    (root / 'verified.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'PASS: {len(steps)} visibility CLI steps.')


if __name__ == '__main__':
    main()
