"""Optional independent engine acceptance of CLI-authored baked assemblies.

Uses supplied studio/FTE/FTEQCC executables and Pillow. Original fixtures cover
nested animated attachments, per-part playback, linked loose/package skins,
flattened MD2 and multi-surface MD3. FTE screenshots come from its headless EGL
render target, never the desktop.
"""
from __future__ import annotations

import json
import math
from pathlib import Path
import re
import struct

from PIL import Image

from model_render_common import RenderHarness, digest

PARTS = ('root', 'child', 'grand')
FORMATS = ('md2', 'md3')
PHASES = tuple('pose-' + str(i) for i in range(5)) + ('blend-0-1', 'blend-1-2')
COLOURS = ((220, 40, 30), (30, 210, 80), (35, 70, 230), (230, 210, 30))
HALF_SIZES = ((8, 5), (6, 4), (5, 3))
LIFTS = (8, 6, 4)
# OBJ flat faces retain independent corner vertices. MD2 compacts coincident
# positions across every pose but keeps the corresponding independent UVs.
CORNER_ORDER = (0, 1, 2, 0, 2, 3)
IDENTITIES = [fmt + '-' + name for fmt in FORMATS for name in ('frame-count', 'first-frame', 'last-frame')]


def plus(a, b):
    return tuple(x + y for x, y in zip(a, b))


def times(a, factor):
    return tuple(x * factor for x in a)


def turn(a, degrees):
    angle = math.radians(degrees)
    c, s = math.cos(angle), math.sin(angle)
    return (c * a[0] - s * a[1], s * a[0] + c * a[1], a[2])


def expected_pose(seconds):
    """Closed-form geometry for this fixture, independent of studio decoders.

    Root loops 0->1->0; child clamps after frame 1; grandchild holds stored
    frames. All tag rotations share Z, so spherical interpolation is simply
    their signed angle. Scales accumulate as 1.25, 1 and .75.
    """
    a = 1 - abs(seconds % 2 - 1)
    b = min(seconds * .5 + .25, 1)
    c = math.floor((seconds + .25) % 2)
    root = (0, -34, 0)
    child = plus(root, times(turn(plus((4 * a, 26, 4 * a), turn((3, 0, 0), 30 * a)), 10), 1.25))
    grand = plus(child, turn(plus((0, 24 + 4 * b, 2 * b), turn((2, 0, 0), -20 * b)), 30 * a))
    tilt = math.radians(25)
    result = []
    for i, (origin, angle, scale, amount) in enumerate(zip(
            (root, child, grand), (10, 30 * a, 30 * a - 20 * b - 15), (1.25, 1, .75), (a, b, c))):
        w, h = HALF_SIZES[i]
        corners = [plus(origin, times(turn((x, y * math.cos(tilt), y * math.sin(tilt) + LIFTS[i] * amount), angle), scale))
                   for x, y in ((-w, -h), (w, -h), (w, h), (-w, h))]
        result.append({'corners': corners, 'normal': turn((0, -math.sin(tilt), math.cos(tilt)), angle)})
    return result


def phase_pose(phase):
    if phase.startswith('pose-'):
        return expected_pose(.5 + int(phase[-1]) / 2)
    first = int(phase[-3])
    a, b = expected_pose(.5 + first / 2), expected_pose(1 + first / 2)
    # Engine interpolation blends baked vertices, not the assembly's tag curve.
    return [{'corners': [times(plus(x, y), .5) for x, y in zip(p['corners'], q['corners'])]}
            for p, q in zip(a, b)]


def project(point, column):
    factor = 320 / (140 - point[2])
    return (320 + (point[0] + (-60 if column == 0 else 60)) * factor, 240 - point[1] * factor)


