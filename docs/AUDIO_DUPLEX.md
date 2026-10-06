# Duplex recording integration

`core/audio_duplex` and `core/audio_duplex_queue` provide a device-independent
recording engine. `app/audio_duplex_device` binds an optional pinned PortAudio
backend; `app/audio_recording` coordinates devices and durable take storage.
**Multitrack → Record Tracks…** connects these services to explicit arming,
monitoring, punch timing and grouped review. **Single Take…** remains available
for standalone capture. Synthetic verification does not establish physical
device or platform acceptance. See the [full DAW plan](plans/audio-daw.md).

## One clock and one mixer

The prepared renderer has compensated and live modes. Playback, mixdown and
stem callers retain compensated mode. Live mode processes the physical frame
sequence without consuming future source audio to hide insert latency. Dry
input can enter armed tracks before their inserts, through the same effects,
automation, buses, sends and master used by session rendering. Track routing
and effect state are prepared before callbacks; the graph is immutable during
a recording run. Bounded loop recording repeats authored positions while the
device/DSP clock continues forward. Seeking, live graph edits and automation
writing are not supported within a run.

`AudioDuplexPass` declares backing playback start, half-open punch bounds,
input channel count, additional calibration, clock tolerance, output gain and
one to eight arms. Each arm selects an existing non-bus track and one or two
distinct hardware channels from an explicit 1–32-channel input configuration.
Track IDs are unique within the recording pass. Channels are zero-based in
the C++ API and one-based in the UI and import JSON. Mapping order is retained;
there is no implicit summing, resampling or channel substitution.

**Loop passes** selects 1–10,000 repetitions of the punch range. One means
ordinary single-pass recording. Preroll runs once; subsequent passes repeat
`[punchFirst,punchEnd)`. The unwrapped recording end is
`punchFirst + (punchEnd - punchFirst) * loopPasses` and must fit the normal
timeline bound. Stop can finish a partial pass. This is a finite pass count;
open-ended looping and comp lanes remain future work.

`AudioTimelineLoop` maps authored source/automation positions independently of
physical processing frames. The live renderer splits source windows at loop
boundaries while track/bus/master inserts, routing delays, monitoring and LFO
state continue. Automation wraps at each processor's latency-corrected signal
time, including bus and master lanes. There is no callback allocation, repeated
device open, repeated preroll or additional compensation per pass. Effects and
queued output drain once at the end of the run. Buffered session audition now
uses the same authored-time mapping and block splitter, retaining DSP across
repeats. Audition compensates latency by priming wrapped future context once;
live recording still advances real input time without consuming future input.
Finite recording bounds and journal placement remain unchanged.

Monitoring defaults off. Its gain is separate from dry captured data. An arm
can replace backing clips during the punch only when monitoring is enabled;
outside the punch those clips retain ordinary playback. Monitor input follows
track inserts/routing and can therefore be delayed by the graph. Use isolated
routing or headphones when enabling monitoring. The UI starts monitor gain at
approximately −12 dB and output gain at 0.7. Stored samples preserve headroom;
they are not normalized, clipped or printed through the monitor effects.

## Placement and timing

The device supplies first-sample ADC and DAC timestamps in one monotonic
timebase. The processor derives the round-trip frame distance and its initial
input timeline origin:

```text
inputTimelineOrigin = playbackFirst - roundTripFrames
                      - processingLatencyFrames - calibrationFrames
```

Positive calibration places input earlier. It is an additional user-measured
correction, bounded to ±10 seconds at the session rate; driver timing and graph
latency are already accounted for once. Selected dry frames are written only
where their corrected timeline positions intersect the punch. Imported grouped
takes use that corrected placement and zero extra compensation. The separate
standalone take workflow retains its own explicit compensation field.

The renderer accounts for track, bus, send and master processing latency.
Input/output timestamp discontinuities outside the explicitly selected clock
tolerance (plus half-frame rounding tolerance) stop capture. Tolerance is
0–one second in frames; the UI starts at 128 frames or the session rate if
smaller. This permits bounded timestamp jitter, not concealed dropped samples
or automatic hardware calibration. Non-finite input/timing, insufficient
preroll, rendering failure, timeline exhaustion and any reported xrun stop
the pass visibly. Priming callbacks are accounted for separately.

