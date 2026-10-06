"""Generated UDMF -> lossless edit -> real ZDBSP -> package/launch-plan proof.

Does not launch a game or operate native input. Compiler rewrites are checked
separately from the editor's byte-preserving source edits.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
from level_doom_mirror_compiler_workflow import fixture, lumps, wad


def textmap(namespace):
    text = '// Generated UDMF compiler fixture\nnamespace = "'+namespace+'";\nuser_global = "retained";\n'
    for x, y in ((0.25, 0.25), (0.25, 256.25), (256.25, 256.25), (256.25, 0.25)):
        text += f'vertex {{ x = {x}; y = {y}; user_float = 0.125; }}\n'
    for i in range(4):
        text += f'linedef {{ v1 = {i}; v2 = {(i+1)%4}; sidefront = {i}; blocking = true; }}\n'
        text += 'sidedef { sector = 0; texturemiddle = "STONE"; }\n'
    text += 'sector { heightfloor = 0; heightceiling = 128; texturefloor = "FLOOR0_1"; textureceiling = "CEIL1_1"; lightlevel = 192; }\n'
    text += 'thing { x = 64.25; y = 64.25; type = 1; angle = 90; skill1 = true; skill2 = true; skill3 = true; skill4 = true; skill5 = true; single = true; coop = true; dm = true; user_note = "retained"; }\n'
    return text.encode()


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
        (root/f'{label}.json').write_bytes(p.stdout)
        (root/f'{label}.stderr.txt').write_bytes(p.stderr)
        steps.append({'step': label, 'command': command, 'exitCode': p.returncode})
        (root/'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert (p.returncode == 0) == success, (label, p.returncode, p.stdout, p.stderr)
        print(label, 'passed', flush=True)
        return json.loads(p.stdout)

    run('register', ['compiler', 'set-path', 'zdbsp', '--executable', compiler])
    run('install', ['install', 'add', root, '--install-game', 'doom', '--install-engine', 'idtech1',
                    '--install-name', 'Generated UDMF proof', '--install-executable', binary])
    rest = lumps(fixture(False))
    rest = rest[next(i for i, item in enumerate(rest) if item[0] == 'MAP02'):]
    rest += [('USERDATA', b'first'), ('USERDATA', b'second')]
    for namespace in ('doom', 'zdoom'):
        source = root/f'{namespace}.wad'
        raw = textmap(namespace)
        source.write_bytes(wad([('MAP01', b''), ('TEXTMAP', raw), ('PORTDATA', b'\0opaque-sidecar'), ('ENDMAP', b'')] + rest))
        original_hash = hashlib.sha256(source.read_bytes()).hexdigest()
        edited = root/f'{namespace}-edited.wad'
        words = ['map', 'edit-udmf', source, '--map-name', 'MAP01', '--object', 'vertex:0', '--set', 'x=16.75', '--output', edited]
        run(namespace+'-dry-run', words+['--dry-run'])
        assert not edited.exists()
        run(namespace+'-edit', words)
        before, after = lumps(source.read_bytes()), lumps(edited.read_bytes())
        assert after[1] == ('TEXTMAP', raw.replace(b'x = 0.25;', b'x = 16.75;', 1))
        assert after[:1]+after[2:] == before[:1]+before[2:]
        assert hashlib.sha256(source.read_bytes()).hexdigest() == original_hash
        run(namespace+'-launch-refused', ['launch', 'plan', '--map', 'MAP01', '--bsp', edited], False)
        for variant, flags in [('extended', '-X'), ('compressed', '-Z')]:
            label = namespace+'-'+variant
            output = root/f'{label}.wad'
            run(label+'-build', ['compiler', 'run', 'zdbsp-nodes', '--input', edited, '--output', output,
                                '--extra-args', '-m MAP01 '+flags, '--working-directory', root,
                                '--manifest', root/f'{label}.manifest.json'])
            report = run(label+'-inspect', ['map', 'inspect', output, '--map', 'MAP01'])['map']['nodeBuild']
            assert report['state'] == 'present' and report['format'].startswith('XGL' if variant == 'extended' else 'ZGL'), report
            props = run(label+'-properties', ['map', 'inspect-udmf', output, '--map', 'MAP01'])['udmf']
            assert any(p['key'] == 'user_global' and p['literal'] == '"retained"' for p in props['globals'])
            vertex = next(b for b in props['blocks'] if b['object'] == 'vertex:0')['properties']
            assert any(p['key'] == 'x' and float(p['literal']) == 16.75 for p in vertex), vertex
            assert any(p['key'] == 'user_float' for p in vertex)
            built = lumps(output.read_bytes())
            assert next(data for name, data in built if name == 'PORTDATA') == b'\0opaque-sidecar'
            assert built[next(i for i, item in enumerate(built) if item[0] == 'MAP02'):] == rest
            run(label+'-package', ['package', 'validate', output])
            plan = run(label+'-launch-plan', ['launch', 'plan', '--map', 'MAP01', '--bsp', output])['plan']
            assert plan['runnable'] and plan['validatedArtifactSha256'] == hashlib.sha256(output.read_bytes()).hexdigest()
            variants.append({'namespace': namespace, 'variant': variant, **report})
    summary = {'steps': len(steps), 'variants': variants, 'binarySha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
               'compilerSha256': hashlib.sha256(compiler.read_bytes()).hexdigest()}
    (root/'verified.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print('UDMF compiler/package/launch-plan proof passed', flush=True)


if __name__ == '__main__':
    main()