def components(image, column):
    pixels = image.load()
    # All authored texels (including filtered quadrant boundaries) stay bright
    # against the fixed dark background. Saturation alone would create false
    # holes where differently coloured texels filter to a neutral shade.
    pending = {(x, y) for y in range(480) for x in range(column * 320, (column + 1) * 320)
               if max(pixels[x, y]) > 50}
    result = []
    while pending:
        point = pending.pop()
        found, queue = {point}, [point]
        while queue:
            x, y = queue.pop()
            for neighbour in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if neighbour in pending:
                    pending.remove(neighbour)
                    found.add(neighbour)
                    queue.append(neighbour)
        result.append(found)
    return sorted(result, key=lambda found: sum(y for x, y in found) / len(found), reverse=True)


def edge_clearance(polygon, point):
    """Signed distance to the closest edge of this convex projected panel."""
    pairs = tuple(zip(polygon, polygon[1:] + polygon[:1]))
    sign = 1 if sum(a[0] * b[1] - b[0] * a[1] for a, b in pairs) > 0 else -1
    x, y = point
    return min(sign * ((b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0])) /
               math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in pairs)


def inspect_picture(path, phase):
    with Image.open(path) as source:
        assert source.size == (640, 480), source.size
        image = source.convert('RGB')
    expected = phase_pose(phase)
    checks = []
    for column, fmt in enumerate(FORMATS):
        observed = components(image, column)
        assert len(observed) == 3, (phase, fmt, 'expected three disconnected panels', [len(p) for p in observed])
        for part, geometry, points in zip(PARTS, expected, observed):
            polygon = [project(p, column) for p in geometry['corners']]
            bounds = [min(x for x, y in polygon), min(y for x, y in polygon),
                      max(x for x, y in polygon), max(y for x, y in polygon)]
            actual = [min(x for x, y in points), min(y for x, y in points),
                      max(x for x, y in points) + 1, max(y for x, y in points) + 1]
            clearances = {(x, y): edge_clearance(polygon, (x + .5, y + .5))
                          for y in range(max(0, math.floor(bounds[1])), min(480, math.ceil(bounds[3]) + 1))
                          for x in range(max(0, math.floor(bounds[0])), min(640, math.ceil(bounds[2]) + 1))}
            raster = {p for p, distance in clearances.items() if distance >= 0}
            # Sample pixel centres; an integer-filled polygon biases very small
            # panels. Permit a one-pixel border for native quantization and the
            # renderer's edge rule, while requiring the interior and exterior.
            interior = {p for p, distance in clearances.items() if distance >= 1}
            coverage = len(interior & points) / len(interior)
            outside = sum(edge_clearance(polygon, (x + .5, y + .5)) < -1 for x, y in points)
            overlap = len(raster & points) / len(raster | points)
            failures = []
            if any(abs(x - y) > 2 for x, y in zip(bounds, actual)) or coverage < .99 or outside:
                failures.append('pose-projection')
            samples = []
            bl, br, tr, tl = geometry['corners']
            for corner, (u, v) in enumerate(((.25, .25), (.75, .25), (.25, .75), (.75, .75))):
                point = plus(times(plus(times(tl, 1 - u), times(tr, u)), 1 - v),
                             times(plus(times(bl, 1 - u), times(br, u)), v))
                x, y = map(round, project(point, column))
                colour = image.getpixel((x, y))
                samples.append({'pixel': [x, y], 'rgb': colour, 'expectedRgb': COLOURS[corner]})
                if max(abs(a - b) for a, b in zip(colour, COLOURS[corner])) > 12:
                    failures.append('texture-corner-' + str(corner))
            checks.append({'check': phase + '-' + fmt + '-' + part, 'passed': not failures, 'failures': failures,
                           'expectedPolygon': polygon, 'expectedBounds': bounds, 'observedBounds': actual,
                           'silhouetteIntersectionOverUnion': overlap, 'interiorCoverage': coverage,
                           'pixelsBeyondOnePixelBorder': outside, 'samples': samples})
    return checks


