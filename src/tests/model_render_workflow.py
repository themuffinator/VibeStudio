"""Optional native model rendering through an isolated FTE EGL pbuffer.

Supply existing studio/FTE/FTEQCC executables and Pillow. Engine screenshot
commands read the render target; no display server, desktop capture or input
automation is used. All fixtures, caches and evidence stay in repository/.agents/tmp.
"""
from __future__ import annotations

import re
import struct

from PIL import Image

from model_render_common import RenderHarness, digest

PHASES = ('pose-0', 'pose-1', 'blend', 'group-0', 'group-1')
FORMATS = ('mdl', 'md2', 'md3')
COLOURS = ((220, 40, 30), (30, 210, 80), (35, 70, 230), (230, 210, 30))
IDENTITIES = [fmt + '-' + check for fmt in FORMATS for check in ('frame-count', 'frame-0-name', 'frame-1-name')]
IDENTITIES += ['group-frame-count', 'group-duration']


def inspect_picture(path, phase):
    """Project authored positions independently and inspect their texture quadrants."""
    with Image.open(path) as image:
        assert image.size == (640, 480), image.size
        pixels = image.convert('RGB')
    results = []
    for index, fmt in enumerate(FORMATS):
        depth = 80 if phase == 'pose-1' or (phase == 'group-1' and fmt == 'mdl') else 88 if phase == 'blend' else 96
        factor = 320 / depth  # 640px / (2 * distance * tan(90 degrees / 2)).
        centre = 320 + (index - 1) * 48 * factor
        expected = [centre - 16 * factor, 240 - 16 * factor, centre + 16 * factor, 240 + 16 * factor]
        points = [(x, y) for y in range(80, 400) for x in range(index * 640 // 3, (index + 1) * 640 // 3)
                  if max(pixels.getpixel((x, y))) - min(pixels.getpixel((x, y))) > 40]
        failures = []
        observed = None
        samples = []
        if not points:
            failures.append('model-not-rendered')
        else:
            observed = [min(x for x, y in points), min(y for x, y in points),
                        max(x for x, y in points) + 1, max(y for x, y in points) + 1]
            if any(abs(a - b) > 2 for a, b in zip(observed, expected)):
                failures.append('pose-projection')
            area = (expected[2] - expected[0]) * (expected[3] - expected[1])
            if abs(len(points) / area - 1) > .05:
                failures.append('face-coverage')
            for corner, (u, v) in enumerate(((.25, .25), (.75, .25), (.25, .75), (.75, .75))):
                x = round(expected[0] * (1 - u) + expected[2] * u)
                y = round(expected[1] * (1 - v) + expected[3] * v)
                colour = pixels.getpixel((x, y))
                samples.append({'pixel': [x, y], 'rgb': colour, 'expectedRgb': COLOURS[corner]})
                if max(abs(a - b) for a, b in zip(colour, COLOURS[corner])) > 12:
                    failures.append('texture-corner-' + str(corner))
        results.append({'check': phase + '-' + fmt, 'passed': not failures, 'failures': failures,
                        'expectedBounds': expected, 'observedBounds': observed, 'colouredPixels': len(points), 'samples': samples})
    return results


def main():
    harness = RenderHarness(__file__, 'model_render_fixture.qc', __doc__)
    root, assets, cli = harness.root, harness.assets, harness.cli
    progs = assets / 'progs'
    progs.mkdir(parents=True)
    (assets / 'gfx').mkdir()
    palette = bytearray(value for i in range(256) for value in (i, i, i))
    for slot, colour in zip((40, 80, 144, 192), COLOURS):
        palette[slot * 3:slot * 3 + 3] = bytes(colour)
    palette_path = assets / 'gfx/palette.lmp'
    palette_path.write_bytes(palette)
    texture = Image.new('RGB', (32, 32))
    texture.putdata([COLOURS[(y >= 16) * 2 + (x >= 16)] for y in range(32) for x in range(32)])
    # Quake GL skin loading flood-fills the first pixel's connected colour.
    # A black index-0 guard keeps that engine preprocessing out of used quadrants.
    texture.putpixel((0, 0), (0, 0, 0))
    texture_path = root / 'quadrants.png'
    texture.save(texture_path)
    skin = assets / 'models/native/skin.pcx'
    skin.parent.mkdir(parents=True)
    cli('skin-export', ['texture', 'export', texture_path, '--profile', 'pcx', '--palette-file', palette_path, '--output', skin])
    obj = root / 'panel.obj'
    obj.write_text('v -16 -16 0\nv 16 -16 0\nv 16 16 0\nv -16 16 0\n'
                   'vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3\nf 1/1 3/3 4/4\n', encoding='ascii')
    mesh = root / 'panel.mesh.json'
    cli('model-import', ['model', 'import', obj, '--output', mesh])

    def edit(label, *words):
        cli(label, ['model', 'edit', mesh, *words, '--output', mesh, '--overwrite'])

    edit('frame-0', '--operation', 'rename-frame', '--frame', '0', '--name', 'rest')
    edit('frame-1', '--operation', 'duplicate-frame', '--frame', '0', '--name', 'lift')
    edit('lift-pose', '--operation', 'transform', '--frame', '1', '--vertices', 'all', '--offset', '0,0,16')
    edit('material', '--operation', 'material', '--faces', 'all', '--material', 'models/native/skin.pcx')
    edit('md2-size', '--operation', 'md2-skin-size', '--skin-size', '32,32')
    mdl = root / 'mdl.mesh.json'
    cli('mdl-skin', ['model', 'mdl', mesh, '--operation', 'add-skin', '--image', skin, '--output', mdl])
    for fmt in FORMATS:
        cli('export-' + fmt, ['model', 'build', mdl if fmt == 'mdl' else mesh, '--output', progs / ('panel_' + fmt + '.' + fmt)])
    grouped = root / 'grouped.mesh.json'
    cli('mdl-group', ['model', 'mdl', mdl, '--operation', 'group', '--first-frame', '0', '--last-frame', '1',
                     '--duration', '0.2', '--output', grouped])
    cli('export-group', ['model', 'build', grouped, '--output', progs / 'grouped.mdl'])
    harness.compile_fixture()
    cases = []

    def execute(name, mutation=None, expected=frozenset(), reasons=()):
        base, log, hashes = harness.render_case(name, mutation)
        identities = re.findall(r'^VSMODEL RENDER PASS (.+)$', log, re.MULTILINE)
        assert identities == IDENTITIES and 'VSMODEL RENDER FAIL ' not in log, (name, identities)
        assert re.findall(r'^VSMODEL RENDER CAPTURE (.+)$', log, re.MULTILINE) == list(PHASES)
        assert log.count('VSMODEL RENDER COMPLETE') == 1
        checks = []
        for phase in PHASES:
            checks.extend(inspect_picture(base / 'renders' / (phase + '.png'), phase))
        failures = {check['check'] for check in checks if not check['passed']}
        observation = {'case': name, 'checks': checks, 'identityChecksPassed': identities,
                       'inputHashes': hashes, 'deliberateFailures': sorted(expected),
                       'expectedFailureReasons': reasons,
                       'failed': sorted(failures), 'screenshotHashes': {
                           phase: digest(base / 'renders' / (phase + '.png')) for phase in PHASES}}
        cases.append(observation)
        harness.record('cases.json', cases)
        assert failures == set(expected), (name, sorted(failures))
        assert all(check['passed'] or tuple(check['failures']) == reasons for check in checks), (name, checks)

    execute('positive')

    def reverse_md2(base):
        path = base / 'progs/panel_md2.md2'
        data = bytearray(path.read_bytes())
        count = struct.unpack_from('<i', data, 32)[0]
        triangles, commands = struct.unpack_from('<i', data, 52)[0], struct.unpack_from('<i', data, 60)[0]
        for index in range(count):
            face = triangles + index * 12
            for start in (face, face + 6):
                data[start + 2:start + 4], data[start + 4:start + 6] = data[start + 4:start + 6], data[start + 2:start + 4]
            command = commands + index * 40
            assert struct.unpack_from('<i', data, command)[0] == 3
            data[command + 16:command + 28], data[command + 28:command + 40] = data[command + 28:command + 40], data[command + 16:command + 28]
        path.write_bytes(data)

    def flip_md3_uv(base):
        path = base / 'progs/panel_md3.md3'
        data = bytearray(path.read_bytes())
        surface = struct.unpack_from('<i', data, 100)[0]
        count = struct.unpack_from('<i', data, surface + 80)[0]
        st = surface + struct.unpack_from('<i', data, surface + 96)[0]
        for vertex in range(count):
            at = st + vertex * 8 + 4
            struct.pack_into('<f', data, at, 1 - struct.unpack_from('<f', data, at)[0])
        path.write_bytes(data)

    execute('negative-md2-winding', reverse_md2, {phase + '-md2' for phase in PHASES}, ('model-not-rendered',))
    execute('negative-md3-uv', flip_md3_uv, {phase + '-md3' for phase in PHASES},
            tuple('texture-corner-' + str(corner) for corner in range(4)))

    def flood_mdl_skin(base):
        for name in ('panel_mdl.mdl', 'grouped.mdl'):
            path = base / 'progs' / name
            data = bytearray(path.read_bytes())
            assert data[:4] == b'IDPO' and struct.unpack_from('<i', data, 84)[0] == 0
            assert data[88] == 0 and data[89] == 40
            data[88] = 40  # Connect the top-left used quadrant to the flood-fill seed.
            path.write_bytes(data)

    execute('negative-mdl-flood-fill', flood_mdl_skin, {phase + '-mdl' for phase in PHASES},
            ('face-coverage', 'texture-corner-0'))
    harness.finish('FTE EGL/llvmpipe model rendering through MenuQC: native faces, UV/material colours, '
                   'two poses, midpoint blend and discrete grouped MDL samples. No original-engine gameplay, '
                   'physical input, desktop capture or packaged release acceptance.', cases)
    print('Completed four offscreen render cases, five engine screenshots each; all three negative controls detected.', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
