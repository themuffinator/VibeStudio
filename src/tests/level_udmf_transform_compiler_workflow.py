"""Generated UDMF transforms -> real ZDBSP -> package validation and launch plan.

Uses independent affine/winding oracles and retained source bytes. No game is
launched and no native input or capture is used.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import subprocess
from level_udmf_compiler_workflow import textmap
from level_doom_mirror_compiler_workflow import fixture, lumps, wad


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def native(props, kind):
    return [{p['key']: p['literal'] for p in block['properties']}
            for block in props['blocks'] if block['object'].startswith(kind + ':')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    root = args.output_root.resolve()
    assert root.is_relative_to(repo / '.agents' / 'tmp')
    root.mkdir(parents=True, exist_ok=True)
    binary, compiler = args.binary.resolve(), args.compiler.resolve()
    binary_hash = digest(binary)
    steps, variants = [], []

    def run(label, words, success=True):
        command = [str(binary), '--cli', '--json', '--settings-file', str(root/'settings.ini')] + list(map(str, words))
        process = subprocess.run(command, cwd=repo, capture_output=True, timeout=120)
        (root/f'{label}.json').write_bytes(process.stdout)
        (root/f'{label}.stderr.txt').write_bytes(process.stderr)
        steps.append({'step': label, 'command': command, 'exitCode': process.returncode})
        (root/'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert (process.returncode == 0) == success, (label, process.returncode, process.stdout, process.stderr)
        print(label, 'passed', flush=True)
        return json.loads(process.stdout)

    def inspect(label, path):
        return run(label, ['map', 'inspect-udmf', path, '--map', 'MAP01'])['udmf']

    def check_coordinates(props, expected, endpoints, reordered=False):
        vertices = native(props, 'vertex')
        assert len(vertices) == len(expected)
        identities = []
        for index, vertex in enumerate(vertices):
            matches = [i for i, (x, y) in enumerate(expected)
                       if abs(float(vertex['x'])-x) < 1e-8 and abs(float(vertex['y'])-y) < 1e-8]
            assert len(matches) == 1 and (reordered or matches[0] == index), (index, vertex, matches)
            identities.append(matches[0])
            x, y = expected[matches[0]]
            assert abs(float(vertex['x']) - x) < 1e-8 and abs(float(vertex['y']) - y) < 1e-8, (vertex, x, y)
            assert vertex['user_float'] == '0.125'
        assert len(set(identities)) == len(expected)
        lines = native(props, 'linedef')
        sides = native(props, 'sidedef')
        assert len(lines) == len(endpoints)
        assert {int(line['user_edge']) for line in lines} == set(range(len(endpoints)))
        for line in lines:
            start, end = endpoints[int(line['user_edge'])]
            assert identities[int(line['v1'])] == start and identities[int(line['v2'])] == end
            side = sides[int(line['sidefront'])]
            assert int(side['sector']) == 0 and side['texturemiddle'] == '"STONE"'
        # A front-sided clockwise boundary must remain clockwise after mirroring.
        signed_area = sum(expected[a][0] * expected[b][1] - expected[a][1] * expected[b][0] for a, b in endpoints) / 2
        assert signed_area < 0
        return -signed_area

    run('register', ['compiler', 'set-path', 'zdbsp', '--executable', compiler])
    run('install', ['install', 'add', root, '--install-game', 'doom', '--install-engine', 'idtech1',
                    '--install-name', 'Generated UDMF transform proof', '--install-executable', binary])
    rest = lumps(fixture(False))
    rest = rest[next(i for i, item in enumerate(rest) if item[0] == 'MAP02'):]
    rest += [('USERDATA', b'first'), ('USERDATA', b'second')]
    for namespace in ('doom', 'zdoom'):
        source = root/f'{namespace}.wad'
        raw = textmap(namespace)
        for i in range(4):
            raw = raw.replace(f'sidefront = {i};'.encode(), f'sidefront = {i}; user_edge = {i};'.encode())
        source.write_bytes(wad([('MAP01', b''), ('TEXTMAP', raw), ('PORTDATA', b'\0opaque-sidecar'), ('ENDMAP', b'')] + rest))
        original_hash = digest(source)
        current = source
        expected = [(0.25, 0.25), (0.25, 256.25), (256.25, 256.25), (256.25, 0.25)]
        endpoints = [(i, (i+1) % 4) for i in range(4)]

        def transform(label, operation, options, dry=False):
            nonlocal current
            output = root/f'{namespace}-{label}.wad'
            command = ['map', operation, current, '--map', 'MAP01', *options, '--output', output]
            if dry:
                run(namespace+'-'+label+'-dry-run', command+['--dry-run'])
                assert not output.exists()
            old = lumps(current.read_bytes())
            run(namespace+'-'+label, command)
            new = lumps(output.read_bytes())
            assert old[:1]+old[2:] == new[:1]+new[2:], 'Every unrelated WAD record must retain identity and bytes'
            current = output
            return output

        refused = root/f'{namespace}-refused.wad'
        run(namespace+'-partial-mirror-refused', ['map', 'flip', current, '--map', 'MAP01', '--object', 'linedef:0',
                                                '--axis', 'x', '--output', refused], False)
        assert not refused.exists()
        transform('move', 'move', ['--object', 'sector:0', '--delta', '.375,-.125,0'], True)
        expected = [(x+.375, y-.125) for x, y in expected]
        check_coordinates(inspect(namespace+'-move-check', current), expected, endpoints)
        transform('thing', 'move', ['--object', 'thing:0', '--delta', '.375,-.125,.125'])
        transform('rotate', 'rotate', ['--object', 'sector:0', '--object', 'thing:0', '--axis', 'z', '--degrees', '22.5', '--pivot', '0,0,0'])
        c, s = math.cos(math.radians(22.5)), math.sin(math.radians(22.5))
        expected = [(x*c-y*s, x*s+y*c) for x, y in expected]
        check_coordinates(inspect(namespace+'-rotate-check', current), expected, endpoints)
        low = [min(v[i] for v in expected) for i in range(2)]
        high = [max(v[i] for v in expected) for i in range(2)]
        transform('resize', 'resize', ['--object', 'sector:0', '--object', 'thing:0', '--mins', '-2.125,4.375,0',
                                      '--maxs', '126.25,388.5,.125'])
        expected = [(-2.125+(x-low[0])*128.375/(high[0]-low[0]), 4.375+(y-low[1])*384.125/(high[1]-low[1])) for x, y in expected]
        check_coordinates(inspect(namespace+'-resize-check', current), expected, endpoints)
        transform('mirror', 'flip', ['--object', 'linedef:0', '--object', 'thing:0', '--connected', '--axis', 'x'])
        centre_twice = min(x for x, _ in expected) + max(x for x, _ in expected)
        expected = [(centre_twice-x, y) for x, y in expected]
        endpoints = [(b, a) for a, b in endpoints]
        check_coordinates(inspect(namespace+'-mirror-check', current), expected, endpoints)
        transform('snap', 'snap', ['--object', 'sector:0', '--object', 'thing:0', '--grid', '.125'])
        expected = [tuple(math.copysign(math.floor(abs(v)*8+.5)/8, v) for v in point) for point in expected]
        final_props = inspect(namespace+'-snap-check', current)
        area = check_coordinates(final_props, expected, endpoints)
        thing = native(final_props, 'thing')[0]
        assert float(thing['height']) == .125 and int(thing['angle']) == 67
        assert thing['user_note'] == '"retained"'
        existing_hash = digest(current)
        run(namespace+'-overwrite-refused', ['map', 'move', source, '--object', 'vertex:0', '--delta', '1,0,0', '--output', current], False)
        assert digest(current) == existing_hash and digest(source) == original_hash
        run(namespace+'-unbuilt-launch-refused', ['launch', 'plan', '--map', 'MAP01', '--bsp', current], False)
        for variant, flags in [('extended', '-X'), ('compressed', '-Z')]:
            label = namespace+'-'+variant
            output = root/f'{label}.wad'
            run(label+'-build', ['compiler', 'run', 'zdbsp-nodes', '--input', current, '--output', output,
                                '--extra-args', '-m MAP01 '+flags, '--working-directory', root,
                                '--manifest', root/f'{label}.manifest.json'])
            report = run(label+'-inspect', ['map', 'inspect', output, '--map', 'MAP01'])['map']['nodeBuild']
            assert report['state'] == 'present' and report['format'].startswith('XGL' if variant == 'extended' else 'ZGL'), report
            # ZDBSP legitimately reorders native vertices. Resolve identities by
            # coordinates, then verify directed edges and sidedef ownership.
            compiled_props = inspect(label+'-geometry', output)
            check_coordinates(compiled_props, expected, endpoints, reordered=True)
            compiled_thing = native(compiled_props, 'thing')[0]
            assert compiled_thing['user_note'] == '"retained"' and int(compiled_thing['angle']) == 67
            assert float(compiled_thing['height']) == .125
            assert any(p['key'] == 'user_global' and p['literal'] == '"retained"' for p in compiled_props['globals'])
            assert digest(current) == existing_hash, 'The node builder must not rewrite authoring input'
            built = lumps(output.read_bytes())
            assert next(data for name, data in built if name == 'PORTDATA') == b'\0opaque-sidecar'
            assert built[next(i for i, item in enumerate(built) if item[0] == 'MAP02'):] == rest
            run(label+'-package', ['package', 'validate', output])
            plan = run(label+'-launch-plan', ['launch', 'plan', '--map', 'MAP01', '--bsp', output])['plan']
            assert plan['runnable'] and plan['validatedArtifactSha256'] == digest(output)
            variants.append({'namespace': namespace, 'variant': variant, 'floorArea': area, **report})
    assert digest(binary) == binary_hash, 'The application changed during validation'
    summary = {'steps': len(steps), 'variants': variants, 'binarySha256': binary_hash, 'compilerSha256': digest(compiler)}
    (root/'verified.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print('UDMF transform/compiler/package/launch-plan proof passed', flush=True)


if __name__ == '__main__':
    main()
