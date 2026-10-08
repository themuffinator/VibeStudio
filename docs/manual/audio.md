# Audio

The **Audio** page lists the sounds in a package, draws their waveforms and
plays them. The audio editor cleans up, converts and delivers game-ready sound
effects, and multitrack sessions arrange several sounds into one.

> [!NOTE]
> **Status: Partial.** Browsing, waveform editing, analysis and delivery in
> each game's format work and have automated tests, but have not been proven
> in real projects. Playback needs Qt Multimedia, which Linux release builds do
> not include yet, and recording has not been tested with real audio hardware.

## Browse sounds

Choose **Audio** on the rail, or press <kbd>Ctrl</kbd>+<kbd>5</kbd>
(<kbd>Cmd</kbd> on macOS). The **Sounds** list shows the audio in the open
package, so open a package or a folder first (see [Packages](packages.md));
**Go to File** (<kbd>Ctrl</kbd>+<kbd>P</kbd>) opens a loose sound's folder and
selects it. The filter takes words and terms such as `ext=wav size>1mb` or
`folder:sound/ambience`.

Select a sound to see its waveform and its **Format** tab: codec, channels,
sample rate, bit depth, bitrate and duration. **Metadata** shows the raw header
details. Compressed sounds show their header details in the browser; open them
with **Edit Sound** to see the decoded waveform.

| Format | Browse and play | Edit |
| --- | --- | --- |
| WAV: 8, 16, 24 or 32-bit PCM, 32 or 64-bit float | Yes | Yes |
| Doom digital sound lumps (DMX) | Yes | Yes |
| MP3, FLAC, Ogg Vorbis | Yes | Yes, decoded on import |
| Ogg Opus | Details; playback depends on your system | No |
| Doom PC speaker sounds, MUS, MIDI | No | No |

## Play sounds

Use **Play Sound** (<kbd>Space</kbd>), **Stop Sound** and **Loop Sound** above
the waveform, with **Volume** and a **Position** slider that also seeks within
compressed sounds.

Playback uses Qt Multimedia. Windows and macOS release builds include it; Linux
release builds do not yet, so the playback controls stay disabled there and the
**Volume** tooltip says the build has no audio playback. Everything else on
this page, including editing and export, works without it. If no output device
is available, VibeStudio reports a retryable error: choose an output in your
system's sound settings and press Play again.

## Edit a sound

1. Select a sound and choose **Edit Sound**, or choose **Open Audio…** to open a
   local WAV, DMX, MP3, FLAC or Ogg Vorbis file, a `.vsaudio` project, a
   `.vssession` session or a `.vstake` recording.
2. Drag across the waveform to select a range, or type exact start and end
   frames (the end is exclusive).
3. Apply edits, then save the project or export the result.

The editor works on a separate floating-point copy, so the source file or
package is never changed. **New…** (<kbd>Ctrl</kbd>+<kbd>N</kbd>) starts an
empty sound or a stretch of silence.

- Navigate: <kbd>Left</kbd> and <kbd>Right</kbd> move one frame and
  <kbd>Shift</kbd> extends the selection; **Zoom In**, **Zoom Out**,
  **Fit Sound**, **Fit Selection** and <kbd>Ctrl</kbd>+wheel zoom down to single
  samples.
- Edit: **Copy**, **Cut** and **Paste** move exact samples between audio
  editors (sample rate and channels must match), and **Trim**, **Delete**,
  **Silence**, **Fade In** and **Fade Out** work on the selection.
- **Effects**: **Insert Silence…**, **Mix Clipboard at Selection Start**,
  **Gain…** (-96 to +24 dB), **Normalize…** (to a peak, -1 dBFS by default),
  **Reverse**, **Convert to Mono**, **Convert Mono to Stereo**,
  **Invert Polarity**, **Remove DC Offset** and **Resample…**, which keeps
  pitch and duration.
- **Markers…** sets named cues and one loop; **Select Loop** selects it for
  playback.
- **Play Selection**, or **Play From Cursor** when nothing is selected,
  auditions your edits when playback is available; <kbd>Space</kbd> does the
  same while the waveform has focus.

Undo and redo name each edit and keep up to 32 steps. Long operations show
their progress and can be cancelled; a cancelled or failed edit leaves the
sound as it was.

## Save, recover and export

- **Save Project** (<kbd>Ctrl</kbd>+<kbd>S</kbd>) writes a `.vsaudio` project
  that keeps the exact samples, markers and selection; **Save As…** writes a
  copy.
- With **Keep local recovery copies** ticked, unsaved work is checkpointed in
  the background. **Recoveries…** in the editor, or **File** >
  **Recover Audio…**, restores a copy as an unsaved draft. Settings >
  **Getting Started** > **Audio Recovery** controls checkpoints and the offer
  at start-up.
- **Export Audio…** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>E</kbd>) writes WAV
  as 8, 16, 24 or 32-bit PCM or 32-bit float, with optional dither for integer
  output. Exporting does not mark the project as saved.
