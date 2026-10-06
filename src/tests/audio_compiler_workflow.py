"""Optional Audio authoring, speaker, compiler and package acceptance.

Uses original generated samples and the existing sealed-room texture fixture.
Runs external compilers only; never launches a game, opens an audio device,
injects input, captures the desktop or reads commercial game data.
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import io
import json
import os
import platform
import re
import struct
import subprocess
import wave
import zipfile

from texture_compiler_workflow import lump, room


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pak_entries(data):
    assert data[:4] == b'PACK'
    offset, size = struct.unpack_from('<2i', data, 4)
    assert offset >= 12 and size % 64 == 0 and offset + size == len(data)
    result = {}
    for at in range(offset, offset + size, 64):
        name = data[at:at + 56].split(b'\0', 1)[0].decode('ascii')
        start, length = struct.unpack_from('<2i', data, at + 56)
        assert name not in result and 12 <= start <= start + length <= offset
        result[name] = data[start:start + length]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--qbsp', type=Path, required=True)
    parser.add_argument('--q3map2', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    if not __debug__:
        parser.error('Run without -O; assertions verify the outputs.')
    repo = Path(__file__).resolve().parents[2]
    output = args.output_root.resolve()
    if not output.is_relative_to(repo / '.agents' / 'tmp'):
        parser.error('Output root must be inside this repository/.agents/tmp.')
    if output.exists():
        parser.error('Choose a new output directory so earlier evidence is preserved.')
    binary, qbsp, q3map2 = (path.resolve(strict=True) for path in (args.binary, args.qbsp, args.q3map2))
    output.mkdir(parents=True)
    runtime = output / 'runtime'
    runtime.mkdir()
    env = dict(os.environ, TEMP=str(runtime), TMP=str(runtime), TMPDIR=str(runtime),
               PYTHONDONTWRITEBYTECODE='1')
    steps, checks = [], []
    executables = {str(path): digest(path) for path in (binary, qbsp, q3map2)}

    def run(label, words, cli=True):
        command = ([str(binary), '--settings-file', str(runtime / 'settings.ini'), '--cli'] + list(map(str, words)) + ['--json']
                   if cli else list(map(str, words)))
        result = subprocess.run(command, cwd=output, env=env, capture_output=True, timeout=120)
        (output / (label + '.stdout.txt')).write_bytes(result.stdout)
        (output / (label + '.stderr.txt')).write_bytes(result.stderr)
        steps.append({'step': label, 'command': command, 'exitCode': result.returncode})
        (output / 'steps.json').write_text(json.dumps(steps, indent=2) + '\n', encoding='utf-8')
        assert result.returncode == 0, (label, result.stdout.decode(errors='replace'), result.stderr.decode(errors='replace'))
        print(label + ': PASS', flush=True)
        return json.loads(result.stdout) if cli else result.stdout.decode(errors='replace') + result.stderr.decode(errors='replace')

    source = output / 'original.wav'
    rate, frames = 22050, 2205
    # Integer amplitudes make the gain and final PCM16 bytes independently exact.
    pattern = [0, 8192, -8192, 4096, -4096, 16384, -16384, 0]
    values = [pattern[index % len(pattern)] for index in range(frames)]
    with wave.open(str(source), 'wb') as wav:
        wav.setparams((1, 2, rate, 0, 'NONE', 'not compressed'))
        wav.writeframes(struct.pack('<' + 'h' * frames, *values))
    original_hash = digest(source)
    imported = output / 'imported.vsaudio'
    edited = output / 'edited.vsaudio'
    run('import-native', ['asset', 'audio-project', source, '--output', imported])
    imported_hash = digest(imported)
    run('edit-gain', ['asset', 'audio-edit', imported, '--operation', 'gain', '--db', '-6.020599913279624', '--output', edited])
    edited_hash = digest(edited)
    analysis = run('analyze-edited', ['asset', 'audio-analyze', edited])['audioAnalysis']
    assert (analysis['sampleRate'], analysis['channelCount'], analysis['frames'], analysis['peak']) == (rate, 1, frames, 0.25)
    expected_pcm = struct.pack('<' + 'h' * frames, *(value // 2 for value in values))
    texture = output / 'wall.vtexture'
    run('generate-wall', ['texture', 'create', '--size', '32x32', '--color', '#808080', '--output', texture])
    palette = output / 'palette.lmp'
    palette.write_bytes(bytes(value for index in range(256) for value in (index, index, index)))
    sound_path = 'sound/audiotest/edited.wav'
    for game, engine, base_name, archive_format, suffix, bsp_version in [
        ('quake2', 'idTech2', 'baseq2', 'pak', 'pak', 38),
        ('quake3', 'idTech3', 'baseq3', 'pk3', 'pk3', 46),
    ]:
        base = output / game / base_name
        maps = base / 'maps'
        maps.mkdir(parents=True)
        delivered = base / sound_path
        delivered.parent.mkdir(parents=True)
        delivery = ['asset', 'audio-export', edited, '--preset', game, '--output', delivered]
        dry = run(game + '-delivery-dry', delivery + ['--dry-run'])['audioExport']
        assert not delivered.exists() and not dry['written'] and dry['frames'] == frames
        run(game + '-delivery', delivery)
        wav_bytes = delivered.read_bytes()
        with wave.open(io.BytesIO(wav_bytes), 'rb') as wav:
            assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes()) == (1, 2, rate, frames)
            assert wav.readframes(frames) == expected_pcm
        assets = [(delivered, sound_path)]
        if game == 'quake2':
            for name in ['audiotest/wall', 'skip']:
                options = output / (name.replace('/', '-') + '.json')
                options.write_text(json.dumps({'name': name}), encoding='utf-8')
                wall = base / ('textures/' + name + '.wal')
                wall.parent.mkdir(parents=True, exist_ok=True)
                run(game + '-wall-' + name.replace('/', '-'), ['texture', 'export', texture, '--profile', 'quake2-wal',
                    '--palette-file', palette, '--export-options', options, '--output', wall])
                if name != 'skip':
                    assets.append((wall, 'textures/' + name + '.wal'))
        else:
            wall = base / 'textures/audiotest/wall.tga'
            wall.parent.mkdir(parents=True)
            run(game + '-wall', ['texture', 'export', texture, '--profile', 'tga', '--output', wall])
            scripts = base / 'scripts'
            scripts.mkdir()
            (scripts / 'shaderlist.txt').write_text('audiotest\n', encoding='utf-8')
            (scripts / 'audiotest.shader').write_text('textures/audiotest/wall\n{\n { map textures/audiotest/wall.tga }\n}\n', encoding='utf-8')
            assets += [(wall, 'textures/audiotest/wall.tga'), (scripts / 'shaderlist.txt', 'scripts/shaderlist.txt'),
                       (scripts / 'audiotest.shader', 'scripts/audiotest.shader')]
        draft = output / (game + '.vibepackage')
        create = ['package', 'create', draft, '--format', archive_format]
        for path, virtual in assets:
            create += ['--add-file', path, '--as', virtual]
        run(game + '-draft', create)
        untextured = maps / 'untextured.map'
        map_text = room()
        if game == 'quake3':
            # Use the same format marker as VibeStudio's new Quake III maps.
            map_text = '// Q3Radiant\n' + map_text.replace('info_player_start', 'info_player_deathmatch')
        untextured.write_text(map_text, encoding='utf-8')
        input_map = maps / 'source.map'
        run(game + '-map-material', ['map', 'apply-texture', untextured, '--object', 'entity:0', '--texture',
                                     'audiotest/wall', '--engine', engine, '--output', input_map])
        input_map_hash = digest(input_map)
        mapped = maps / 'audiotest.map'
        placement = ['map', 'place-sound', input_map, '--game', game, '--package', draft,
                     '--sound', sound_path, '--mode', 'loop-on', '--origin', '16,0,48', '--output', mapped]
        dry = run(game + '-placement-dry', placement + ['--dry-run'])['sound']
        reference = 'audiotest/edited.wav' if game == 'quake2' else sound_path
        assert not mapped.exists() and dry['reference'] == reference
        actual = run(game + '-placement', placement)['sound']
        assert actual == dry
        dependencies = run(game + '-dependencies', ['map', 'dependencies', mapped, '--package', draft, '--engine', engine])['dependencies']
        assert Path(dependencies['package']).resolve() == draft.resolve()
        if game == 'quake2':
            logs = [run(game + '-bsp', [qbsp, '-q2bsp', '-threads', '2', mapped], cli=False)]
        else:
            home = output / 'q3-home'
            home.mkdir()
            command = [q3map2, '-game', 'quake3', '-fs_basepath', output / game, '-fs_homepath', home,
                       '-fs_game', base_name, '-threads', '2']
            logs = [run(game + '-' + name, command + flags + [mapped], cli=False) for name, flags in
                    [('bsp', ['-meta']), ('vis', ['-vis', '-fast']), ('light', ['-light', '-fast'])]]
        assert all('LEAK' not in text.upper() and 'WARNING' not in text.upper() and 'ERROR:' not in text.upper() for text in logs), logs
        bsp_path = mapped.with_suffix('.bsp')
        bsp = bsp_path.read_bytes()
        assert bsp[:4] == b'IBSP' and struct.unpack_from('<i', bsp, 4)[0] == bsp_version
        entity_text = lump(bsp, 0).decode('ascii').rstrip('\0')
        entities = [dict(re.findall(r'"([^"\n]+)"\s+"([^"\n]*)"', text))
                    for text in re.findall(r'\{([^{}]*)\}', entity_text)]
        speakers = [entity for entity in entities if entity.get('classname') == 'target_speaker']
        assert len(speakers) == 1 and speakers[0]['noise'] == reference and speakers[0]['spawnflags'] == '1'
        assert tuple(map(float, speakers[0]['origin'].split())) == (16, 0, 48)
        final = output / (game + '-release.' + suffix)
        run(game + '-package', ['package', 'save-as', draft, final, '--format', archive_format,
                                '--add-file', bsp_path, '--as', 'maps/audiotest.bsp'])
        validation = run(game + '-validate', ['package', 'validate', final])['validation']
        assert validation['valid'] and validation['verifiedCount'] == len(assets) + 1
        if game == 'quake2':
            packaged = pak_entries(final.read_bytes())
        else:
            with zipfile.ZipFile(final) as archive:
                packaged = {entry.filename: archive.read(entry) for entry in archive.infolist() if not entry.is_dir()}
        assert set(packaged) == {virtual for _, virtual in assets} | {'maps/audiotest.bsp'}
        assert packaged[sound_path] == wav_bytes and packaged['maps/audiotest.bsp'] == bsp
        for path, virtual in assets:
            assert packaged[virtual] == path.read_bytes()
        packaged_analysis = run(game + '-packaged-analysis', ['asset', 'audio-analyze', final, '--entry', sound_path])['audioAnalysis']
        assert (packaged_analysis['frames'], packaged_analysis['sampleRate'], packaged_analysis['peak']) == (frames, rate, 0.25)
        assert digest(input_map) == input_map_hash
        checks.append({'game': game, 'bspVersion': bsp_version, 'speaker': speakers[0],
                       'verified': 'Exact edited PCM16, unpublished draft placement, compiler entity lump, dependency review, package bytes and reopened Audio analysis.',
                       'bspSha256': digest(bsp_path), 'soundSha256': digest(delivered), 'packageSha256': digest(final)})
    assert digest(source) == original_hash and digest(imported) == imported_hash and digest(edited) == edited_hash
    assert all(digest(Path(path)) == value for path, value in executables.items())
    report = {'verifiedAtUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'stepsPassed': len(steps), 'checks': checks, 'platform': platform.platform(),
              'executableHashes': executables, 'sourceFixtureSha256': original_hash,
              'importedProjectSha256': imported_hash, 'editedProjectSha256': edited_hash,
              'testSourceSha256': digest(Path(__file__)),
              'sharedFixtureSourceSha256': digest(Path(__file__).with_name('texture_compiler_workflow.py')),
              'scope': 'Generated-sample CLI and external compiler acceptance; no game runtime, physical playback, device, keyboard or screen-reader acceptance.'}
    (output / 'verified.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
