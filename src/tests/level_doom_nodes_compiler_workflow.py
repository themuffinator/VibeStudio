"""Prove saved node readiness through real ZDBSP, CLI inspection and launch plans.

Generated Doom/Hexen fixtures only. Does not launch a game or inject native input.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import zlib

from level_doom_mirror_compiler_workflow import fixture, lumps, wad


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
    steps, variants = [], []

    def run(label, words, success=True):
        command = [str(binary), '--cli', '--json', '--settings-file', str(root/'settings.ini')] + list(map(str, words))
        p = subprocess.run(command, cwd=repo, capture_output=True, timeout=120)
        (root/f'{label}.stdout.json').write_bytes(p.stdout)
        (root/f'{label}.stderr.txt').write_bytes(p.stderr)
        steps.append({'step': label, 'command': command, 'exitCode': p.returncode})
        (root/'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert (p.returncode == 0) == success, (label, p.returncode, p.stdout, p.stderr)
        print(label, 'passed', flush=True)
        return json.loads(p.stdout)

    def build(label, source, flags='', success=True):
        output = root/f'{label}.wad'
        result = run(label, ['compiler', 'run', 'zdbsp-nodes', '--input', source, '--output', output,
                            '--extra-args', '-m MAP01 -q -R -w '+flags, '--working-directory', root,
                            '--manifest', root/f'{label}.manifest.json'], success)
        return output, result

    def inspect(label, source):
        return run(label, ['map', 'inspect', source, '--map', 'MAP01'])['map']['nodeBuild']

    run('register', ['compiler', 'set-path', 'zdbsp', '--executable', compiler])
    run('install', ['install', 'add', root, '--install-game', 'doom', '--install-engine', 'idtech1',
                    '--install-name', 'Generated node fixture', '--install-executable', binary])
    for hexen in (False, True):
        name = 'hexen' if hexen else 'doom'
        source = root/f'{name}-source.wad'
        source.write_bytes(fixture(hexen))
        assert inspect(name+'-missing', source)['state'] == 'missing'
        refused = run(name+'-launch-refused', ['launch', 'plan', '--map', 'MAP01', '--bsp', source], False)
        assert not refused['plan']['runnable']
        for tag, flags, expected in [('classic', '', 'classic'), ('extended', '-X', 'XNOD'),
                                      ('compressed', '-Z', 'ZNOD'), ('gl-extended', '-g -X', 'XNOD + XGLN'),
                                      ('gl-compressed', '-g -z', 'ZNOD + ZGLN'),
                                      ('gl-separate', '-g', 'classic'),
                                      ('gl-only-extended', '-g -x -X', 'XGLN'),
                                      ('gl-only-compressed', '-g -x -z', 'ZGLN')]:
            label = f'{name}-{tag}'
            output, _ = build(label, source, flags)
            report = inspect(label+'-inspect', output)
            assert report['state'] == 'present' and not report['needsBuild'] and report['format'] == expected, report
            plan = run(label+'-launch-plan', ['launch', 'plan', '--map', 'MAP01', '--bsp', output])['plan']
            assert plan['runnable'] and plan['validatedArtifactSha256'] == hashlib.sha256(output.read_bytes()).hexdigest()
            assert plan['nodeBuild']['MAP01']['state'] == 'present'
            run(label+'-package', ['package', 'validate', output])
            records = lumps(output.read_bytes())
            original = lumps(source.read_bytes())
            i = next(i for i, (n, _) in enumerate(records) if n == 'USERDATA')
            j = next(i for i, (n, _) in enumerate(original) if n == 'USERDATA')
            assert records[i:] == original[j:], 'unrelated maps and duplicate resources retained'
            variants.append({'variant': label, **report})
        gl_edit = root/f'{name}-gl-separate-edit.wad'
        run(name+'-gl-separate-edit', ['map', 'flip', root/f'{name}-gl-separate.wad', '--map', 'MAP01', '--object', 'linedef:0',
                                      '--connected', '--axis', 'y', '--output', gl_edit])
        assert not any(n == 'GL_MAP01' for n, _ in lumps(gl_edit.read_bytes())), 'obsolete GL companion removed'
        assert inspect(name+'-gl-separate-edit-inspect', gl_edit)['state'] == 'missing'
        unbuilt, result = build(name+'-no-node-build', source, '-N', False)
        manifest = json.loads((root/f'{name}-no-node-build.manifest.json').read_text(encoding='utf-8'))
        assert manifest['exitCode'] == 0 and manifest['state'] == 'failed', manifest
        assert not manifest['registeredOutputPaths'], manifest
        # A geometric edit of an extended/compressed build must clear all derived
        # records and reopen as missing; no private status receipt is required.
        edited = root/f'{name}-edited.wad'
        run(name+'-edit', ['map', 'flip', root/f'{name}-gl-compressed.wad', '--map', 'MAP01', '--object', 'linedef:0',
                           '--connected', '--axis', 'x', '--output', edited])
        assert inspect(name+'-edited-inspect', edited)['state'] == 'missing'
        built, _ = build(name+'-rebuilt', edited, '-z')
        assert inspect(name+'-rebuilt-inspect', built)['state'] == 'present'
    # A tiny compressed payload must not expand past the bounded node budget.
    bomb = zlib.compress(bytes(128*1024*1024+1), 1)
    records = lumps(fixture(False))
    records = [(n, b'ZNOD'+bomb if n == 'NODES' else b) for n, b in records]
    oversized = root/'oversized.wad'
    oversized.write_bytes(wad(records))
    assert inspect('oversized-inspect', oversized)['state'] == 'invalid'
    report = {'verified': True, 'steps': len(steps), 'variants': variants,
              'binarySha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
              'compilerSha256': hashlib.sha256(compiler.read_bytes()).hexdigest()}
    (root/'verified.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'Verified {len(variants)} compiler variants in {len(steps)} steps.', flush=True)


if __name__ == '__main__':
    main()