`AudioDuplexProgress` reports processed/captured/priming frames, input origin,
round trip, processing latency, maximum clock deviation, output peak and
over-range counts. These values are single-owner state. During capture the
worker publishes copies through bounded telemetry; other threads must not
read the processor directly. Session meter taps remain available from the
renderer; dry-input and device-output readings use the same sample meter engine.

## Recording meters

The **Meters** tab shows each armed input channel in its mapped order, followed
by stereo device output. Inputs are measured before monitor gain, inserts and
routing, including real preroll through the planned punch end. Priming
callbacks and processing padding do not invent input readings. Output is
measured after the pass output gain and before the safety clamp. It can therefore
show clipping even though emitted samples are bounded to full scale.
Input readings describe received samples, including any measured before a later
render or capture-queue failure; verified take lengths describe durable audio.

Each row exposes sample peak, 300 ms exponential RMS, held maximum, headroom
relative to the maximum, and the number of samples strictly above full scale.
Peaks decay at 24 dB/s against the sample clock. Mono arms have one input row;
stereo arms preserve their selected hardware-channel order. Numeric values,
native level bars, accessible cells and explicit state text accompany the
selected row's wrapped details. Full-scale samples are distinguished from
over-range samples. These readings do not detect analog clipping or true peaks.

**Reset Meter History** clears readings immediately and requests a callback-owned
reset through a lock-free epoch. Until acknowledgement, older queued telemetry
cannot restore the cleared history. Once callbacks have closed, the control
worker applies resets during finalization. Reset changes no capture frames,
DSP state, timing or pass-wide diagnostic output counts. The fixed-size readings
travel through the existing bounded telemetry ring without callback allocation.

The tab stays usable while Record/Review editing is disabled and retains the
final readings until reset or another pass. Readings are transient, not part of
the plan, receipt or take files. Reopened passes use existing reviewed
take export, waveform analysis or imported-session meters for offline analysis;
CLI inspection does not claim to reproduce live meters or open a device.

## Callback and capture queue

Preparation allocates graph/scratch storage away from the callback. Processor
blocks are bounded to 1–4,096 frames. Native callbacks of up to 65,536 frames
are split into bounded processor blocks. Callback processing performs no C++
allocation, mutex locking, file I/O, QObject access, logging or notifications.
Output is stereo; capture can retain eight mono/stereo arms.

`AudioDuplexCaptureQueue` is a single-producer/single-consumer frame ring.
Publication is all-or-nothing across arms. Consumer spans are valid only until
consumption; the disk thread appends every arm before releasing a block.
Partial reads preserve order and contiguous frame positions. Capacity is
1–262,144 frames, subject to the 16 MiB allocation bound across mapped channels;
the recording worker defaults to 65,536 frames. A full queue stops recording
without overwriting unread samples or substituting silence. A later arm's
storage failure can leave unequal durable prefixes even though queue
publication itself was atomic.

Stop ends input processing and the already queued samples drain to storage.
Processing-latency padding and queued native output drain before a completed
pass closes. This does not render the session's configured offline effect tail.
Renderer effect state is reset only at explicit preparation/reset boundaries;
callback partitioning does not restart effects or change frame placement.

`audio-duplex-smoke` checks independent sample ordinals, corrected placement,
monitor/backing behavior, routing/latency, callback boundaries, timing faults
and output tails. `audio-duplex-queue-smoke` checks atomic arm publication,
partial consumption, wrapping, saturation and concurrent producer/consumer
ordering. These are deterministic synthetic tests, not hardware measurements.

## Native device boundary

Meson option `audio_duplex` controls the optional private PortAudio adapter.
Use `-Daudio_duplex=disabled -Daudio_playback=disabled` for device-free builds.
Review, import, rendering and CLI operations remain available without devices.
The dependency, pinned revision, licence and local patches are recorded in
[Dependencies](DEPENDENCIES.md), [Credits](CREDITS.md) and [Stack](STACK.md).

`AudioDuplexDevice` has one control-thread owner. Construction, enumeration,
open/start, status, close and destruction occur on that thread. Selections use
enumeration-scoped IDs and must explicitly choose an input and stereo output
within one host API. There is no implicit default selection. Supported requests
are 8–384 kHz, 1–32 input channels and a 64–16,384-frame buffer hint; the UI
offers 128–16,384. Unsupported combinations fail rather than silently changing
the rate or channel map. CoreAudio additionally requires a single duplex
device already configured at the session rate and avoids implicit conversion.
ALSA plugin configurations can perform conversion outside VibeStudio.

