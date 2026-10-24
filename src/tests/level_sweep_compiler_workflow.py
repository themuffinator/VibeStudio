"""Original sweep fixtures -> VibeMap3 collision/render geometry -> verified PK3.

Runs without a game, user input or screen capture. Outputs stay in .agents/tmp.
The cap oracle uses closed-form coordinates independent of VibeStudio geometry.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct
import subprocess
import zipfile

from level_compiler_test_helpers import box, png


def expected_caps(mode):
    """Four stations from the +X face centred at (0, 0, 48)."""
    caps = [[(0, y, z) for y in (-16, 16) for z in (32, 64)]]
    for iteration in range(2 if mode == 'repeated' else 1):
        for step in range(1, 5):
            t = step / 4
            angle = 0 if mode == 'straight' else math.pi * t / 2
            c, s = math.cos(angle), math.sin(angle)
            if mode == 'arc':
                centre = (64 * s, 64 * (1 - c), 48 + 32 * t)
            elif mode == 's-bend':
                t2, t3 = t*t, t*t*t
                length = 96  # length of the (64, 64, 32) chord
                centre = (length*(t3-2*t2+t) + 64*(-2*t3+3*t2),
                          64*(-2*t3+3*t2) + length*(t3-t2), 48+32*(-2*t3+3*t2))
            else:
                centre = ((96 if mode == 'straight' else 64)*t, 0, 48)
            cap = []
            for y in (-16, 16):
                for z in (-16, 16):
                    if mode == 'compound':
                        # Rx(90), then a world Rz(90), is a 120-degree turn
                        # about (1,1,1). Rodrigues' formula is independent of
                        # the native handle quaternion and sweep code.
                        c, s = math.cos(2*math.pi*t/3), math.sin(2*math.pi*t/3)
                        along = (y+z)*(1-c)/3
                        k = 1+.5*t
                        cap.append((64*t+k*(along+(z-y)*s/math.sqrt(3)),
                                    64*t+k*(y*c+along-z*s/math.sqrt(3)),
                                    48+64*t+k*(z*c+along+y*s/math.sqrt(3))))
                        continue
                    scaled_y = y * (1 + (.5 if mode == 'straight' else -.5 if mode == 'repeated' else 0)*t)
                    scaled_z = z * (1 - .25*t if mode == 'straight' else 1)
                    p = (centre[0]-s*scaled_y, centre[1]+c*scaled_y, centre[2]+scaled_z)
                    if iteration:
                        # A second application of destination Rz(90) * S(1,.5,1),
                        # then (64,0,0): order matters and translation turns too.
                        p = (64-.5*p[1], p[0], p[2])
                    cap.append(p)
            caps.append(cap)
    return caps


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    root = args.output_root.resolve()
    assert root.is_relative_to((repo / '.agents/tmp').resolve())
    base = root / 'game/baseq3'
    maps = base / 'maps'
    maps.mkdir(parents=True, exist_ok=True)
    (root / 'home').mkdir(exist_ok=True)
    steps, variants = [], []

    def run(label, words, cli=True):
        command = ([str(args.binary.resolve()), '--cli', '--json', '--settings-file', str(root / 'settings.ini')]
                   + list(map(str, words)) if cli else list(map(str, words)))
        result = subprocess.run(command, cwd=repo, capture_output=True, timeout=180)
        (root / f'{label}.stdout.txt').write_bytes(result.stdout)
        (root / f'{label}.stderr.txt').write_bytes(result.stderr)
        steps.append({'step': label, 'command': command, 'exitCode': result.returncode})
        (root / 'steps.json').write_text(json.dumps(steps, indent=2), encoding='utf-8')
        assert result.returncode == 0, (label, result.stdout, result.stderr)
        print(label, 'passed', flush=True)
        return json.loads(result.stdout) if cli else result.stdout.decode('utf-8', errors='replace')

    room = [((-272,-272,-16),(272,272,0)), ((-272,-272,256),(272,272,272)),
            ((-272,-272,0),(-256,272,256)), ((256,-272,0),(272,272,256)),
            ((-256,-272,0),(256,-256,256)), ((-256,256,0),(256,272,256))]
    for name in ('wall', 'source', 'swept'):
        png(base / f'textures/sweep/{name}.png')
    prefix = [args.compiler.resolve(), '-game', 'quake3', '-fs_basepath', root / 'game',
              '-fs_homepath', root / 'home', '-fs_game', 'baseq3', '-threads', '2']
    for kind in ('classic', 'valve', 'primitive'):
        source = maps / f'{kind}-source.map'
        source.write_text('{\n"classname" "worldspawn"\n'
                          + '\n'.join(box(lo,hi,kind,'sweep/wall') for lo,hi in room)
                          + '\n' + box((-32,-16,32),(0,16,64),kind,'sweep/source')
                          + '\n}\n{\n"classname" "info_player_deathmatch"\n"origin" "0 -160 64"\n}\n'
                          + '{\n"classname" "light"\n"origin" "0 0 224"\n"light" "600"\n}\n', encoding='utf-8')
        source_bytes = source.read_bytes()
        original = run(f'{kind}-source', ['map','brush-components',source,'--brush','6'])['brush']
        vertices = {v['id']: tuple(v[a] for a in ('x','y','z')) for v in original['vertices']}
        face = next(f['id'] for f in original['faces'] if all(vertices[i][0] == 0 for i in f['vertices']))
        for mode in ('straight', 'arc', 's-bend', 'repeated', 'compound'):
            label = f'{kind}-{mode}'
            edited = maps / f'{label}.map'
            caps = expected_caps(mode)
            options = (['--delta','96,0,0','--scale','1,1.5,.75'] if mode == 'straight' else
                       ['--delta','64,64,64','--scale','1.5,1.5,1.5','--axis','1,1,1','--angle','120'] if mode == 'compound' else
                       ['--delta','64,0,0','--scale','1,.5,1','--angle','90','--iterations','2'] if mode == 'repeated' else
                       ['--delta','64,64,32','--angle','90'])
            report = run(label+'-sweep', ['map','sweep',source,'--set',f'brush:6/face/{face}',
                         '--texture','sweep/swept','--segments','4','--alignment','free',
                         '--path','straight' if mode in ('repeated','compound') else mode,'--output',edited,'--overwrite'] + options)
            assert report['sourcesRetained'] and not report['skipped'] and report['created'] == len(caps)-1
            retained = run(label+'-retained', ['map','brush-components',edited,'--brush','6'])['brush']
            assert retained == original and source.read_bytes() == source_bytes
            for stage, options in [('bsp',['-meta']), ('vis',['-vis','-fast']), ('light',['-light','-fast'])]:
                log = run(label+'-'+stage, prefix+options+[edited], cli=False)
                assert 'LEAKED' not in log and 'ERROR:' not in log, log
            bsp = edited.with_suffix('.bsp').read_bytes()
            assert bsp[:8] == b'IBSP' + struct.pack('<i',46)

            def lump(index):
                offset, size = struct.unpack_from('<2i',bsp,8+8*index)
                assert offset >= 0 and size >= 0 and offset+size <= len(bsp)
                return bsp[offset:offset+size]

            shaders = [lump(1)[i:i+64].split(b'\0',1)[0].decode() for i in range(0,len(lump(1)),72)]
            boundaries = []
            for at in range(0,len(lump(8)),12):
                first, count, shader = struct.unpack_from('<3i',lump(8),at)
                if shaders[shader] == 'textures/sweep/swept':
                    boundaries.append([struct.unpack_from('<4f',lump(2),struct.unpack_from('<i',lump(9),i*8)[0]*16)
                                       for i in range(first,first+count)])
            assert len(boundaries) == len(caps)-1, 'each authored section must reach collision geometry'
            remaining = list(boundaries)
            for first, last in zip(caps,caps[1:]):
                matching = [boundary for boundary in remaining
                            if all(sum(p[a]*v[a] for a in range(3))-p[3] < .04 for p in boundary for v in first+last)]
                assert len(matching) == 1, ('analytic section collision hull',label,first,last)
                remaining.remove(matching[0])
            compiled = []
            for at in range(0,len(lump(13)),104):
                header = struct.unpack_from('<12i',lump(13),at)
                if shaders[header[0]] == 'textures/sweep/swept':
                    for index in range(header[3],header[3]+header[4]):
                        vertex = struct.unpack_from('<10f4B',lump(10),index*44)
                        assert all(math.isfinite(v) for v in vertex[:10])
                        compiled.append(vertex[:3])
            # The repeated turn overlaps its inner two destination corners
            # with an earlier section. CSG removes those internal vertices;
            # the two outer corners remain visible. Collision checks above
            # independently require all four corners of every station.
            visible_corners = caps[-1][:2] if mode == 'repeated' else caps[-1]
            for cap_corner in visible_corners:
                assert any(max(abs(a-b) for a,b in zip(cap_corner,v)) < .04 for v in compiled), ('destination render corner',cap_corner)
            assets, package = root / f'{label}-assets.pk3', root / f'{label}-level.pk3'
            run(label+'-dependencies', ['package','subset',base,assets,'--map-input',edited,'--engine','idTech3','--overwrite'])
            run(label+'-package', ['package','save-as',assets,package,'--format','pk3','--add-file',edited,'--as',f'maps/{label}.map',
                                  '--add-file',edited.with_suffix('.bsp'),'--as',f'maps/{label}.bsp','--overwrite'])
            validation = run(label+'-validate', ['package','validate',package])['validation']
            assert validation['valid'] and validation['uncheckedCount'] == 0
            with zipfile.ZipFile(package) as archive:
                assert archive.testzip() is None
                assert archive.read(f'maps/{label}.map') == edited.read_bytes() and archive.read(f'maps/{label}.bsp') == bsp
                for name in ('wall','source','swept'):
                    path = f'textures/sweep/{name}.png'
                    assert archive.read(path) == (base/path).read_bytes()
            variants.append({'dialect':kind,'path':mode,'sections':len(boundaries),'compiledVertexSamples':len(compiled),
                             'validation':validation,'mapSha256':hashlib.sha256(edited.read_bytes()).hexdigest(),
                             'bspSha256':hashlib.sha256(bsp).hexdigest()})
    (root/'verified.json').write_text(json.dumps({'stepsPassed':len(steps),'variants':variants,
        'applicationSha256':hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        'compiler':str(args.compiler.resolve()),'compilerSha256':hashlib.sha256(args.compiler.read_bytes()).hexdigest()},indent=2),encoding='utf-8')
    print(f'PASS: {len(variants)} sweep cases, independent collision/cap oracles, original source and verified PK3 payloads.',flush=True)


if __name__ == '__main__':
    main()
