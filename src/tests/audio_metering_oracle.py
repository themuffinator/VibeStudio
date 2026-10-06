"""Optional independent FFmpeg and NumPy checks of original generated PCM.

No device or external media is used. FFmpeg is a separate test tool, not a
VibeStudio runtime dependency. Pass a task-local output directory to retain
input WAVs, raw reports, tool identity, tolerances and exact binary hashes.
NumPy supplies long windowed-sinc reconstruction, independent of r8brain.
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import math
import os
import random
import re
import struct
import subprocess
import wave
import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--ffmpeg', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    binary, ffmpeg, output = args.binary.resolve(), args.ffmpeg.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env['QT_QPA_PLATFORM'] = 'offscreen'
    env['TMPDIR'] = env['TEMP'] = env['TMP'] = str(output)
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    version = subprocess.check_output([str(ffmpeg), '-version'], cwd=output, env=env, text=True).splitlines()[0]
    rng = random.Random(1770)
    cases = [
        ('mono-calibration', 48000, 1, 2, lambda frame, channel, rate: .1 * math.sin(2 * math.pi * 997 * frame / rate)),
        ('stereo-energy', 44100, 2, 2, lambda frame, channel, rate: .1 * math.sin(2 * math.pi * 997 * frame / rate)),
        ('intersample-headroom', 48000, 1, 2, lambda frame, channel, rate: 1.2 * math.sin(math.pi * frame / 2 + math.pi / 4)),
        ('relative-gate', 48000, 1, 9, lambda frame, channel, rate: (.001 if 3 * rate <= frame < 6 * rate else .1) * math.sin(2 * math.pi * 997 * frame / rate)),
        ('weighted-spectrum', 96000, 2, 2, lambda frame, channel, rate: .05 * math.sin(2 * math.pi * (120 if channel else 7000) * frame / rate)),
        ('noise-and-dc', 48000, 2, 2, lambda frame, channel, rate: .1 + rng.uniform(-.1, .1)),
        ('silence', 48000, 1, 1, lambda frame, channel, rate: 0),
    ]
    results = []
    for name, rate, channels, seconds, sample in cases:
        path = output / (name + '.wav')
        pcm = bytearray()
        for frame in range(rate * seconds):
            for channel in range(channels):
                value = round(sample(frame, channel, rate) * 32768)
                assert -32768 <= value <= 32767, 'Fixture must not clip at integer encoding.'
                pcm += struct.pack('<h', value)
        with wave.open(str(path), 'wb') as wav:
            wav.setnchannels(channels)
            wav.setsampwidth(2)
            wav.setframerate(rate)
            wav.writeframes(pcm)
        before = digest(path)
        reference = subprocess.run([str(ffmpeg), '-hide_banner', '-nostdin', '-i', str(path),
                                    '-af', 'ebur128=peak=true', '-f', 'null', '-'],
                                   cwd=output, env=env, text=True, capture_output=True, timeout=60)
        (output / (name + '-ffmpeg.txt')).write_text(reference.stderr, encoding='utf-8')
        reference.check_returncode()
        probe = subprocess.run([str(binary), '--settings-file', str(output / 'settings.ini'), '--cli',
                                'asset', 'audio-analyze', str(path), '--json'],
                               cwd=output, env=env, text=True, capture_output=True, timeout=60)
        (output / (name + '-vibestudio.json')).write_text(probe.stdout, encoding='utf-8')
        (output / (name + '-vibestudio-stderr.txt')).write_text(probe.stderr, encoding='utf-8')
        probe.check_returncode()
        report = json.loads(probe.stdout)['audioAnalysis']
        summary = reference.stderr.rsplit('Summary:', 1)[1]
        integrated = float(re.search(r'Integrated loudness:\s*I:\s*([-+\d.]+) LUFS', summary)[1])
        peak = float(re.search(r'True peak:\s*Peak:\s*([-+\d.inf]+) dBFS', summary)[1])
        actual_loudness = report['loudness']['integratedLufs']
        actual_peak = report['truePeakDbtp']
        # FFmpeg's 0.1 dB summary is a loudness oracle. Its true-peak filter
        # has different bandwidth and initial boundary handling. Retain that
        # comparison, but use an independent long sinc with zero boundaries
        # for the reconstruction check rather than tuning to FFmpeg's filter.
        reconstructed = None
        if name != 'silence':
            x = np.frombuffer(pcm, dtype='<i2').astype(float).reshape(-1, channels) / 32768
            maximum = float(abs(x).max())
            taps = np.arange(-512, 513)
            for phase in range(1, 16):
                coefficients = np.sinc(taps + phase / 16) * np.hanning(len(taps))
                for channel in range(channels):
                    maximum = max(maximum, float(abs(np.convolve(x[:, channel], coefficients, mode='full')).max()))
            reconstructed = 20 * math.log10(maximum)
        passed = (actual_loudness is None and actual_peak is None and report['loudness']['status'] == 'below-gate') if name == 'silence' else (
            actual_loudness is not None and actual_peak is not None and
            abs(actual_loudness - integrated) <= .12 and abs(actual_peak - reconstructed) <= .3)
        assert digest(path) == before, 'Analysis modified an input fixture.'
        results.append({'case': name, 'frames': rate * seconds, 'sampleRate': rate, 'channels': channels,
                        'fixtureSha256': before, 'ffmpegLufs': integrated, 'ffmpegDbtp': peak if math.isfinite(peak) else None,
                        'longSincDbtp': reconstructed,
                        'vibestudioLufs': actual_loudness, 'vibestudioDbtp': actual_peak, 'passed': passed})
        print(name + ': ' + ('pass' if passed else 'FAIL'), flush=True)
    record = {'verifiedAtUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'binary': str(binary), 'binarySha256': digest(binary), 'ffmpeg': version, 'ffmpegSha256': digest(ffmpeg),
              'numpyVersion': np.__version__, 'sincInputTaps': 1025, 'sincOversampling': 16,
              'scope': 'Independent FFmpeg loudness and long Hann-windowed sinc true-peak checks; original quantized tones, noise and silence. FFmpeg true peak is diagnostic only because its bandwidth and boundary handling differ. No device or external audio. Not a broadcast-certification suite.',
              'loudnessToleranceLu': .12, 'truePeakToleranceDb': .3, 'cases': results}
    (output / 'verification.json').write_text(json.dumps(record, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    return 0 if all(item['passed'] for item in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