WASAPI patches preserve input packet QPC timestamps, detect discontinuous
packet positions, preserve silent input as zero samples and handle variable
callback sizes. Conservative underflow reporting and a bounded two-second
output-drain wait prevent false completion. CoreAudio xrun consumption uses
atomic flags. These changes and their pinned upstream provenance are covered
by the PortAudio patch fixture; they do not prove actual driver behavior.

Input/output timestamps may be driver estimates. Reported timing is distinct
from measured loopback latency and must not be advertised as hardware
sample-accurate acceptance. A native exclusive lease prevents simultaneous
PortAudio recording owners. Refresh can reinitialize a closed host. Failed
native close detaches the callback target after a bounded in-flight callback
check and quarantines the failed runtime context until process exit; further
recording is unavailable. Arbitrary blocking driver calls can still delay
shutdown. Automatic reconnect is not implemented.

`audio-duplex-device-smoke` injects a synthetic native API into the production
adapter and never initializes a physical host. Linux selected-object checks
cover both disabled devices and a private ALSA-enabled backend. Native Windows,
macOS and Linux device/permission/clock/latency acceptance remain outstanding.

## Recording worker and durable pass folders

`app/audio_recording` coordinates an explicit Record through microphone
permission and playback-shutdown acknowledgement before preparing a pass.
Injected fixtures access no hardware. The session dialog waits for browser
and waveform players to release retired backend objects, then for actual
session worker/device shutdown. A newer playback generation invalidates an
old acknowledgement. The application-modal recording/review dialog locks
session edits and blocks new auditions. Stop invalidates pending gates; late
or duplicate completions and destroyed owners cannot start input.

The device control thread owns preparation and device lifecycle. A separate
disk thread creates a new `.vsrecord` folder and opens every arm journal before
input starts. The callback publishes samples to the capture queue and status
to a four-slot SPSC telemetry ring. Full telemetry drops a display update only.
Control drains telemetry every 5 ms and coalesces UI publication to one pending
notification at roughly 50 ms intervals. Only after native close joins or
detaches callbacks may control read the processor directly.

A processed-frame progress watchdog accepts 100–60,000 ms (default 5 seconds).
Endless priming cannot evade it. Output drain uses at least the watchdog period
or reported output latency plus one second. Runtime faults, timing changes,
overflows, device stalls and disk failures stop without automatic reconnect.
Device-close errors interrupt completion and propagate unavailable status when
the native backend has quarantined the failed driver.

Already queued audio drains after device shutdown. Explicit Stop writes clean
footers; failure or application destruction retains interrupted prefixes.
Destruction joins device and disk work, so outstanding filesystem/driver calls
can delay application shutdown. Close in the UI requests cancellation and waits
for finalization while status remains visible.

Each folder owns fixed relative files:

- `plan.json`: new-only, checksummed canonical JSON with rate, timing, arm maps,
  monitoring configuration, session-path provenance and common start time.
- `arm-01.vstake` through `arm-08.vstake`: ordinary new-only take journals.
  Metadata contains corrected punch placement with no extra import compensation.
  Only planned arm files are read.
- `result.json`: new-only, checksummed final timing/outcome receipt, bound to
  the plan digest and each independently scanned journal prefix and footer.

Single-pass plans retain canonical version 1. Loop plans use version 2 with
required `loopPasses` from 2–10,000; receipts and `.vstake` journals remain
version 1. Old readers reject the newer plan instead of flattening its intent.
Each arm journal concatenates pass samples continuously. A pass of length `L`
and zero-based index `p` occupies stored frames `[p*L,(p+1)*L)`. Corrected
placement remains the original punch start. Receipt counts cover all passes,
including a final partial pass after Stop. CLI inspection adds `completePasses`
and `partialPassFrames` to each independently verified arm summary.

Neither receipts nor footers provide multi-file atomicity. Failed later arm
writes can leave earlier arms longer; failed finalization can leave some
journals complete and others incomplete. Missing, corrupt, replaced or
mismatched receipts never hide independently verified take prefixes. Readers
reject unsupported fields, noncanonical envelopes, duplicate keys, changed
checksums, linked ancestry and metadata mismatches. Existing files are retained.
File data is flushed; POSIX additionally synchronizes directory entries.
Windows directory-entry survival across power loss needs separate acceptance.
Plan/receipt JSON is bounded to 128 KiB. Session paths are provenance only and
are never opened during inspection. The plan does not fingerprint the backing
session; GUI import checks the current document revision, and CLI callers can
pin the source file with `--expected-session-sha256`.

