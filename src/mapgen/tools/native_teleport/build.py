"""Pin read-only stock sources and build the separate private teleport host."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

PROJECT = Path(__file__).resolve().parents[2]
SOURCE = Path('E:/_SOURCE/_CODE/Quake-2-master')
LOCAL = PROJECT/'tools/native_teleport'
SCRATCH = PROJECT/'.agents/tmp/relay-native-v1'
OUTPUT = PROJECT/'out/native-teleport'
EXECUTABLE = SCRATCH/'build/qmg_native_teleport.exe'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2)+'\n', encoding='utf-8')


def pin():
    path = LOCAL/'reference.json'
    if path.exists():
        raise ValueError('Never replace the pinned external source declaration')
    names = ['game/'+name for name in re.findall(r'^SOURCE=\.\\([^\r\n]+)',
        (SOURCE/'game/game.dsp').read_text(), re.M) if name.endswith(('.c', '.h'))]
    if len([n for n in names if n.endswith('.c')]) != 48:
        raise ValueError('Unexpected stock game compilation list')
    names += ['game/game.dsp', 'server/sv_world.c', 'server/server.h', 'qcommon/pmove.c',
              'qcommon/cmodel.c', 'qcommon/md4.c', 'qcommon/qcommon.h', 'qcommon/qfiles.h', 'readme.txt', 'gnu.txt']
    save(path, {'schema_version': 1, 'reference': 'id Software Quake II 3.19',
        'source_url': 'https://github.com/id-Software/Quake-2',
        'license': 'GPL-2.0-or-later; optional private combined executable only',
        'dependencies': [{'relative_path': name, 'sha256': sha(SOURCE/name)} for name in names]})
    return {'pinned_external_files': len(names), 'reference_sha256': sha(path)}


def build():
    reference = json.loads((LOCAL/'reference.json').read_text())
    for entry in reference['dependencies']:
        if sha(SOURCE/entry['relative_path']) != entry['sha256']:
            raise ValueError('External source pin changed')
    for path in (PROJECT, PROJECT/'.agents', PROJECT/'.agents/tmp', SCRATCH):
        if path.exists() and (path.is_symlink() or path.lstat().st_file_attributes & 1024):
            raise ValueError('Writable build path traverses a filesystem link')
    SCRATCH.mkdir(exist_ok=True)
    attempt = len(list(OUTPUT.glob('build-attempt-*.json')))+1
    record_path = OUTPUT/f'build-attempt-{attempt:02}.json'
    record = {'status': 'started', 'external_dependencies': reference['dependencies'],
        'local_sources': [{'path': str(p.relative_to(PROJECT)), 'sha256': sha(p)}
                          for p in sorted(LOCAL.iterdir()) if p.is_file()],
        'working_directory': str(LOCAL), 'scratch_directory': str(SCRATCH),
        'commands': [['cmake','--preset','windows-clang'], ['cmake','--build','--preset','windows-clang']],
        'license_boundary': reference['license']}
    save(record_path, record)
    for index, command in enumerate(record['commands']):
        logfile = OUTPUT/f'build-{attempt:02}-{index}.log'
        with logfile.open('w') as stream:
            result = subprocess.run(command, cwd=LOCAL, stdout=stream, stderr=subprocess.STDOUT, timeout=120)
        if result.returncode:
            record.update({'status': 'failed', 'failed_command': index, 'exit_code': result.returncode})
            save(record_path, record)
            raise ValueError('Build failed; first diagnostics retained in '+str(logfile))
    record.update({'status': 'completed', 'executable_sha256': sha(EXECUTABLE), 'executable': str(EXECUTABLE)})
    compiler = Path('C:/Program Files/LLVM/bin/clang-cl.exe')
    linker = Path('C:/Program Files/LLVM/bin/lld-link.exe')
    record['compiler'] = {'path': str(compiler), 'sha256': sha(compiler),
                          'version': subprocess.check_output([str(compiler),'--version'],text=True)}
    record['linker'] = {'path': str(linker), 'sha256': sha(linker)}
    save(record_path, record)
    return {'ok': True, 'record': str(record_path.relative_to(PROJECT)), 'executable_sha256': sha(EXECUTABLE)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('pin', 'build'))
    args = parser.parse_args()
    print(json.dumps({'pin': pin, 'build': build}[args.mode](), indent=2))
