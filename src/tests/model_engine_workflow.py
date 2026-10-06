"""Optional native model/collision acceptance through an FTE dedicated server.

All game data is generated. Supply existing CLI/compiler/server executables and
a new --output-root inside this repository/.agents/tmp. On Windows, Linux FTE
tools can run under --wsl-distribution; studio and map compilers remain native.
No client, screen capture, interactive input, downloaded assets or game install.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

FORMATS = ('mdl', 'md2', 'md3')
MODEL_CHECKS = {f'{fmt}-{name}' for fmt in FORMATS
                for name in ('frame-count', 'frame-0-name', 'frame-1-name', 'pose-0', 'pose-1', 'backface-0', 'backface-1')}
TAG_CHECKS = {'md3-tag-identity', 'md3-tag-pose-0-origin', 'md3-tag-pose-0-axes',
              'md3-tag-pose-1-origin', 'md3-tag-pose-1-axes'}
COLLISION_CHECKS = {'clip-point-pass-through', 'clip-player-hull-blocked', 'clip-large-hull-blocked-earlier',
                    'clip-player-hull-start-solid', 'clip-clear-path', 'room-floor',
                    'walkmove-clear', 'walkmove-clip-blocked'}
CHECKS = MODEL_CHECKS | TAG_CHECKS | COLLISION_CHECKS | {'mdl-group-frame-count', 'mdl-group-duration'}
KNOWN_FTE_FINDINGS = {'md3-tag-pose-1-axes'}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_checks(log):
    rows = re.findall(r'^VSMODEL (PASS|FAIL) ([a-z0-9-]+)\s*$', log, re.MULTILINE)
    names = [name for _, name in rows]
    if len(names) != len(set(names)) or set(names) != CHECKS:
        raise AssertionError(f'Incomplete or repeated engine assertions: {rows}')
    complete = re.findall(r'^VSMODEL COMPLETE (\d+) (\d+)\s*$', log, re.MULTILINE)
    passed = [name for status, name in rows if status == 'PASS']
    failed = [name for status, name in rows if status == 'FAIL']
    if complete != [(str(len(passed)), str(len(failed)))]:
        raise AssertionError(f'Missing or inconsistent completion marker: {complete}')
    return {'passed': passed, 'failed': failed,
            'samples': re.findall(r'^VSMODEL SAMPLE (.+)$', log, re.MULTILINE)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('binary', 'qbsp', 'q3map2', 'engine', 'qcc', 'output-root'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--wsl-distribution')
    args = parser.parse_args()
    if not __debug__:
        parser.error('Assertions must remain enabled.')
    if os.name == 'nt' and not args.wsl_distribution:
        parser.error('Use a Linux server with --wsl-distribution on Windows; Windows console input is not supported.')
    if os.name != 'nt' and args.wsl_distribution:
        parser.error('--wsl-distribution is only available on Windows.')
    repo = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(repo / 'scripts'))
    from source_companion_common import reject_links, walk_files
    root = args.output_root.absolute()
    reject_links(root)
    root = root.resolve()
    if not root.is_relative_to(repo / '.agents/tmp') or root.exists():
        parser.error('Use a new output directory inside this repository/.agents/tmp.')
    binaries = {}
    for name in ('binary', 'qbsp', 'q3map2', 'engine', 'qcc'):
        path = getattr(args, name).absolute()
        reject_links(path)
        if not path.is_file():
            parser.error(f'Missing executable: {path}')
        setattr(args, name, path)
        binaries[name] = {'path': str(path), 'sha256': digest(path)}
    root.mkdir(parents=True)
    runtime = root / 'runtime'
    runtime.mkdir()
    env = dict(os.environ, TEMP=str(runtime), TMP=str(runtime), TMPDIR=str(runtime), PYTHONDONTWRITEBYTECODE='1')
    fixture = Path(__file__).with_name('model_engine_fixture.qc')
    inputs = {path.relative_to(repo).as_posix(): digest(path) for path in
              (Path(__file__), fixture, Path(__file__).with_name('model_collision_compiler_workflow.py'))}
    steps = []

    def record(name, data):
        (root / name).write_text(json.dumps(data, indent=2), encoding='utf-8')

    def run(label, command, cwd=root, expect_code=0, timeout=60):
        result = subprocess.run(list(map(str, command)), cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                                capture_output=True, timeout=timeout)
        (root / (label + '.stdout.txt')).write_bytes(result.stdout)
        (root / (label + '.stderr.txt')).write_bytes(result.stderr)
        steps.append({'step': label, 'command': list(map(str, command)), 'cwd': str(cwd), 'exitCode': result.returncode})
        record('steps.json', steps)
        log = (result.stdout + result.stderr).decode('utf-8', errors='replace')
        if result.returncode != expect_code:
            raise AssertionError(f'{label}: exit {result.returncode}\n{log[-8000:]}')
        print(label, 'completed', flush=True)
        return log

    def cli(label, words):
        return json.loads(run(label, [args.binary, '--cli', '--settings-file', root / 'settings.ini', *words, '--json']))

    def engine_path(path):
        if not args.wsl_distribution:
            return str(path)
        if not re.fullmatch('[a-zA-Z]:', path.drive):
            raise ValueError('WSL fixtures and executables must use local drive paths.')
        return '/mnt/' + path.drive[0].lower() + path.as_posix()[2:]

    def linux_command(executable, words, cwd):
        settings = ['TMPDIR=' + engine_path(runtime), 'PYTHONDONTWRITEBYTECODE=1',
                    'SDL_VIDEODRIVER=dummy', 'SDL_AUDIODRIVER=dummy']
        command = ['env', *settings, 'timeout', '--signal=KILL', '30s', engine_path(executable), *words]
        if args.wsl_distribution:
            return ['wsl', '-d', args.wsl_distribution, '--cd', engine_path(cwd), '--exec', *command]
        return command

    record('inputs.json', {'startedAtUtc': datetime.now(timezone.utc).isoformat(),
                           'binaries': binaries, 'sourceHashes': inputs,
                           'wslDistribution': args.wsl_distribution})
    compiled = root / 'compiled'
    run('compiler-proof', [sys.executable, Path(__file__).with_name('model_collision_compiler_workflow.py'),
                          '--binary', args.binary, '--qbsp', args.qbsp, '--q3map2', args.q3map2,
                          '--output-root', compiled], timeout=120)
    proof = json.loads((compiled / 'verified.json').read_text(encoding='utf-8'))
    assert proof['stepsPassed'] == 21 and len(proof['checks']) == 3
    assets = root / 'assets'
    progs = assets / 'progs'
    progs.mkdir(parents=True)
    source_obj = assets / 'panel.obj'
    source_obj.write_text('v 0 0 0\nv 16 0 0\nv 16 16 0\nv 0 16 0\n'
                          'vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n'
                          'f 1/1 2/2 3/3\nf 1/1 3/3 4/4\n', encoding='ascii')
    mesh = assets / 'panel.mesh.json'
    cli('model-import', ['model', 'import', source_obj, '--output', mesh])

    def edit(label, *words):
        return cli(label, ['model', 'edit', mesh, *words, '--output', mesh, '--overwrite'])

    edit('frame-rest', '--operation', 'rename-frame', '--frame', '0', '--name', 'rest')
    edit('frame-lift', '--operation', 'duplicate-frame', '--frame', '0', '--name', 'lift')
    edit('frame-deform', '--operation', 'transform', '--frame', '1', '--vertices', 'all', '--offset', '0,0,8')
    edit('skin-path', '--operation', 'material', '--faces', 'all', '--material', 'models/native/skin.pcx')
    edit('md2-skin-size', '--operation', 'md2-skin-size', '--skin-size', '32,32')
    palette = compiled / 'synthetic-palette.lmp'
    skin = assets / 'models/native/skin.pcx'
    skin.parent.mkdir(parents=True)
    cli('skin-image', ['texture', 'export', compiled / 'wall.vtexture', '--profile', 'pcx',
                        '--palette-file', palette, '--output', skin])
    mdl_mesh = assets / 'mdl.mesh.json'
    cli('mdl-skin', ['model', 'mdl', mesh, '--operation', 'add-skin', '--image', skin, '--output', mdl_mesh])
    for fmt in FORMATS:
        cli('model-export-' + fmt, ['model', 'build', mdl_mesh if fmt == 'mdl' else mesh,
                                    '--output', progs / ('flat.' + fmt)])
    grouped = assets / 'grouped.mesh.json'
    cli('mdl-group', ['model', 'mdl', mdl_mesh, '--operation', 'group', '--first-frame', '0', '--last-frame', '1',
                     '--duration', '0.2', '--output', grouped])
    cli('model-export-group', ['model', 'build', grouped, '--output', progs / 'grouped.mdl'])
    edit('tag-add', '--operation', 'add-tag', '--name', 'tag_mount', '--tag-origin', '2,3,4')
    edit('tag-origin', '--operation', 'set-tag-origin', '--tag', 'tag_mount', '--frame', '1', '--tag-origin', '5,7,9')
    edit('tag-rotation', '--operation', 'transform-tag', '--tag', 'tag_mount', '--frame', '1',
         '--rotate', '0,0,90', '--pivot-mode', 'selection')
    cli('model-export-tags', ['model', 'build', mesh, '--output', progs / 'tagged.md3'])
    # Independent MD3 byte checks follow id's qfiles.h and tr_model.c rather
    # than FTE's synthesized frame names and transposed tag-matrix lookup.
    md3 = (progs / 'tagged.md3').read_bytes()
    frame_offset, tag_offset = struct.unpack_from('<2i', md3, 92)
    assert [md3[frame_offset + f * 56 + 40:frame_offset + f * 56 + 56].split(b'\0', 1)[0]
            for f in range(2)] == [b'rest', b'lift']
    assert struct.unpack_from('<3f', md3, tag_offset + 112 + 64) == (5., 7., 9.)
    axes = struct.unpack_from('<9f', md3, tag_offset + 112 + 76)
    assert all(abs(x - y) < 1e-6 for x, y in zip(axes, (0, 1, 0, -1, 0, 0, 0, 0, 1)))
    record('md3-layout.json', {'sha256': digest(progs / 'tagged.md3'), 'frameNames': ['rest', 'lift'],
                              'rotatedTagOrigin': [5, 7, 9], 'rotatedTagAxes': axes,
                              'scope': 'Independent file-layout audit against original Quake III tag basis convention.'})
    qc = root / 'qc'
    qc.mkdir()
    shutil.copy2(fixture, qc / fixture.name)
    (qc / 'progs.src').write_text('progs.dat\n' + fixture.name + '\n', encoding='ascii')
    run('compile-qc', linux_command(args.qcc, ['-O0'], qc), cwd=qc)
    assert (qc / 'progs.dat').is_file()
    inputs.update({path.relative_to(root).as_posix(): digest(path) for path in walk_files(assets).values()})
    record('generated-inputs.json', inputs)

    def prepare(name, bsp):
        case = root / name
        base = case / 'id1'
        base.mkdir(parents=True)
        shutil.copytree(progs, base / 'progs')
        shutil.copytree(assets / 'models', base / 'models')
        shutil.copy2(qc / 'progs.dat', base / 'progs.dat')
        (base / 'maps').mkdir()
        shutil.copy2(bsp, base / 'maps/acceptance.bsp')
        (base / 'default.cfg').write_text('', encoding='ascii')
        (base / 'ftesrv.cfg').write_text('', encoding='ascii')
        (case / 'test.fmf').write_text('FTEManifestVer 1\ngame vibestudio-native-test\n'
                                       'name "VibeStudio generated acceptance fixture"\n'
                                       'basegame id1\ndisablehomedir 1\n', encoding='ascii')
        # Startup configuration disables public/listening sockets and automatic
        # configuration writes. No console key events or standard input are read.
        config = ('set developer 1\nset sv_public -1\nset sv_port ""\nset sv_port_ipv6 ""\n'
                  'set sv_listen_nq 0\nset sv_listen_dp 0\nset sv_listen_qw 0\nset sv_listen_q3 0\n'
                  'set sv_port_tcp ""\nset sv_port_unix ""\nset sv_port_rtc ""\n'
                  'set cfg_save_auto 0\nset sv_progs progs.dat\nset sv_gameplayfix_setmodelrealbox 1\n'
                  'set sv_nqplayerphysics 1\nset sv_mintic 0.01\nmap acceptance\n')
        (base / 'acceptance.cfg').write_text(config, encoding='ascii')
        return case

    cases = []

    def execute(name, bsp, mutation=None, expected_failures=frozenset()):
        case = prepare(name, bsp)
        if mutation:
            mutation(case)
        hashes = {key: digest(path) for key, path in walk_files(case).items()}
        command = linux_command(args.engine, ['-dedicated', '-nostdin', '-nocolour', '-nohome',
                                '-basedir', engine_path(case), '-manifest', engine_path(case / 'test.fmf'),
                                '+exec', 'acceptance.cfg'], case)
        log = run(name, command, cwd=case)
        checks = parse_checks(log)
        assert set(checks['failed']) - KNOWN_FTE_FINDINGS == set(expected_failures), (name, checks)
        assert all(digest(case / key) == value for key, value in hashes.items()), 'Engine altered fixture inputs'
        cases.append({'case': name, 'deliberateFailures': sorted(expected_failures),
                      'engineFindings': sorted(set(checks['failed']) & KNOWN_FTE_FINDINGS), 'inputHashes': hashes, **checks})
        record('cases.json', cases)

    for target, base in (('quake', 'id1'), ('quake2', 'baseq2'), ('quake3', 'baseq3')):
        execute(target, compiled / target / base / 'maps/collision.bsp')

    def damaged_pose(case):
        # MD2 frame scale/translation layout: GPL-2.0-or-later id Software
        # qcommon/qfiles.h (credited in docs/CREDITS.md). Keep the file valid but
        # lower only pose 1 to pose 0; native decoding must expose the wrong pose.
        path = case / 'id1/progs/flat.md2'
        data = bytearray(path.read_bytes())
        assert data[:4] == b'IDP2'
        stride = struct.unpack_from('<i', data, 16)[0]
        offset = struct.unpack_from('<i', data, 56)[0]
        struct.pack_into('<f', data, offset + stride + 20, 0.)
        path.write_bytes(data)

    quake_maps = compiled / 'quake/id1/maps'
    execute('negative-md2-pose', quake_maps / 'collision.bsp', damaged_pose, {'md2-pose-1'})
    run('compile-empty-room', [args.qbsp, '-threads', '2', quake_maps / 'room.map'])
    execute('negative-no-collision', quake_maps / 'room.bsp', expected_failures={
            'clip-player-hull-blocked', 'clip-large-hull-blocked-earlier',
            'clip-player-hull-start-solid', 'walkmove-clip-blocked'})
    for binary in binaries.values():
        assert digest(Path(binary['path'])) == binary['sha256'], 'Executable changed during acceptance'
    for key, value in inputs.items():
        path = repo / key if key.startswith('src/') else root / key
        assert digest(path) == value, f'Input changed during acceptance: {key}'
    findings = sorted({finding for case in cases for finding in case['engineFindings']})
    record('verified.json', {'recordedAtUtc': datetime.now(timezone.utc).isoformat(), 'binaries': binaries,
                            'status': 'completed-with-engine-findings' if findings else 'passed', 'engineFindings': findings,
                            'sourceAndAssetHashes': inputs, 'compilerProofSha256': digest(compiled / 'verified.json'),
                            'compilerSteps': proof['stepsPassed'], 'stepsPassed': len(steps), 'cases': cases,
                            'server': 'FTE dedicated', 'wslDistribution': args.wsl_distribution,
                            'interactiveInput': False, 'clientLaunched': False,
                            'scope': 'FTE native decoding, pose traces, MD3 tags and collision movement with generated '
                                     'assets. No texture rendering, original-engine/client gameplay or release-package acceptance.'})
    print(f'Completed {len(cases)} server cases; {len(CHECKS)} assertions each; both negative controls detected. '
          f'Engine findings: {findings}', flush=True)
    return 4 if findings else 0


if __name__ == '__main__':
    raise SystemExit(main())