`asset audio-recording inspect FOLDER.vsrecord --json` shares this reader and
reports `planValid`, `receiptValid`, `receiptMatches`, digests, raw plan/result,
and independently verified `takes`. A readable plan returns exit 0 even if its
receipt is missing or interrupted; inspect those fields before claiming
completion. Invalid/missing plans return exit 3 and unsupported/duplicate
options return exit 2. Receipt outcomes are 0 complete, 1 stopped, 2 interrupted.
There is no CLI device start. Individual ranges also remain available through
`asset audio-take export` with a reviewed hash and explicit prefix acceptance.

## Reviewed grouped import

`core/audio_recording_import` rechecks the plan/receipt hashes and each selected
prefix, explicit stored channels, frame bounds and destination track before
producing an immutable session. Ranges are zero-based and half-open, bounded
by the selected pass and verified take data. Targets must exist and cannot be
buses. Recording/session rates must match. Media limits remain 16,777,216
interleaved samples per source and 67,108,864 across the session; streaming
recording does not remove the embedded-session limits.

Replace clears only old clip portions inside the destination ranges. All
requested clears precede any import, so multiple arms targeting one track
cannot remove each other's newly added clips. Automation retains authored
positions. Optional linking creates a clip group when at least two clips are
imported. Failure or cancellation publishes no partial session. The GUI prepares
waveforms in the background, rechecks the captured base revision, then adopts
the entire pass as one undoable change. Source journals and all associated
fixed recording filenames remain protected across undo/redo history.

The Review tab keeps per-take selection, target, range, channel order, placement
and replace choices. **Recorded pass** chooses an available loop pass for each
arm; first/end are local to that pass. Changing pass resets its selected range
and retains the destination/channel choices. Ordinary review imports one pass
per arm; comp mode queues multiple sections, including repeats of the same pass. Recorded
alignment is `punchFirst + first` for every pass; disable it for
manual placement. Timing compensation is not applied again. Missing, invalid,
mismatched or interrupted receipts require explicit prefix acceptance even if
individual journals have clean footers. **Show File and Import Details** exposes
verification and an `importPlan` JSON object. Copy that object alone into the
CLI review file:

```sh
vibestudio --cli asset audio-recording import ./music.vssession --review ./review.json --output ./recorded.vssession --dry-run --json
```

Review JSON uses format `VibeStudioRecordingImport`, version 1, `directory`,
`planSha256`, `receiptSha256` (empty for an absent receipt), `allowInterrupted`,
`groupRegions` and `selections`. Each selection contains one-based `arm`,
`prefixSha256`, `first`, `end`, one-based stored `channels`, `trackId`, `position`
and `replaceExisting`. All fields are required and unknown fields are rejected.
Relative recording directories resolve from the review file's parent. In-place
updates require `--overwrite` and recheck the identity read before import;
optional `--expected-session-sha256` pins the user's reviewed source revision.
Separate outputs use the normal overwrite guard. Dry runs validate and decode
without writing. Recording files, review JSON and source media remain protected.
Parse, read/import and write failures use exit codes 2, 3 and 4 respectively.
No implicit rate conversion or startup scan of recording folders is performed.

When any selection addresses a later loop pass, review JSON uses version 2 and
every selection additionally requires one-based `loopPass`. `first` and `end`
remain local to that pass. Version 1 selects the first pass and retains its
eight-arm bound. Version 2 accepts up to 128 distinct arm/pass pairs within
ordinary source/sample budgets, so CLI callers can import several passes from
one arm. Duplicate pairs, unavailable passes and overreads of a partial pass
reject the entire import. `audio-take export` still addresses raw journal
frames; it does not reinterpret those frames as loop-local positions.

## Recording comp sections

Enable **Assemble comp sections** in Review, choose a source arm, recorded pass,
range, channel order, destination and placement, then **Add Current Section**.
Selecting a queued row restores its fields; **Update Selected Section** validates
and replaces that row, and **Remove Selected Section** removes it. Draft field
changes enter the comp only through Add/Update. Invalid edits retain the valid
queue and show a diagnostic. The queue reports planned fade-in/out lengths.

