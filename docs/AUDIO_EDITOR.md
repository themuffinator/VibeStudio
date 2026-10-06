# Audio Editor

On **Audio**, choose a package sound and **Edit Sound**, or use **Open Audio…**
for a local file. The editor keeps a separate floating-point copy of the samples.
Opening a sound, editing it, and staging it never modify the source file or archive.
Release acceptance is tracked in the [audio editor audit](plans/audio-editor-release-candidate.md);
the broader [DAW development plan](plans/audio-daw.md) is still in progress.

**Audio → Generate** makes new sound effects from a description, with the
built-in synthesizer or your sound model, and **Open in Audio Editor** brings
one here to continue by hand. See Generating Sounds in
[AI Automation](AI_AUTOMATION.md#generating-sounds) and
`asset audio-generate` in the CLI section below.

## Multitrack Sessions

Choose **Audio → Multitrack…** to arrange sounds, or **To Session** in the waveform
editor to import its current sample snapshot. **Import…** accepts the existing
audio formats and `.vsaudio` documents. Sessions accept mono/stereo sources;
convert larger channel layouts explicitly in the waveform editor first. A rate
mismatch needs explicit resampling of the imported copy. The original remains
unchanged. **New** chooses a sample rate and starts at 120 BPM; CLI creation also
accepts `--tempo`. An empty session opened through **To Session** adopts the first
sound's rate.

**Tempo / Meter…** stages the session's tempo and time-signature maps. In Tempo,
enter a `bar.beat.tick` position and BPM, then **Set Tempo** to add or replace a
change. The origin is `1.1.0`; select a row to inspect it or remove a later change.
In Meter, **Set Meter** adds/replaces the signature at a bar start. The origin at
bar 1 is editable but cannot be removed. **OK** applies the staged map as one
undoable edit and stops playback; Cancel leaves the session unchanged. Audio
clips, fades and automation retain their authored sample positions.

Choose **Bars and beats** in Ruler to show musical positions and tempo/meter
changes. The Position field accepts `bar.beat.tick`; **Go** or Enter seeks to the
corresponding sample and brings it into view. Bars and beats are one-based, ticks
zero-based, with **960 ticks per quarter note**. BPM always counts quarter notes;
the beat in the position and snap controls follows the signature's denominator.
For example, 7/8 has seven 480-tick beats; at 120 BPM its bars last 1.75 seconds.
Snap offers bar, beat, half-beat, quarter-beat and beat-triplet grids in addition
to frame/millisecond steps. Drag snapping chooses the closest grid frame, ties
later; Left/Right moves to the strictly previous/next grid frame. Integer-frame
controls remain exact. A ruler spanning many bars thins its labels without
changing the grid used for edits.

Tempo changes are step changes at quarter-note tick positions. Meter changes
are anchored to bar numbers, so editing an earlier signature changes the later
bars' tick positions; tempo changes retain their ticks. There are up to 4,096
changes of each kind, BPM 20–400, 1–32 beats per bar and denominators
1/2/4/8/16/32. All change positions must fit the session's frame limit. Conversion
integrates fractional frames and rounds the final absolute sample, including at
tempo boundaries. At very low sample rates several musical ticks can share a
frame; frame-to-position reports the nearest tick and need not recover the
original tick. Tempo ramps, musical anchoring/time-stretch of clips, metronome,
MIDI and external synchronization remain open.

The timing map stays in the editable session and its recovery copies. Playback,
mixdown, stems and waveform handoff retain the authored sample ranges; WAV/game
delivery does not embed a tempo map or invent musical markers. Existing waveform
analysis, package staging and level placement therefore receive the same samples.
Use the CLI `position` command to resolve musical locations into exact frame
bounds for those workflows. Tempo-map interchange is still a delivery gap.

Select clips across tracks in the native tree using Control/Command or Shift,
or add/toggle timeline clips with those modifiers. The selection count and
**Edit targets** count distinguish explicit clips from linked group members.
**Link grouped clips** is enabled initially. The tree's Group column exposes
saved names; groups need at least two members. Selection itself does not dirty
the session.

**Selection…** stages Move, Duplicate, Split, gain, fades, mute, Group / rename,
Ungroup or Remove. Move/duplicate use a signed frame delta, preserving relative
spacing and each clip's track. Values apply to every effective target; an invalid
member rejects the whole edit. Gain is absolute clip dB, not a relative trim.
Group creates a named link, merges selected groups when linking is on, or
renames an exactly selected existing group without changing its identity.
Ungroup/removal releases any remaining singleton. Copies have independent group
identities and share immutable media; a copied single group member is ungrouped.
Changing clips leaves track/effect automation at its authored sample positions.

Dragging or Left/Right moves the effective selection by the anchor clip's snap
delta. **Duplicate** appends a copy after the selection's complete time span;
**Selection… → Duplicate** allows a specific offset, including overlaps.
**Split at Cursor** cuts selected clips that strictly cross the cursor; other
selected clips stay intact. Right-hand pieces form independent groups when two
or more linked clips are cut. Every cut preserves the audible fade, even inside
overlapping fade-in/out ranges or after repeated splits. **Remove** removes
selected clips; with only a track selected it removes that track. Source
snapshots remain available in the session.

**Clip…** inspects only the focused clip, independently of group links, with
exact position, source offset, length, gain, mute and fade lengths. End frames
are exclusive. Trim/slip/move change descriptors; removed portions remain in
the embedded source. Split pieces display their inherited fade segment.
Changing either fade length or choosing **Reset inherited fade segment** starts
a fresh envelope over the clip's visible length; those fade lengths must fit.
**Selection… → Set fades** explicitly resets envelopes for every target.
Overlapping clips sum, including clips on one track. All arrangement changes
use one undo transaction, stop playback, and persist through native saves and
recovery. Waveform handoff, mixdown, stems, game delivery and package/level
placement use the same rendered fade envelopes; groups remain editing metadata
in the native session. Musical anchoring remains open.

### Managing session media

Choose **Media…** to inspect embedded sources, channels, frame counts, clip
usage and external file references. Selecting a source shows the tracks that
use it and the highest required source frame. A missing or unreadable reference
does not interrupt playback: the session contains its own audio snapshot.
Availability is a point-in-time filesystem check, not a content comparison.

Select an operation and choose **Review**, then **Apply**. File reads, decoding,
validation and waveform preparation run on a cancellable worker. Relink and
Replace show current/proposed waveform tabs, resulting format, affected clip
count and the input file's SHA-256. Changing any field invalidates the review.
Applying rechecks the reviewed file's content and resolved path; a changed file
requires another review. Cancel leaves the session unchanged.

| Operation | Behavior |
| --- | --- |
| Rename source | Change its shared label; existing clip names stay unchanged. |
| Relink identical audio | Change the file reference only. Rate, channel count and every decoded sample bit must match; embedded markers and metadata remain intact. |
| Replace audio in all clips | Adopt reviewed audio under a new source identity and retarget every referencing clip, including muted clips. Preserve clip IDs, timing, source offsets, groups, fades and automation. |
| Remove selected unused sources | Remove selected embedded snapshots that have no clip references. Control/Command and Shift select multiple sources. |
| Prune all unused sources | Remove every embedded snapshot without clip references. |

Replacement preserves channel count. Use the waveform editor for channel
conversion first. Different rates require **Resample**;
the shared antialias resampler also converts markers. Replacement must cover
every existing clip's source range, even if some clips are muted. Native
`.vsaudio` replacements retain their markers and metadata, use the reviewed
file itself as provenance, and expose its complete source range. The existing
source label is retained. Relink compares raw decoded samples before any
resampling; it cannot silently substitute different audio.

All operations are one undoable session change, with normal recovery and native
v7 save/reopen. New replacement IDs keep waveform caches and undo samples
independent. Unused-source removal never deletes files. Current, undo and redo
source paths remain protected from native saves, mixdown, stems and preset
exports while those states are retained. Undo's normal memory budget still
applies; pruning need not release all memory until older states are evicted.

The shared renderer carries replacements into playback, mixdown, stems and
**Edit Mixdown**, followed by waveform analysis, game export, package staging
and level placement. A saved waveform `.vsaudio` can serve as the reviewed
replacement. Automatic source-to-waveform round trips, bulk relink search,
external source streaming and editable DAW interchange remain integration gaps.
The CLI exposes the same rules through `asset audio-session media`; see
[CLI Strategy](CLI_STRATEGY.md).

The `audio-media-smoke`, `audio-media-cli-smoke` and `audio-media-ui-smoke`
fixtures verify transactional edits, exact rendered output, reviewed-file
conflicts, source protection, native/recovery persistence and scaled/RTL
review controls with fake devices. The current
[DAW checkpoint](plans/audio-daw.md#session-media-management-checkpoint-2026-10-06)
records their platform scope and remaining acceptance work.

### Time-range editing

Set **Range start/end frame** (end exclusive), then choose **Range…**. The
timeline brackets and shades the selected interval, also used by playback,
waveform handoff, mixdown and stems. The staged dialog chooses exact checked
tracks or **All tracks**, including buses that carry automation. Clip group
links never broaden that scope.

| Operation | Result |
| --- | --- |
| Clear clips, leave gap | Remove clip portions in the range; later material and automation retain their frames. |
| Delete range, close gap | Remove the interval and shift later material earlier by its length. |
| Insert silence at start | Split crossing clips and insert the range length at its start, shifting later material. |
| Repeat range after end | Insert a copy immediately after the range and shift the original tail later. |

Time edits follow gain, balance and effect automation by default. Disable
**Follow track automation** to retain its authored frames. Master effect curves
can follow only when all tracks and automation following are enabled; their
separate checkbox defaults on. Inserted time holds the boundary value. Clear
leaves every lane unchanged and disables automation options. Tempo and meter
markers stay in place: these operations edit sample time, not musical bars.

Cut curves retain their original interpolation domain, including partial Smooth
segments and jumps at a splice. Repeated edits preserve the sampled envelope;
inserting an evaluated handle also preserves it. A point marked **(segment)**
has an inherited domain. Changing its frame or value redefines both adjacent
segments; changing its curve redefines only the outgoing one. Removing a point
connects its neighbours using the preceding outgoing curve. Empty lanes remain
empty. Clips retain source and fade windows; repeated groups get independent
identities and incomplete singleton copies are ungrouped.

A range edit validates all affected descriptors and envelopes before one undo
commit. The 4,096-clip/4,096-point-per-lane limits include generated fragments
and boundary points. Timeline overflow or any invalid result rejects the whole
edit. Stateful effects run normally on the new arrangement; cutting time does
not copy a delay or reverb processor's past state. Save/recovery retain the
editable result, while rendered WAVs, package staging and level placement share
the same audio and automation. Editable interchange remains a separate gap.

### Track mixing and automation

**Track…** controls gain, pan, mute and solo; **Master Gain…** controls the stereo
output. Mono uses equal-power panning (approximately −3.01 dB per channel in the
center); stereo uses balance with unity gain in the center. Mute wins over solo.
**Automation…** edits strictly increasing absolute frame/value points. Gain
automation adds to the track's dB trim, while pan automation replaces static pan.
Each point chooses the outgoing Linear, Step or Smooth curve; values hold
before/after the endpoints. Smooth uses a monotone smoothstep between values.
The curve preview and native point table share selection; edit frame/value/curve
with labelled controls, add at the cursor, remove or clear points. Optional
graph gestures select/drag points and double-click to add. Each lane has at most
4,096 points. Playback and export use the same evaluation. Live write/touch/latch
recording remains planned.

**Add Bus…** creates a stereo subgroup/return. Select a track or bus and open
**Routing / Sends…** to choose its output, swap stereo channels, invert either
channel's polarity, and add up to eight sends. A send has its own destination,
gain, stereo balance, enabled state and pre/post-fader signal point. New sends
start disabled at −12 dB. The dialog stages its changes; Apply validates the
whole graph, stops playback and makes one undoable edit. Cancel leaves the
session unchanged. Routing persists through save, recovery and CLI operations.

Outputs and sends target the stereo master or a bus. There are at most 64 strips
in total, including at most 32 buses. Buses cannot contain clips; select or add
an audio track for imports and recorded takes. Every edge must reference an
existing bus, and cycles are rejected even when an edge is disabled. Reroute
referencing outputs/sends before removing a bus. Two different routes to the
same destination sum deliberately, including an output and a send to one bus.

Clip gain/fades precede routing. Swap then polarity operate before strip gain,
pan and sends. Pre-fader sends bypass strip gain/pan automation; mono clips are
centred at equal power there. Post-fader sends include strip gain/pan and inserts. Sends use
stereo balance, and receiving buses apply their own gain/balance automation
before forwarding audio. Master gain applies once after summing. Mute silences
every route, including pre-fader sends. Solo admits paths that cross at least
one selected track or bus: selecting a bus retains its feeders without their
parallel bypass outputs, while a solo track retains downstream returns.
Separate pending/audible sums prevent unrelated signals from leaking through a
shared bus when several strips are soloed.

The prepared routing graph is shared by playback, transport diagnostics,
streaming WAV export and waveform handoff. Render calls use caller-owned double
accumulation storage with no allocation or graph mutation. The largest routed
block (65,536 frames, 32 buses, solo active) needs 67 MiB of scratch without
effects, or 69 MiB with effects; ordinary direct-master sessions with no enabled
effects retain their previous renderer and sample results.
Enabling meters adds two routed scratch blocks (2 MiB at that maximum block
size); the direct path uses four blocks instead of one. Seamless mixer updates
during playback, sidechains and surround routing remain open.

### Session Meters

**Meters…** opens a modeless native table for every track, bus and the master.
Choose **Before fader and inserts** or **After fader and inserts** without
restarting playback. Numeric left/right sample peaks, RMS, held maxima,
over-range sample counts and stereo phase correlation accompany native level
bars. All levels are dBFS. Zero amplitude displays −∞; correlation is undefined
when either channel has no measured energy. Counts include samples strictly
above full scale, so a sample at exactly ±1 does not count as over-range.

Pre-fader track readings include clip gain/fades and channel swap/polarity;
mono sources are centred at equal power. Post-fader readings include strip
gain/pan automation and inserts. Bus readings include summed upstream paths;
master pre precedes master gain/inserts, and master post measures the rendered
float32 mix before audition volume and clipping. Mute silences both taps. Solo
uses the same path rules as playback, including the combined signal feeding a
selected downstream bus through nonlinear inserts.

Live peak envelopes fall at 24 dB/s and RMS uses a 300 ms exponential energy
time constant. Held maxima, over-range counts and integrated statistics last
until reset. Pause retains readings; stop, a new playback start and seek reset
them. Loop iterations retain history. **Reset Meters** clears readings without
seeking or changing queued audio. Meter updates are coalesced so a busy UI cannot
accumulate an unbounded queue of snapshots. These are render-time readings:
device buffering and processing lookahead can lead the audible cursor. The
selected row reports measured frames and its last signal frame. Loop readings
use unwrapped time and count every cycle; upstream taps can lead master output
by their processing-latency difference. Disconnected strips with longer insert
latency can lag other rows during playback.

Set the session's Range start/end, then **Analyze Range** for device-independent
offline measurements. Playback stops while analysis runs on the existing
cancellable session worker. Effects start with the same fresh history as
playback at the selected start. Peak columns show the maximum over the selected
range; RMS and correlation use the entire range. Analysis compensates each
tap's latency, includes its first transient, and flushes late disconnected
strips so every row covers exactly the requested frames. Failure or cancellation
does not publish a partial report. This read-only operation does not alter
session edits, undo/recovery, source files, or exported samples.

`asset audio-session meters SESSION` provides the same range analysis, with
optional `--start-frame`, `--end-frame` and `--block-frames` (1–65536). JSON
contains both taps for each stable strip ID, per-channel linear/dBFS values,
frame counts, held sample peaks, over-range counts and correlation. Silence
and undefined correlation use JSON null for the corresponding dB/correlation
fields. No audio device is opened. Meters are sample-peak meters, not true-peak,
LUFS, or a phase-repair tool; waveform loudness analysis remains separate.

### Session Effects

Select a track or bus and choose **Effects…**, or use **Master Effects…** for the
stereo output. Each chain holds up to eight ordered processors. Add, remove,
move earlier/later, change parameters or bypass an individual processor in the
staged inspector. **Apply Effects** validates the complete session, stops playback
and commits one undoable change. Cancel leaves the session unchanged. Source
samples remain unchanged; save and recovery retain chain IDs, order, parameters
and bypass states.

Track and bus inserts run after fader/pan automation and before the main output
and post-fader sends. Pre-fader sends stay dry. Master inserts run after master
gain. Processing latency is compensated across main outputs, pre/post sends,
bus merges and the master. Creative delay remains part of the intended sound.
There is no pre-fader insert position, external sidechain or plugin host yet.

| Processor / CLI type | Controls and behavior |
| --- | --- |
| Gain / `gain` | `gainDb`, −96…+24 dB. |
| Low/high pass, notch / `low-pass`, `high-pass`, `notch` | Second-order filters with `frequencyHz` and `q` (0.1…18). |
| Parametric and shelving EQ / `peak-eq`, `low-shelf`, `high-shelf` | Frequency, Q and `gainDb` (−24…+24 dB). Neutral gain is bypassed internally. |
| Compressor / `compressor` | Linked stereo peak detector, `thresholdDb` (−96…0), `ratio` (1…40), `kneeDb` (0…24), `attackMs` (0.1…200), `releaseMs` (1…5000), `makeupDb` (−24…+24) and parallel `mix` (0…1). |
| Noise gate / `gate` | Linked peak detector, threshold/attack/release as above, `rangeDb` (0…96), `holdMs` (0…2000) and `hysteresisDb` (0…24). Starts at closed attenuation. |
| Sample peak limiter / `limiter` | `ceilingDb` (−24…0), instantaneous linked reduction and `releaseMs` (1…5000). No lookahead; this does not limit reconstructed true peak. |
| Lookahead limiter / `lookahead-limiter` | `ceilingDb` (−24…0), `releaseMs` (1…5000), `attackMs` (0.01…20) and fixed `lookaheadMs` (0…20, default 5). Stereo-linked future sample peaks drive attack/release smoothing with a final linked sample-peak safety gain. This does not guarantee reconstructed true peak. |
| Stereo delay / `delay` | Independent `leftMs`/`rightMs` (1…2000), `feedback` (0…0.95), and `mix` (0…1). Fractional taps use linear interpolation, with at least one sample of delay. |
| Soft saturation / `saturation` | `driveDb` (0…36), `outputDb` (−24…+12) and `mix` (0…1), using a normalized tanh curve. No oversampling or alias-suppression guarantee. |
| Stereo reverb / `reverb` | `roomSize` (0.25…2), low-frequency `decaySeconds` (0.1…30), `dampingHz`, `preDelayMs` (0…250), `width` (0…1) and `mix`. Eight damped combs and four allpass stages per channel; stereo excitation preserves antiphase input. |
| Chorus / `chorus` | `minDelayMs` (1…50), `depthMs` (0…20), `rateHz`, `stereoPhaseDeg`, signed `feedback` (−0.95…0.95) and `mix`. |
| Flanger / `flanger` | `minDelayMs` (0.01…10), `depthMs` (0…10), modulation rate/phase, signed feedback and mix as above. |
| Tremolo / `tremolo` | `rateHz`, `stereoPhaseDeg` and `depth` (0…1). Sine amplitude modulation ranges from unity to 1 − depth. |
| Phaser / `phaser` | `frequencyHz`, `depthOctaves` (0…4), modulation rate/phase, `feedback` (−0.9…0.9) and `mix`; four swept allpass stages per channel. |

Modulation rates span 0.01…min(20 Hz, 0.25 × sample rate), with stereo phase
offsets from 0 to 360 degrees. Chorus/flanger sweep sinusoidally from minimum
delay to minimum + depth, use linear interpolation and retain at least one
sample of delay. Phaser center frequency spans min(20 Hz, 0.01 × rate) to
min(5 kHz, 0.25 × rate); its individual stages stay below Nyquist. Reverb damping
spans min(20 Hz, 0.01 × rate) to min(20 kHz, 0.49 × rate). Its decay setting
describes low-frequency feedback decay, not a guaranteed measured RT60 for
every signal or room setting. Topology/tuning attribution is in
[Credits](CREDITS.md#audio-reverb-reference).

Filter frequencies range from min(10 Hz, 0.01 × rate) to min(20 kHz, 0.49 × rate).
The GUI exposes the session-specific bounds. DSP state, including delay buffers,
latency compensation and separate solo-path histories, has a shared 128 MiB admission limit. A solo
graph reserves two histories per strip so nonlinear processing of shared buses
retains the combined signal without leaking bypass paths. Preparation allocates
state; processing and history reset do not allocate.

**Session tail (s)** is a global saved setting, initially 2 seconds and adjustable
from 0 to 60. An enabled filter, delay, reverb, chorus, flanger or phaser extends the default range once beyond
the last clip, rounded upward to whole frames. Choose enough tail for the decay;
there is no automatic silence detection. Explicit ranges retain their chosen
end. A tail beyond the timeline limit is rejected. Empty sessions and chains
without enabled tail-producing processors add no frames.

Contiguous blocks and loop repeats retain effect history. Pause/resume retains
it; stop, seek, a changed range or loop policy, cancellation and failed rendering
reset it.
Playback and export starting at the same frame use the same fresh state, with
no implicit preroll. Starting inside a tail therefore does not reconstruct sound
from earlier clips. Bypass skips the processor entirely. Live automation recording,
true-peak limiting and plugin hosting remain
open in the [DAW plan](plans/audio-daw.md).

**Automation…** inside Effects edits eligible numeric parameters of the selected
processor on a track, bus or master. Choose a parameter, then author absolute
frame/value points with Linear, Step or Smooth outgoing segments. **Read
automation** off retains points while using the static parameter. Clear removes
the lane. Static controls remain the fallback for an empty/disabled lane; an
active lane replaces the static value, including before its first point.
The nested dialog edits the effects draft; outer Apply commits one undoable
change and stops playback. Cancel preserves the session. Reordering or bypassing
an effect retains its lanes; removing/replacing it removes its lanes.

Lookahead is a structural setting: Apply rebuilds compensation and stops playback
through the normal staged edit. Its value rounds upward to a whole sample at the
session rate. It is excluded from the automation selector; native/CLI validation
rejects a lookahead lane even when disabled. Ceiling, attack and release remain
automatable. The inspector reports the selected chain's insert latency in frames.

Latency alignment retains the requested frame count and first/last samples;
there is no leading export padding and no added tail for the lookahead limiter.
Each fresh range primes the processing pipeline from its requested start, using
later session audio as lookahead context, including audio beyond the chosen end.
This does not reconstruct earlier history. For looped session playback, future
context wraps inside the selected range, including when lookahead exceeds a
whole cycle. Compensation primes once; subsequent cycles retain the pipeline. Gain/pan and effect automation
remain tied to authored frames, and upstream compensation does not advance a
downstream modulator's phase before its input arrives. Strip taps use only their
upstream processing latency. A muted or soloed route retains its prepared timing;
bypassing an insert removes its latency when a new snapshot is prepared.

CLI inspect exposes a separate `processingLatency` object with total frames/ms,
master input/insert frames, compensation bytes and per-strip timing. Transport,
mixdown and each stem manifest entry report `processingLatencyFrames`; these
describe processing alignment, not measured hardware or recording latency.
Native session version 7 includes timing maps, clip groups, fade windows and cut automation domains; preset version 1 remains unchanged. Earlier builds
that do not recognize `lookahead-limiter` reject that effect type.

Automation uses absolute session frames in playback, export and explicit ranges.
Pause preserves its clock. Loop repeats wrap authored source and automation
positions while processing time, LFO phase, delay/reverb tails and compensation
continue. Stop, seek and changing the loop policy reset history and read the
curve at the new frame. There is no implicit preroll. Up to 96 lanes per
chain and 65,536 effect points per session are allowed, including disabled lanes.
The saved metadata limit is separate from these in-memory descriptor limits.
Preparation reserves delay/reverb storage for the greatest base/active point
values; processing changes taps and coefficients without resizing storage.
Reverb taps now interpolate fractional delays, so nonintegral room/rate tuning
can sound slightly different from earlier rounded taps. Step curves and rapid
parameter changes may produce intentional discontinuities or pitch changes.

Open **Presets** in the effects inspector to load a factory recipe or `.vsfx`
file, name the current staged chain, or save it for reuse. Nine factory recipes
cover dialogue cleanup, radio voice, small room, large hall, wide chorus, jet
flange, phase sweep, rhythmic tremolo and master peak control. Loading replaces
the selected draft chain, clears its automation and gives every insert a fresh ID. Apply commits it as
one session edit; Cancel leaves the session unchanged. Saving a preset writes
the staged recipe independently of Apply. File operations run on a worker with
visible progress, cancellation and errors.

Presets retain static physical parameter values, source sample rate and suggested
tail. Session automation is excluded from `.vsfx` files. Loading keeps the longer of the existing session tail and preset tail;
adjust the session tail explicitly to shorten it. A file whose parameters are
invalid at the destination rate is rejected without clamping. Factory recipes
adapt to the current rate before staging. `.vsfx` version 1 is strict JSON,
limited to 64 KiB and eight effects. It contains no media or executable code.
Atomic save, explicit overwrite, imported-source protection (including hard
links) and last-loaded/saved file digest checks protect existing files. Presets
are opened through the effects inspector or CLI; generic asset-browser opening
and project preset libraries remain integration gaps.

`asset audio-session presets [--sample-rate RATE] --json` lists factory IDs and
every processor's parameter bounds and `automatable` eligibility. The `effects` command supports
`--operation preset --preset ID` or `--preset-file FILE.vsfx` and
`--operation save-preset --name NAME --output FILE.vsfx`. Normal output,
dry-run and overwrite guards apply; see [CLI Strategy](CLI_STRATEGY.md).

### Saving and Playback

**Save** and **Save As…** write a lossless `.vssession` document containing source
snapshots, arrangement and mixer settings. Saving the current file checks its
previous digest and rejects external changes. Separate exports cannot overwrite
imported source paths or the input session. Files use atomic replacement and
cooperating writer locks. Named undo/redo keeps up to 64 states within a 256 MiB
sample/descriptor history estimate; the current document and active worker have
separate storage. Abandoned undo branches release their waveform caches.

**Keep local recovery copies** shares the waveform editor and setup preference.
Accepted edits queue a background checkpoint after one second; continuous edits
do not postpone that timer. One in-flight write and one replaceable pending
snapshot bound the work. **Review Audio Recoveries…** and **File → Recover Audio**
show both document kinds. Startup offers review without opening a document.
The shared limit is 32 copies / 512 MiB; full storage preserves existing copies
and reports a checkpoint failure. Turning recovery off retains existing copies.

Session copies contain the arrangement, automation and original audio. Restore
checks the reviewed digest and creates a new unsaved draft, protecting both the
original session path and the reviewed copy. Saving, returning to the saved
revision, or approving discard retires only the current draft's checkpoint.
An interrupted process leaves its last completed copy available for review.
Checkpoints do not preserve undo history, view selection or device state, and
the short queued-edit window is not a write-ahead journal. Recording uses a
separate take journal below; power-loss/filesystem acceptance remains open.

Select **Range start frame** and **Range end frame**, then **Play Range** to stream
that range from the current arrangement. **Output** selects a device or the system
default; **Refresh** updates the list. A disconnected selection remains unavailable
until explicitly changed. The output must support stereo float32 at the session
rate; no implicit rate or channel conversion occurs. Output and buffer choices
last for this window and are not saved in the document.

**Output buffer** requests 256–16,384 frames, initially 2,048. Smaller buffers
reduce delay and larger buffers tolerate slower processing; the device may choose
a different size, reported in the transport status. Pause/resume retains queued
audio. Changing the cursor seeks within the playing range and discards old queued
audio; changing the range, output or buffer stops playback. Loop repeats the exact
frame range with continuous effects, routing delays and modulation. Toggling it
discards buffered lookahead and resets processing before continuing at the
device-estimated cursor. Reapplying the same policy leaves the stream intact.
The cursor wraps; the DSP clock advances independently, so loops near the final
legal authored frame keep playing. This does not extend the native timeline.

Status reports the device-estimated frame position, output buffer, dropout count,
and stereo master peaks/over-range samples before audition volume. Output samples
are clamped to full scale after audition volume; exports retain their selected
precision. Dropout counts reflect observed device starvation, not a complete
hardware xrun counter. A disconnected/error output stops with a message; it does
not silently switch devices. Five seconds without progress also stops playback.
Preparation and mixing run on a dedicated worker. Qt's portable buffered output
is not a hard-real-time or low-latency-monitoring guarantee.

Audition volume does not affect export. Starting a session audition stops the
browser/waveform audition. Edits stop the current stream; a new Play prepares the
current state. **Edit Mixdown** sends the same range
to the waveform editor for analysis, final dither/game delivery, package staging
and level placement. This handoff creates a separate sound asset and does not
save the arrangement. Save the session, map and package through their own normal
document controls.

**Export WAV…** streams the range in bounded blocks as float32 or PCM8/16/24/32.
Float32 retains finite headroom; integer delivery clips values above full scale
and reports the count. Optional TPDF dither applies once at integer delivery,
with a continuous deterministic sequence across blocks (GUI seed zero). Use
**Edit Mixdown** and the existing delivery controls when DMX/game presets or
cue/loop authoring is required. Source markers remain in the native session;
mixdown does not invent a combined marker timeline.

Initial limits are 64 tracks, 128 sources, 4,096 clips, 16,777,216 samples per
source and 67,108,864 samples (256 MiB) across source media. Source media remains
in memory. Positions use exact 64-bit frames up to 33,177,600,000; distant clips
do not allocate preceding silence. Streamed audition can traverse the full
frame range with bounded processing buffers; it does not stream source media
from disk. Editor handoff remains limited to 8,388,608 stereo frames. Streamed
export uses the classic RIFF 4 GiB file boundary. Long-media disk streaming, surround routing,
tempo ramps, grouped/ripple edits, overdubbing, MIDI, advanced effects and plugins remain
open in the DAW plan. Playback/editing needs no microphone permission or cloud account.

The shared CLI is `asset audio-session new|inspect|import|edit|automation|effects|effect-automation|presets|tempo-map|position|mixdown|stems|recover|transport`.
Use `inspect --json` or an import result to obtain track/clip IDs. All writes
require `--output`; `--dry-run` validates/prepares without creating a file, and
`--overwrite` explicitly permits replacement. In-place native edits also verify
the digest captured when reading. Unknown, repeated and inapplicable flags fail.

```sh
vibestudio --cli asset audio-session new --sample-rate 48000 --tempo 120 --output ./scene.vssession --json
vibestudio --cli asset audio-session tempo-map ./scene.vssession --tempo-points 3840:90 --meter-points 5:7/8 --output ./scene.vssession --overwrite --json
vibestudio --cli asset audio-session position ./scene.vssession --at-position 5.1.0 --snap bar --json
vibestudio --cli asset audio-session import ./scene.vssession --input ./wind.wav --resample --at-frame 48000 --output ./scene.vssession --overwrite --json
vibestudio --cli asset audio-session inspect ./scene.vssession --json
vibestudio --cli asset audio-session edit ./scene.vssession --operation track --track TRACK_ID --db -6 --pan -1 --output ./scene.vssession --overwrite
vibestudio --cli asset audio-session edit ./scene.vssession --operation add-bus --name Dialogue --output ./scene.vssession --overwrite --json
vibestudio --cli asset audio-session edit ./scene.vssession --operation routing --track TRACK_ID --output-bus BUS_ID --output ./scene.vssession --overwrite
vibestudio --cli asset audio-session edit ./scene.vssession --operation send --track TRACK_ID --target-bus BUS_ID --db -12 --pre-fader true --output ./scene.vssession --overwrite
vibestudio --cli asset audio-session automation ./scene.vssession --track TRACK_ID --gain-points 0:-12,48000:0 --pan-points 0:-1,96000:1 --output ./scene.vssession --overwrite
vibestudio --cli asset audio-session mixdown ./scene.vssession --start-frame 0 --end-frame 96000 --wav-format float32 --output ./scene-mix.wav --dry-run --json
vibestudio --cli asset audio-recoveries --json
vibestudio --cli asset audio-session recover ./UUID.vssession-recovery --expected-sha256 REVIEWED_SHA256 --output ./restored.vssession --dry-run --json
```

Edits include `add-track`/`remove-track`, `track`, `region`, `remove-region`,
`duplicate`, `split`, `master`, `add-bus`, `routing`, `send`, and `remove-send`.
Routing/send options and literal bus-ID escapes are documented in
[CLI Strategy](CLI_STRATEGY.md). Region fields are `--at-frame`,
`--offset-frame`, `--frames`, `--fade-in`, `--fade-out`, `--db`, `--mute`, and
`--name`. Track edits accept `--db`, `--pan`, `--mute`, `--solo`, and `--name`.
Boolean values are `true` or `false`. Omitted fields retain their current values.
Duplicate/split use `--at-frame`; automation uses comma-separated `frame:value[:linear|step|smooth]`
pairs or `none` to clear one lane while retaining an unspecified lane.

`transport SESSION --frames N` runs the same frame clock/mixer without opening
an audio device or writing files. Optional `--start-frame`, `--end-frame`,
`--block-frames` (1–65,536; default 1,024) and `--loop true|false` control the run.
The request is capped at 16,777,216 frames. Text/JSON reports rendered frames,
final cursor, loop count, master peaks, over-range count and a SHA-256 of rendered
stereo float32 little-endian samples. A finite range stops at its exclusive end;
unused device padding is excluded from the digest. This is a deterministic
processing diagnostic, not a hardware latency test. Loop digests include
continuous effect history and wrapped lookahead/automation, matching session
playback. Exports and Edit Mixdown still render the requested finite range once.

Recovery records report `kind: waveform|session`. To discard a session copy,
use `asset audio-recoveries --discard UUID --kind session --expected-sha256 HASH`;
discard remains a dry run until `--write` is supplied. Omitting `--kind` retains
the existing waveform behavior. A live editor lease prevents committed discard.
`audio-session recover` requires the digest, a separate `.vssession` output, and
the usual explicit overwrite/dry-run flags. It never removes the reviewed copy.

## Selecting And Editing

**New** (Ctrl+N) creates an empty document or initial silence with a chosen sample
rate and channel count. Empty documents retain their format, support save and
recovery, and accept pasted samples or inserted silence. Playback, export, and
staging require at least one frame.

Drag across the waveform to select a range. Left/Right moves one frame; Shift
extends from the selection anchor. Page Up/Down moves a tenth of the visible
range, and Home/End moves to the sound boundaries. The start/end fields and
waveform use exact frames, including selections shorter than a millisecond.
The end frame is exclusive. **Select All** restores the full range.

**Zoom In**, **Zoom Out**, **Fit Sound**, and **Fit Selection** control the view.
Ctrl+wheel zooms at the pointer. The scrollbar, wheel, middle-button drag, or
full-sound overview pans without changing samples or selection. At sample scale,
the view shows individual sample points; wider views show exact min/max ranges
for each channel. The overview outlines the visible window, and the range label
reports frames and milliseconds. Zoom shortcuts apply only at the waveform.

- **Copy**, **Cut**, and **Paste** share exact floating-point samples between
  VibeStudio audio editors. Paste replaces the selection, or inserts at an empty
  selection. The application audio clipboard is separate from the system
  clipboard; it lasts until replaced or the application exits. Ctrl+C/X/V apply
  to audio while the waveform has focus and retain normal text behavior elsewhere.
  Sample rate and channel count must match; conversion is never implicit.
- **Trim** keeps the selected frames; **Delete** removes them, including the
  entire sound. **Silence** replaces selected samples without changing duration.
- **Effects > Insert Silence…** inserts an exact number of frames at the
  selection start and shifts the remaining sound.
- **Effects > Mix Clipboard at Selection Start** adds all copied frames, extending
  the sound when necessary. Mixing retains float headroom, so check the peak
  warning before integer WAV export.
- **Fade In** and **Fade Out** apply a linear ramp across the selected range.
- **Effects > Gain** accepts −96 to +24 dB. Floating-point headroom is retained
  while editing; the peak display warns when PCM export will clip.
- **Effects > Normalize** scales the selection to a target peak from −96 to
  0 dBFS (default −1). All channels share a gain value, preserving their balance.
- **Effects > Reverse** reverses frames without exchanging the channels.
- **Effects > Convert to Mono** averages the channels of the whole sound.
- **Effects > Convert Mono to Stereo** duplicates a whole mono sound into left
  and right channels. **Invert Polarity** negates selected samples, and **Remove
  DC Offset** subtracts the selected mean independently for each channel.

Undo and redo name the edit and restore samples, format, selections, and markers. The editor retains up to 32 edits,
with a 256 MiB sample/cache/marker history budget. Returning to the last saved project
revision clears the unsaved indicator. Opening another sound or closing a modified
document, or creating a new one, offers Save, Discard, or Cancel. Loading and processing show an operation status and support
cancellation; failed or cancelled work leaves the current sound intact.
No-op detection compares exact float bits on the worker, including signed zero;
an unchanged result does not consume history or replace the waveform cache.
Allocation failures and unexpected worker errors are reported in the operation
status while preserving the open document, selection, history and saved revision.
The history budget counts each retained state conservatively, including its
sample, waveform-cache and marker storage; it is separate from the current
document, clipboard and temporary processing buffers.

## Resampling

**Effects > Resample…** changes the whole sound's sample rate while retaining
pitch and duration. Choose a common rate preset or enter 1–384000 Hz; the dialog
previews the new frame count and duration before processing. Conversion runs on
a cancellable worker and forms one named undo step, including sample rate and
selection. An empty document can also change its rate. Clipboard paste still
requires matching formats; resample the source document before copying it.

The shared [r8brain converter](DEPENDENCIES.md#r8brain-free-src) uses a
double-precision, linear-phase filter with anti-aliasing. Output stays float32,
including headroom and filter ringing; integer quantization happens only at
delivery. Finite clip boundaries are zero-extended. The output frame count is
rounded to the nearest duration frame, ties upward, with at least one frame for
nonempty input. Cursor and selection boundaries follow the same time mapping;
an end-of-sound boundary remains at the end. Very short ranges may collapse to a
cursor when downsampling. Opaque project metadata is retained unchanged; typed
cues and loop boundaries follow the same time mapping.

The 16,777,216-sample output limit is checked before allocation. Filter input and
padding are additionally limited to 67,108,864 interleaved samples of processing;
an extreme downsampling ratio may fail this bound with a clear error. A failed
or cancelled conversion leaves the document and undo history untouched.

## Cue Markers And Loops

**Markers…** edits named cues at exact sample frames and one forward, indefinitely
repeating loop. Edit a table cell with F2, add a cue at the selection start, or
remove the selected cue. **Use Selection for Loop** copies the current bounds;
the loop's end is exclusive. OK applies the pending metadata as one undo step;
Cancel leaves the document unchanged. Invalid ranges keep the dialog open with
an explanation. Marker-only changes share the existing sample and waveform
buffers and participate in saving, recovery, and dirty-state tracking.

Cues appear as triangles with dotted lines. A labeled bracket identifies the
loop; dashed boundaries still identify the selection. The waveform's accessible
description reports the cue count and loop range. **Select Loop** selects and
frames that range for the existing Play and Loop controls. Audition
buffers omit embedded markers so the editor's transport controls repetition.
Qt backend looping is not a promise of sample-gapless device playback.

Markers follow these shared GUI/CLI edit rules:

| Operation | Cue and loop behavior |
|---|---|
| Trim/copy | Keep cues inside the selected half-open range and rebase them. Intersect the loop with that range; remove an empty intersection. |
| Delete/cut | Remove cues in deleted frames and shift later cues. Contract, shift, or remove the loop with the deleted range. |
| Insert | Shift cues at/after insertion. Insertion strictly inside a loop expands it; insertion at its start moves the loop, and insertion at its end stays outside. |
| Replace/paste | Remove covered destination cues, shift later cues, and import clipboard cues. An overlapping destination loop follows the replacement span; if no destination loop survives, adopt the clipboard loop. |
| Mix | Import offset clipboard cues; keep the destination loop if present, otherwise adopt the clipboard loop. |
| Reverse | Reflect selected cue frames. Reflect a fully contained loop; keep a loop enclosing the selection; remove a partially intersected loop because its samples no longer form one contiguous range. |
| Resample | Map marker time to the nearest output frame. Preserve an EOF loop end exactly; remove a loop that collapses to zero frames. |
| Gain, fades, silence, polarity, DC, channels | Preserve marker positions. |

Pasted/mixed cue ID collisions receive deterministic unused IDs. Exceeding the
cue/name bounds fails the whole operation. Edit status reports removed loops and
reduced cue counts; the marker dialog exposes all resulting positions.

The document supports 256 cues, unique unsigned 32-bit IDs, names of at most
128 UTF-16 code units each and 16 KiB of UTF-8 names in total, and one nonempty
loop within the sound. Cues identify existing frames, not the EOF boundary.
Names must be valid Unicode without null characters.

WAV import/export supports `cue `, `LIST/adtl/labl`, and one `smpl` forward loop
with zero fraction and infinite play count. The WAV inclusive loop end converts
to the document's exclusive end. Output labels use UTF-8 with `CSET` code page
65001; input also accepts default/explicit Latin-1 (0, 1004, 28591). Associated
marker chunks are bounded to 64 KiB. Duplicate/invalid cues, conflicting offsets,
unsupported loop modes/counts, malformed labels, and unsupported declared label
encodings fail explicitly. Other container tags, instrument tuning, notes, and
playlist instructions are outside this typed marker model.

Original Quake/II `cue` plus `LIST/adtl/ltxt` length markers also import. An
unlabeled first cue describing that loop is treated as its sentinel, avoiding
duplicate authored cues. A bare cue without `smpl` or a legacy length marker is
ambiguous: it remains a cue, without inferring a loop. Review such legacy assets
and assign their intended loop explicitly before game delivery.

## Analysis

**Analyze…** measures the selected frame range, or the whole sound when the
selection is an empty cursor. A cancellable worker produces a read-only report
with overall sample peak and RMS, plus per-channel signed minimum/maximum,
sample peak and its earliest absolute frame, RMS, signed DC offset, samples
above full scale, exact full-scale endpoints, the first over-range frame, and
the longest consecutive over-range run. The report states its source, range,
sample rate, and channel order. It does not change samples, selection, undo
history, recovery, or the saved revision.

RMS is the square root of the mean squared sample value, including DC. DC is
the arithmetic mean; its percentage uses 1.0 as 100%. dBFS uses a linear level
of 1.0 as 0 dBFS; silence displays −∞. A full-scale sine therefore has about
−3.01 dBFS RMS. Each channel is measured independently, and overall RMS averages
energy across all measured samples without mixing channels together.

An over-range sample has absolute value greater than 1.0 and saturates during
integer delivery. An exact ±1 endpoint is counted separately; these endpoint
counts cannot determine whether imported audio was previously clipped. The
measurements describe the editable samples before delivery mixing/resampling.
True peak is also measured for every channel, including LFE or channels excluded
from loudness. The report gives each channel's dBTP and the largest overall
value. The pinned r8brain-free-src double-precision converter uses 8× oversampling
below 96 kHz, 4× below 192 kHz, 2× below 384 kHz and sample peak at 384 kHz.
Its 2% transition band and finite output grid limit accuracy near Nyquist;
the result is at least the original sample peak. The selected range is treated as
an isolated sound with silence outside its boundaries; interpolation includes
both filter tails. A sample peak below 0 dBFS can still exceed 0 dBTP.

Integrated loudness uses BS.1770 K-weighting, complete 400 ms blocks with 100 ms
steps, a −70 LUFS absolute gate and a −10 LU relative gate. Mono defaults to
center and stereo to left/right. Block steps round to the nearest whole frame;
at 11025 Hz a complete block needs 4412 frames. Availability messages and JSON
report that exact requirement. For more than two channels, **Analyze…** first
opens **Loudness Channel Layout**: review each role in source channel order,
choose the displayed order preset, or turn off **Measure loudness**
to measure sample statistics and true peak only. LFE and excluded channels have
zero loudness weight; left/right surround use 1.41, and front/back roles use 1.
Roles are reviewed for each analysis because the current document does not
persist a speaker layout. A preset describes an order; it does not detect one.

True peak and loudness support 8–384 kHz. Lower sample rates still have sample
statistics. Loudness is explicitly unavailable for selections shorter than one
complete block or an unreviewed surround layout. Signals below the absolute gate
report **Below gate**; skipping loudness reports **Not requested**. Empty ranges
have no true-peak result. Silence has zero linear true peak (−∞ dBTP) and no
gated loudness value. Missing measurements never become zero LUFS.

These are read-only measurements of editable audio. To check a game delivery
after its channel mix, conversion and quantization, analyze the exported file.
LUFS normalization, momentary/short-term meters and loudness range are not
implemented. Meter verification uses original calibration fixtures and
independent signal checks; no broadcast-certification claim is made.
**Definitions** expands the exact conventions and limitations.
The table scrolls for additional channel fields, and the report body scrolls in
short windows while Close remains visible. The CLI exposes the same values as
structured JSON for automated asset checks.

## Projects And Recovery

**Save Project** (Ctrl+S) writes a lossless `.vsaudio` document. **Save As…** writes
a separately reviewed destination. Projects preserve every finite float32 sample
bit, including headroom above full scale, plus channels, sample rate, selection,
source provenance, cues/loop, and bounded editor metadata. They do not store undo history.
**Open…**, a file drop, or `--open file.vsaudio` opens native audio projects.
WAV export and package staging
leave an edited project unsaved; they are delivery operations with different
precision and persistence guarantees.

Project writes use an atomic replacement and a cooperating-writer lock. Saving
an opened project compares its canonical path and SHA-256 against the version
loaded, and checks again immediately before commit. An external change, deleted
file, retargeted path, failed write, or lock conflict preserves the current edits
and asks the user to review/save a separate copy. Non-cooperating external writers
can still race in the small interval between the final check and atomic rename.
The native format has a version, bounded JSON header, little-endian interleaved
float32 payload, and SHA-256 checksum; unsupported, corrupt, or oversized files
never partially replace a document.
Marker-bearing documents use format version 2; marker-free documents continue
to use version 1. The reader accepts both. Older readers reject version 2 instead
of silently discarding its markers. Recovery uses the same versioned format.

**Keep local recovery copies** is enabled by default and can be switched off in
the editor or **Settings → Getting Started → Audio Recovery**. Changes apply to
an already open editor. Accepted edits and subsequent selection changes checkpoint through
one background writer; at most one newer snapshot waits behind an in-flight write.
Copies live in the application's `audio-recovery` data folder, or beside an
explicit `--settings-file` profile in its own `audio-recovery` folder. The
`VIBESTUDIO_AUDIO_RECOVERY_ROOT` override takes precedence. Copies contain audio
and source paths and stay on the local device. Settings shows the folder;
the editor status tooltip shows the last
written path. Turning recovery off retains existing copies. A successful project
save, undo to a clean revision, or approved discard retires that document's copy,
including any write already in flight.

**Recoveries…** lists local copies with their sound name, time, format, size,
verification state, paths, and SHA-256. Scanning and checksum/sample verification
run in a cancellable worker; closing the manager cancels inspection. Corrupt
copies stay visible with individual errors. **Restore as Draft** rechecks the
reviewed digest before opening an unsaved document. Save it to an explicit
project destination; the original source and selected recovery copy remain.

Recovery storage allows 32 managed copies and 512 MiB. Reaching either limit
stops checkpoints with a visible error and preserves all existing copies. There
is no automatic age-based eviction. **Discard Selected Copy…** requires a
confirmation, verifies the reviewed bytes again, and removes only that copy.
A live editor's session lease prevents discard. A listed session file may also
be left by an interrupted process; Qt handles stale leases when committing.
Dry runs do not acquire locks or guarantee that a later commit can acquire them.
Cooperating writers and discard operations are serialized. A non-cooperating
external process can still replace a file between the final digest check and
removal; filesystem races and native platform behavior remain part of release
acceptance.

Only canonical UUID `.vsaudio` names directly inside the recovery directory are
managed. Ancestor links, linked entries, and nonregular files are rejected;
source paths inside records are provenance only. Scans inspect at most 256
matching directory entries and verify at most 512 MiB of record bytes. A limited
scan or unsafe directory blocks new checkpoints. Oversized records remain
visible but require separate filesystem review.

At application startup, a background metadata-only scan offers retained files
through a dismissible **Local audio recovery** notice. It never reads sample
payloads, opens a document, changes a copy, or takes focus. An existing crash
notice stays visible first; the audio notice waits for dismissal. Discovery
errors and scan limits remain visible and are recorded in Activity. These are
recovery candidates, possibly including live sessions or corrupt files; opening
**Review Audio** performs full verification in the manager.

**Offer copies at startup** is a separate, enabled-by-default setting in the
same Audio Recovery panel. Turning it off suppresses discovery on the next start
and cancels any pending offer. It does not delete files. **File → Recover Audio**,
the command palette, **Review Copies** in setup, and the editor's **Recoveries…**
remain available when checkpointing and startup offers are both off. Dismissing
an offer affects the current session; retained files may be offered next start.

## Playback And Output

The Audio browser prepares previews and playback on background workers. Rapid
selection changes keep only the latest pending request. **Stop** cancels playback
preparation; choosing another sound, replacing the package snapshot or starting
an editor audition also cancels it. Old results and backend errors cannot replace
or stop a newer sound. The transport reports Loading, Playing, Paused, Buffering
and failure states and uses the same device/error handling as the editor.
Clearing the selection cancels preview and audition; Play, Edit Sound and Export
require a selected sound.

Browser audition retains original WAV precision and compressed bytes. DMX samples
are widened to PCM16 WAV for the device. Actual codec support remains dependent on
Qt's backend, including WAV codecs beyond the editable PCM/float contract. Preview
reads at most 64 MiB; larger sounds show sampled header metadata (64 KiB) without
a partial waveform. Audition input and prepared output are each limited to 128 MiB.
Streaming reads check cancellation and package integrity. Header analysis and
conversion are bounded phases with cancellation checked between them. Compressed
duration estimates update when the backend reports a duration.
The browser's labeled **Position** slider also seeks sounds without a decoded
waveform. It preserves a paused session; dragging commits the seek on release.

Repeated WAD sound names retain their selected occurrence through preview,
audition, **Edit Sound** and browser WAV export. Path-only CLI operations still
refuse ambiguous names rather than choosing an occurrence implicitly.
For automation, obtain the current `entryIndex` from `package list --json`,
extract that row with `package extract <archive> --entry-index N --as <name>
--output <folder>`, and pass the local sound to `asset audio-project` or
`asset audio-edit`. Use a filename extension matching the sound's codec.
See [exact package selectors](CLI_STRATEGY.md) for snapshot and output rules.

With Qt Multimedia, **Play Selection** auditions the edited samples. When the
selection is empty, **Play From Cursor** auditions from that frame to the end;
at frame zero, **Play Sound** auditions the full clip. Pause, resume, stop,
looping, and playback volume are available. Space controls playback while the
waveform has focus; it retains its normal behavior in other controls. Playback
volume does not change exported samples. Editing, export, and staging also work
in builds with `-Daudio_playback=disabled` and in AI-free mode.

Audition preparation retains the selected float32 sample bits, including detail
below 16-bit precision and floating-point headroom. Output-device conversion,
mixing and saturation are controlled by Qt and the system audio stack. Reduce
gain or normalize out-of-range samples before judging full-scale output. The
loop repeats the prepared selection, or cursor-to-end range; it does not alter
the document's saved loop marker. If the backend reaches the end despite Loop
being enabled, the transport queues a rewind and restart. Stop, a replacement
sound, failure, or turning Loop off cancels that repeat. A source that cannot
seek back reports an error with a Loop-off retry action. Restarting also uses
the loading timeout; looping is not guaranteed to be gapless.

The **Playback frame** slider and numeric field seek within the active audition,
including while paused, without changing the editing selection. Dragging commits
on release. The displayed frame follows the backend's millisecond position;
requests round down to that position relative to the exact selection start.
Use the editing cursor or selection start before Play for an exact first frame.
Seeking to the exclusive end stops playback. Seeking is disabled when the
backend reports that the media is not seekable. Stop retains the last reported
playhead; the next Play starts the selected range again.

**Stop** also cancels pending preparation, preventing delayed autoplay. Loading,
buffering, playing, paused, stopped and failed states appear beside the position.
A 30-second loading/buffering timeout releases stalled media and leaves Play
available for retry. Each new audition uses the current system default audio output;
missing or disconnected output devices produce an actionable error. Choose an
output in system sound settings and try Play again. Device errors and late
callbacks from retired media cannot change the audio document or restart an
old audition. Browser/editor auditions continue to stop each other.

**Export Audio…** offers a **WAV (custom precision)** preset with unsigned
8-bit PCM, signed 16/24/32-bit PCM, and 32-bit
float at the current sample rate and channel count. PCM16 without dither is the
default. Integer formats round and saturate at full scale; normalize or reduce
gain when samples exceed −1 to +1. Float WAV preserves every finite working
sample bit, including headroom, negative zero, and subnormals. Choosing a wider
integer container cannot recreate detail already absent from the float source.

**Triangular dither** adds independent, low-level TPDF noise at integer
quantization. Apply it once at final delivery; intermediate `.vsaudio` and float
WAV exports preserve samples directly and reject dither. GUI exports use seed
zero for reproducibility; the CLI accepts an explicit unsigned 64-bit seed.
Integer precisions above 16 bits and sounds with more than two channels use
`WAVE_FORMAT_EXTENSIBLE`. Unnamed multichannel streams retain channel order with
an unspecified speaker mask. Float output includes a `fact` frame-count chunk;
odd-sized PCM data has RIFF word padding. Classic game readers may require mono
or stereo PCM8/PCM16 instead of these interchange formats.

Preparation runs on a cancellable worker. Once prepared, a short, non-cancellable
atomic commit writes the complete result with a cooperating-writer lock. The original
sound/package path is protected even when overwrite is requested. Dry runs
create neither a file nor a lock. Supported cues and loops use the delivery rules
below; other source container tags are not copied. Export does not mark a project saved.

**Stage Sound** uses the visible package delivery preset and optional dither.
The default WAV preset stages PCM16 at the document rate/channel count. Quake
presets stage their fixed mono PCM format in a folder, PAK, ZIP, or PK3. A Doom
IWAD/PWAD selects the Doom preset; WAD2/WAD3 texture archives are excluded.
Use a game-relative WAV path such as `sound/ambience/wind.wav`. Doom staging
requires `DS` followed by 1–6 uppercase ASCII letters, digits, or underscores,
such as `DSWIND`, without an extension. This avoids structural WAD lump names. Existing paths
require **Replace existing entry**; unsafe paths and collisions leave the staging
plan unchanged. Review and save the pending changes in **Packages**. The editor
reads already-staged samples when reopening an existing package entry. A package
switch, reload or plan revision change during preparation prevents the handoff
until the destination is reviewed, including a change at the same package path.

Levels can reference the staged WAV through their usual sound entity properties;
the shared dependency inspector and package exporter see those staged bytes.
The Audio browser lists pending additions and replacements immediately. Preview,
audition, browser WAV export, and editor reopening use an immutable snapshot of
the same package plan. A plan refresh invalidates cached playback, including a
replacement at an unchanged path. The original package still changes only when
the plan is saved. The shared inspection view keeps readable sounds available
when unrelated package conflicts block saving.
Browser WAV export copies canonical PCM16 bytes directly; when PCM conversion
is needed, it preserves supported cues and loops, including the legacy Quake/II
convention. Unsupported marker metadata blocks conversion instead of being
silently stripped. Other container tags are only preserved by direct copying.

## Place a Sound in a Level

With a Quake II or Quake III map and a folder/PAK/ZIP/PK3 open, **Stage & Place
in Level** reviews the map, package, game, XYZ position, playback mode and target
name. The initial position is the level viewport center. The preview shows the
`target_speaker`, `noise` and `spawnflags` values before applying anything.
Delivery uses the matching game preset (mono, 22050 Hz, PCM16) and the handoff's
dither choice, independently of the ordinary Stage Sound preset.

Quake II references omit the leading `sound/`; Quake III references include it.
The package path must start with `sound/`, end with lowercase `.wav`, contain
only ASCII letters, digits, underscores, hyphens, dots and directory separators,
and be shorter than 64 bytes. `.` and `..` path segments are rejected. Choose
loop initially on, loop initially off, or play when triggered. The last two need
a target name; connect an appropriate trigger's `target` to that name in Levels.
Placement does not automatically create a trigger. Embedded WAV loop markers
keep the delivery behavior described below; Quake III loops the whole sound.

A known Quake II target comment or Quake III map selects its profile. An unmarked
legacy Quake-format map requires an explicit Quake II choice because Quake uses
the same grammar. A mismatched profile is rejected. Stock Quake has no arbitrary
sound speaker, and stock Doom sound replacement uses DS lumps; these keep their
package delivery and game-specific entity workflows. Custom mod classes, sound
keys and source-port-specific entities require the level entity tools. This
dialog implements the original Quake II/III speaker contracts.

The conversion is asynchronous and cancellable. Map and package identities are
checked after review and again after conversion. Staging and the map entity are
applied together in memory; failure changes neither. The level edit is one map
undo step and the asset is one package undo step. **Their histories and saves
remain independent:** undoing placement in Levels keeps the staged sound, while
undoing the asset in Packages can leave a missing map dependency. Review **Level
Dependencies**, save both surfaces, then compile/package/test through the normal
project workflow. Handoff never marks the native audio project saved.

`map place-sound` performs the same validation and entity edit using a sound
already in an archive, folder or `.vibepackage` draft. It writes a separate `.map`
and never modifies its package input. It requires a complete mono 22050 Hz PCM16
WAV; use `asset audio-export --preset quake2|quake3` first for other source formats.

```sh
vibestudio --cli map place-sound ./maps/start.map --package ./assets --sound sound/world/hum.wav --game quake3 --origin 64,0,96 --output ./maps/start-sound.map --dry-run --json
```

`--game` may be omitted for a known target. `--mode loop-on|loop-off|triggered`
defaults to `loop-on`; `--targetname` is required for the inactive modes. JSON
includes the game, resolved package path, entity properties, origin and selector.
Dry runs validate/read the same inputs without creating output. Unknown, duplicate
or missing options are usage errors (2); incompatible/missing sound references
are validation errors (4). Map load failures retain the shared map exit codes.
Existing separate outputs require `--overwrite`; input maps and package sources
are protected even with it. Speaker dependency inspection honors the engine's
sound root so an unrelated root file cannot shadow a Quake II `sound/` asset.

`audio-level-smoke` covers independent WAV bytes, malformed input, path/game
validation, atomic rollback, both undo histories, pending dependencies, root
shadowing, CLI dry runs, JSON, exit codes and source preservation.
`audio-level-ui-smoke` exercises review/cancellation, stale map/package revisions,
actual shell/browser handoff, and 100/200% high-contrast/RTL/expanded layouts
using direct Qt calls and widget rendering. Physical keyboard and screen-reader
acceptance, game listening and native macOS/Linux execution remain separate checks.

## Game Sound Delivery

Export and package handoff share these fixed sound-effect presets:

| Preset / CLI ID | Container | Rate | Channels | Precision |
|---|---|---:|---:|---|
| WAV / `wav` | WAV | Document rate | Document channels | Chosen precision; PCM16 default |
| Doom / `doom` | DMX format 3 | 11025 Hz | Mono | Unsigned PCM8 |
| Quake / `quake` | Legacy PCM WAV | 11025 Hz | Mono | Unsigned PCM8 |
| Quake II / `quake2` | Legacy PCM WAV | 22050 Hz | Mono | Signed PCM16 |
| Quake III / `quake3` | Legacy PCM WAV | 22050 Hz | Mono | Signed PCM16 |

Game presets average all channels equally, resample with the shared anti-aliasing
converter, then quantize once, with optional dither. Their preview shows the
resulting rate, channel count, precision, and frame count. Conversion affects a
delivery copy; source samples, selection, undo history, and saved revision stay
intact. Opposite-polarity channels can cancel in a mono mix. These are conservative
presets for original-engine sound effects, not music profiles or a guarantee for
every mod/source port. They do not normalize automatically; post-conversion
headroom is reported when integer delivery clips.

DMX files use `.dmx` or `.lmp`. The 8-byte little-endian header contains format 3,
a 16-bit rate, and a 32-bit padded sample count. Sixteen bytes at each end repeat
the adjacent quantized endpoint and are excluded from playable duration. At least
17 playable output frames are required; shorter sounds fail explicitly because
DMX-compatible readers reject padded counts of 48 or fewer.

| Preset | Marker delivery |
|---|---|
| WAV | Standard WAV cues, UTF-8 labels, and `smpl` loop; positions follow the document. |
| Doom | Omits cues/loop because DMX has no marker container. |
| Quake / Quake II | Resamples marker times, emits the loop first in the legacy cue/length layout and also as `smpl`, then authored cues. Without a loop, omits cues to prevent the original engine treating the first cue as an unintended loop. Original engines play only through the loop end; later samples remain in the exported file. |
| Quake III | Standard WAV markers survive interchange, but the original engine does not use embedded loop markers. Game code controls whole-sound looping. |

The export and staging summaries report retained cue count, loop disposition,
and game-specific limitations. Delivery leaves the working metadata unchanged.

## Formats And Limits

Editable inputs are PCM WAV (8/16/24/32-bit), floating-point WAV (32/64-bit),
supported extensible WAV subtypes, digital Doom DMX format 3, MP3, native FLAC,
and Ogg Vorbis. DMX padding is
removed on import. Non-finite floating-point input samples follow the native
decoder's silence sanitization. WAV containers must be complete, with exactly one
format chunk and one sample-data chunk. Inputs are limited to 128 MiB, 1–8
channels, 1–384000 Hz, and 16,777,216 interleaved samples (64 MiB of working PCM).

Compressed import uses bundled decoders without a playback device or Qt Multimedia.
It keeps the source sample rate and channel count, decodes into finite float32
samples without clipping, and builds the editor waveform on a cancellable worker.
Save a `.vsaudio` project to retain those decoded sample bits. Saving cannot undo
loss already introduced by an MP3 or Vorbis encoder; compressed output is not offered.

| Compressed input | Contract |
|---|---|
| MP3 | MPEG-1/2/2.5 Layer III, standard CBR/VBR bitrates, mono/stereo. Complete frame sequence with a stable rate/channel count; Xing/Info/LAME gapless timing is checked against frames. Streams without gapless timing retain decoder/encoder padding. Free-format, Layer I/II and truncated frames fail explicitly. MP3 payload corruption is not comprehensively detected by frame validation. |
| Native FLAC | 1–8 channels, 4–32-bit source PCM. Complete STREAMINFO and bounded metadata; declared sample count and nonzero stream MD5 are verified when present. At least a sample count or MD5 is required. Float32 retains every sample through 24-bit PCM; higher precision may round. |
| Ogg Vorbis | One complete, sequential logical stream, 1–8 channels, with checked page CRCs, packet bounds and final sample count. Vorbis channels are reordered into standard WAVE/FLAC speaker order without mixing. Chained/multiplexed streams, Ogg Opus, Ogg FLAC and cropped streams whose decoder length disagrees with the final granule are unsupported. |

Compressed container tags, artwork and embedded marker conventions are omitted.
The import status and CLI reports expose that warning; native documents preserve
it in `metadata.importWarnings`. Author cues and a loop with **Markers…** after import.
Multichannel samples retain their order; the editor does not yet expose speaker
layout labels or preserve a source channel-mask field. Each decoder has a 16 MiB
allocation budget in addition to the input/output limits. Metadata is capped at
4 MiB and Vorbis packets at 1 MiB (4 MiB for comments); unusually large valid
streams may be rejected with a clear diagnostic. Cancellation and errors discard
partial samples and preserve the current document.

The Audio browser's compact preview still reads compressed headers only; open
**Edit Sound** for the decoded waveform. **Export WAV** performs complete stream
validation and PCM16 conversion asynchronously, with cancellation during preparation.
Once atomic writing starts, its actual completion or failure is reported.

PC speaker tone sequences and MUS/MIDI are not implemented. Initial input capture
uses the session's Record / Takes workflow described below.
Clipboard mixing combines samples; independently editable layers use Multitrack.

## Authoring Scope

The waveform document is a single sound asset with up to eight interleaved
channels. That fits game sound-effect cleanup, assembly, conversion, and package
delivery. Clipboard mixing permanently combines samples as one undoable edit;
it does not retain independently editable tracks. The separate multitrack session
now supports initial layered sound design and reproducible mixdown into that
delivery pipeline. Full music production and advanced recording remain on the
[DAW plan](plans/audio-daw.md), including synchronized overdubbing, monitoring,
automatic latency calibration, dedicated comp lanes, surround routing, plugins, MIDI
and low-latency engine acceptance. Read-only true-peak and integrated loudness analysis is
available for reviewing rendered music and voice.

## CLI

The audio commands below accept the supported compressed inputs through the same
decoder as the editor. For example, `asset audio-project ./wind.flac --output
./wind.vsaudio --json` imports a separate lossless working document. `audio-edit`,
`audio-analyze`, `audio-markers`, `audio-export`, and package `audio-wav` also share
compressed decoding and report import limitations. `--dry-run` prepares and
validates the result without writing it.

`asset audio-generate --prompt <description> --game <game> --output <project>`
makes new sound effects with the synthesizer, or with `--source ai` the
configured sound model, and delivers them through the presets in
[Game Sound Delivery](#game-sound-delivery); `--preview <wav>` writes the
working sound to listen to first. Its options are in
[CLI Strategy](CLI_STRATEGY.md).

`asset audio-markers <input>` inspects a sound/project, with optional `--entry`
for a package sound. To replace the complete marker set, pair `--markers
<JSON file>` with `--output <separate .vsaudio|.wav>`. The manifest is bounded to
64 KiB and has exactly these fields (use empty cues and `null` loop to clear):

```json
{"cues":[{"id":1,"frame":2205,"name":"Wind starts"}],"loop":{"first":4410,"end":22050}}
```

Frame limits follow the input sound. Unknown fields and invalid ranges/IDs/names
fail validation. Output supports `--dry-run`, explicit `--overwrite`, and the
usual WAV precision/dither options; `.vsaudio` preserves exact samples. Input,
recorded source, and manifest paths are protected. Recovery ownership metadata
is cleared when creating an editable copy. JSON `audioMarkers` contains `before`
and resulting `markers`, format, output, dry-run, and written state. Invalid
options use exit 2; marker/output validation failures use 4; unreadable inputs
use 1. Inspection and dry runs create no files or locks.

```sh
vibestudio --cli asset audio-markers ./wind.vsaudio --json
vibestudio --cli asset audio-markers ./wind.wav --markers ./markers.json --output ./wind-marked.vsaudio --dry-run --json
```

`asset audio-analyze` reads WAV, DMX, or `.vsaudio`, with optional `--entry` for a
package sound and exact `--start-frame`/`--end-frame` bounds. It defaults to the
whole sound, independently of the selection saved in a project. An empty range
is valid: levels are zero and frame positions are absent. JSON `audioAnalysis`
has `schemaVersion: 1`, format/range fields, overall statistics, and a `channels`
array with one-based channel numbers. Additive fields include overall/per-channel
`truePeak` and `truePeakDbtp`, true-peak status/message/method/oversampling, and a
`loudness` object with method, status/message, `integratedLufs`,
`relativeThresholdLufs`, gate settings, block size and source-order `channelMap`.
Silent dBFS/dBTP, absent measurements and absent positions use JSON `null`.
`--channel-map L,R,C,LFE,Ls,Rs` supplies reviewed roles; available IDs are `L`,
`R`, `C`, `LFE`, `Ls`, `Rs`, `Lb`, `Rb`, `Cb` and `X` (excluded from loudness).
Use exactly one role per source channel without repeated speaker positions;
`LFE`/`X` may repeat. IDs are case-insensitive. With no map, only mono/stereo
loudness is automatic; surround true peak still works. `--no-loudness` skips
integrated metering and cannot be combined with `--channel-map`.
Invalid numeric/role options return exit 2, invalid audio/ranges/maps exit 4, and
unreadable sources exit 1. Analysis creates no output or lock files and rejects
delivery/edit options such as `--output` and `--operation`.

```sh
vibestudio --cli asset audio-analyze ./wind.vsaudio --json
vibestudio --cli asset audio-analyze ./surround.wav --channel-map L,R,C,LFE,Ls,Rs --json
vibestudio --cli asset audio-analyze ./sound.wav --no-loudness --json
vibestudio --cli asset audio-analyze ./sounds.wad --entry DSWIND --start-frame 0 --end-frame 11025 --json
```

`asset audio-export` renders a separate delivery without changing an editable
project. It accepts WAV/DMX/`.vsaudio` input, optional `--entry` for a package,
required `--output`, and `--preset wav|doom|quake|quake2|quake3` (default `wav`).
Only `wav` accepts `--wav-format`; game presets fix precision/rate/channels.
`--dither none|tpdf` and an optional unsigned `--dither-seed` apply to final integer
quantization. Existing outputs require `--overwrite`; input and native provenance
paths remain protected. `--dry-run` performs conversion/encoding but creates no
output or lock. JSON `audioExport` reports input/output format, frames, bytes,
converted peak/headroom, dither, and whether it wrote a file. Invalid options use
exit 2, invalid source/conversion exit 4, and read/write failures exit 1.
Marker fields are `inputMarkers`, `outputCues` (authored cues, excluding the
legacy loop sentinel), `outputLoop`, and `markerSummary`.

```sh
vibestudio --cli asset audio-export ./wind.vsaudio --preset doom --output ./DSWIND.dmx --dither tpdf --dry-run --json
vibestudio --cli asset audio-export ./wind.wav --preset quake3 --output ./wind-game.wav
vibestudio --cli asset audio-export ./sounds.wad --entry DSWIND --output ./wind-float.wav --wav-format float32
vibestudio --cli package save-as ./sounds.wad --output ./sounds-edited.wad --add-file ./DSWIND.dmx --as DSWIND --dry-run
```

`asset audio-edit` uses the same decoder, processing, and writer as the editor:

```sh
vibestudio --cli asset audio-edit ./wind.wav --operation normalize --db -1 --output ./wind-normalized.wav --dry-run --json
vibestudio --cli asset audio-edit ./wind.wav --operation trim --start-frame 2205 --end-frame 22050 --output ./wind-trimmed.wav
vibestudio --cli asset audio-edit ./mod.pk3 --entry sound/ambience/wind.wav --operation fade-out --start-frame 11025 --end-frame 22050 --output ./wind-faded.wav
vibestudio --cli asset audio-edit ./wind.vsaudio --operation gain --db 2 --output ./wind-louder.vsaudio
vibestudio --cli asset audio-edit ./wind.vsaudio --operation resample --sample-rate 22050 --output ./wind-22050.vsaudio --json
vibestudio --cli asset audio-project ./wind.vsaudio --output ./wind-float.wav --wav-format float32
vibestudio --cli asset audio-edit ./wind.vsaudio --operation normalize --db -1 --output ./wind-final.wav --wav-format pcm16 --dither tpdf --dither-seed 42 --json
vibestudio --cli asset audio-new --sample-rate 44100 --channels 1 --output ./new-sound.vsaudio
vibestudio --cli asset audio-edit ./new-sound.vsaudio --operation paste --paste-input ./wind.wav --output ./assembled.vsaudio
vibestudio --cli asset audio-edit ./assembled.vsaudio --operation insert-silence --start-frame 0 --frames 4410 --output ./delayed.vsaudio
vibestudio --cli asset audio-edit ./wind.wav --operation mix --paste-input ./rain.wav --start-frame 11025 --output ./weather.vsaudio
```

Operations are `trim`, `delete`, `silence`, `reverse`, `fade-in`, `fade-out`,
`gain`, `normalize`, `mono`, `stereo`, `invert`, `remove-dc`, `paste`, `mix`, and
`insert-silence`, and `resample`. The default range is the entire clip; `--db`
is accepted only for gain and normalization. Paste and mix require a local
`--paste-input` WAV, DMX, or `.vsaudio` file with matching format. Paste replaces
the range; set equal start/end frames to insert without replacing. Mix and silence
insertion use `--start-frame` and reject `--end-frame`. Silence insertion requires
`--frames`. Both source files are protected against output overwrite. Existing outputs require
`--overwrite`. `--dry-run` validates and processes without creating an output.
JSON reports include input/output frames, the selected range, sample rate,
channels, peak level, clipped sample count, output path, and whether a file was written. Stage the exported file
with the usual `package save-as --add-file ... --as ...` workflow.

`resample` requires `--sample-rate` and rejects frame-range options because the
document format changes as a whole. Native input selections are mapped by time;
native output preserves project metadata and source provenance. JSON includes
`inputSampleRate`, the resulting `sampleRate`, and the new frame count. Dry runs
perform and validate conversion but create no output.

WAV output from `audio-edit`, `audio-project`, and `audio-new` accepts
`--wav-format pcm8|pcm16|pcm24|pcm32|float32` (default `pcm16`),
`--dither none|tpdf` (default `none`), and `--dither-seed` when TPDF is selected.
These options are rejected for native output and inspection-only commands.
`audio-edit` and `audio-project` JSON include WAV format, bits, dither, and the
seed as a decimal string, so large seeds remain exact. `losslessOutput` refers
to sample preservation; `metadataPreserved` separately identifies native
metadata retention. Float output reports no integer clipping.
Typed cues/loop are reported separately: `audio-project.markers`, and
`audio-edit.inputMarkers`/`markers`. `metadataPreserved` refers to opaque native
metadata, not the supported WAV cue/loop contract.

`asset audio-new` accepts `--sample-rate` (default 44100), `--channels` (default 1),
`--frames` (default 0), and required `--output`. Zero frames require `.vsaudio`;
positive frames create silence and may also output WAV with the precision options above. It supports the
same dry-run, overwrite, and JSON options. Deleting all samples likewise requires
native output; an empty document is not a playable WAV.

`asset audio-project <input>` inspects a project, recovery copy, WAV, or DMX sound.
Add `--output <file.vsaudio>` to import/copy/recover losslessly, or `--output
<file.wav>` for WAV export (PCM16 by default). It supports `--json`, `--dry-run`, and explicit
`--overwrite`; source paths remain protected. Recovery metadata is reported on
inspection and removed when saving a new editable copy. `asset audio-edit` also
accepts `.vsaudio` input/output so CLI edit chains need not quantize between steps.

`asset audio-recoveries [--directory <folder>] --json` uses the same verification
and returns `audioRecoveries.records`, storage limits, scan state, and individual
errors. Exit `0` means the inventory verified; `4` means corrupt, unreadable, or
incompletely inspected content; `2` means invalid command options. To preview
discard, add `--discard <id> --expected-sha256 <reviewed-hash>`. It remains a
read-only dry run unless `--write` is supplied; `--write` and `--dry-run` are
exclusive. Restore/export a copy with `asset audio-project --output` as above.

`audio-recovery-smoke` covers inventory verification, corrupt records, live
leases, stale digests, storage/count/scan limits, read-only dry runs, exact
discard boundaries, cancellation, and CLI validation. The editor suite checks
manager integration, draft restore, live-copy protection, and scaled layouts.
`audio-startup-ui-smoke` adds background metadata discovery, cancellation,
notice ordering without focus changes, one offer per session, disabled startup
scans, direct File/command recovery, exact draft restoration, preference
synchronization, and source/copy preservation. Setup controls are exercised with
direct Qt methods and rendered at 100% and 200% expanded RTL high contrast.
Physical keyboard and screen-reader acceptance remain separate release checks.

## Validation

`audio-routing-smoke` checks independent bus/send, polarity, mono/stereo and
path-aware solo expectations, bus automation, disabled-edge cycles, limits,
version-1/2 migration and version-3 round trips. Its capacity fixture combines
64 strips, 32 buses, 4,096 clips and 16,384 gain points; the 256- and 65,536-frame
renders must match exactly and agree with an independent scalar recurrence.
Finite high-gain chains must report overflow and clear the entire output.
The playback worker fixture compares routed samples with offline rendering
through partial byte writes. The real CLI fixture checks bus/send edits,
invalid literal IDs, file preservation and exact routed WAV output.

`audio-routing-ui-smoke` invokes the session action directly through Qt, then
checks draft validation, one-step undo/redo, native save/reload and bus take
import rejection. Direct-widget renders cover 100/125/200% themes, doubled
translated strings, RTL and a 600-pixel-wide scrollable layout. The fixture
also verifies that its expanded translator reaches the actual routing context.

`audio-effects-smoke` checks independent frequency responses, shelf endpoints,
fractional-delay recurrences, compressor/gate timing, stereo linking, limiter
ceilings and saturation curves. Stateful blocks must be sample-exact across
block sizes; seek/stop resets, continuous loop histories and pause continuity
have explicit fixtures.
Cancellation and late overflow clear complete output and history. Native v4
malformed-state rejection, memory/tail bounds and a 64-strip/520-effect solo
graph exercise admission and maximum scratch storage. This headless workload
does not establish hardware latency or dropout performance.

`audio-effect-automation-smoke` checks all 68 automatable numeric parameters against manual
settings, independent curve/gain/fractional-delay oracles, exact block-size
invariance, seek/loop clocks, nonlinear solo isolation, maximum reverb taps and
65,536-point native round trips. It independently downgrades v4 envelopes to
v1/v2/v3 point layouts and checks strict transactional rejection. The automation
UI fixture verifies draft read/off state, parameter switching, cancellation,
preset pruning, native accessibility metadata and scaled/expanded RTL layouts.
Track automation actions verify the static-pan fallback and one-step undo/redo.

`audio-latency-smoke` checks independent limiter ceilings, linked stereo gain,
future transient reduction, exact delay/reset behavior, parallel polarity nulls,
nested bus/pre/post/master alignment, absolute automation and modulation phase.
It covers arbitrary blocks, ranges, short loops, pause/resume/seek/stop, cancellation,
the final legal timeline frame, shared-container reads and aggregate effect plus
compensation memory admission. GUI/CLI, native/recovery, stem and fake-output
fixtures include lookahead processing. This does not establish device latency.

`audio-effects-ui-smoke` invokes the actual track/master actions, checks staged
parameters/order/bypass/tail, one-step undo/redo and native reload, and renders
the scrollable inspector at 100/125/200%, high contrast and expanded RTL.
The CLI fixture verifies effect operations, protected failures, dry runs and
exact float WAV parity. Recovery fixtures retain complete effect state; the
device-free playback worker compares effects output with the offline renderer
through writes that split sample bytes. Native devices and screen readers need
separate acceptance.

`audio-playback-loop-smoke` compares an independently expanded linear
arrangement against repeated playback, with bus sends, nonlinear solo paths,
track/bus/master automation, delay, reverb, modulation and lookahead longer than
a cycle. An independent echo recurrence checks exact delayed samples. Fixtures
cover allocation-free processing, arbitrary blocks, one-frame loops, history
reset/pause, cancellation and the separate physical clock bound. CLI float
digests and partial-write worker output use the same linear oracle.

`audio-session-transport-smoke` checks a modulo/analytic sample oracle, far-frame
loops, arbitrary block boundaries, fade/automation parity, pause/seek, finite tails,
headroom and cancellation. `audio-session-playback-smoke` exercises the actual
worker with a fake output: partial byte writes, drain timing, pause/resume,
seek-buffer invalidation, dropouts, stalls, disconnects, cancellation and long
sparse ranges. Physical output and low-latency monitoring acceptance remain open.

`audio-session-smoke` checks independent signal expectations, exact block-boundary
invariance, automation/fades, mute/solo, nondestructive edits, far frame positions,
native corruption and round trips, cancellation and external-save races. Streamed
WAV is compared with the independently tested precision encoder. The CLI suite
executes real JSON commands, checks dry runs, explicit SRC and source protection.
`audio-session-ui-smoke` uses direct Qt calls and a fake playback backend for
worker completion, undo/save state, cancellation, mix handoff, history-branch
storage release, exact timeline geometry and accessible controls. Optional direct
widget renders cover dark, high-contrast light and 200% expanded RTL layouts.
These checks do not establish physical device or screen-reader acceptance.

`audio-clip-smoke` covers sample math, stereo frame ordering, malformed/truncated
input, cached waveform ranges against independent full scans, cancellation,
protected export, staging conflicts, and CLI/GUI output
equivalence. `audio-project-smoke` adds independent float-byte fixtures, checksum
and metadata corruption, exact project round trips, dry runs, concurrent locks,
external-write races, protected paths, cancellation, recovery, and CLI project
workflows. `audio-resample-smoke` compares converted tones with analytic signals
for pitch, phase, and passband amplitude, checks rejection of out-of-band tones,
impulse position/symmetry, eight independent channels, float headroom, exact
same-rate samples, bounds, empty/short documents, concurrent workers, and
cancellation. `audio-export-smoke` checks independent PCM byte fixtures, exact
float bits, RIFF padding and extensible headers, dither statistics/reproducibility,
cancellation, locks, protected paths, and integer staging. `audio-delivery-smoke`
checks independent DMX bytes/padding, minimum length, preset headers, mono
amplitude, quantization error, cancellation, source protection, WAD staging/save,
and CLI JSON/exit codes. `audio-editor-ui-smoke` exercises the actual dialog and shell
handoff, including undo/redo, saved revisions, failed loads, cancellation,
native save conflicts, recovery restoration, changed package context, new/cut/paste/mix,
empty saves, waveform cursor/zoom/pan, frame fields, high contrast, 200% text, RTL layout,
and expanded translations. It calls widget methods directly and optionally
renders widgets into images; it does not inject user input or capture the desktop.
Qt accessibility-interface checks also verify that named document, analysis,
delivery, placement and recovery labels expose their current text, and that
correcting a marker field clears the previous accessible validation error.
`audio-history-smoke` adds injected worker allocation/reader failures, late-failure
cancellation, bit-exact no-op and signed-zero decisions, saved revision retention,
history branching, the 32-step limit and the 256 MiB budget with full-size buffers.
It records near-limit edit time and GUI timer activity without requiring an
output device or control of the user's keyboard or mouse.
`audio-transport-smoke` adds deterministic backend-event tests for exact start
and end bounds, pause/resume, live and paused seek precision, non-seekable media,
loop/volume changes, deferred repeats when a backend announces EOF, both
EOF/Stopped event orders, repeat cancellation and timeout, natural completion,
missing-device errors, late callbacks,
retry, loading/buffering timeouts and cancellation before autoplay. Its UI checks
compare float bits in prepared selections and render 100/200% high-contrast dark/
light, expanded RTL layouts. These tests use an injected backend, so they never
open a physical audio device. With Qt Multimedia enabled, the suite also passes
mono, stereo and six-channel prepared float WAV through QAudioDecoder and checks
exact samples and frame counts without an output device. Device listening,
unplug/replug acceptance and native platform playback behavior have separate
release checks in the [audio editor audit](plans/audio-editor-release-candidate.md).

`audio-browser-smoke` exercises unknown/updated media durations, initial seeks,
late session callbacks, coalesced requests, streaming cancellation, checksum
failure, byte-limit preflight, and a near-64 MiB preview with GUI timer activity.
It also drives the real browser commands, editor handoff and repeated WAD
occurrence export through direct Qt calls and a fake output backend. It verifies
selection clearing, same-path staged replacement and package undo. Browser
renders cover 100/200% high contrast, expanded labels and RTL. These checks make
no claim about physical audio output or manual assistive-technology acceptance.

Set `VIBESTUDIO_AUDIO_BENCHMARK=1` when running the UI smoke test to include a
16,777,216-sample workload. It reports worker load time, GUI timer activity during
loading, and full/detail waveform render time. These are measurements of the
current build/machine, not a guarantee about every device or release build.

`map dependencies --package <draft.vibepackage>` reviews the same planned
contents used by sound placement. Staged deletion appears as a missing reference;
undo restores it. Corrupt drafts fail to load, and review does not modify them.
`audio-level-smoke` verifies these CLI boundaries alongside sound placement.

The optional `src/tests/audio_compiler_workflow.py` accepts `--binary`, `--qbsp`,
`--q3map2` and a new `--output-root` inside `.agents/tmp`. It generates an original
PCM fixture, imports and edits a native project, exports the Quake II/III sound
presets, and places speakers using unpublished package drafts. The existing
generated texture/room fixture supplies compiler inputs without game assets.
It checks exact edited PCM bytes, dry runs, source preservation, dependency
review, compiled BSP speaker properties and final PAK/PK3 payloads. Reopening
the packaged sound through Audio analysis closes the delivery loop. Compiler
commands, logs, executable hashes and a verification report remain in the
chosen directory. This checks compiler and package handoff; physical listening,
game execution and manual GUI accessibility remain separate release checks.

## Recording and Recorded Takes

The [duplex integration contract](AUDIO_DUPLEX.md) describes the tested internal
live mixer, punch clock, capture queue and optional native device adapter.
The recording worker now coordinates permissions, playback acknowledgement,
per-arm disk journals, bounded telemetry and failure recovery in `.vsrecord`
folders. **Multitrack → Record Tracks…** exposes explicit input/output selection,
up to eight armed mono/stereo tracks, backing start, punch in/out frames, per-track
monitoring and device timing. Input opens only after Record and permission,
playback shutdown and journal preparation. Monitor levels affect listening only;
stored input remains dry. Monitoring starts off. The selected session rate must
be supported by the devices; no implicit rate conversion occurs.

**Loop passes** repeats the punch range 1–10,000 times, within the recording
timeline limit. Preroll plays once. Monitoring, delay/reverb tails and other
processing remain continuous across passes; automation repeats at the same
musical positions with processing latency accounted for. Stop retains any
partial final pass. **Review → Recorded pass** chooses a pass independently for
each arm. Trim frames are local to that pass, and recorded alignment places all
passes at the original punch range. Switching pass resets its trim. Review
imports one pass per arm by default; retained folders can be opened again.
Enable **Assemble comp sections** to queue several ranges, including repeated
sections from one pass. Add the current fields, select a queued row to update
or remove it, and set **Crossfade frames**: zero for hard cuts, at least two for
complementary linear fades after adjacent cuts. The outgoing pass needs extra
audio after its cut, with matching channel counts and sufficient incoming
length. Overlaps or unavailable handles are rejected. **Import Comp** creates
editable clips and optional grouping in one undo step. Journals are retained;
session save, recovery, rendering and delivery use the existing clip/fade path.

During a pass, **Meters** shows each mapped dry input and both device-output
channels. Sample peak, RMS, held maximum, headroom and over-range counts remain
visible with explicit full-scale/clipping states. Input includes preroll; output
is measured before safety clipping. **Reset Meter History** changes readings
only, with a pending state until the worker acknowledges it. Final readings stay
available after Stop or completion but are not stored in the recording folder.
For reopened takes, use reviewed export and waveform analysis or imported-session
meters. The meters do not measure analog clipping or true peaks.

After completion or Stop, Review selects verified takes. Choose destination
tracks, frame/channel ranges, recorded or manual placement, replace versus layer,
and optional linked clips. Interrupted passes require explicit prefix acceptance.
Import re-verifies reviewed hashes on a cancellable worker and adds the whole
pass as one undoable change. **Review → Open Recording…** reopens retained folders
against the current session; add destination tracks before opening review.
**Show File and Import Details** exposes verification and the import plan for
`asset audio-recording import`. Inspect/import CLI paths never open devices.
See [the complete contract and CLI fields](AUDIO_DUPLEX.md). Device selections
and arming are not restored. Hardware timing, open-ended loops, dedicated comp
lanes and reconnect handling remain open.

**Save Review** and **Save Review As…** preserve the active choices in portable
review JSON. **Open Saved Review…** verifies and restores every ordered selection,
including multiple passes from one arm, into **Use queued take selections**.
Comp assembly enables the queue and adds its validated crossfades; non-comp
queues retain ordinary layered-import rules. A file/status label shows unsaved
changes. Close, Open and Record offer Save/Discard/Cancel before replacing changed
choices; a failed save leaves the working review open. Saving an opened review checks its loaded revision, so external changes
require reopening or Save As. The same files feed CLI import/preview and
`asset audio-recording save-review`. Recipes remain separate from native sessions;
keep the corresponding session and recording journals. Reimport follows the
reviewed ranges/replacement flags and does not identify or revise an earlier
import automatically. See [Saved reviews](AUDIO_DUPLEX.md#save-and-reopen-recording-reviews).

Before import, **Audition Current Section** plays the focused take fields and
**Audition Review** plays the queued comp or checked takes. The Audition tab has
output/buffer selection, listening volume, Pause/Resume, frame seeking and repeat.
Disable **Include backing clips** to isolate selected clips through the existing
mixer/effects; mute and solo remain effective. Audition spans the imported clips
without added preroll or tails. Hashes are checked again, and listening adds no
session edit or undo step. Review edits stop stale playback. Recording, import
and close wait for audition output shutdown. CLI `asset audio-recording preview`
exports the same reviewed span to float32 WAV without a device; `--isolated`
excludes backing and `--dry-run` verifies without writing.

The standalone capture workflow follows.

In **Multitrack → Single Take…**, choose a new `.vstake` file, an explicit
input device, the device channel count and the channel numbers to record.
Channel numbers in the UI and CLI start at 1; comma order determines stored
channel order. Select up to eight distinct channels from a 1–32-channel input.
Capture uses the session rate and a requested 256–16,384-frame device buffer.
An unsupported rate/channel configuration fails without resampling or switching
devices. Supported device PCM is float32, signed 32/16-bit or unsigned 8-bit;
conversion preserves float headroom and does not apply gain or clipping.

Arm the channels and press **Record**. Opening the dialog, refreshing inputs or
arming does not open a microphone. Only Record requests platform permission and
starts input. Session, browser and waveform playback stop before recording.
This standalone capture workflow has no backing playback or software monitoring;
use Record Tracks for synchronized backing, monitoring, punch and finite loop
recording. Comping remains open. Positive
compensation values place imported audio earlier; these are user-measured frame
offsets, not an automatic device-latency calibration.

Input and storage have separate workers. A fixed queue holds sixteen blocks of
up to 4,096 frames, plus one assembling block. The status shows received and
stored frames, queue occupancy, sample-peak hold and over-full-scale samples.
Stop closes input and flushes already delivered complete frames. Device-buffer
samples not yet delivered to the worker are outside that stopping boundary;
input/output sample-exact alignment is not established. A full disk queue,
write/flush failure, reported device error/dropout, invalid sample or five-second
no-data stall stops capture visibly. Missing samples are never synthesized.
Destroying an active capture controller closes input and joins storage without
marking the take complete. Its retained frames still require prefix review;
normal Stop writes the completion record after the queued frames drain.

Each journal block has a chained checksum and an OS flush request. Interrupted
recording retains earlier verified blocks; the independent process-interruption
fixture does not establish power-loss durability for every filesystem/device.
The take file is a user document and is never overwritten, resumed, automatically
evicted or removed by the document-checkpoint manager. It is capped at 64 GiB and
33,177,600,000 frames. At most one block being assembled, queued blocks and a
partially written disk block may be lost on abrupt termination.

After Stop, **Review / Import** shows the verified frame count, channel map,
completion/error state and prefix SHA-256. **Open Take…** or normal **Open Audio**
can reopen a saved or interrupted take. Incomplete/damaged files require explicit
acceptance of the verified prefix. Recovery stops at the first bad record; it
never skips a gap. A changed verified prefix requires a new review. The digest
covers verified data, not ignored trailing damage.

Choose a first frame, exclusive end frame and one or two stored channels.
Recorded placement is `position + firstFrame - compensation`; disable that option
to enter a manual position. Negative or out-of-range placement is refused.
Import verifies the complete reviewed prefix again on a cancellable worker and
uses the normal session import, undo, save and recovery services. Original take
paths remain protected provenance. Import additional channel pairs on separate
tracks. Session import still admits at most 16,777,216 interleaved samples per
source and 67,108,864 across the session; long takes must be imported in selected
ranges. The journal streams to disk, but session media is still embedded in memory.
Recorded paths are not automatically scanned at startup; reopen the chosen file.

The device-free CLI uses the same scanner and range reader:

```sh
vibestudio --cli asset audio-take inspect ./voice.vstake --json
vibestudio --cli asset audio-take export ./voice.vstake --expected-prefix-sha256 <reviewed-hash> --start-frame 0 --end-frame 48000 --channels 1,2 --output ./voice.vsaudio --allow-incomplete --dry-run
vibestudio --cli asset audio-take export ./voice.vstake --expected-prefix-sha256 <reviewed-hash> --start-frame 0 --end-frame 48000 --channels 1 --format wav --output ./voice.wav --allow-incomplete
```

`--format project` is the default and retains placement/provenance metadata.
`wav` writes float32 for further waveform editing, analysis and game delivery.
`--allow-incomplete` is required only for incomplete/damaged prefixes. Existing
outputs require `--overwrite`; inputs remain protected. These CLI commands never
open an input device. Take review and export work with Qt Multimedia disabled.

Recording requires optional Qt Multimedia. On macOS, Qt 6.5+ permission support,
the Qt microphone permission backend and a properly packaged application's
`NSMicrophoneUsageDescription` are prerequisites; the usage-description guard
refuses undeclared access. The current portable packaging path has not passed
macOS microphone-bundle acceptance. Windows/Linux use supported Qt permission
handling or OS enforcement when opening the input on older Qt. Input selection,
arming and compensation are per dialog and never restored as an active recording.

`audio-take-smoke` checks independent wire records, every byte truncation,
corruption, changed digests, exact channel/range reads and process interruption.
`audio-capture-smoke` injects partial PCM reads, queue overload, disk faults,
disconnects, non-finite samples, stalled input and stop/destruction. CLI and
widget suites cover reviewed export, arming, session undo/save handoff and
scaled/expanded/RTL layouts. They do not establish hardware monitoring,
latency, native permission prompts or platform durability acceptance.

### Take Journal Wire Format

`.vstake` version 1 begins with `56 53 54 41 4B 0D 0A 1A`, followed by little-endian
uint32 version and JSON byte length at offsets 8/12. At most 64 KiB of UTF-8 JSON
contains exactly `name`, `sampleRate`, `inputChannels`, `channelMap` (zero-based
hardware indices), `position`, `latencyFrames`, `trackId`, `sourceSessionPath`,
`deviceName` and `startedUtc`. Timestamp text uses UTC ISO format. Text, channel,
rate, frame and compensation bounds are validated. The header ends in SHA-256
of all preceding header/JSON bytes, which seeds the record chain.

Each record has a 24-byte header: four-byte `DATA` or `DONE` tag, uint32 sequence,
uint64 first frame, uint32 frame count and uint32 payload byte count, all numeric
fields little-endian. Sequence starts at zero and frames must be contiguous.
A DATA record has 1–4,096 frames of interleaved finite float32 little-endian samples
in stored channel order. DONE has zero frames and no payload. Both append
SHA-256 of the previous checksum, record header and payload. DONE must end the
file for a complete take. Scanning stops before an incomplete, damaged or invalid
record and retains the preceding verified checksum/frame count. Writers use
new-only creation, a cooperating path lease, reject links and request file flushes
through `_commit` on Windows or `fsync` on POSIX (also the new parent directory
on POSIX). Readers never open the recorded source-session path.

### Native Project Wire Format

Session recovery uses a separate version-1 `.vssession-recovery` envelope, leaving
the native session schema unchanged. Its magic is `56 53 52 4D 58 0D 0A 1A`,
followed by little-endian uint32 version and JSON length at offsets 8/12. At most
64 KiB of UTF-8 JSON contains exactly `sourcePath` and `writtenUtc` (UTC ISO time).
The complete native `.vssession` follows, then a 32-byte SHA-256 over everything
preceding it. The envelope is capped at the native 320 MiB limit plus 64 KiB and
48 framing bytes. Readers verify the envelope and native checksums, metadata,
all samples and arrangement bounds before replacing a draft. Managed inventory
scans only top-level UUID filenames and never opens the recorded source path.

`audio-session-recovery-smoke` tests an independent envelope fixture, malformed
metadata/native content, mixed storage budgets, leases, exact-kind discard and
CLI restoration. The offscreen recovery UI suite tests coalescing, interrupted
child processes, retirement, saved revisions, original-copy preservation and
shared startup/preferences. Tests use fake playback and no physical input/output.

Multitrack `.vssession` versions 1–7 use `56 53 4D 49 58 0D 0A 1A`, followed by
little-endian uint32 version and JSON size at offsets 8/12. The UTF-8 JSON is
bounded to 16 MiB in versions 4–7 (4 MiB for versions 1–3) and stores name, rate,
tempo, meter, master gain, exact session
length, source records, tracks, regions and gain/pan points. Each source record
includes the byte length of its embedded `.vsaudio` document, in source-array
order. The final 32 bytes are SHA-256 over everything preceding them. Total size
is limited to 320 MiB. Readers verify dimensions, source provenance/lengths,
strictly ordered automation, source ranges, finite samples, embedded and outer
checksums, and exact payload consumption before replacing a session. Unknown
container versions or arrangement fields fail, preventing silent loss of future
processing state. Individual `.vsaudio` sources retain their own metadata
and marker format described below.

The writer emits version 7; versions 1–6 remain readable. Version 2 introduced the required track `routing`
object with exactly `bus`, `outputId`, `outputEnabled`, `invertLeft`,
`invertRight`, `swapChannels` and `sends`. Each send contains exactly `targetId`,
`gainDb`, `pan`, `preFader` and `enabled`. Empty destination IDs mean master;
other destinations must identify buses. Gain is finite in −96…+24 dB, balance
in −1…+1; duplicate send destinations and cycles are rejected. The reader migrates
version-1 tracks to direct-master audio strips with no sends or channel changes.
Version 3 also requires a track `effects` array and root `effects` object with
exactly `master` (an effect array) and `tailSeconds`. Each effect contains exactly
`id`, `type`, `enabled` and `parameters`; its parameter object must contain every
key for that processor and no others. Chain IDs must be unique within their
chain. Unknown types/keys, missing fields, non-finite/out-of-range values and
excessive state allocation fail before a session is replaced. Versions 1/2 load
with empty chains and the default tail setting, preserving their original range.
Version 4 adds the required track `effectAutomation` array and root
`effects.automation` array. Each lane has exactly `effect`, `parameter`,
`enabled` and `points`; each point has exactly `frame`, `value` and `curve`
(`linear`, `step` or `smooth`). Gain/pan points also require `curve`. Duplicate
parameter targets, missing effects, invalid curves and excessive aggregate
points fail validation. Versions 1–3 migrate their two-field gain/pan points
to linear curves and empty effect lanes. Older applications reject version 4;
there is no silent automation downgrade.

Version 5 adds a required root `timing` object containing exactly `beatUnit`,
`tempoChanges` and `meterChanges`. The existing root `tempo` and `beatsPerBar`
fields retain origin values. Tempo entries contain exactly `tick` and `bpm`;
meter entries contain exactly `bar`, `beatsPerBar` and `beatUnit`. Entries are
strictly ordered and exclude tick 0/bar 1. Versions 1–4 load an empty change map
with denominator 4. Unknown fields, unsupported meters, duplicate changes and
positions beyond the session frame limit fail before adoption. Older readers
reject version 5, preventing silent loss of musical timing.

Version 6 adds root `groups`, an array of exactly `{id, name}` objects, and
region `groupId`, `fadeStart`, `fadeSpan` fields (13 region fields total).
Group IDs are unique, names are nonempty and at most 256 UTF-16 code units,
and every group must reference at least two clips. Empty `groupId` is ungrouped;
other IDs must exist. The group limit is 2,048 within the 4,096-clip limit.
`fadeSpan == 0` means the current region length and requires `fadeStart == 0`.
Otherwise the positive span is bounded by the source sample limit, and the
visible region must fit in `[fadeStart, fadeStart + length)` within that span.
Fade lengths fit the envelope span. At local frame `i`, evaluate the existing
linear fade product at `i + fadeStart` over that span. Splits retain the original
span and advance the right segment's start, so repeated cuts preserve audio
without creating new sample buffers. Source offsets remain independent.
Versions 1–5 supply empty groups and zero fade windows. Older applications
reject v6 instead of dropping edit links or changing split fade audio.

Version 7 adds required `segment` to every gain/pan/effect automation point.
It is `null` for ordinary adjacent-point interpolation or exactly
`{offset, frames, first, last}` for an inherited curve domain. Frames/offset are
whole timeline-bounded values, with `0 <= offset < frames`; the visible segment
must fit in the remaining span. `first`/`last` are finite values within that
parameter's bounds. The authored point value must match its original-domain
sample (relative tolerance `1e-12`). Step and terminal points cannot carry a
domain. At the next point, that point's value takes precedence, allowing a
splice discontinuity without reshaping preceding samples. Versions 1–6 migrate
with ordinary domains; older applications reject v7 instead of discarding them.

Version 1 uses the eight bytes `56 53 41 55 44 0D 0A 1A`, a little-endian uint32
version at offset 8, and a little-endian uint32 JSON byte length at offset 12.
UTF-8 JSON begins at offset 16 and is limited to 256 KiB. Required keys are
`channels`, `sampleRate`, `frames`, `selectionStart`, `selectionEnd`, `sourceName`,
`sourcePath`, and the `metadata` object. Selection ends are exclusive. Channel,
rate, frame, and selection values must be bounded integers. Zero frames are valid
and require both selection bounds to be zero. Source strings are
limited to 8,192 UTF-16 code units each.

The payload contains exactly `frames × channels` IEEE-754 float32 values in
little-endian, interleaved channel order. NaN and infinity are rejected. The final
32 bytes are SHA-256 over the entire preceding header, JSON, and sample payload.
No trailing bytes, missing frames, or checksum mismatch are accepted. The file
limit is 64 MiB of samples plus 256 KiB of metadata and 48 bytes of framing.
Recovery copies use the same format, with `recoveryWrittenUtc` and
`recoveryOwnerPid` in metadata. These fields carry provenance only.


## Stem delivery

**Export Stems…** in Multitrack exports checked tracks/buses and an optional
master WAV. The native list, signal point, shared frame range, precision,
dither seed, filename prefix and existing destination folder define one batch.
The preview lists sanitized names with stable session ordinals. **Delivery
Report…** exposes the last batch's file results and manifest. Rendering and
file checks run on the normal cancellable session worker and preserve edits.

Post-fader taps include region gain/fades, polarity/swap, strip gain/pan and
their automation, followed by strip inserts. Pre-fader taps omit the selected
strip's gain/pan/inserts; mono pre-fader content uses the same centered
equal-power law as pre-fader sends. Bus inputs retain upstream processing and
sends. Both taps precede downstream routes and master processing. The optional
master always includes the full master chain. Temporary solos are ignored by
default; **Respect mixer solos** includes the paths heard through selected
tracks or downstream solo buses. Track and region mutes always remain active.
Every file has the same start/end, including the session tail by default.
Explicit ranges start fresh without preroll. Arbitrary track/bus selections can
double-count routed content, and nonlinear master processing changes the sum;
choose disjoint bus outputs when preparing stems for reassembly.

WAV precision and optional TPDF dither use the same encoder as waveform delivery.
Dither seeds are unsigned 64-bit decimal values. The master uses the base seed;
each strip adds its stable session ordinal modulo 2^64, avoiding identical
noise on quiet tails. Each effective seed is recorded in the manifest, and its
sequence continues across blocks. Float32 retains finite headroom; integer
PCM clips samples outside full scale, reported per file. No loudness targeting,
automatic normalization, RF64, editable interchange or external-media relinking
is implied by stem export.

The batch preflights all paths, source/hard-link protection, overwrite permission
and existing digests before writing a version-1 `delivery.stems.json` manifest.
The manifest starts **in-progress**, updates after each atomic WAV commit, and
records final status, range, format, routing policy, peaks, overs and SHA-256s.
A newly appearing file never inherits overwrite permission. Cancellation retains
completed WAVs and performs a bounded final manifest update. A crash leaves an
explicitly incomplete record; a manifest conflict preserves the external revision
and reports failure. Inspect existing files before retrying. The manifest
describes a render snapshot, not the digest of a saved
session. Publication is atomic per file, not across the entire directory.

The CLI uses repeated `--stem ID|master`, `--output DIR`, `--name PREFIX`,
`--tap pre|post`, `--respect-solo true|false`, shared frame/precision options and
`--dither none|tpdf --dither-seed N`. `--dry-run` renders without writing;
`--overwrite` authorizes replacement only after revision checks. Structured
failure output retains completed-file results and the manifest path.
