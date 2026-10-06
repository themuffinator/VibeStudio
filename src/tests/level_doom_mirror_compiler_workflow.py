"""Generated Doom/Hexen reflection -> registered ZDBSP -> package validation proof.

No game launch or native input. Output stays under this project's .agents/tmp.
Checks compiler seg orientation against independent sector-interior rectangles.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess


def wad(lumps):
    payload = bytearray(b'PWAD' + bytes(8))
    directory = bytearray()
    for name, data in lumps:
        directory += struct.pack('<ii8s', len(payload), len(data), name.encode('ascii'))
        payload += data
    struct.pack_into('<ii', payload, 4, len(lumps), len(payload))
    return bytes(payload + directory)


def lumps(data):
    count, directory = struct.unpack_from('<ii', data, 4)
    result = []
    for i in range(count):
        offset, size, name = struct.unpack_from('<ii8s', data, directory + i * 16)
        assert 0 <= offset <= len(data) and 0 <= size <= len(data) - offset
        result.append((name.rstrip(b'\0').decode('ascii'), data[offset:offset + size]))
    return result


def fixture(hexen):
    points = [(0, 0), (0, 128), (128, 128), (128, 0), (256, 128), (256, 0),
              (512, 0), (512, 64), (608, 64), (608, 0)]
    edges = [(0, 1), (1, 2), (2, 3), (3, 0), (2, 4), (4, 5), (5, 3), (6, 7), (7, 8), (8, 9), (9, 6)]
    vertices = b''.join(struct.pack('<hh', *p) for p in points)
    lines = b''
    for i, edge in enumerate(edges):
        flags, back = (20, 11) if i == 2 else (9, 65535)
        lines += (struct.pack('<HHH6BHH', *edge, flags, 80, *range(11+i, 16+i), i, back) if hexen else
                  struct.pack('<7H', *edge, flags, 31, 17+i, i, back))
    sides = b''.join(struct.pack('<hh8s8s8sH', 3+i, -5-i, b'STONE', b'STONE', b'-' if i in (2, 11) else b'STONE',
                                1 if i == 11 else 0 if i < 4 else 1 if i < 7 else 2) for i in range(12))
    sectors = b''.join(struct.pack('<hh8s8sHHH', i*16, 128-i*16, b'STONE', b'CEIL', 176+i*16, i, 7+i) for i in range(3))
    things = (struct.pack('<HhhhHHH6B', 42, 40, 24, 8, 30, 1, 0x707, 80, *range(21, 26)) if hexen else
              struct.pack('<hhHHH', 40, 24, 30, 1, 7))
    group = [('MAP01', b''), ('THINGS', things), ('LINEDEFS', lines), ('SIDEDEFS', sides), ('VERTEXES', vertices),
             ('SEGS', b''), ('SSECTORS', b''), ('NODES', b''), ('SECTORS', sectors), ('REJECT', b''), ('BLOCKMAP', b'')]
    if hexen:
        group += [('BEHAVIOR', bytes.fromhex('41435300080000000000000000000000')), ('SCRIPTS', b'// generated fixture\n')]
    return wad(group + [('USERDATA', b'first'), ('USERDATA', b'second')] + [('MAP02', b'')] + group[1:])


def node_oracle(data, hexen, axis):
    # Only the first map: keep occurrence identity instead of collapsing the WAD.
    records = []
    for name, payload in lumps(data):
        if name == 'MAP02':
            break
        records.append((name, payload))
    records = dict(records)
    vertices = list(struct.iter_unpack('<hh', records['VERTEXES']))
    stride = 16 if hexen else 14
    lines = [records['LINEDEFS'][i:i+stride] for i in range(0, len(records['LINEDEFS']), stride)]
    sides = [struct.unpack_from('<H', records['SIDEDEFS'], i+28)[0] for i in range(0, len(records['SIDEDEFS']), 30)]
    rects = [(0, 128, 0, 128), (128, 256, 0, 128), (512, 608, 0, 64)]
    if axis == 'x':
        rects[0], rects[1] = rects[1], rects[0]
    segs = list(struct.iter_unpack('<6H', records['SEGS']))
    assert segs and records['NODES'] and len(records['NODES']) % 28 == 0
    seen = set()
    for start, end, angle, line_id, direction, offset in segs:
        assert direction in (0, 1) and line_id < len(lines)
        line = lines[line_id]
        ls, le = struct.unpack_from('<HH', line)
        side = struct.unpack_from('<H', line, (12 if hexen else 10) + 2*direction)[0]
        assert side < len(sides)
        if direction:
            ls, le = le, ls
        x, y = vertices[start]
        xx, yy = vertices[end]
        a, b = vertices[ls], vertices[le]
        dx, dy = xx-x, yy-y
        assert dx*(b[0]-a[0]) + dy*(b[1]-a[1]) > 0, 'seg follows its native side'
        length = (dx*dx + dy*dy)**0.5
        px, py = (x+xx)/2 + dy/length*0.25, (y+yy)/2 - dx/length*0.25
        left, right, bottom, top = rects[sides[side]]
        assert left < px < right and bottom < py < top, (line_id, direction, sides[side], px, py)
        seen.add((line_id, direction))
    assert {(i, 0) for i in range(11)} | {(2, 1)} == seen
    subsectors = list(struct.iter_unpack('<HH', records['SSECTORS']))
    assert sum(count for count, _ in subsectors) == len(segs)
    for count, first in subsectors:
        assert count and first+count <= len(segs)
    node_count = len(records['NODES']) // 28
    for at in range(0, len(records['NODES']), 28):
        for child in struct.unpack_from('<HH', records['NODES'], at+24):
            assert (child & 32767) < (len(subsectors) if child & 32768 else node_count)
    return {'segs': len(segs), 'subsectors': len(subsectors), 'nodes': node_count, 'orientedSides': len(seen)}


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

    def run(label, words):
        command = [str(binary), '--cli', '--json', '--settings-file', str(root/'settings.ini')] + list(map(str, words))
        p = subprocess.run(command, cwd=repo, capture_output=True, timeout=120)
        (root/f'{label}.stdout.json').write_bytes(p.stdout)
        (root/f'{label}.stderr.txt').write_bytes(p.stderr)
        steps.append({'step': label, 'command': command, 'exitCode': p.returncode})
        (root/'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert p.returncode == 0, (label, p.stdout, p.stderr)
        print(label, 'passed', flush=True)
        return json.loads(p.stdout)

    def build(label, source, output):
        return run(label, ['compiler', 'run', 'zdbsp-nodes', '--input', source, '--output', output,
                           '--extra-args', '-m MAP01 -q -R -w', '--working-directory', root,
                           '--manifest', root/f'{label}.manifest.json'])

    run('register', ['compiler', 'set-path', 'zdbsp', '--executable', compiler])
    for hexen in (False, True):
        name = 'hexen' if hexen else 'doom'
        source, compiled = root/f'{name}.wad', root/f'{name}-source-nodes.wad'
        source.write_bytes(fixture(hexen))
        build(f'{name}-source', source, compiled)
        baseline = compiled.read_bytes()
        node_oracle(baseline, hexen, '')
        old = lumps(baseline)
        for axis in ('x', 'y'):
            label = f'{name}-{axis}'
            flipped, output = root/f'{label}.wad', root/f'{label}-nodes.wad'
            flip = run(label+'-flip', ['map', 'flip', compiled, '--map', 'MAP01', '--object', 'linedef:0', '--object', 'thing:0',
                                     '--connected', '--axis', axis, '--output', flipped])
            edited = lumps(flipped.read_bytes())
            assert [n for n, _ in old] == [n for n, _ in edited]
            foreign = False
            for (n, a), (_, b) in zip(old, edited):
                foreign |= n in ('USERDATA', 'MAP02')
                if foreign or n in ('SIDEDEFS', 'SECTORS', 'BEHAVIOR', 'SCRIPTS'):
                    assert a == b, (label, 'retained payload', n)
            assert {'SEGS', 'SSECTORS', 'NODES', 'BLOCKMAP', 'REJECT'} <= set(flip['save']['staleLumps'])
            for node_lump in ('SEGS', 'SSECTORS', 'NODES', 'BLOCKMAP', 'REJECT'):
                assert not dict(edited[:11])[node_lump], 'obsolete runtime records cleared on save'
            build(label+'-build', flipped, output)
            oracle = node_oracle(output.read_bytes(), hexen, axis)
            run(label+'-package', ['package', 'validate', output])
            new = lumps(output.read_bytes())
            a_at = next(i for i, (n, _) in enumerate(old) if n == 'USERDATA')
            b_at = next(i for i, (n, _) in enumerate(new) if n == 'USERDATA')
            assert old[a_at:] == new[b_at:], 'node compilation keeps unrelated WAD entries'
            assert compiled.read_bytes() == baseline, 'source WAD unchanged'
            variants.append({'variant': label, **oracle, 'sha256': hashlib.sha256(output.read_bytes()).hexdigest()})
    report = {'verified': True, 'steps': len(steps), 'variants': variants,
              'binarySha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
              'compilerSha256': hashlib.sha256(compiler.read_bytes()).hexdigest()}
    (root/'verified.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
