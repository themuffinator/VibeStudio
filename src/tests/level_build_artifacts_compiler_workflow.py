"""Real q3map2 external lightmaps/generated shaders -> reviewed PK3 acceptance.

Reuses the generated model/draft/map proof. No commercial assets, game launch,
native input or screen capture. All output stays in the project's .agents/tmp.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    root = args.output_root.resolve()
    assert root.is_relative_to(repo / '.agents' / 'tmp') and not root.exists()
    root.mkdir(parents=True)
    baseline = root / 'baseline'
    command = [sys.executable, str(Path(__file__).with_name('level_build_workspace_compiler_workflow.py')),
               '--binary', str(args.binary.resolve()), '--compiler', str(args.compiler.resolve()), '--output-root', str(baseline)]
    fixture = subprocess.run(command, cwd=repo, capture_output=True, timeout=240)
    (root / 'fixture.stdout.txt').write_bytes(fixture.stdout)
    (root / 'fixture.stderr.txt').write_bytes(fixture.stderr)
    assert fixture.returncode == 0, fixture.stderr
    steps = []

    def run(label, words, code=0):
        command = [str(args.binary.resolve()), '--cli', '--json', '--settings-file', str(root / 'settings.ini')] + list(map(str, words))
        result = subprocess.run(command, cwd=repo, capture_output=True, timeout=180)
        (root / f'{label}.stdout.txt').write_bytes(result.stdout)
        (root / f'{label}.stderr.txt').write_bytes(result.stderr)
        steps.append({'step': label, 'command': command, 'exitCode': result.returncode})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert result.returncode == code, (label, result.stdout, result.stderr)
        print(label, 'passed', flush=True)
        return json.loads(result.stdout)

    shader = root / 'source.shader'
    shader.write_text(''.join(f'textures/workspace/{name}\n{{\n qer_editorimage textures/workspace/{name}.png\n'
                             f' {{ map $lightmap }}\n {{ map textures/workspace/{name}.png\n blendFunc filter }}\n}}\n'
                             for name in ('wall', 'model')), encoding='utf-8')
    draft = root / 'runtime-assets.vibepackage'
    run('stage-lightmapped-materials', ['package', 'draft-save', baseline / 'assets.vibepackage', draft,
                                       '--replace-file', shader, '--replace-entry', 'scripts/workspace.shader'])
    workspace = root / 'prepared'
    captured = run('prepare', ['build', 'prepare', baseline / 'arena.map', '--package', draft,
                              '--output', workspace, '--name', 'arena'])['workspace']
    base = workspace / 'game/baseq3'
    pipeline = ['build', 'run-prepared', workspace, '--tool', 'q3map2=' + str(args.compiler.resolve()),
                '--stage-args', 'bsp=-threads 2 -meta', '--stage-args', 'vis=-threads 2 -fast']
    external = pipeline + ['--stage-args', 'light=-threads 2 -fast -extlmhacksize 256']
    assert run('external-compile', external)['pipeline']['succeeded']
    artifacts = run('external-artifacts', ['build', 'artifacts', workspace])['artifacts']
    assert artifacts['verified']
    roles = {entry['role'] for entry in artifacts['outputs']}
    assert {'compiled-map', 'generated-shader', 'external-lightmap', 'diagnostic'} <= roles, roles
    for entry in artifacts['outputs']:
        data = (workspace / entry['path']).read_bytes()
        assert len(data) == entry['bytes'] and hashlib.sha256(data).hexdigest() == entry['sha256']
    custom = (base / 'scripts/q3map2_arena.shader').read_text(encoding='utf-8')
    lightmaps = set(re.findall(r'maps/arena/lm_[0-9]+\.tga', custom))
    assert lightmaps and all((base / path).is_file() for path in lightmaps), custom
    bsp = (base / 'maps/arena.bsp').read_bytes()
    offset, size = struct.unpack_from('<2i', bsp, 8 + 8)
    shaders = [bsp[i:i+64].split(b'\0', 1)[0].decode() for i in range(offset, offset+size, 72)]
    generated_names = set(re.findall(r'^([^/\s][^\s{}]*)\s*\n\{', custom, re.MULTILINE))
    assert generated_names.intersection(shaders), (generated_names, shaders)
    output = root / 'external.pk3'
    publish = ['build', 'publish-prepared', workspace, '--output', output, '--expected-output-sha256', artifacts['recordSha256']]
    run('publish-dry', publish + ['--dry-run'])
    assert not output.exists()
    published = run('publish', publish)['publication']
    assert published['committed'] and published['deterministic']
    with zipfile.ZipFile(output) as archive:
        assert archive.testzip() is None
        assert archive.read('maps/arena.bsp') == bsp
        assert set(archive.namelist()) == set(published['paths'])
        for path in lightmaps | {'scripts/q3map2_arena.shader', 'models/workspace/prop.md3', 'sound/workspace/tone.wav'}:
            assert archive.read(path) == (base / path).read_bytes()
        assert 'maps/arena.map' not in archive.namelist() and 'maps/arena.prt' not in archive.namelist()
    validation = run('validate', ['package', 'validate', output])['validation']
    assert validation['valid'] and validation['uncheckedCount'] == 0
    original_package = output.read_bytes()
    run('overwrite-refused', publish, code=4)
    run('publish-source-backup', publish + ['--include-source', '--overwrite'])
    assert Path(str(output) + '.bak').read_bytes() == original_package
    with zipfile.ZipFile(output) as archive:
        assert archive.read('maps/arena.map') == (base / 'maps/arena.map').read_bytes()
    selected_lightmap = base / sorted(lightmaps)[0]
    original_lightmap = selected_lightmap.read_bytes()
    selected_lightmap.write_bytes(original_lightmap[:-1] + bytes([original_lightmap[-1] ^ 1]))
    run('changed-output-refused', ['build', 'artifacts', workspace], code=4)
    run('changed-output-publish-refused', publish + ['--overwrite'], code=4)
    selected_lightmap.write_bytes(original_lightmap)
    # A clean internal-lightmap build must stop shipping stale custom shader/TGA files.
    run('internal-rebuild', pipeline + ['--stage-args', 'light=-threads 2 -fast'])
    internal = run('internal-artifacts', ['build', 'artifacts', workspace])['artifacts']
    assert internal['verified'] and internal['recordSha256'] != artifacts['recordSha256']
    assert not {'generated-shader', 'external-lightmap'}.intersection(entry['role'] for entry in internal['outputs'])
    assert not (base / 'scripts/q3map2_arena.shader').exists()
    for path in lightmaps:
        assert not (base / path).exists()
        assert (workspace / 'history' / internal['runId'] / 'game/baseq3' / path).is_file()
    run('stale-review-refused', publish + ['--overwrite'], code=4)
    internal_output = root / 'internal.pk3'
    final = run('publish-internal', ['build', 'publish-prepared', workspace, '--output', internal_output])['publication']
    assert not lightmaps.intersection(final['paths']) and 'scripts/q3map2_arena.shader' not in final['paths']
    for entry in captured['inputs']:
        data = (workspace / entry['path']).read_bytes()
        assert len(data) == entry['bytes'] and hashlib.sha256(data).hexdigest() == entry['sha256']
    (root / 'verified.json').write_text(json.dumps({
        'stepsPassed': len(steps), 'baselineStepsPassed': 9, 'externalLightmaps': sorted(lightmaps),
        'generatedShaderNames': sorted(generated_names), 'externalOutputs': len(artifacts['outputs']),
        'externalPackageSha256': published['sha256'], 'internalPackageSha256': final['sha256'],
        'staleOutputsRetainedOutsideCompilerPaths': True, 'capturedInputsUnchanged': True,
        'packageValidation': validation}, indent=2), encoding='utf-8')
    print('PASS: real external lightmaps and generated shaders, reviewed publication, backup, tamper and clean rebuild.', flush=True)


if __name__ == '__main__':
    main()
