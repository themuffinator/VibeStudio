# Compressed audio fixtures

These files contain VibeStudio-generated tones and a distinct transient per
channel. They contain no game assets or third-party media. `manifest.json`
records every input and reference hash, stream format, decoded length, numerical
tolerance, and the tool versions used to generate the references.

The 16 cases cover 16/24-bit native FLAC, MPEG-1/2/2.5 Layer III, CBR/VBR MP3
with and without Xing timing, and Vorbis with one through eight channels.
The `.f32` files are little-endian interleaved float32 reference samples in
standard WAVE/FLAC speaker order. Each channel has different content so channel
exchanges cannot pass the sample comparison.

Regeneration needs an explicitly selected FFmpeg with `libmp3lame` and
`libvorbis`, plus Python `soundfile`/libsndfile and NumPy. From the repository:

```sh
python src/tests/audio_decode_fixtures.py --ffmpeg /absolute/path/to/ffmpeg
```

Normal Meson tests consume these checked-in files without those tools. FFmpeg
7.1 supplies MP3/FLAC reference decoding. Vorbis references use libsndfile 1.2.2
because the selected FFmpeg build trims the short synthetic Ogg streams
differently. The generator labels libsndfile's Vorbis channel order and reorders
it by speaker name independently of the application's decoder.

`audio-decode-smoke` compares every sample and exercises truncation, corruption,
allocation exhaustion at several budgets, exact output limits, cancellation,
concurrent decoder regions, native persistence, CLI commands and package export.
`audio-import-ui-smoke` checks asynchronous imports, current-document retention,
warning persistence and the browser's WAV export through direct Qt calls.