def md3_surfaces(data):
    assert data[:4] == b'IDP3' and struct.unpack_from('<i', data, 4)[0] == 15
    assert struct.unpack_from('<3i', data, 76) == (5, 0, 3)
    at = struct.unpack_from('<i', data, 100)[0]
    result = []
    for part in PARTS:
        assert data[at:at + 4] == b'IDP3'
        assert data[at + 4:at + 68].split(b'\0')[0].decode('ascii') == part + '_s0'
        assert struct.unpack_from('<4i', data, at + 72) == (5, 1, 6, 2)
        result.append(at)
        at += struct.unpack_from('<i', data, at + 104)[0]
    assert at == len(data)
    return result


def check_native(progs):
    """Independent offsets from id's qfiles.h; see docs/CREDITS.md.

    Never calls VibeStudio's importer or uses its emitted geometry as expected
    data. MD3 normals use the format's latitude/longitude encoding.
    """
    md3 = (progs / 'assembly_md3.md3').read_bytes()
    surfaces = md3_surfaces(md3)
    md2 = (progs / 'assembly_md2.md2').read_bytes()
    assert md2[:4] == b'IDP2' and struct.unpack_from('<i', md2, 4)[0] == 8
    assert struct.unpack_from('<2i', md2, 8) == (32, 32)
    assert struct.unpack_from('<3i', md2, 20) == (1, 12, 18)
    assert struct.unpack_from('<i', md2, 32)[0] == 6 and struct.unpack_from('<i', md2, 40)[0] == 5
    size, frames = struct.unpack_from('<i', md2, 16)[0], struct.unpack_from('<i', md2, 56)[0]
    skin_offset = struct.unpack_from('<i', md2, 44)[0]
    assert md2[skin_offset:skin_offset + 64].split(b'\0')[0] == b'models/assembly/skin.pcx'
    for surface in surfaces:
        shader = surface + struct.unpack_from('<i', md3, surface + 92)[0]
        assert md3[shader:shader + 64].split(b'\0')[0] == b'models/assembly/skin.pcx'
    results = []
    for frame in range(5):
        expected = expected_pose(.5 + frame / 2)
        md3_frame = struct.unpack_from('<i', md3, 92)[0] + frame * 56
        assert md3[md3_frame + 40:md3_frame + 56].split(b'\0')[0].decode() == f'bake{frame:04d}'
        md2_at = frames + frame * size
        scales = struct.unpack_from('<3f', md2, md2_at)
        shifts = struct.unpack_from('<3f', md2, md2_at + 12)
        assert md2[md2_at + 24:md2_at + 40].split(b'\0')[0].decode() == f'bake{frame:04d}'
        for part, surface in enumerate(surfaces):
            geometry = surface + struct.unpack_from('<i', md3, surface + 100)[0] + frame * 6 * 8
            position_error, normal_error, md2_error = 0, 0, 0
            for vertex, corner in enumerate(CORNER_ORDER):
                want = expected[part]['corners'][corner]
                point = tuple(x / 64 for x in struct.unpack_from('<3h', md3, geometry + vertex * 8))
                position_error = max(position_error, *(abs(x - y) for x, y in zip(point, want)))
                normal = struct.unpack_from('<H', md3, geometry + vertex * 8 + 6)[0]
                lat, lng = (normal >> 8) * math.tau / 255, (normal & 255) * math.tau / 255
                decoded = (math.cos(lat) * math.sin(lng), math.sin(lat) * math.sin(lng), math.cos(lng))
                normal_error = max(normal_error, *(abs(x - y) for x, y in zip(decoded, expected[part]['normal'])))
                packed = md2[md2_at + 40 + (part * 4 + corner) * 4:md2_at + 43 + (part * 4 + corner) * 4]
                point2 = tuple(v * scale + shift for v, scale, shift in zip(packed, scales, shifts))
                md2_error = max(md2_error, *(abs(x - y) for x, y in zip(point2, want)))
            assert position_error <= 1 / 128 + 1e-4 and normal_error < .035, (frame, part, position_error, normal_error)
            assert md2_error <= max(scales) / 2 + 1e-4, (frame, part, md2_error, scales)
            results.append({'frame': frame, 'part': PARTS[part], 'md3MaxPositionError': position_error,
                            'md3MaxNormalComponentError': normal_error, 'md2MaxPositionError': md2_error,
                            'md2PositionTolerance': max(scales) / 2 + 1e-4})
    return results