**Crossfade frames** defaults to zero (hard cuts). Values of at least two create
complementary linear fades after each exactly adjacent cut on the same target
track. The outgoing section gains that many additional source frames after its
selected end, fading from full gain to zero; the incoming section fades from
zero to full gain over the same interval. Both must have matching channel counts,
the incoming section must be at least that long, and the outgoing handle must
fit within its recorded pass and verified prefix. This is an after-cut fade,
not a centred or equal-power fade. Gaps remain unfaded; authored sections on the
same track may not overlap. Invalid handles disable import with a diagnosis.

**Import Comp** creates ordinary editable, optionally grouped session clips in
one undo step. Replacement clears the old backing through the added handles;
all clears precede new clips. Source metadata retains each reviewed cut/pass and
the actual unwrapped journal range, including handles. Source journals remain
unchanged. Session save/recovery, mixing, stems and game delivery use the existing
region/fade services; native session schema remains version 7. A comp can be
edited in the arrangement or undone. Saved review files reopen the editable
selection recipe described below. Dedicated alternate-take lanes and recipes
embedded in native sessions remain open. Audition works before import.

Comp review JSON uses version **3**, requires root `comp: true` and integer
`crossfadeFrames`, and requires `loopPass` on every selection, including pass 1.
Other fields retain the version-2 meanings. Up to 128 sections may repeat an
arm/pass pair, subject to existing source, clip and sample budgets. Handles
count toward those budgets. Versions 1/2 retain distinct-pair validation.
The shared pure planner validates the inspected snapshot before import; the
worker re-inspects the folder and batch-decodes all selected ranges from each
arm in one journal scan per arm, in addition to the receipt inspection. A bad
range, changed hash or cancellation discards the whole batch. No device, account,
new dependency or external code is involved.

## Save and reopen recording reviews

**Save Review** writes the current import choices to a separate JSON file;
**Save Review As…** creates a named variation. **Open Saved Review…** restores the
ordered sections, passes, channel maps, target tracks, placements, replacement
flags, interruption acceptance, grouping and crossfade length. The file and
saved/unsaved status remain visible in Review. Closing, opening another recording
or review, and starting a new recording offer Save/Discard/Cancel when choices
have changed. Save must finish successfully before the requested action continues;
Cancel or a save failure retains the working review. During ongoing work, Save
is unavailable until it stops. Import commits clips directly; save the recipe
first when you want to retain its editable selections too. Saving does not import clips,
create session undo, start a device or modify recording journals.

**Use queued take selections** is off by default. Comp assembly enables it.
Opening any saved review enables the queue so all selections remain represented,
including version-2 reviews with several passes from one arm. With comp assembly
off, distinct arm/pass pairs retain the ordinary layered-import rules and no
crossfades are added. Checked source rows apply only when the queue is off.
Draft section fields enter the queue through Add/Update; Save captures the
active queue or checked-take choices, rather than unqueued draft fields.

Open and Save verify the recording on cancellable workers and preflight every
selection against the current session. A missing target, changed plan/receipt/
take hash, invalid range or cancellation retains the current queue and previous
saved file. Saving an opened review compares its loaded SHA-256 under the
normal output lock; an external edit requires reopening or Save As. No partial
file is published. The session, recording plan/receipt/journals and source-media
paths are protected. The 128 KiB JSON uses the existing review versions 1/2/3.
Its recording folder is relative to the review file when representable, so the
review and recording can move together without depending on the working directory.

`asset audio-recording save-review SESSION --review PLAN.json --output REVIEW.json`
uses the same verification and atomic writer. `--dry-run` verifies without
writing; `--overwrite` permits replacing an output, and an in-place review update
also checks the just-read identity. `--expected-session-sha256` guards the base
session. The output feeds existing `import` and `preview` commands unchanged.
No session content is embedded: keep the matching session and recording journals.
Reopening a recipe does not track or replace an earlier import by clip identity;
it reviews another import against the current session and its replacement flags.
Native take lanes and session-embedded comp revisions remain separate work.

## Audition before import

**Audition Current Section** hears the focused take's draft pass, trim, channel
order, destination and placement, even when a different version of that section
is in the comp queue. **Audition Review** hears the queued comp or checked takes
with their exact planned crossfades and replacement rules. Both open the
**Audition** tab after rechecking all reviewed file hashes on a cancellable worker.
No session edit, undo entry, saved file or recording permission is created.

**Include backing clips** is on by default. Turning it off removes other clips
from the temporary playback snapshot while retaining track, bus and master
routing, effects, automation, mute and solo. This is isolated playback through
the mixer, not a dry-input bypass. The half-open audition range spans the selected
imported clips and their handles, including gaps; it adds no preroll or trailing
tail. Playback begins with the normal transport's fresh processing history.

