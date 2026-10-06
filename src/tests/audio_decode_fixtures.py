"""Regenerate synthetic compressed fixtures and independent float oracles.

No game data or third-party media. The checked-in fixtures run without FFmpeg;
regeneration needs an explicitly selected FFmpeg with libmp3lame/libvorbis,
plus python-soundfile/libsndfile for sample-exact Vorbis container trimming.
"""
from pathlib import Path
import argparse
import array
import hashlib
import json
import math
import subprocess
import sys
import soundfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", required=True)
    args = parser.parse_args()
    folder = Path(__file__).resolve().parent / "fixtures" / "audio_decode"
    folder.mkdir(parents=True, exist_ok=True)
    cases = [
        ("flac-mono16", "flac", 1, 22050, ["-sample_fmt", "s16"]),
        ("flac-stereo24", "flac", 2, 48000, ["-sample_fmt", "s32"]),
        ("flac-surround24", "flac", 8, 44100, ["-sample_fmt", "s32"]),
        ("mp3-cbr", "mp3", 2, 44100, ["-b:a", "128k"]),
        ("mp3-vbr", "mp3", 2, 48000, ["-q:a", "2"]),
        ("mp3-mpeg2", "mp3", 1, 22050, ["-b:a", "48k"]),
        ("mp3-mpeg25", "mp3", 1, 8000, ["-b:a", "24k"]),
        ("mp3-no-xing", "mp3", 2, 44100, ["-b:a", "128k", "-write_xing", "0"]),
    ] + [(f"vorbis-{n}ch", "ogg", n, 48000, ["-q:a", "5"]) for n in [1, 2, 3, 4, 5, 6, 7, 8]]
    version = subprocess.check_output([args.ffmpeg, "-version"], text=True).splitlines()[0]
    manifest = {"source": "VibeStudio-generated synthetic tones and transient; no external media", "encoder": version, "oracles": {"mp3/flac": version, "vorbis": f"libsndfile {soundfile.__libsndfile_version__} via python-soundfile {soundfile.__version__}"}, "cases": []}
    for name, extension, channels, rate, options in cases:
        frames = 8192
        samples = array.array("f", (0.42 * math.sin(2 * math.pi * (237 + ch * 179) * i / rate)
                                  + 0.08 * math.cos(2 * math.pi * (43 + ch * 31) * i / rate)
                                  + (0.2 if i == 317 + ch * 11 else 0)
                                  for i in range(frames) for ch in range(channels)))
        if sys.byteorder != "little":
            samples.byteswap()
        output = folder / f"{name}.{extension}"
        codec = {"mp3": "libmp3lame", "flac": "flac", "ogg": "libvorbis"}[extension]
        base = [args.ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-y"]
        layout = {1: "mono", 2: "stereo", 3: "3.0", 4: "quad", 5: "5.0", 6: "5.1", 7: "6.1", 8: "7.1"}[channels]
        subprocess.run(base + ["-f", "f32le", "-ar", str(rate), "-ac", str(channels), "-channel_layout", layout, "-i", "pipe:0", "-c:a", codec, *options, "-map_metadata", "-1", str(output)], input=samples.tobytes(), check=True, cwd=folder)
        if extension == "ogg":
            decoded, decoded_rate = soundfile.read(output, dtype="float32", always_2d=True)
            assert decoded_rate == rate and decoded.shape == (frames, channels)
            # libsndfile returns Vorbis packet order. Label speakers from the
            # Xiph specification, then select them in WAVE/FLAC order. This
            # independent oracle does not consume VibeStudio's decoder output.
            speakers = {
                1: ["M"], 2: ["FL", "FR"], 3: ["FL", "FC", "FR"],
                4: ["FL", "FR", "BL", "BR"], 5: ["FL", "FC", "FR", "BL", "BR"],
                6: ["FL", "FC", "FR", "BL", "BR", "LFE"],
                7: ["FL", "FC", "FR", "SL", "SR", "BC", "LFE"],
                8: ["FL", "FC", "FR", "SL", "SR", "BL", "BR", "LFE"],
            }[channels]
            standard = [s for s in ["M", "FL", "FR", "FC", "LFE", "BL", "BR", "BC", "SL", "SR"] if s in speakers]
            decoded = decoded[:, [speakers.index(s) for s in standard]]
            expected = decoded.astype("<f4").tobytes()
        else:
            expected = subprocess.check_output(base + ["-i", str(output), "-f", "f32le", "-c:a", "pcm_f32le", "pipe:1"], cwd=folder)
        oracle = folder / f"{name}.f32"
        oracle.write_bytes(expected)
        manifest["cases"].append({"file": output.name, "oracle": oracle.name, "channels": channels, "sampleRate": rate, "frames": len(expected) // (channels * 4), "tolerance": 0 if extension == "flac" else 0.00002, "sha256": hashlib.sha256(output.read_bytes()).hexdigest(), "oracleSha256": hashlib.sha256(expected).hexdigest()})
    (folder / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {len(cases)} synthetic fixtures and independent decoded oracles in {folder}")


if __name__ == "__main__":
    main()
