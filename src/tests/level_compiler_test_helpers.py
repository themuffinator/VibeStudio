"""Original generated box/texture fixtures shared by compiler acceptance proofs."""
import math
import struct
import zlib


def png(path, width=64, height=64):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    raw = b''.join(b'\0' + b''.join(bytes((52, 179, 207) if (x//8+y//8) % 2 else (31, 40, 51)) for x in range(width)) for y in range(height))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', width, height, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))

def axes(axis):
    return ((1, 0, 0), (0, 1, 0)) if axis == 2 else (((0, 1, 0), (0, 0, 1)) if axis == 0 else ((1, 0, 0), (0, 0, 1)))

def primitive_basis(axis, sign):
    return [((0, sign, 0), (0, 0, -1)), ((-sign, 0, 0), (0, 0, -1)), ((0, 1, 0), (sign, 0, 0))][axis]

def dot(a, b):
    return sum(x*y for x, y in zip(a, b))

def box(lo, hi, kind, name, unique=False):
    lines = ['{'] + (['brushDef', '{'] if kind == 'primitive' else [])
    for axis in range(3):
        for positive in (False, True):
            sign = 1 if positive else -1
            p = list(lo)
            p[axis] = hi[axis] if positive else lo[axis]
            a, b = p.copy(), p.copy()
            first, second = (axis+1) % 3, (axis+2) % 3
            # .map planes use cross(p0-p1, p2-p1), opposite the usual
            # cross(p1-p0, p2-p0). Clockwise triples point outward here.
            if positive:
                first, second = second, first
            a[first] += 1
            b[second] += 1
            geometry = ' '.join('( ' + ' '.join(map(str, point)) + ' )' for point in (p, a, b))
            material = f'{name}/face{axis*2+int(positive)}' if unique else name
            if kind == 'primitive':
                parameters = f'( ( 0.03125 0.0078125 0.125 ) ( -0.015625 0.0625 -0.75 ) ) "{material}" 0 0 0'
            elif kind == 'valve':
                s, t = axes(axis)
                c, sn = math.cos(math.radians(15)), math.sin(math.radians(15))
                u = [(c*s[i]+sn*t[i]) / -0.5 for i in range(3)]
                v = [(sn*s[i]-c*t[i]) / 2 for i in range(3)]
                parameters = f'"{material}" [ {" ".join(map(str, u))} 7 ] [ {" ".join(map(str, v))} 9 ] 0 1 1 0 0 0'
            else:
                parameters = f'"{material}" 7 9 15 -0.5 2 0 0 0'
            lines.append(geometry + ' ' + parameters)
    if kind == 'primitive':
        lines.append('}')
    return '\n'.join(lines + ['}'])