Choose an audition output and buffer independently of the duplex recording
endpoints. Play resolves the current system default once; an explicit missing
output remains visible and fails clearly. Volume defaults to 70% and affects
listening only. Pause/Resume, exact frame seeking and repeat use the shared
compensated session transport; loops keep effects continuous. The named status
reports range, consumed position, underruns, over-range sample counts and device
errors. Playback clips output to the device's full-scale range as normal.

Changing review selections, cuts, comp options, backing mode, output or buffer
stops stale audition. Play Again re-verifies the current choices. Stop cancels
pending playback handoffs and verification as well as active playback. Browser,
waveform and parent-session outputs acknowledge shutdown before audition starts.
Recording waits for audition output to shut down before opening input. Import
and Close also await actual output shutdown; delayed or duplicate handoff
callbacks cannot start a cancelled or closed review. The parent session remains
revision-locked while review is open. Closing a review discards its temporary
playback snapshot; source journals are retained.

The device-free CLI equivalent is:

```sh
vibestudio --cli asset audio-recording preview ./music.vssession --review ./review.json --output ./preview.wav --isolated --dry-run --json
```

Omit `--isolated` to include backing. Preview accepts the same review versions,
relative-folder resolution, `--expected-session-sha256`, dry-run and overwrite
guards as import. It writes one stereo float32 WAV over the exact audition span,
without device volume or output clipping. The session, review, journals and
source-media paths are protected. JSON reports first/end frames, sample rate,
rendered frames, backing mode, sample peak and over-range counts. Error codes
remain 2 for usage, 3 for review/read failure and 4 for rendering/output failure.
Use normal session/stem delivery for other formats, ranges, tails or dithering.
Device-free builds retain CLI preview and import while GUI audition reports
unavailable playback. No new library or native file schema is introduced.

## Verification and remaining work

`audio-recording-smoke`, `audio-recording-worker-smoke` and
`audio-recording-cli-smoke` cover independent files, exact channel ordinals and
punch placement, unequal failed prefixes, finalization, overflow, stalled and
endlessly priming devices, permission denial/cancellation/lifetime gates, drain,
failed close, occupied receipts, playback acknowledgement and callback C++
allocation counts. Shared fake devices exercise the real worker and storage.

`audio-recording-import-smoke` adds exact ranges/channel ordering, all-or-nothing
replacement, same-track layered imports, grouping, stale hashes, interrupted
acceptance, cancellation and original-session preservation.
`audio-recording-ui-smoke` exercises pending permission/playback release,
grouped review, cancellation, one-step undo/redo/save and native accessible
metadata. It also verifies mapped meter rows, clipping states and reset without
invalidating reviewed takes. `audio-duplex-meter-smoke` compares sample-clock
levels against an independent numerical oracle across callback sizes and reset
boundaries, including exact dry capture and pre-clamp output. Worker fixtures
check reset acknowledgement, stale telemetry rejection and zero C++ callback
allocations. Direct widget renders check scaled, high-contrast and expanded RTL
layouts without OS capture or input injection. Synthetic/offscreen checks do
not establish physical-device, native keyboard or assistive-technology acceptance.
`audio-duplex-loop-smoke` compares continuous loop rendering against an
independently expanded arrangement, including short loops, reordered dry
capture, effects, routing and latency-corrected automation. Worker, import,
CLI and GUI fixtures cover one stream across passes, total journal lengths,
later-pass selection, partial final passes, duplicate rejection and exact
local range placement. These are synthetic checks, not hardware acceptance.

`audio-recording-comp-smoke` checks independently calculated crossfade samples,
source retention, repeated-pass cuts, native round trips, missing handles, stale
hashes and cancellation. Take tests exercise overlapping batched reads and
all-or-nothing failure. CLI and offscreen UI checks cover v3 review, queue edits,
invalid-handle rejection and one-step undo/redo.

`audio-recording-audition-smoke` drives fake output through the real playback
worker with partial byte writes, pause/seek/repeat, context/isolated modes, draft
section playback, gate cancellation/duplication, stale files and device failure.
It checks that recording/import/close await shutdown and audition adds no undo
entry. Core and CLI fixtures compare exact prepared/rendered samples.

Open-ended loops, dedicated comp lanes, seamless live edits, reconnect handling
and physical Windows/macOS/Linux acceptance remain open in the full DAW plan.
