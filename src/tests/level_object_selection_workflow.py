"""Original source-line/sibling CLI acceptance fixtures. No game data or launch."""
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
        command = [str(args.binary.resolve()), '--cli', '--json', '--settings-file', str(root / 'settings.ini')]
        command += list(map(str, words))
        result = subprocess.run(command, cwd=repo, capture_output=True, timeout=60)
        (root / f'{label}.stdout.json').write_bytes(result.stdout)
        (root / f'{label}.stderr.txt').write_bytes(result.stderr)
        steps.append({'step': label, 'expectedCode': code, 'exitCode': result.returncode, 'command': command})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert result.returncode == code, (label, result.stdout[-1800:], result.stderr[-900:])
        payload = json.loads(result.stdout)
        print(label, 'OK', flush=True)
        return payload

    for dialect in ('classic', 'valve', 'primitive'):
        lines = ['// generated source selection fixture', '{', '"classname" "worldspawn"']
        spans = {}

        def primitive(name, x):
            spans[name] = len(lines) + 1
            lines.extend(box((x, 0, 0), (x + 16, 16, 16), dialect, 'selection/test').splitlines())
            spans[name + '-end'] = len(lines)

        primitive('world', 0)
        primitive('other', 32)
        lines.extend(['}', '// comment between entities'])
        spans['comment'] = len(lines)
        spans['owner'] = len(lines) + 1
        lines.extend(['{', '"classname" "func_door"', '"targetname" "gate"'])
        primitive('child', 64)
        primitive('peer', 96)
        lines.append('}')
        spans['point'] = len(lines) + 1
        lines.extend(['{', '"classname" "light"', '"origin" "0 0 96"', '}', ''])
        source = root / f'{dialect}.map'
        source.write_bytes('\r\n'.join(lines).encode())
        original = source.read_bytes()
        sha = hashlib.sha256(original).hexdigest()

        def query(label, action, options, expected):
            result = run(f'{dialect}-{label}', ['map', action, source, *options])
            assert set(result['selection']) == set(expected), (label, result)
            assert len(result['selection']) == len(set(expected))
            assert result['queryOnly'] and not result['mapWritten'] and result['sourceSha256'] == sha
            assert source.read_bytes() == original
            return result

        all_objects = ['brush:0', 'brush:1', 'brush:2', 'brush:3', 'entity:2']
        query('siblings', 'select-siblings', ['--object', 'brush:0'], all_objects)
        query('owned-siblings', 'select-siblings', ['--object', 'brush:2'], ['brush:2', 'brush:3'])
        query('owner-wrapper', 'select-siblings', ['--object=entity:1'], ['brush:2', 'brush:3'])
        query('multiple-parents', 'select-siblings', ['--object', 'brush:0', '--object', 'brush:2'], all_objects)
        query('filtered-owner', 'select-siblings', ['--object', 'brush:0', '--exclude', 'entity:1'], ['brush:0', 'brush:1', 'entity:2'])
        for name, expected in [('world', ['brush:0']), ('world-end', ['brush:0']), ('child', ['brush:2']),
                               ('owner', ['brush:2', 'brush:3']), ('point', ['entity:2']), ('comment', [])]:
            result = query(name, 'select-lines', ['--line', spans[name]], expected)
            assert result['lineBasis'] == 'loaded-source'
        query('owner-and-child', 'select-lines', ['--line', f"{spans['owner']}, {spans['child']}"], ['brush:2'])
        result = query('duplicate-positions', 'select-lines', ['--line', spans['child'], '--line', f"{spans['point']} {spans['child']}"], ['brush:2', 'entity:2'])
        assert result['lines'] == sorted({spans['child'], spans['point']})
        query('empty-result', 'select-lines', ['--line=2147483647'], [])
        arabic_digits = str(spans['child']).translate(str.maketrans('0123456789', '٠١٢٣٤٥٦٧٨٩'))
        query('localised-digits', 'select-lines', ['--line', arabic_digits], ['brush:2'])
        query('excluded-child-fallback', 'select-lines', ['--line', spans['child'], '--exclude', 'brush:2'], ['brush:3'])
        # A diagnostic line -> stable selector -> normal authoring command.
        selected = query('handoff', 'select-lines', ['--line', spans['child']], ['brush:2'])['selection'][0]
        moved = root / f'{dialect}-moved.map'
        run(f'{dialect}-move-selected', ['map', 'move', source, '--object', selected, '--delta', '0,0,16', '--output', moved, '--overwrite'])
        assert moved.read_bytes() != original and source.read_bytes() == original
        assert run(f'{dialect}-moved-owner', ['map', 'select-siblings', moved, '--object', 'brush:2'])['selection'] == ['brush:2', 'brush:3']

    for number, options in enumerate([
            [], ['--line', '0'], ['--line=-1'], ['--line=+1'], ['--line', '2147483648'], ['--line', '1;2'],
            ['--line', '1 nope'], ['--line', '1', '--object', 'brush:0'], ['--line', '1', '--output', root / 'forbidden.map'],
            ['--line', '1', '--dry-run'], ['--line', '1', '--unknown'], ['--line', '1', '--exclude', 'brush:99'],
            ['--line', '1', '--context', 'a', '--context', 'b'], ['--line', '1', '--json'], ['--line', '1', 'extra'],
    ]):
        result = run(f'invalid-lines-{number}', ['map', 'select-lines', source, *options], code=2)
        assert result['message'] and not result['mapWritten']
    for number, options in enumerate([[], ['--object', 'brush:99'], ['--object', 'brush:0', '--line', '1']]):
        run(f'invalid-siblings-{number}', ['map', 'select-siblings', source, *options], code=2)
    run('filtered-seed', ['map', 'select-siblings', source, '--object', 'brush:0', '--exclude', 'brush:0'], code=4)
    run('missing-context', ['map', 'select-lines', source, '--line', '1', '--context', 'missing'], code=4)
    assert not (root / 'forbidden.map').exists()

    grouped = root / 'grouped.map'
    group = run('group-create', ['editor', 'scene', 'create', source, '--kind', 'group', '--name', 'Door group', '--output', grouped, '--overwrite'])['createdId']
    run('group-assign', ['editor', 'scene', 'assign', grouped, '--id', group, '--objects', 'entity:1,brush:0', '--output', grouped, '--overwrite'])
    grouped_bytes = grouped.read_bytes()
    body = grouped_bytes.decode()
    child_line = next(i for i, line in enumerate(body.splitlines(), 1) if '"targetname"' in line)
    result = run('group-source', ['map', 'select-lines', grouped, '--line', child_line])
    assert set(result['selection']) == {'brush:0', 'entity:1'}
    result = run('group-context-siblings', ['map', 'select-siblings', grouped, '--object', 'brush:0', '--context', group])
    assert set(result['selection']) == {'brush:0', 'brush:2', 'brush:3'}
    result = run('group-context-lines', ['map', 'select-lines', grouped, '--line', child_line, '--context', group])
    assert set(result['selection']) == {'brush:2', 'brush:3'}
    result = run('group-filter', ['map', 'select-lines', grouped, '--line', child_line, '--exclude', 'brush:0'])
    assert result['selection'] == [] and grouped.read_bytes() == grouped_bytes
    run('group-lock', ['editor', 'scene', 'lock', grouped, '--id', group, '--locked', 'true', '--output', grouped, '--overwrite'])
    assert run('group-locked-lines', ['map', 'select-lines', grouped, '--line', child_line])['selection'] == []
    run('group-locked-seed', ['map', 'select-siblings', grouped, '--object', 'brush:0'], code=4)
    report = {'stepsPassed': len(steps), 'dialects': ['classic', 'valve220', 'brushDef'],
              'applicationSha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(),
              'sourceFilesUnchangedByQueries': True, 'selectionToAuthoringHandoffs': 3}
    (root / 'verified.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'PASS: {len(steps)} object selection CLI steps.')


if __name__ == '__main__':
    main()
