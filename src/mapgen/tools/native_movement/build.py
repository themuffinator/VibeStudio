"""Build the private optional external-reference binary and retain its provenance."""
import json
from pathlib import Path
import subprocess

from tools.native_movement.run import BUILD_RECORD, NATIVE, PROJECT, require, save, sha


def main():
    source = PROJECT/'tools/native_movement'
    scratch = PROJECT/'.agents/tmp/native-movement'
    scratch.mkdir(parents=True, exist_ok=True)
    reference = Path('E:/_SOURCE/_CODE/Quake-2-master')
    pins = json.loads((source/'reference.json').read_text(encoding='utf-8'))
    dependencies = []
    for entry in pins['dependencies']:
        path = reference/entry['relative_path']
        require(sha(path) == entry['sha256'], f'Pinned external reference changed: {path}')
        dependencies.append({'path': str(path), 'sha256': sha(path)})
    commands = [['cmake', '--preset', 'windows-clang'], ['cmake', '--build', '--preset', 'windows-clang']]
    for name, command in zip(('configure', 'build'), commands):
        with (scratch/(name+'.log')).open('w', encoding='utf-8') as log:
            subprocess.run(command, cwd=source, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
    for entry in dependencies:
        require(sha(Path(entry['path'])) == entry['sha256'], 'External dependency changed during build')
    compiler = Path('C:/Program Files/LLVM/bin/clang-cl.exe')
    linker = Path('C:/Program Files/LLVM/bin/lld-link.exe')
    record = {'schema_version': 1, 'reference': pins['reference'], 'source_url': pins['source_url'],
              'external_dependencies': dependencies,
              'local_sources': [{'path': str(source/name), 'sha256': sha(source/name)} for name in
                                ('harness.c', 'CMakeLists.txt', 'CMakePresets.json', 'reference.json')],
              'executable': str(NATIVE), 'executable_sha256': sha(NATIVE),
              'compiler': {'path': str(compiler), 'sha256': sha(compiler),
                           'version': subprocess.check_output([str(compiler), '--version'], text=True)},
              'linker': {'path': str(linker), 'sha256': sha(linker)},
              'commands': commands, 'working_directory': str(source),
              'floating_point': 'Portable C (C_ONLY), no optimization, /fp:strict; x64 Clang/MSVC ABI',
              'licence_boundary': 'Optional local combined GPL executable; no external source copied, no install or packaging target, never include binary in map deliverables',
              'limits': 'Official 3.19 baseline; does not reproduce full game module, dynamic entities, client prediction, network, Q2REPRO changes or human input'}
    save(BUILD_RECORD, record)
    (PROJECT/'out/native-movement').mkdir(parents=True, exist_ok=True)
    save(PROJECT/'out/native-movement/build-provenance.json', record)
    print(json.dumps({'executable_sha256': record['executable_sha256'], 'external_dependencies': len(dependencies)}))


if __name__ == '__main__':
    main()
