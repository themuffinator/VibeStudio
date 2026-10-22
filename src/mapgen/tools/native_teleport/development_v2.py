"""Identical development commands after the documented NULL-configstring host fix."""
import argparse
import json
from pathlib import Path
from tools.native_teleport import run as original
from tools.native_teleport.build import OUTPUT, sha, save

original.BUILD_RECORD = OUTPUT/'build-attempt-03.json'


def contract():
    protocol = original.development_contract()
    previous = original.read(OUTPUT/'development-protocol.json')
    original.require(protocol['trials'] == previous['trials'], 'Repair must preserve every first input')
    protocol.update({'id': 'stock-teleport-development-v2',
        'previous_protocol_sha256': sha(OUTPUT/'development-protocol.json'),
        'repair_runner_sha256': sha(Path(__file__)),
        'repair': 'Host configstring import accepts NULL, matching stock PF_Configstring. All v1 attempts stopped in SpawnEntities. No command or expectation changed.'})
    return protocol


def declare():
    path = OUTPUT/'development-v2-protocol.json'
    original.require(not path.exists(), 'Never replace the repair declaration')
    save(path, contract())
    return {'declared': True, 'native_runs': 0, 'protocol_sha256': sha(path)}


def execute():
    # Reuse the unmodified first-attempt implementation, supplying only the
    # separately declared protocol/output paths and contract function.
    text = Path(original.__file__).read_text()
    start = text.index('def run_development():')
    end = text.index("\n\nif __name__", start)
    body = text[start:end].replace("'development-protocol.json'", "'development-v2-protocol.json'").replace("'development-v1'", "'development-v2'")
    namespace = dict(vars(original)); namespace['development_contract'] = contract
    exec(compile(body, __file__+':unchanged-first-attempt-loop', 'exec'), namespace)
    return namespace['run_development']()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('declare', 'run'))
    args = parser.parse_args()
    print(json.dumps({'declare': declare, 'run': execute}[args.mode](), indent=2))