def main():
    harness = RenderHarness(__file__, 'model_assembly_render_fixture.qc', __doc__)
    root, assets, cli = harness.root, harness.assets, harness.cli
    progs = assets / 'progs'
    progs.mkdir()
    (assets / 'gfx').mkdir()
    palette = bytearray(value for i in range(256) for value in (i, i, i))
    for slot, colour in zip((40, 80, 144, 192), COLOURS):
        palette[slot * 3:slot * 3 + 3] = bytes(colour)
    palette_path = assets / 'gfx/palette.lmp'
    palette_path.write_bytes(palette)
    texture = Image.new('RGB', (32, 32))
    texture.putdata([COLOURS[(y >= 16) * 2 + (x >= 16)] for y in range(32) for x in range(32)])
    png = root / 'quadrants.png'
    texture.save(png)
    skin = assets / 'models/assembly/skin.pcx'
    skin.parent.mkdir(parents=True)
    cli('skin-export', ['texture', 'export', png, '--profile', 'pcx', '--palette-file', palette_path, '--output', skin])

    meshes, skin_inputs = [], []
    skin_package = root / 'skin-package'
    skin_package.mkdir()
    for i, part in enumerate(PARTS):
        w, h = HALF_SIZES[i]
        obj = root / (part + '.obj')
        obj.write_text(f'v {-w} {-h} 0\nv {w} {-h} 0\nv {w} {h} 0\nv {-w} {h} 0\n'
                       'vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3\nf 1/1 3/3 4/4\n', encoding='ascii')
        mesh = root / (part + '.mesh.json')
        meshes.append(mesh)
        cli(part + '-import', ['model', 'import', obj, '--output', mesh])

        def edit(label, *words):
            return cli(part + '-' + label, ['model', 'edit', mesh, *words, '--output', mesh, '--overwrite'])

        edit('tilt', '--operation', 'transform', '--vertices', 'all', '--rotate', '25,0,0')
        edit('duplicate', '--operation', 'duplicate-frame', '--frame', '0', '--name', 'raised')
        edit('lift', '--operation', 'transform', '--frame', '1', '--vertices', 'all', '--offset', f'0,0,{LIFTS[i]}')
        # Deliberately unusable source material: the accepted texture must come
        # from the linked skin, while independent input hashes prove no rewrite.
        edit('material', '--operation', 'material', '--faces', 'all', '--material', 'models/assembly/unassigned.pcx')
        if part != 'grand':
            first, second, angle = ('0,26,0', '4,26,4', '30') if part == 'root' else ('0,24,0', '0,28,2', '-20')
            edit('tag', '--operation', 'add-tag', '--name', 'tag_link', '--tag-origin', first)
            edit('tag-move', '--operation', 'set-tag-origin', '--tag', 'tag_link', '--frame', '1', '--tag-origin', second)
            edit('tag-turn', '--operation', 'transform-tag', '--tag', 'tag_link', '--frame', '1', '--rotate', '0,0,' + angle,
                 '--pivot', second)
        surface = json.loads(mesh.read_text(encoding='utf-8'))['surfaces'][0]['name'].lower()
        if len(surface) > 2 and surface[-2] == '_':
            surface = surface[:-2]
        binding = (skin_package if part == 'child' else root) / (part + '.skin')
        binding.write_text(surface + ',models/assembly/skin.pcx\ntag_link,\n', encoding='ascii')
        skin_inputs.append(binding)

    source_hashes = {str(path): digest(path) for path in [*meshes, *skin_inputs]}
    recipe = root / 'chain.assembly.json'
    cli('assembly-root', ['model', 'assembly', '--new', '--name', 'Original animated chain', '--part', 'root',
                         '--model', meshes[0], '--fps', '1', '--translation', '0,-34,0', '--rotation', '0,0,10',
                         '--scale', '1.25', '--loop', 'on', '--interpolate', 'on', '--skin', skin_inputs[0], '--output', recipe])
    cli('assembly-child', ['model', 'assembly', recipe, '--operation', 'add', '--part', 'child', '--model', meshes[1],
                          '--parent', 'root', '--tag', 'tag_link', '--fps', '.5', '--phase', '.25', '--loop', 'off',
                          '--interpolate', 'on', '--translation', '3,0,0', '--rotation', '0,0,-10', '--scale', '.8',
                          '--skin', 'child.skin', '--skin-kind', 'package', '--skin-entry-index', '0', '--package', skin_package,
                          '--output', recipe, '--overwrite'])
    cli('assembly-grand', ['model', 'assembly', recipe, '--operation', 'add', '--part', 'grand', '--model', meshes[2],
                          '--parent', 'child', '--tag', 'tag_link', '--fps', '1', '--phase', '.25', '--loop', 'on',
                          '--interpolate', 'off', '--translation', '2,0,0', '--rotation', '0,0,-15', '--scale', '.75',
                          '--skin', skin_inputs[2], '--package', skin_package, '--output', recipe, '--overwrite'])
    assert json.loads(recipe.read_text(encoding='utf-8'))['version'] == 3
    assert all(digest(Path(path)) == value for path, value in source_hashes.items()), 'Linking changed model or skin inputs'
    protected = {str(recipe): digest(recipe), **source_hashes}
    baked = root / 'baked.mesh.json'
    common = ['model', 'assembly', recipe, '--operation', 'bake-animation', '--time', '.5',
              '--frames', '5', '--sample-fps', '2', '--clip-name', 'chain', '--package', skin_package]
    source_report = cli('bake-source', [*common, '--output', baked])
    native_report = cli('bake-md3', [*common, '--output', progs / 'assembly_md3.md3'])
    for report in (source_report, native_report):
        assert report['written'] and report['surfaces'] == 3 and report['vertices'] == 18
        assert report['sampling'] == {'clipName': 'chain', 'durationSeconds': 2.5, 'frameCount': 5,
                                      'framesPerSecond': 2, 'lastSampleSeconds': 2.5, 'startSeconds': .5}
        assert {item['source']: item['sha256'] for item in report['inputs']} == {
            path.as_posix(): protected[str(path)] for path in meshes}
        for item, part, skin_input in zip(report['inputs'], PARTS, skin_inputs):
            link = item['skin']
            assert link['sha256'] == protected[str(skin_input)] and link['bytes'] == skin_input.stat().st_size
            assert link['entryIndex'] == (0 if part == 'child' else -1)
            assert link['kind'] == ('package' if part == 'child' else 'file')
            assignment, = link['bindings']['assignments']
            assert assignment['previousMaterial'] == 'models/assembly/unassigned.pcx'
            assert assignment['material'] == 'models/assembly/skin.pcx'
    source = json.loads(baked.read_text(encoding='utf-8'))
    assert source['version'] == 6 and source['tags'] == []
    assert source['animations'] == [{'count': 5, 'first': 0, 'framesPerSecond': 2, 'name': 'chain'}]
    joined = root / 'joined.mesh.json'
    cli('join-for-md2', ['model', 'surfaces', baked, '--operation', 'join', '--surfaces', '0,1,2',
                         '--target-surface', '0', '--output', joined])
    cli('md2-size', ['model', 'edit', joined, '--operation', 'md2-skin-size', '--skin-size', '32,32',
                     '--output', joined, '--overwrite'])
    cli('export-md2', ['model', 'build', joined, '--output', progs / 'assembly_md2.md2'])
    byte_checks = check_native(progs)
    harness.record('native-oracle.json', byte_checks)
    harness.record('bake-reports.json', {'source': source_report, 'native': native_report})
    harness.compile_fixture()
    cases = []

    def execute(name, mutation=None, expected=frozenset(), required=()):
        base, log, hashes = harness.render_case(name, mutation)
        identities = re.findall(r'^VSASSEMBLY RENDER PASS (.+)$', log, re.MULTILINE)
        assert identities == IDENTITIES and 'VSASSEMBLY RENDER FAIL ' not in log, (name, identities)
        assert re.findall(r'^VSASSEMBLY RENDER CAPTURE (.+)$', log, re.MULTILINE) == list(PHASES)
        assert log.count('VSASSEMBLY RENDER COMPLETE') == 1
        checks = [check for phase in PHASES for check in inspect_picture(base / 'renders' / (phase + '.png'), phase)]
        failures = {check['check'] for check in checks if not check['passed']}
        cases.append({'case': name, 'checks': checks, 'identityChecksPassed': identities, 'inputHashes': hashes,
                      'deliberateFailures': sorted(expected), 'requiredFailureReasons': required,
                      'failed': sorted(failures), 'screenshotHashes': {
                          phase: digest(base / 'renders' / (phase + '.png')) for phase in PHASES}})
        harness.record('cases.json', cases)
        assert failures == set(expected), (name, sorted(failures), sorted(expected))
        assert all(check['passed'] or set(required).issubset(check['failures']) for check in checks), (name, checks)

    execute('positive')

    def stale_md2(base):
        path = base / 'progs/assembly_md2.md2'
        data = bytearray(path.read_bytes())
        size, frames = struct.unpack_from('<i', data, 16)[0], struct.unpack_from('<i', data, 56)[0]
        target, other = frames + size, frames + 3 * size
        data[target:target + 24] = data[other:other + 24]
        data[target + 40:target + size] = data[other + 40:other + size]
        path.write_bytes(data)

    def offset_grand(base):
        path = base / 'progs/assembly_md3.md3'
        data = bytearray(path.read_bytes())
        surface = md3_surfaces(data)[2]
        geometry = surface + struct.unpack_from('<i', data, surface + 100)[0]
        for vertex in range(5 * 6):
            at = geometry + vertex * 8
            struct.pack_into('<h', data, at, struct.unpack_from('<h', data, at)[0] + 14 * 64)
        path.write_bytes(data)

    def flip_child_uv(base):
        path = base / 'progs/assembly_md3.md3'
        data = bytearray(path.read_bytes())
        surface = md3_surfaces(data)[1]
        st = surface + struct.unpack_from('<i', data, surface + 96)[0]
        for vertex in range(6):
            at = st + vertex * 8 + 4
            struct.pack_into('<f', data, at, 1 - struct.unpack_from('<f', data, at)[0])
        path.write_bytes(data)

    execute('negative-stale-md2-pose', stale_md2,
            {phase + '-md2-' + part for phase in ('pose-1', 'blend-0-1', 'blend-1-2') for part in PARTS}, ('pose-projection',))
    execute('negative-grand-offset', offset_grand, {phase + '-md3-grand' for phase in PHASES}, ('pose-projection',))
    execute('negative-child-uv', flip_child_uv, {phase + '-md3-child' for phase in PHASES},
            tuple('texture-corner-' + str(i) for i in range(4)))
    assert all(digest(Path(path)) == value for path, value in protected.items()), 'Assembly inputs changed'
    harness.finish('Five CLI-baked poses plus two native blends of a generated three-part chain in FTE EGL/llvmpipe. '
                   'Loose and exact-package linked skins override deliberately unusable source materials without rewriting inputs. '
                   'Independent MD2/MD3 shader/geometry byte and projected silhouette/texture checks; three deliberate controls. '
                   'No live engine attachment, original-game gameplay, physical input, desktop capture or release-package acceptance.',
                   cases, nativeChecks=byte_checks, protectedSourceHashes=protected)
    print('Completed baked-assembly rendering: four cases, seven engine screenshots each.', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
