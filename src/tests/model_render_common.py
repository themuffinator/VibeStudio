"""Isolation and evidence collection for optional FTE render-target tests.

Fixtures and pixel oracles live in the calling workflow. This module never
captures a desktop, opens a display server, or injects user input.
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
import subprocess
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class RenderHarness:
    def __init__(self, workflow, fixture_name, description):
        parser = argparse.ArgumentParser(description=description)
        for name in ('binary', 'engine', 'qcc', 'output-root'):
            parser.add_argument('--' + name, required=True, type=Path)
        parser.add_argument('--wsl-distribution')
        self.args = args = parser.parse_args()
        if not __debug__:
            parser.error('Assertions must remain enabled.')
        if (os.name == 'nt') != bool(args.wsl_distribution):
            parser.error('Windows requires --wsl-distribution and Linux FTE binaries; POSIX runs them directly.')
        self.repo = Path(workflow).resolve().parents[2]
        sys.path.insert(0, str(self.repo / 'scripts'))
        from source_companion_common import reject_links, walk_files
        self.walk_files = walk_files
        root = args.output_root.absolute()
        reject_links(root)
        self.root = root = root.resolve()
        if not root.is_relative_to(self.repo / '.agents/tmp') or root.exists():
            parser.error('Use a new directory inside this repository/.agents/tmp.')
        self.binaries = {}
        for name in ('binary', 'engine', 'qcc'):
            path = getattr(args, name).absolute()
            reject_links(path)
            if not path.is_file():
                parser.error(f'Missing executable: {path}')
            setattr(args, name, path)
            self.binaries[name] = {'path': str(path), 'sha256': digest(path)}
        root.mkdir(parents=True)
        self.runtime = root / 'runtime'
        self.runtime.mkdir(mode=0o700)
        self.assets = root / 'assets'
        self.assets.mkdir()
        self.fixture = Path(workflow).with_name(fixture_name).resolve()
        self.sources = {path.relative_to(self.repo).as_posix(): digest(path) for path in
                        (Path(workflow).resolve(), Path(__file__).resolve(), self.fixture,
                         self.repo / 'scripts/source_companion_common.py')}
        self.env = dict(os.environ, TEMP=str(self.runtime), TMP=str(self.runtime), PYTHONDONTWRITEBYTECODE='1')
        self.steps = []
        self.case_inputs = []
        self.record('inputs.json', {'startedAtUtc': datetime.now(timezone.utc).isoformat(),
                                   'binaries': self.binaries, 'sourceHashes': self.sources})

    def record(self, name, data):
        (self.root / name).write_text(json.dumps(data, indent=2), encoding='utf-8')

    def run(self, label, command, cwd=None, timeout=90):
        cwd = cwd or self.root
        command = list(map(str, command))
        result = subprocess.run(command, cwd=cwd, env=self.env, stdin=subprocess.DEVNULL,
                                capture_output=True, timeout=timeout)
        (self.root / (label + '.stdout.txt')).write_bytes(result.stdout)
        (self.root / (label + '.stderr.txt')).write_bytes(result.stderr)
        self.steps.append({'step': label, 'command': command, 'cwd': str(cwd), 'exitCode': result.returncode})
        self.record('steps.json', self.steps)
        log = (result.stdout + result.stderr).decode('utf-8', errors='replace')
        assert result.returncode == 0, (label, result.returncode, log[-8000:])
        print(label, 'completed', flush=True)
        return log

    def cli(self, label, words):
        return json.loads(self.run(label, [self.args.binary, '--cli', '--settings-file',
                                          self.root / 'settings.ini', *words, '--json']))

    def linux(self, path):
        if not self.args.wsl_distribution:
            return str(path)
        if not re.fullmatch('[a-zA-Z]:', path.drive):
            raise ValueError('WSL inputs must use local drive paths.')
        return '/mnt/' + path.drive[0].lower() + path.as_posix()[2:]

    def external(self, command, cwd, settings):
        words = ['env', 'TMPDIR=' + self.linux(self.runtime), 'PYTHONDONTWRITEBYTECODE=1', *settings,
                 'timeout', '--signal=KILL', '60s', *command]
        return (['wsl', '-d', self.args.wsl_distribution, '--cd', self.linux(cwd), '--exec', *words]
                if self.args.wsl_distribution else words)

    def compile_fixture(self):
        qc = self.root / 'qc'
        qc.mkdir()
        shutil.copy2(self.fixture, qc / 'fixture.qc')
        (qc / 'progs.src').write_text('menu.dat\nfixture.qc\n', encoding='ascii')
        log = self.run('compile-qc', self.external([self.linux(self.args.qcc), '-O0'], qc, []), cwd=qc)
        assert not re.search(r'\bwarning [A-Z]\d+\b', log, re.IGNORECASE), log
        shutil.copy2(qc / 'menu.dat', self.assets / 'menu.dat')
        self.asset_hashes = {key: digest(path) for key, path in self.walk_files(self.assets).items()}
        self.record('generated-assets.json', self.asset_hashes)

    def render_case(self, name, mutation=None):
        case = self.root / name
        base = case / 'id1'
        case.mkdir()
        shutil.copytree(self.assets, base)
        (base / 'renders').mkdir()
        for folder in ('runtime', 'cache', 'config', 'data'):
            (case / folder).mkdir(mode=0o700)
        for file in ('default.cfg', 'fte.cfg', 'config.cfg', 'autoexec.cfg'):
            (base / file).write_text('', encoding='ascii')
        (case / 'test.fmf').write_text('FTEManifestVer 1\ngame vibestudio-render-test\n'
            'name "VibeStudio generated render fixture"\nbasegame id1\ndisablehomedir 1\n', encoding='ascii')
        config = ('set developer 1\nset cfg_save_auto 0\nset sv_public -1\nset sv_port ""\n'
                  'set sv_listen_nq 0\nset sv_listen_dp 0\nset sv_listen_qw 0\nset sv_listen_q3 0\n'
                  'set vid_gamma 1\nset vid_conautoscale 0\nset vid_conwidth 640\nset vid_conheight 480\n'
                  'set cl_maxfps 60\nset scr_sshot_type png\nset in_windowed_mouse 0\nset r_noframegrouplerp 1\n'
                  'set r_replacemodels ""\n')
        (base / 'acceptance.cfg').write_text(config, encoding='ascii')
        if mutation:
            mutation(base)
        hashes = {key: digest(path) for key, path in self.walk_files(case).items()}
        self.case_inputs.append((case, hashes))
        settings = ['DISPLAY=', 'WAYLAND_DISPLAY=vibestudio-no-display',
                    'XDG_RUNTIME_DIR=' + self.linux(case / 'runtime'),
                    'XDG_CACHE_HOME=' + self.linux(case / 'cache'),
                    'XDG_CONFIG_HOME=' + self.linux(case / 'config'),
                    'XDG_DATA_HOME=' + self.linux(case / 'data'), 'MESA_SHADER_CACHE_DISABLE=true',
                    'EGL_PLATFORM=surfaceless', 'LIBGL_ALWAYS_SOFTWARE=1',
                    'SDL_VIDEODRIVER=dummy', 'SDL_AUDIODRIVER=dummy']
        command = [self.linux(self.args.engine), '-nostdin', '-nohome', '-nosound', '-nocdaudio', '-nomouse', '-nojoy',
                   '-window', '-width', '640', '-height', '480', '-basedir', self.linux(case),
                   '-manifest', self.linux(case / 'test.fmf'), '-set', 'vid_renderer', 'egl_headless',
                   '-set', 'vid_fullscreen', '0', '-set', 'r_replacemodels', '', '+exec', 'acceptance.cfg']
        log = self.run(name, self.external(command, case, settings), cwd=case)
        assert 'Headless OpenGL (EGL) renderer initialized' in log and 'GL_RENDERER: llvmpipe' in log
        assert all(digest(case / key) == value for key, value in hashes.items()), 'Engine changed fixture inputs'
        return base, log, hashes

    def finish(self, scope, cases, **extra):
        assert all(digest(self.repo / key) == value for key, value in self.sources.items())
        assert all(digest(self.assets / key) == value for key, value in self.asset_hashes.items())
        assert all(digest(Path(item['path'])) == item['sha256'] for item in self.binaries.values())
        assert all(digest(case / key) == value for case, hashes in self.case_inputs for key, value in hashes.items())
        self.record('verified.json', {'recordedAtUtc': datetime.now(timezone.utc).isoformat(), 'status': 'passed',
                                     'binaries': self.binaries, 'sourceHashes': self.sources,
                                     'assetHashes': self.asset_hashes, 'stepsPassed': len(self.steps),
                                     'cases': cases, 'wslDistribution': self.args.wsl_distribution,
                                     'scope': scope, **extra})