- **Export WAV** on the Audio page writes the selected package sound as a
  separate 16-bit WAV.

Export and staging also offer game presets, which mix to mono and resample a
delivery copy while your project keeps its original samples:

| Preset | Output |
| --- | --- |
| Doom | DMX lump, 11025 Hz, 8-bit mono |
| Quake | WAV, 11025 Hz, 8-bit mono |
| Quake II, Quake III | WAV, 22050 Hz, 16-bit mono |

Cues and loops are kept where the format can hold them; the export summary says
what was left out. MP3, FLAC and Ogg Vorbis output are not offered.

## Put a sound in a package or map

- **Stage Sound** adds the result to the open package's staged changes: a WAV
  in a folder, PAK, ZIP or PK3, or a DMX lump in a Doom WAD with a sound name
  such as `DSDOOR` and no extension. Review and save it from **Packages**.
- **Stage & Place in Level…**, with a Quake II or Quake III map open in
  **Levels** and a folder, PAK, ZIP or PK3 package open, converts the sound to
  22050 Hz 16-bit mono, stages it, and adds an undoable `target_speaker` after
  you review its position, looping and target name.

Staged sounds play and open from the Audio page before you save. Save the map
and the package separately (see [Level editing](levels.md) and
[Packages](packages.md)).

## Analyse levels and loudness

**Analyze…** measures the selection, or the whole sound when nothing is
selected, without changing it: sample peak, RMS, DC offset and full-scale
counts per channel, plus true peak and integrated loudness. Untick
**Measure loudness** to skip loudness. The figures describe the sound before
any game conversion, so analyse an exported file to check what the game will
play.

## Multitrack sessions and recording

**Multitrack…** opens a `.vssession` session for arranging mono and stereo
clips on tracks, with gain, pan, mute and solo, fades, automation, buses and
sends, up to eight effects per track, **Tempo / Meter…**, **Range…** edits,
and mixdown or **Export Stems…**. **To Session** in the waveform editor brings
the current sound into a session. Sessions have undo, saves and recovery like
the editor.

A session offers two ways to record:

- **Single Take…** records one input to a `.vstake` file.
- **Record Tracks…** records up to eight armed tracks against the session's
  backing, with punch-in, repeated loop passes and a review step for picking
  takes.

Recording starts only when you press Record, and stopping keeps what was
captured. Single takes need Qt Multimedia, so they are not available in Linux
release builds; multitrack recording needs the build's native audio support,
and the dialog says when it is unavailable. On macOS, recording needs
microphone permission. Recording has not yet been tested with real audio
hardware, and MIDI, plugins and live automation recording are planned.

## Generate sound effects (optional)

**Generate** on the page header opens the Sound Generator. Choose
**The synthesizer, on this machine**, which needs no AI and makes the same sound
for the same description and seed, or **The sound model** (ElevenLabs or a
custom endpoint) after you turn AI on. Variants are trimmed, faded, normalised
and delivered in the game's format; play them, then choose **Save to Project**,
**Save and Place in Map** or **Open in Audio Editor**. See
[AI assistant](ai.md#generate-levels-textures-and-sounds).

## Command-line equivalents

```sh
vibestudio --cli asset audio-wav ./pak0.pk3 sound/items/pickup.wav --output ./pickup.wav --dry-run
vibestudio --cli asset audio-edit ./sound.wav --operation normalize --db -1 --output ./normalized.wav --dry-run --json
vibestudio --cli asset audio-export ./sound.vsaudio --preset doom --output ./DSWIND.dmx --dry-run --json
vibestudio --cli asset audio-analyze ./sound.vsaudio --json
```

<details>
<summary>All audio commands</summary>

| Task | Command |
| --- | --- |
| Export a package sound as WAV | `asset audio-wav` |
| Inspect, import or convert to a `.vsaudio` project | `asset audio-project` |
| Create an empty or silent sound | `asset audio-new` |
| Edit samples | `asset audio-edit` |
| Deliver with a game preset | `asset audio-export` |
| Measure peaks and loudness | `asset audio-analyze` |
| Read or replace cues and loops | `asset audio-markers` |
| Work with multitrack sessions | `asset audio-session` |
| Review recordings | `asset audio-take`, `asset audio-recording` |
| Review or discard recovery copies | `asset audio-recoveries` |
| Generate a sound effect | `asset audio-generate` |
| Place a Quake II or III speaker | `map place-sound` |

</details>

These commands never open an audio device. See [Command line](cli.md) for
options and exit codes.

## Learn more

- [Audio editor reference](../AUDIO_EDITOR.md): formats, limits, sessions, effects and delivery.
- [Recording and review](../AUDIO_DUPLEX.md): how multitrack recording works and its platform limits.
- [AI automation](../AI_AUTOMATION.md#generating-sounds): how the Sound Generator works.
