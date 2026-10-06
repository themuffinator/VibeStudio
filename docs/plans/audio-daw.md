# Professional audio workstation development

Objective: further advance the DAW until it has a full suite of basic to advanced
tooling and capabilities a modern audio professional would expect. This extends
the earlier single-document [Audio RC audit](audio-editor-release-candidate.md);
the earlier release packages do not establish completion of this broader goal.

The current worktree has waveform editing, undo/recovery, compressed import,
resampling, precision/dither, markers, analysis/loudness, audition and game
delivery. Initial multitrack sessions now connect those workflows through a
nondestructive arrangement and stereo mix. It does not yet provide a complete
DAW; the gates below remain the full objective.

## Required capabilities and evidence

| Area | Required behavior | Completion evidence | Current status |
| --- | --- | --- | --- |
| Sessions and arranging | Non-destructive media, multitrack clips, trims/slips/splits, fades/crossfades, grouping, snapping, ripple/range edits, tempo/meter maps and navigation | Independent sample/timing fixtures, saved/reopened arrangements and usable timeline controls | Sessions, grouped clip and scoped range editing with automation following, and stepped tempo/meter maps implemented; tempo ramps and musical anchoring open |
| Recording | Explicit input device/channel choice, arming, monitoring, latency compensation, punch/loop recording, takes/comping and crash-safe capture | Device-free stream fixtures plus actual device/dropout/reconnect acceptance | Standalone capture, duplex engine/native adapter, grouped durable journals, Record Tracks/review, dry-input/output meters and finite loop recording, comp-section assembly, review audition with GUI/CLI preview and saved editable review recipes implemented; open-ended loops, dedicated take lanes and physical device acceptance open |
| Mixer and routing | Track/bus/master controls, stereo/surround mapping, sends/returns, sidechains, phase, meters, solo/mute and routing-cycle validation | Known multichannel signals, routing fixtures, safe runtime changes | Stereo buses/sends, polarity and path-aware solo implemented; per-strip meters implemented; seamless live controls, sidechains/surround and device acceptance open |
| Automation | Sample-accurate envelopes, editable curves, parameter lanes, trim/read/touch/latch/write and automation recording | Block-boundary invariance and timeline/device acceptance | Sample-clock gain/pan/effect lanes with linear/step/smooth curves and graphical/native controls implemented; write/touch/latch recording, trim modes and device acceptance open |
| Effects | Non-destructive chains, EQ/filtering, dynamics/gate/limiter, delay/reverb, modulation, saturation, presets, bypass and compensation | Independent frequency/time-domain tests and realtime/offline parity | Native chains, numeric automation, presets, lookahead sample-peak limiting and route compensation implemented; true-peak limiting, pre-fader inserts and plugin hosting open |
| Instruments and MIDI | MIDI I/O/files, piano roll, velocity/controllers, quantization, drum tools, virtual instruments, MIDI clock and game music interchange | Event/timing/file fixtures and hardware acceptance | Open |
| Advanced editing | Pitch/time processing, transient/beat tools, spectral editing/analysis, restoration and loudness targeting | Independent numerical/audio fixtures and reversible workflows | Analysis exists; authoring additions open |
| Plugins | Reviewed portable formats, discovery/isolation, parameter/preset/state persistence, automation, latency and crash containment | Licence review, fixture plugins, failure recovery and platform tests | Open |
| Transport and performance | Shared sample clock, streaming long media, low-latency monitoring, seeking/looping, underrun reporting and cancellation | Long-session/resource workloads and hardware/platform acceptance | Shared frame transport, continuous compensated playback loops and buffered output implemented; disk streaming and device acceptance open |
| Delivery and integration | Master/stem/range export, batch naming, dithering, metadata, editable project interchange, package/level handoff | Independent files and exact GUI/CLI parity | Session mixdown, waveform handoff and aligned guarded stems implemented; editable interchange and advanced delivery open |
| Reliability and usability | Session undo/recovery/conflicts, media management/relinking, accessibility, keyboard workflows, scalable/RTL UI and localization | Corrupt/crash/conflict fixtures, responsive UI checks and physical acceptance | Shared recovery and reviewed source management/relinking implemented; bulk discovery, source round trips and physical acceptance open |

The feature survey uses the official [Ardour overview](https://manual.ardour.org/welcome-to-ardour/)
and [REAPER capabilities](https://www.reaper.fm/about.php), reviewed 2026-10-05,
only to check coverage. No code, assets or UI layout from those products is
incorporated. Implementation remains original C++/Qt with Meson/Ninja and shared
headless services. New external libraries require the normal licence/dependency
review before incorporation.

## First implementation: multitrack sessions

Native recording integration now includes an optional pinned PortAudio host and
an injectable C++ device boundary. Packet timing, xruns, silence, explicit
selection, Stop and output drain are covered by synthetic device/processor/take
fixtures. See [the duplex integration contract](../AUDIO_DUPLEX.md). This does
not complete the recording gate. The recording worker now provides permission
and playback-acknowledgement gates, a disk worker, durable grouped receipts,
bounded telemetry and stall detection, with shared read-only CLI diagnostics.
Native Record Tracks/Review controls now coordinate browser, waveform and
session playback shutdown, bind review to the current session revision and
adopt selected takes as one undoable change. Shared grouped import re-verifies
plan/receipt/prefix hashes, exact ranges/channels and destination tracks; CLI
import uses the same service and guarded output. Live dry-input and pre-clamp
output meters share the sample meter engine, with callback-owned history reset
and retained final readings. Finite loop recording keeps DSP and the device
clock continuous while sources and latency-corrected automation repeat. Durable
pass counts and reviewed pass-local GUI/CLI ranges preserve journal provenance.
Buffered audition shares continuous processing and authored-time mapping,
including wrapped compensation lookahead and accumulated meters. Recording
review now queues repeated-pass comp sections with validated after-cut linear
crossfades, shared v3 CLI review and one-step adoption as editable clips.
Review audition now compares focused take fields or the complete comp with
optional backing, shared pause/seek/repeat and device-free float32 CLI preview.
Saved review JSON now restores complete editable queues with relative recording
paths and guarded GUI/CLI save. Dedicated take lanes and session-embedded comp
revisions remain open.
Fake-device/offscreen
verification is separate from outstanding real-device, native accessibility,
open-ended loop and comping acceptance.

The initial implementation adds a versioned, checksummed `.vssession` file with embedded, independently
owned source snapshots. Region descriptors refer to source frames; timeline and
mixer edits retain the original samples. A prepared block renderer gives
deterministic stereo mixing, track mute/solo, clip gain/fades and frame-based
gain/pan automation. It also supports 64-bit timeline positions without allocating
the silence before a clip. Streaming mixdown reuses the existing WAV precision
encoder and atomic output guards. A bounded range can enter the existing waveform
editor for analysis, resampling, game delivery, staging and level placement.

This is an initial vertical slice of the complete DAW. In-memory imported media,
offline rendering, a stereo master and initial session controls do not satisfy
the remaining recording, realtime streaming, routing, MIDI, effects, plugin or
advanced editing requirements. Keep those gates open until their implementation
and appropriately scoped evidence exist.

Workspace manifests do not yet reopen native session windows. Long-media
streaming and bulk relink discovery remain open. New sessions start at 120 BPM;
Tempo / Meter and the CLI now edit stepped tempo and bar-boundary meter maps.
Musical ruler/navigation/snapping share a bounded timing service. Audio and
automation stay sample-anchored; ramps, metronome and musical media anchoring
remain open.

The first focused Windows core/CLI/offscreen UI run passed on 2026-10-05.
Follow-up checks cover native schema rejection, undo-branch media release,
normal shell file routing, audition coordination and scaled timeline spacing.
Current logs, direct widget renders and final source/build evidence belong in
`.agents/tmp/audio-daw/`. Verification must state its exact build/platform scope;
synthetic/fake-backend tests do not establish hardware or assistive-technology
acceptance. Bulk media relinking, long-media streaming and the
remaining capability gates stay open after this vertical slice.

The independent capacity fixture uses 64 tracks, 4,096 clips and 16,384 gain
points. On the 2026-10-05 Windows build, its 64-second float32 mix rendered in
about 1.4 seconds with maximum error below `7.3e-9` against an analytic oracle.
This measures offline rendering on this machine; it is not evidence of low-latency
device performance. The fixture, command timings and hashes are retained under
`.agents/tmp/audio-daw/scale-fixture` and `scale-verification.json`.

The recovery increment connects session checkpoints, verified restoration and
live-document/conflict guards to the existing recovery UI, settings/startup
discovery and CLI. A separate checksummed envelope retains embedded media and
provenance without changing native sessions. Waveform and session copies share
the 32-copy/512-MiB budget and preserve existing `.vsaudio` recoveries. Background
writes coalesce, retiring drafts cannot resurrect, and restoring a reviewed copy
creates a separate draft. Validation evidence for this increment is recorded
separately from the initial arrangement checkpoint below. Recording and long-media
streaming then need an explicit shared clock/device model; the current prepared
range audition must not be relabelled as that engine. These priorities do not
remove any capability from the full gate list above.

### Verification checkpoint, 2026-10-05

The ten Windows Audio suites passed, followed by focused session/image checks
and an updated browser test for the shared command's disabled-selection state.
Eleven suites have passing latest results. All eleven new Audio C++ translation
units also compile with GCC 13.3 / Qt 6.4.2 and playback disabled. Full Linux
link/runtime verification is still open: unrelated `level_prefab.cpp` publication
initialization and `level_map.cpp` delta-remapping warnings stop the overall
warnings-as-errors build. A separate Targa byte-reader warning was repaired and
its Windows image suite passed. These findings must not be reported as a full
Linux test pass or completion of the DAW goal.

The evidence manifest is `.agents/tmp/audio-daw/verification.json`; it includes
exact source/binary hashes, test lineage, capacity results and render paths.
Windows offscreen font fallback missed one Chinese glyph in the test filename;
native font fallback and assistive-technology acceptance remain unverified.
Other active worktree changes were preserved. No physical audio device, game,
operating-system capture or user keyboard/mouse input was used for these checks.

### Session recovery checkpoint, 2026-10-05

Waveform and multitrack sessions now share background checkpoints, bounded
inventory, live-editor protection, preferences, startup offers and review.
Session recovery envelopes retain embedded media and provenance separately from
the native session schema. GUI and CLI restoration verify the reviewed digest,
create a separate draft/output and protect the original. Restoring an editor's
own copy retains that origin before a Save/Discard guard can retire it; the same
correction applies to waveform documents.

Nine focused Windows suites have passing latest results, including malformed
envelopes, shared quotas, coalescing, in-flight retirement, exact-kind discard,
actual child-process interruption, save/undo/redo and both self-restoration guards.
UI tests now wait for the current shared browser command's preview readiness and
explicit startup discovery. Seventeen targeted Audio objects compile with GCC
13.3 / Qt 6.4.2, playback disabled and warnings as errors. Full Linux link/runtime
verification is still open. The existing Level Map/Prefab build issues and
Windows offscreen CJK fallback limitation remain recorded separately from these
results. Native device/accessibility and power-loss acceptance are not established.

`.agents/tmp/audio-daw/recovery-verification.json` records exact test lineage,
binary/source hashes, targeted Linux objects and direct-widget render paths.
An automatic policy review blocked removal of the failed test's disposable
fixture. Its path and a remaining Qt icon-cache directory are recorded in the
evidence. The complete DAW goal and all remaining capability gates stay active.

### Streaming transport checkpoint, 2026-10-05

Session audition now streams bounded stereo blocks through the same renderer used
by mixdown. A headless frame transport supplies exact range/loop/seek/pause behavior,
caller-owned accumulation/output storage and master peak/over-range metering. A
dedicated Qt worker owns preparation and the selected QAudioSink output; it retains
partial writes, drains finite tails, discards old lookahead on seek/loop changes,
pins the resolved output across those changes and reports starvation/device errors.
The output and requested buffer controls are local to the session window. Existing
waveform/browser transport and delivery handoffs retain their shared coordination.

The GUI no longer materializes an entire range just to play it. Imported media
still lives in memory, and Qt's processed-time position is an estimate of device
progress. This buffered baseline does not establish hard-real-time operation,
low-latency monitoring, streaming media from disk or any recording capability.
Those requirements remain open, along with the rest of the full capability table.

`asset audio-session transport --frames N` exercises the same clock and mixer
without opening a device or writing audio. Its digest and sample counters are
independent of block size. The 64-track / 4,096-clip / 16,384-point capacity fixture
rendered 64 seconds in about 1.08 seconds at both 256- and 4,096-frame block sizes;
its float samples exactly match the previously verified offline mix. These are
headless processing measurements, not hardware latency or dropout acceptance.

Eleven focused Windows suites have passing latest results. The new core fixture
initially omitted a required track name; it now validates before running its
independent modulo/analytic assertions. Device-free worker fixtures cover partial
byte writes, finite drain, pause/resume, seek/loop buffer invalidation, changing
system defaults, stalls, disconnects and cancellation. Offscreen UI renders include
output controls and live status at 100/125/200% with expanded RTL text. The existing
Windows offscreen CJK fallback limitation remains visible. All 21 catalogs compile.

Fourteen targeted Audio objects compile with GCC 13.3 / Qt 6.4.2 and playback
disabled. Full Linux linking/runtime, actual audio devices and native accessibility
remain unverified; earlier unrelated Level build blockers are recorded in the
previous checkpoints. Shared worktree changes are preserved. Evidence belongs in
`.agents/tmp/audio-daw/transport-verification.json`, with separate capacity results
in `transport-capacity-verification.json`. The full DAW goal remains active;
recording/capture is the next engine integration requirement.

### Recording and take handoff checkpoint, 2026-10-05

The session's **Record / Takes…** workflow now records explicitly selected input
channels into a new append-only `.vstake` journal. Separate input and storage
workers use a bounded queue, preserve native sample headroom, report input/disk
failures and retain verified blocks. Normal Stop drains delivered frames and
writes a completion record; destruction of an active controller retains an
incomplete prefix. The scanner checks chained records and stops at the first
invalid tail, with no resynchronization or invented samples.

Review chooses a frame range and stored channels, applies the recorded manual
compensation or an explicit placement, and requires acceptance of an incomplete
prefix. Import verifies the reviewed digest again and enters the normal session
undo/save/recovery path. Normal shell Open routes take files to that review. The
device-free `asset audio-take inspect/export` commands share the same scanner,
range reader, provenance and protected-output services.

This establishes standalone capture and reviewed handoff. It does not establish
software monitoring, synchronized overdubbing, punch/loop recording, comping,
automatic latency calibration or native device acceptance. Journals stream to
disk, while session imports still materialize bounded ranges. macOS requires its
declared microphone usage description and deployed Qt permission backend; the
portable bundle has not passed that acceptance. The full DAW goal stays active.

Evidence for this increment belongs in
`.agents/tmp/audio-daw/capture-verification.json`, with separate input fault,
process-interruption, CLI, integration and direct-widget layout checks. The
recording suite exposed a destructor/Stop race: all samples survived, but a
timer could write an incorrect completion footer. Separate atomic finish/abort
states fix it, with full-block and partial-tail stress cases retained. Expanded
200% RTL review also exposed wrapped-text clipping; minimum wrapped heights and
geometry checks cover that layout regression.

Sixteen Windows suites have passing latest results; the repaired capture suite
also passed three stress repetitions with 144 additional destructor cases. All
fourteen targeted Audio objects compile with GCC 13.3 / Qt 6.4.2 and device
support disabled. Twelve direct-widget renders cover 100/125/200% and expanded
RTL controls, and all 21 translation catalogs compile. These results do not
establish full Linux linking/runtime, microphone permission prompts, physical
device performance, assistive-technology behavior or power-loss durability.

The shared Windows rebuild needed a configuration refresh for newly registered
camera-key code and a precedence-preserving parenthesis fix in modelling's
boundary-fill code. The CLI documentation validator's initial registry probe
still omits a private settings profile; this increment used an equivalent
read-only probe with task-private settings instead. Prior Linux Level build
issues and offscreen CJK fallback limits remain recorded in earlier evidence;
their native acceptance was not rerun. No physical audio or user input was used.

### Stereo routing checkpoint, 2026-10-05

Sessions now support up to 32 stereo buses within the existing 64-strip limit,
with eight pre/post-fader sends per strip, independent send gain/balance,
main-output enable/destination, channel swap and left/right polarity. A prepared
acyclic graph drives playback, diagnostics, mixdown and waveform handoff. Mute
silences every route; separate pending/audible accumulators make bus and track
solo isolate complete signal paths without leaking parallel bypasses. Disabled
routes are also checked for feedback and retained references prevent accidental
bus removal. Bus gain/balance automation uses the existing sample clock.

The Routing / Sends dialog stages a complete change for one-step undo/redo;
applying stops playback. Native version 2 stores the complete routing state,
while version-1 documents migrate to their original direct-master defaults.
Normal save, recovery and take-import guards remain shared. CLI `add-bus`,
`routing`, `send` and `remove-send` edits use the same services and file guards.
No new library or external implementation was incorporated.

Ten Windows suites pass against the rebuilt application, including numerical
routing, actual playback-worker partial writes, CLI export, recovery and take
handoff. The maximum fixture contains 64 strips, 32 buses, 4,096 clips and 16,384
gain points. Its 256- and 65,536-frame block results are sample-exact and differ
by less than `6.4e-10` from an independent scalar recurrence. Solo requires at
most 67 MiB of scratch at the largest block. These are bounded headless checks,
not device latency or dropout measurements.

Twenty-one targeted objects compile with GCC 13.3 / Qt 6.4.2, device support
disabled and warnings as errors. Nine direct-widget renders cover normal,
high-contrast, doubled-text RTL and small-window layouts. Review exposed a
translation-context mismatch in the fixture and a cramped send list; assertions
now verify actual expansion and absence of horizontal scrolling. All 21
translation catalogs compile, and the CLI registry and documentation checks pass.
Exact build/test/source evidence is in `.agents/tmp/audio-daw/routing-verification.json`.

Seamless live mixer changes, sidechains, surround routing,
effects and the rest of the full DAW capability table remain open. Full Linux
link/runtime, physical audio, macOS and native accessibility acceptance remain
unverified. The unrelated CLI-doc validator still omits a private profile in
its first registry probe; the routing check uses a task-private profile instead.
Prior Level build and offscreen CJK limitations remain recorded in earlier
checkpoints. The full DAW goal stays active.

### Built-in insert effects checkpoint, 2026-10-06

Track, bus and master chains now support up to eight ordered processors: gain,
low/high-pass, notch, parametric/shelving EQ, linked compressor/gate/sample
limiter, fractional stereo delay and soft saturation. The native inspector
stages ordering, parameters, bypass and the global tail as one undoable edit.
CLI effects operations use the same validation and publication guards. Native
version 3 and recovery preserve complete effect state; versions 1/2 load with
empty chains. Track/bus inserts follow fader/pan, pre-fader sends remain dry,
and master inserts follow master gain.

Prepared processing retains contiguous history; pause preserves it, while
stop, seek, loop restart, cancellation and failure reset it. Explicit ranges
start fresh without preroll. A saved 0–60-second tail extends the default range
for enabled filters/delays; tails beyond timeline capacity are rejected. A
128 MiB state limit includes the independent solo-path histories. Nonlinear
shared buses process the audible and combined signals separately, forwarding
their difference only toward selected downstream paths.

Twelve Windows suites have passing latest results. Independent frequency/time
fixtures, cancellation/overflow clearing, malformed checksummed v3 records,
v1/v2 migration, CLI WAV parity, undo/recovery and delayed-tail playback all
pass. The maximum graph has 64 strips, 32 buses and 520 effects; 256- and
65,536-frame blocks produce identical finite samples with 69 MiB maximum
scratch. Two 65,536-frame renders took 2.439 seconds in this headless fixture;
that is not a device latency or dropout guarantee.

Twenty-six targeted Audio objects compile with GCC 13.3 / Qt 6.4.2, device
support disabled and warnings as errors. Nine direct-widget renders cover
100/125/200%, high contrast, expanded RTL and small windows. Review improved
chain-list scrolling and verified every parameter and the final tail against
the actual scrollbar range. Qt's visibility helper follows a spin box's inner
focus proxy; the corrected fixture checks its complete styled frame instead.
All 21 catalogs compile, including the canonical runtime outputs. Documentation
and the real CLI registry pass checks using task-private settings. Evidence is
in `.agents/tmp/audio-daw/effects-verification.json`.

The EQ mathematics is attributed to the W3C Audio EQ Cookbook, with compatible
licence review and retained notices in [Credits](../CREDITS.md#audio-session-eq).
No new processing library is linked. The limiter has no true-peak/lookahead
guarantee, saturation is not oversampled, and reverb/modulation, presets,
parameter automation, pre-fader insert placement and general compensation
remain open. The full DAW goal stays active. Full Linux runtime, macOS,
physical devices and native accessibility remain unverified. Shared worktree
changes were preserved; the earlier unrelated CLI-validator, Level build and
offscreen CJK findings remain recorded with their original scope.

### Reverb, modulation and presets checkpoint, 2026-10-06

The shared effects engine now has 17 processor types, adding stereo reverb,
chorus, flanger, tremolo and phaser. Their state follows the same contiguous
playback, fresh-state seek/loop, export, undo, native version-3 and recovery
rules. Reverb has bounded comb/allpass storage, low-frequency decay, damping,
pre-delay and width. Modulation uses the sample clock and supports stereo phase
offsets. The aggregate 128 MiB state guard includes the new delay storage;
re-preparation releases old descriptor capacity before reserving the new chain.

Nine original factory recipes and version-1 `.vsfx` files are available from
the effects inspector and CLI. Presets replace a staged chain with fresh IDs,
preserve physical parameters and retain the longer session/preset tail. File
values incompatible with the destination rate are rejected. GUI file I/O runs
on a cancellable worker. Atomic publication shares the session/export guards;
a new fixture exposed missing hard-link identity detection in the common Audio
path helper, which now checks filesystem equivalence as well as canonical names.
Known file revisions remain guarded after factory changes too.

Sixteen Windows suites pass, including analytical modulation/allpass responses,
reverb reflection, pre-delay, stereo, decay/damping and extreme-parameter checks;
arbitrary block sizes are sample-exact. Fake playback-worker writes split samples
and still match every offline sample through nonzero tails. Actual GUI/CLI
preset operations, undo, saved metadata, recovery, cancellation, malformed/rate
rejection, overwrite/digest conflicts and hard-link source guards pass. The
existing maximum 520-effect graph also passes; its headless timing is not a
device latency guarantee.

Thirty-two targeted Audio objects compile with GCC 13.3 / Qt 6.4.2, device
support disabled and warnings as errors. Twelve direct-widget renders cover
presets, reverb and modulation at 100/125/200%, high contrast, expanded RTL and
600×400 windows. Review corrected a sixteen-pixel horizontal overflow in wrapped
effect rows and removed an overlapping disclosure arrow. All 21 catalogs compile;
documentation, credits and the real CLI registry are checked with private
settings. Evidence is retained in `.agents/tmp/audio-daw/modulation-verification.json`.

Freeverb topology/tuning was reviewed before incorporation and credited with
the exact public-domain reference in [Credits](../CREDITS.md#audio-reverb-reference).
No new runtime library is linked. Reverb's decay control is not a guarantee of
measured RT60. Preset libraries and generic asset-browser opening remain gaps,
as do effect automation, advanced limiting, compensation and the other full
DAW capability gates. The goal remains active. Full Linux runtime, macOS,
physical devices and native accessibility remain unverified. Earlier unrelated
CLI-validator, Level build and offscreen CJK findings retain their recorded scope.

### Parameter automation and curve editing checkpoint, 2026-10-06

All 65 numeric parameters across the 17 native effects now accept bounded
session lanes on tracks, buses and master. Gain/pan and effect points share
Linear, Step and Smooth interpolation with held endpoints. Prepared processors
bind lanes to the absolute sample clock and reserve maximum variable delay and
reverb storage. Contiguous playback/export retains history; pause preserves the
clock, while discontinuous ranges/seek/loop restart histories at the new curve
time. Reverb now reads fractional taps, which can change nonintegral tuning
relative to earlier rounded taps. No new library or external code is added.

A shared Qt editor connects its curve graph, native table and bounded numeric
controls. Graph gestures have table/field alternatives. Effect parameters have
Read/off state; nested changes remain staged until the outer one-step session
edit. Removing/replacing effects prunes their old targets. Presets remain static
recipes; their format is unchanged. CLI effect-automation and extended gain/pan
syntax use the same validation, undo-compatible state, rendering and publication.
Native v4 persists curves; versions 1–3 migrate linear envelopes and empty effect
lanes. Metadata is bounded to 16 MiB, the envelope to 320 MiB, and effect points
to 4,096 per lane / 65,536 per session including disabled lanes.

Independent curve/gain/fractional-delay oracles, every parameter's constant-lane
versus manual parity, 1/7/256/4096-frame invariance, discontinuity/loop clocks,
automated nonlinear solo isolation, maximum reverb taps and 65,536-point native
round trips pass. Actual CLI WAV output and split-sample fake playback workers
match the renderer. Recovery retains curves and read/off state; GUI track edits
share one undo/redo operation. Layout review corrected small-window overflow,
RTL graph-axis mirroring and negative-number order. Native accessibility and
physical devices remain separate acceptance work.

Eighteen Windows suites pass. Thirty-eight targeted Audio objects compile with
GCC 13.3 / Qt 6.4.2 and devices disabled; this is not full Linux runtime evidence.
Twenty-one direct-widget renders (nine curve views and twelve effects views),
all 21 translation catalogs, docs/guide/credits checks and private CLI registry
checks are retained. The increment's source/binary/test/platform evidence is recorded in
`.agents/tmp/audio-daw/automation-verification.json`. The full goal stays active:
live automation writing, timeline lane zoom/trim modes, advanced limiting,
compensation and all other unclosed gates above remain. Full Linux runtime,
macOS, physical devices and native assistive technology are unverified. The
existing CLI-doc validator's unisolated initial settings probe is bypassed by
a scoped private-profile registry check; earlier Level and offscreen CJK
findings retain their previous scope. No new unrelated defect was identified.

### Stem delivery and streamed dither checkpoint, 2026-10-06

Track/bus pre- and post-fader taps now share the routing renderer, including
upstream effects, absolute automation, polarity, sends and nonlinear solo
domains. Taps prune unrelated/downstream processing and pre-only paths omit
unneeded post effects. Stem delivery selects strips plus an optional master,
uses one frame range including the original session tail by default, and
preserves mutes. Temporary solos are ignored unless explicitly requested.
Explicit ranges start fresh without preroll. Overlapping track/bus stems can
double-count content; nonlinear master processing changes their sum.

GUI and CLI export through the same cancellable service. Portable filenames
use stable session ordinals. Every destination is preflighted before a version-1
JSON manifest starts; existing files are revision-guarded and newly appearing
files never inherit overwrite permission. WAVs commit independently, with
completed files retained and reported on failure/cancellation. The manifest
updates after commits, records per-file hashes, peaks, overs and status, and
finishes bounded status metadata after cancellation. A crash leaves in-progress
state; an external manifest conflict preserves external bytes and reports failure.
CLI errors retain structured partial results; the GUI exposes Delivery Report.

The WAV encoder continues seeded TPDF noise across successful blocks, while a
cancelled block leaves its sequence unchanged. The master uses the base seed;
each stem adds its stable ordinal modulo 2^64, avoiding identical quiet-tail
noise. Every effective seed is recorded. Mixdown now offers TPDF too. No new
library, external code or native session version is introduced. Existing
waveform/package/level handoffs remain available; editable interchange and
automatic loudness delivery remain open.

Independent tap/gain/nonlinear oracles, downstream solo contributions, pre-send
balance and mono law, unneeded-route overflow, seeded full/blocked WAV parity,
source/hard-link protection, overwrite/digest races, manifest conflicts, dry
runs, RIFF bounds, default tails and partial cancellation are covered. Actual
GUI/CLI exports match core bytes. The GUI action preserves session history and
exposes accessible native controls, scroll-reachable fields and textual status.
Nine QWidget renders cover 100/125/200%, high contrast, expanded RTL and small
windows; review shortened status copy to preserve working space. Exact test,
build, catalog and source evidence is in `.agents/tmp/audio-daw/stems-verification.json`.

The full DAW goal remains active. Recording/monitoring, advanced limiting and
compensation, MIDI/plugins, interchange and all other unclosed capability gates
remain. Physical devices, full Linux runtime, macOS and native assistive technology
are unverified. Shared catalog writes temporarily left trailing XML fragments;
the guarded repair check found them already resolved and changed no files.
Final Audio extraction/coverage uses isolated copies and validates all catalog
snapshots. The unrelated CLI-doc validator still requires the established private
profile workaround; previous Level and offscreen CJK findings retain their scope.


### Processing latency and lookahead checkpoint, 2026-10-06

A separate native lookahead sample-peak limiter now adds fixed 0–20 ms latency,
linked attack/release smoothing and linked safety gain. Its structural lookahead
value cannot have automation, including a disabled lane; ceiling/attack/release
remain automatable. Eighteen native processors now expose 68 automatable numeric
parameters. The staged inspector reports latency and retains normal undo/redo,
presets, native version-4 persistence and recovery.

An original longest-path plan aligns main outputs and pre/post sends at every
bus/master merge. Solo domains own independent delay histories. Strip taps prune
downstream latency and unneeded post processing. Effects wait for their input
arrival before advancing histories or modulation, and automation evaluates the
corresponding authored frame. Fresh ranges prime bounded preallocated blocks
from their requested start, read later session context, and retain exact output
lengths without leading padding or earlier preroll. Delay/reverb remain creative
timing. Pause retains history; stop, seek, loops and errors reset it. Processing
latency diagnostics are separate from native serialization and appear in CLI
inspection, transport, mixdown and stem manifests. Effect and compensation state
share the existing 128 MiB admission bound. No new dependency or borrowed code
is introduced.

Independent oracles cover linked ceilings, future transient reduction, parallel
polarity nulls, nested buses/sends, pre/post taps, modulation phase and absolute
automation. Fixtures include arbitrary blocks, short loops, pause/seek/reset,
cancellation, the timeline limit, aggregate memory and actual GUI/CLI exports.
A const-access fix also prevents shared Qt session containers from detaching on
the first render. The recovery fixture now uses a stable effect ID so a killed
child process can be compared byte-for-byte with its independently built parent
fixture. A zero/one-frame slow-attack oracle also verifies that limiter release
starts from any forced safety reduction instead of rebounding on the next sample.
Scaled/expanded RTL review shortened status text to preserve space.

All 22 Windows suites pass. Linux object compilation, catalog coverage and
rendered-layout evidence are recorded with the final source snapshot in
`.agents/tmp/audio-daw/latency-verification.json`. The full DAW goal remains active:
true-peak limiting, plugin hosting, synchronized recording/monitoring, MIDI,
interchange and the other open gates above remain. Hardware latency, full Linux
runtime, macOS and native assistive-technology acceptance are unverified.

The final broad Windows build encountered an unrelated viewport signature change
while concurrent edits were being compiled. Current declaration, definition and
call sites already matched; an incremental rebuild resolved the mixed-object
link failure without changing viewport source. The existing CLI-doc validator
still omits a private settings override in its first registry probe, so this
checkpoint uses the scoped private-profile check. Earlier CJK findings retain
their previous scope.

### Musical timing checkpoint, 2026-10-06

Original `core/audio_tempo` services now provide stepped tempo and bar-boundary
meter maps, musical position conversion and frame-distance grid snapping.
Quarter-note BPM is independent of the notated beat denominator; 960 ticks per
quarter note support bar/beat, half/quarter-beat and beat-triplet grids. Native
version 5 preserves origin values and strictly validated change lists; versions
1–4 retain their constant timing. Media, clip/fade descriptors, automation and
export ranges remain sample-anchored. Maps do not add export silence or alter
waveform/package/level handoff samples.

Tempo / Meter stages a validated draft with native tables and Set/Remove controls,
then applies one normal undo/recovery edit. The ruler shows musical grid and
tempo/meter changes; the native position field provides keyboard-focusable
navigation. New changes use the tempo/meter effective at the cursor. Forms wrap
and scroll under expansion, 200% scaling and RTL. The CLI shares replacement-map,
strict conversion and snap diagnostics, with existing dry-run/conflict guards.

Independent timing fixtures cover mixed and off-beat tempo changes, changing
denominators, 4,096-segment fractional-frame accumulation, low sample rates,
strict previous/next grid navigation and the maximum timeline frame. Compensated
accumulation and numerical tie handling fix half-sample drift; a constrained
inverse keeps the final displayed position navigable. Native fixtures retain
v1–v4 reads, reject hostile checksummed v5 maps transactionally, and verify
unchanged source ownership and output samples. GUI/CLI tests cover authoring,
conversion, guarded writes, undo/redo, save/reopen, validation and cancellation;
the recovery fixture includes tempo/meter changes.

All 26 Windows Audio suites pass. The standalone musical arithmetic target also
runs successfully on GCC 13.3 / Qt 6.4.2 with devices disabled, and 58 selected
Linux objects compile with warnings as errors. Twelve direct QWidget renders,
21 compiled catalogs with 507 extracted Audio keys per snapshot, documentation,
offline guide and private-profile CLI registry checks are recorded in
`.agents/tmp/audio-daw/tempo-verification.json`. This is not full Linux application
runtime, macOS, device, or native assistive-technology acceptance. The full DAW
goal remains active: ramps, musical clip anchors/time stretching, metronome,
group/ripple editing, MIDI, plugins, advanced recording and the other gates above
remain open. Musical-map interchange in WAV/game delivery is also unimplemented.

The first broad build encountered unrelated concurrent Levels viewport symbol
changes; rebuilding aligned the current objects without an Audio-owned viewport
edit. The initial CLI attempt consequently exercised an older executable and is
not counted as verification. Two older fixtures needed their writer-version
assertions updated while retaining explicit legacy reads. A GCC range-loop copy
warning in the new timing fixture was corrected. A concurrent Models CLI header
briefly caused 281 repository-wide translation-check diagnostics; its later
update resolved them, and final global checks pass. The existing CLI-doc validator
still lacks a private settings override in its first probe, so scoped verification
continues to supply one explicitly.

Automatic approval review rejected cleanup of 21 temporary catalog backups and
three one-use helper scripts with only “blocked by policy” as the reason. No
deletion occurred; those files remain under `.agents/tmp/audio-daw/tempo-catalogs/`
and `.agents/tmp/audio-daw/`. Do not retry that rejected cleanup indirectly.


### Grouped arranging checkpoint, 2026-10-06

Native multi-selection, persistent named groups and descriptor-only batch edits
now share `core/audio_arrangement` between the session window and strict CLI.
Linked moves preserve relative frame offsets; copies get independent groups;
removing members normalizes singleton groups. Splits retain original fade-domain
segments, including overlapping fades and repeated cuts. Native v6, undo,
recovery, direct/routed rendering and waveform/stem delivery share this state.
Track automation remains at authored frames. Independent fade-product fixtures
and sample-exact split comparisons cover direct and bus/effect render paths,
repeated cuts, strict v6 decoding, v1–5 compatibility and recovery. Actual CLI
checks cover linked targets, output guards and unchanged mixdown samples.

All 29 Windows Audio suites passed after the final changes. Sixty-four selected
Audio translation units compile with GCC 13.3 / Qt 6.4.2, devices disabled and
warnings as errors; this is not a full Linux application/runtime claim. Native
Qt fixtures verify multiselection, link toggling, primary focus, batch undo,
save/reopen, cancellation and reachable controls at 100%, 125% and 200% with
expanded RTL text. Twenty-one current widget renders were generated and reviewed
by representative layout. Initial visual review exposed a collapsed narrow RTL
name field; wrapping labels, bounded combo sizing and a minimum editable width
repaired it, with per-control reachability tests added. All 21 catalog snapshots
compile and contain the 562 extracted Audio keys in this verification scope.
New translations remain unfinished English fallbacks pending human translation.
Documentation, offline guide, CLI registry and global source-string checks pass.

Evidence and exact source/binary/object hashes are in
`.agents/tmp/audio-daw/arrangement-verification.json`. Original sources remain
immutable, source snapshots are shared through edits, and group/fade metadata
counts toward bounded undo history. No physical input/output device, user input
injection, game, network connector or OS screenshot was used. Native hardware,
macOS, screen-reader acceptance, ripple/range edits, recording/monitoring/comping,
MIDI/instruments, plugin hosting, long-media streaming and the other full DAW
gates remain open. The goal remains active.

An unrelated existing issue remains: the initial registry probe in
`scripts/validate_cli_docs.py` lacks a private settings override; this work used
an explicit task-private profile. Automatic approval review rejected cleanup of
three superseded widget renders with the generic reason “blocked by policy”.
Those files are retained, listed in `arrangement-cleanup-status.json`, and must
not be retried indirectly. Earlier rejected cleanup artifacts were preserved.

### Time-range editing checkpoint, 2026-10-06

The shared `core/audio_range` service clears clip portions, ripple-deletes a
range, inserts silence or repeats a section after its end. Every edit has an
explicit checked-track or all-track scope; named clip groups cannot broaden
it. Time edits optionally follow gain, balance and effect automation, including
bus lanes. Master following is a separate all-track choice. Clear leaves all
automation in place, and tempo/meter markers remain unchanged in every mode.

Clip slices retain immutable sources and original fade windows. Automation
retains its original interpolation endpoints, span and offset through cuts,
so partial smooth curves and splice jumps preserve sampled values. Native v7
stores these domains and reads v1–6 with ordinary interpolation defaults.
Staged Qt controls, timeline range brackets, strict CLI options, single-step
undo, save/reopen, recovery and rendered delivery share that state. Stateful
effects process the resulting arrangement normally; their history is not copied
across a time edit. No dependency or external implementation was introduced.

All 32 Windows Audio suites have passing latest results. Independent scalar
curve and interval oracles cover direct/routed renders, stateless effects,
block boundaries, one-frame and repeated cuts, distant timeline positions,
empty/held/disabled lanes, group scope, and per-lane/aggregate capacity. Actual
CLI checks include byte-identical float WAVs and output/source guards. Native
fixtures reject hostile checksummed domains transactionally and verify legacy
migration and recovery. Qt fixtures cover staged validation/cancel, undo/redo,
save/reopen, unchanged point precision and per-control reachability at
100/125/200% with expanded RTL text. Thirty current widget renders cover the
four range operations, cut-curve preview and selected timeline interval.

The first compact-layout check scrolled to a spin box's input cursor instead
of its complete bounds. Explicit whole-control positioning now verifies every
field. The other 30 suites already passed; only the changed core/UI fixtures
were rerun. Documentation, generated offline guide, task-private CLI registry,
global source-string validation and all 21 catalog snapshots pass. The scoped
catalog check covers 622 Audio strings; new translations remain unfinished
English fallbacks. All 70 selected Audio translation units compile with GCC
13.3 / Qt 6.4.2, devices disabled and warnings as errors. This verifies the
selected sources, not full Linux linking/runtime, macOS or hardware acceptance.

Evidence belongs in `.agents/tmp/audio-daw/range-verification.json`, including
test lineage, source/binary/object hashes and current renders. Initial shared
Windows builds encountered in-progress Modeller symbol/getter changes; the
current shared sources and refreshed configuration resolved them without changes
to that work here. The unrelated documentation validator's first registry probe
still lacks a private settings override; this increment used its own profile.
Previous policy-rejected cleanup artifacts were preserved without retry.

The full capability table remains authoritative. Tempo ramps/musical anchoring,
recording/monitoring/punch/loop/comping, MIDI/instruments, plugin hosting,
long-media streaming, advanced processing/interchange and native platform/device/
assistive-technology acceptance remain open. This checkpoint is progress on the
active DAW goal, not a complete professional workstation or release certification.

### Session media management checkpoint, 2026-10-06

`core/audio_media` now supplies usage/availability inventory, source naming,
bit-identical relinking, explicit replacement across all referencing clips,
and removal of unused embedded sources. Missing file references leave embedded
playback intact. Relinking preserves samples, markers and metadata; replacement
uses a new immutable source identity, preserves channel count, optionally uses
the shared resampler, and must cover every existing clip's source range. Clip
IDs, positions, offsets, groups, fades, routing and automation remain intact.
No external implementation, new dependency or native schema version is added.

The **Media…** dialog performs cancellable inventory and review off the GUI
thread, shows current/proposed waveforms, and invalidates Apply when fields
change. The session worker verifies the reviewed file digest and resolved path
again before adopting one undo/recovery state. Save/export protections include
sources retained by current, undo and redo states. The strict CLI shares the
service, emits inventory/change reports, supports dry-run digest review and
protects original provenance even after the command removes or relinks it.
Shared rendering carries changes into playback, mixdown, stems, waveform/game
delivery, package staging and level placement. Source files are never deleted.

All 35 selected Windows Audio suites have passing latest results. Media core
fixtures cover exact usage, unchanged relink rendering, replacement across all
clips, metadata/marker retention, explicit SRC, native v7/recovery, invalid
requests, cancellation and stale-file rejection. Actual CLI fixtures compare
exported float WAV bytes against the shared renderer and exercise strict options,
reviewed digests, source protection and revision-checked in-place saves. Qt
fixtures cover staged review/invalidation, cancellation, undo/redo, save/reopen,
stale review, undo-held source export protection and control reachability at
100/125/200% with expanded RTL strings.

The first narrow 200% layout exposed a long resampling checkbox label that
forced horizontal scrolling. A concise label with the complete accessible
name/tooltip fixes the form; the repeated media UI suite passes. The other 34
suites passed without changes. Forty-two QWidget renders cover all five
operations at full/compact sizes, including both waveform comparison tabs.
These are direct widget renders and synthetic/fake-device tests, not OS captures
or native device/assistive-technology acceptance.

All 76 selected Audio translation units compile with GCC 13.3 / Qt 6.4.2,
devices disabled and warnings as errors, including the final layout correction.
This is targeted compilation, not full Linux linking/runtime or macOS acceptance.
Documentation, generated offline help, the private-profile CLI registry and
global source-string checks pass. All 21 catalog snapshots compile and contain
705 scoped Audio strings; newly added translations remain unfinished fallbacks.
All 21 native build catalog targets also compile. Current evidence, hashes and
test lineage are recorded in `.agents/tmp/audio-daw/media-verification.json`.
Earlier policy-rejected cleanup artifacts were preserved without retry.

Bulk relink discovery, source streaming, automatic embedded-source waveform
round trips, editable interchange and every other open capability gate above
remain part of the active goal. This checkpoint does not close the professional
DAW objective. The unrelated CLI-doc validator's first registry probe still
omits private settings; verification here supplies its own task profile.


### Strip metering checkpoint, 2026-10-06

The optional shared renderer meter bank now measures pre/post tracks, buses and
master: sample peak, 300 ms exponential RMS, held maxima, per-channel samples
above full scale, and stereo phase correlation. Peak envelopes decay at 24 dB/s;
full-range RMS/correlation are available independently of the live envelopes.
Finite internal amplitudes use normalized energy accumulators. Fixed-capacity
state and caller-owned scratch keep rendering free of allocation, locks and I/O.
Enabling meters preserves every rendered float sample.

Logical tap windows capture latency warmup without counting out-of-range data.
Offline analysis drains disconnected strips whose insert latency exceeds the
master path. Mute, solo and nonlinear bus residuals share the existing routing
semantics. Loops retain meter history; start/seek/stop and explicit meter reset
clear it. Live updates use a coalescing mailbox outside the render path, and
readings precede audition gain/clipping. They represent rendered audio, which
can lead device consumption; per-tap frame counts expose that distinction.

The native Meters window shows both signal points without rerendering. Analyze
Range uses the existing cancellable session worker and changes no document,
undo or recovery state. The read-only `asset audio-session meters` command uses
the same service, strict frame/block options and structured per-strip JSON.
Session v7, sources, waveform/package handoff and delivery precision are unchanged.
No new library, external code or borrowed UI asset is introduced.

Verification covers analytical energy/decay/phase signals, mono pan/gain flow,
64 strips, timeline limits, silence, large finite amplitudes, overflow and
cancellation; independently rendered strip taps, block partitions, lookahead,
solo residuals and disconnected latency; exact output parity; fake-device short
writes, reset acknowledgements and bounded UI notifications; actual CLI option
rejection and file preservation; and native window controls/analysis/cancellation.
Visual review caught overlapping expanded text that the initial widget-bounds
checks missed. A scrolling body, separate action controls, shorter visible
frame details and thin bars below numeric text address it; checks now require
usable row space and non-overlapping buttons. Current test, selected Linux
object, catalog and QWidget render evidence is recorded under
`.agents/tmp/audio-daw/meter-verification.json`.

This checkpoint does not finish the professional DAW goal. Live true-peak/LUFS,
seamless mixer changes, sidechains/surround, synchronized recording/monitoring,
MIDI/instruments, plugin hosting, disk streaming and the other gates above remain
open. Selected devices-disabled Linux compilation is not Linux runtime, macOS,
physical audio-device, assistive-technology or release acceptance.

### Duplex engine groundwork checkpoint, 2026-10-06

The shared renderer now has an explicit physical-clock mode for caller-owned
mono/stereo input windows. It preserves processing delay instead of priming from
future media. Input joins existing track polarity/pan/automation, inserts, sends,
buses and nonlinear master effects. Replacement windows suppress only the
corresponding clips, and an explicit playback endpoint allows effect/input drain.
Existing compensated playback, analysis and exports retain their behavior.

`AudioDuplexProcessor` prepares up to eight armed tracks with explicit maps from
up to 32 input channels. It combines first-sample ADC/DAC timing, graph latency
and signed calibration once, then advances placement by integer sample counts.
Capture crops to a half-open punch range. Monitoring is optional and dry capture
does not inherit monitor or audition gain. Priming, timestamp quality/jitter,
clock jumps, device loss flags, invalid samples, render failure, full queues,
Stop and the physical drain have explicit bounded behavior.

`AudioDuplexCaptureQueue` uses lock-free 64-bit release/acquire counters and a
bounded SPSC frame ring, with complete arm-set publication. Queue reserve depends
on frames rather than callback count. The callback does no allocation, locking,
I/O or notifications. Tests connect its dry, corrected ranges to ordinary
new-only `.vstake` journals, verify complete/interrupted prefixes and use the
normal reviewed reader and session importer. Native schemas and dependencies
are unchanged. The [integration contract](../AUDIO_DUPLEX.md) documents ownership,
limits, sample placement, pending device/take UI work and backend research.

The 44 selected Windows Audio suites passed, including three new live-render,
duplex-clock and queue/journal suites. Fixtures compare independent sample
ordinals and offline mixes, cover nonlinear routing and block partitions,
instrument callback C++ allocations and check Qt sharing, exercise maximum
callback/channel/arm bounds, stress concurrent queue wraparound, and recover
completed/interrupted journals. A test fixture's initial stack allocation was
moved to prepared heap storage after Windows reported stack overflow. GCC found
a signedness warning in a meter-frame assertion; the explicit comparison type
fix also receives a Windows rerun.

Fourteen selected Audio/core/CLI/test objects compile with GCC 13.3 / Qt 6.4.2,
devices disabled and warnings as errors. This is selected compilation, not full
Linux linking or runtime. Catalog verification explicitly checks all fifteen
duplex messages: an initial `tr` helper prevented lupdate from extracting nested
markers, so it now has an unambiguous name. All 21 native catalogs compile.
Current checks and source/binary/object hashes belong in
`.agents/tmp/audio-daw/duplex-verification.json`; they describe this increment
in the concurrent worktree rather than a frozen release.

This is internal groundwork. The current editor still records standalone takes.
A native duplex backend, coordinated playback stop, permission/lifecycle handling,
durable pass receipts, disk aggregation/finalization, bounded telemetry, grouped
take review/import and GUI/CLI diagnostics remain required. Loop takes, comping,
long-media streaming, MIDI, plugins and all other professional DAW gates remain
open. Physical timing, monitoring, dropout/reconnect and accessibility acceptance
are separate from synthetic fixtures. No microphone or physical audio device was
opened for this checkpoint. The unrelated CLI-doc validator still omits private
settings on its initial registry probe; this work supplies an explicit task profile.

### Recording worker and durable pass checkpoint, 2026-10-06

`AudioRecording` now coordinates explicit permission, a playback-shutdown
acknowledgement, worker preparation and a separate disk owner. Every planned
journal opens before the device starts. The real callback owns the prepared
duplex processor, writes to the bounded capture queue and publishes fixed-size
telemetry through a separate SPSC ring. A coalescing UI mailbox bounds pending
notifications. A processed-frame watchdog detects stalled or endlessly priming
devices; Stop, drain, errors and application shutdown have explicit finalization.

New `.vsrecord` folders contain immutable checksummed plans, ordinary per-arm
`.vstake` journals and new-only final receipts bound to independently scanned
prefixes. A failed arm write can leave unequal lengths; a failed footer can
leave mixed completion. Both remain visible instead of being reported as an
atomic group. Driver-close errors interrupt completion, and native quarantine
updates availability. Existing final receipt files are retained on conflicts.
`asset audio-recording inspect` exposes the same validity, timing and prefix
diagnostics without permission or device access. Existing take export/import,
waveform analysis, session undo/recovery and game delivery remain the individual
range handoff. Session and individual-take schemas are unchanged.

Sixteen selected Windows Audio suites pass, including three new grouped-storage,
recording-worker and real-CLI suites. Fixtures verify exact dry channel ordinals,
punch placement and one timing correction; preparation before input; denial and
cancelled/stale lifetime gates; partial write/footer and conflicting-receipt
failures; queue/telemetry bounds; progress and drain; playback acknowledgement;
failed driver close; and zero callback C++ allocations. A final lifecycle review
found that close errors initially could leave a clean outcome; the worker now
checks post-close status, with a regression covering that correction.

Eight selected objects compile with GCC/Qt 6.4.2, both device features disabled
and warnings treated as errors. All 21 translation catalogs compile, with 25
scoped recording/CLI strings extracted; global extraction, docs, offline guide,
private-profile CLI registration and source layout validate. Evidence and
source/binary/object hashes are retained in
`.agents/tmp/audio-daw/recording-verification.json`.

The professional DAW goal remains active. Native recording and grouped
review/import controls must next coordinate all editor audition paths, guard
current session revisions/track IDs, and apply reviewed takes through one
undoable session operation. Loop takes, comping, streaming, MIDI, plugins and
the other full-suite gates remain open. No physical device was opened; Linux
runtime, macOS, hardware timing/dropout/reconnect, power-loss and assistive
technology acceptance are not established by these fixtures. The unrelated
CLI-doc validator's initial registry probe still omits a private settings path.

### Native recording review checkpoint, 2026-10-06

**Record Tracks…** now provides explicit input/output selection, up to eight
armed mono/stereo tracks, backing and punch frames, optional monitor routing,
calibration, buffering and clock tolerance. Input opens after permission,
browser/waveform/session output acknowledgement and durable journal setup.
Review holds the current session revision and supports per-take target tracks,
trim, channel order, recorded/manual placement, clip replacement and grouping.
Interrupted passes require explicit prefix acceptance. Background verification
and waveform preparation produce one undoable session adoption.

The shared grouped importer pins plan, receipt and selected-prefix digests,
preflights session budgets, clears all requested old clip ranges before adding
any takes, and publishes no partial session on failure or cancellation.
CLI `asset audio-recording import` uses the same operation with a versioned
review JSON file, optional reviewed source hash, dry runs and guarded outputs.
Recording files and source provenance remain protected. No session/take schema
or external dependency change was needed.

All 23 selected Windows Audio suites pass. Fifteen selected Audio translation
units compile with GCC/Qt 6.4.2, devices disabled and warnings as errors. All 21
translation catalogs compile; scoped strings, global extraction, documentation,
offline guide, private-profile CLI registry and source layout checks pass.
Direct widget renders cover dark/high-contrast themes, 100–200% text scaling
and expanded RTL layouts. Synthetic controls use widget APIs and fake devices;
no physical audio device, user input or OS screen capture was used.
Evidence is recorded in `.agents/tmp/audio-daw/recording-ui-verification.json`.

An unrelated Qt `slots` macro collision in package staging was fixed with a
local parameter rename. Its additional suite still fails two assertions for
the expected signed-32-bit oversized PAK/WAD diagnostic text; the rejection
assertions themselves pass. A transient model-assembly link mismatch resolved
after refreshing Meson's concurrently changed source list. The CLI-doc
validator's initial registry probe still lacks a private settings file.

The full DAW goal remains open. Native device/permission/latency/reconnect and
power-loss acceptance, live input meters, loop recording, comping, long-media
streaming, MIDI/instruments, plugin hosting and the other capability gates
remain outstanding. Linux evidence here is compilation only; native keyboard,
screen-reader and macOS runtime acceptance are not established.

### Recording meter checkpoint, 2026-10-06

**Record Tracks → Meters** now displays each mapped dry input and stereo output
before safety clipping. The shared sample meter engine supplies peak, 300 ms
RMS, held maximum, headroom and over-range counts. Preroll contributes real
input samples; processing padding does not invent input. Mono arms expose one
row and stereo mappings retain their selected order. Native bars, numeric cells,
explicit clipping states and wrapped selected-row details support scaled,
high-contrast and expanded RTL layouts.

A fixed-size telemetry snapshot carries readings through the existing bounded
callback ring. Reset clears the visible history immediately, then waits for a
lock-free callback epoch acknowledgement. Stale telemetry cannot restore old
peaks. Reset during finalization is owned by the control worker after native
close. Captured samples, timing, DSP state and pass-wide diagnostics remain
unchanged. Final readings stay in the current dialog; no recording/session
schema or dependency is added. Reopened audio uses existing reviewed export
and waveform/session analysis.

All 26 selected Windows Audio suites pass. The worker and UI suites also pass
after adding an explicit fixture acknowledgement before pausing callbacks for
the reset race check. Independent numerical fixtures cover different callback
sizes, reset boundaries, dry sample preservation and pre-clamp output. Fifteen
selected Audio units compile with GCC/Qt 6.4.2, devices disabled and warnings as
errors. All 21 catalogs compile; 201 scoped translation keys, global extraction,
documentation, offline guide, private-profile CLI registration and source layout
checks pass. Eighteen direct widget renders cover Record, Review and Meters.
Evidence is retained in `.agents/tmp/audio-daw/recording-meter-verification.json`.

This is verified progress, not completion of the professional DAW goal. Loop
takes, comping, long-media streaming, MIDI/instruments, plugin hosting and the
remaining capability gates are still open. Synthetic/offscreen tests do not
establish hardware timing, permission, dropout/reconnect, power-loss, native
keyboard/screen-reader or macOS runtime acceptance. Linux evidence is compilation
only. No physical audio device, user input control or OS capture was used.
The unrelated CLI-doc validator still omits a private settings file in its
initial registry probe; previously reported package diagnostic failures were
not retested during this checkpoint.

### Continuous loop recording checkpoint, 2026-10-06

**Record Tracks → Loop passes** now captures a finite 1–10,000 repetitions of
the punch range within the unwrapped timeline bound. Preroll runs once. The
device/DSP clock advances continuously while source positions and automation
repeat. Track, bus and master automation use latency-corrected loop positions;
inserts, delay/reverb tails, routing delays and monitoring retain their state.
Callbacks split source/input windows at loop boundaries without allocation or
device reopening. Stop preserves a partial final pass, and a later-pass device
clock discontinuity stops without relabeling captured input.

Each arm retains one continuous journal. Canonical version-2 recording plans
add the finite pass count; ordinary single-pass plans retain version 1. Existing
receipt and take formats remain unchanged. Review exposes available passes and
local trim ranges independently per arm, retaining the original punch placement.
It imports one selected pass per arm at a time. Version-2 CLI review JSON adds
one-based `loopPass` and supports up to 128 distinct arm/pass pairs within normal
session budgets. Duplicate pairs, unavailable passes and partial-pass overreads
reject the entire change. Source journals remain available for further review.

All 35 selected Windows Audio suites pass. Additional final checks cover master
automation, a clock jump after the first loop, Stop during a later pass and
reachable native pass controls. The loop oracle uses an independently expanded
linear arrangement and exact dry sample ordinals across callback sizes, including
one-sample loops. Nineteen selected Audio units compile with GCC/Qt 6.4.2,
devices disabled and warnings as errors. All 21 catalogs compile; 222 scoped
translation keys, global extraction, documentation, offline guide, private-profile
CLI registration and source layout checks pass. Twenty-four direct widget renders
cover dark/high-contrast, 100–200% scale and expanded RTL forms. Evidence and
hashes are retained in `.agents/tmp/audio-daw/recording-loop-verification.json`.

The full DAW goal remains active. Open-ended recording, comp lanes, long-media
streaming, MIDI/instruments, plugins and the other capability gates remain open.
Buffered audition loops still reset DSP at their boundaries; unifying that
behavior with continuous recording loops is an explicit integration gap.
Synthetic/offscreen checks do not establish physical device, permission,
latency/dropout/reconnect, power-loss or native assistive-technology acceptance.
Linux evidence is compilation only. No physical device, input injection or OS
capture was used. The CLI-doc validator's unrelated settings-isolation issue was
fixed: its initial registry query now uses a disposable private profile too.
All 219 registered-command documentation/routing checks pass with private
settings and the explicit project working directory. Earlier package diagnostic
failures were not retested.

### Continuous playback loop checkpoint, 2026-10-06

Buffered session audition now keeps a separate unwrapped processing clock while
its displayed cursor repeats the selected range. The compensated and live
renderers share source-window splitting and authored-time mapping. Track, bus
and master automation wraps at each processor's signal time; routing delays,
filter/dynamics state, delay/reverb tails and modulation retain their histories.
Lookahead primes once from wrapped future context, including loops shorter than
the graph latency. Playback near the authored timeline limit can continue on
the independent bounded 64-bit clock without extending native session bounds.

Loop meter history counts every processed signal frame. Upstream taps may lead
the compensated master by their latency difference; the audible cursor still
follows device consumption. Pause and meter reset preserve DSP. Stop, seek and
an actual loop-policy change reset processing. Reapplying the same policy is a
no-op in both the core transport and playback worker. Dormant loop bounds reserve
the routing path during preparation, so toggles need no render-time allocation.
Exports and waveform handoff remain one finite pass through the requested range.
No session/recording schema, dependency or new UI control is introduced.

All 36 selected Windows Audio suites pass. The new playback-loop oracle compares
an independently expanded linear arrangement and includes one-frame loops,
nonlinear bus solo paths, sends, automated effects, reverb/modulation, different
block sizes, pause/seek/reset, cancellation and physical-clock bounds. A separate
feedback recurrence checks exact echo samples. CLI canonical sample hashes and
the fake-device worker agree with the linear oracle; partial byte writes retain
sample order and loop boundaries do not reopen output. Existing recording,
delivery, recovery and offscreen UI suites pass. Seventeen selected Audio units
compile with GCC/Qt 6.4.2, devices disabled and warnings as errors.

All 21 catalogs compile, 150 scoped translation keys and global extraction pass,
and documentation, offline guide and source-layout checks pass. The private
CLI registry/help and all 221 registered-command documentation/routing checks
pass. That check found two newly registered Level commands absent from CLI
Strategy; descriptions for `map copy-surface` and `map paste-surface` were added
after checking their handlers. Earlier package diagnostic failures were not
retested. Source, object and binary hashes, exact test results and reusable
verification helpers are retained in
`.agents/tmp/audio-daw/playback-loop-verification.json`.

The previous buffered-playback loop integration gap is closed by this increment.
The full DAW goal remains active: open-ended recording, comp lanes, long-media
streaming, MIDI/instruments, plugins and the other capability gates remain open.
Linux evidence is compilation only. Physical device, latency/dropout/reconnect,
power-loss, macOS runtime and native assistive-technology acceptance remain
unverified. Tests used fake devices and direct Qt APIs; no physical audio device,
user input control or OS screen capture was used.


### Recording comp sections checkpoint, 2026-10-06

Record Tracks review now queues pass-local sections, including repeated sections
from the same arm/pass, with Add/Update/Remove and explicit crossfade length.
A pure planner rejects overlapping authored sections, insufficient outgoing
handles, short incoming sections, channel-count mismatches and source/sample
budget overflow. Adjacent cuts use complementary linear fades after the cut;
zero makes hard cuts. Gaps remain unfaded. The queue shows ranges, destinations,
placement and planned fades, with named native controls and wrapped diagnostics.
Frame fields and table ranges retain numerical direction in RTL layouts.

Import rechecks plan/receipt/take hashes and batch-decodes all selected ranges
from each arm in one scan after folder inspection. Invalid batches or cancellation
expose no replacement session. Old clip windows are cleared before new sections
are added. Source provenance records original cuts and actual handle ranges;
recording journals remain unchanged. The entire comp enters the session as one
undoable set of editable, optionally grouped clips. Native v7 save/recovery,
rendering, stems and game delivery reuse existing source/region/fade services.
CLI review version 3 requires comp:true, crossfadeFrames and one-based loopPass
on every selection. Versions 1/2 retain their previous meaning and pair limits.
No dependency, external code, new CLI command or native session schema is added.

All 37 selected Windows Audio suites pass with warnings as errors. A new
independent sample oracle checks two crossfades, backing outside the comp,
reordered sections, source retention and native round trips. Further cases
cover partial handles, sample/source budgets, gaps, simultaneous target tracks,
stale hashes and cancellation. Batched take tests cover reordered/overlapping
ranges and channel mappings with all-or-nothing failures. CLI tests cover v3
review, dry run, actual saved fades and preserving output after a stale review.
Offscreen UI checks cover queue editing, validation, one-step undo/redo and
native accessibility metadata. Twenty-seven direct widget renders cover dark,
high-contrast, 100–200% scale and expanded RTL layouts; numerical range direction
was corrected after visual inspection.

Fourteen selected Audio compilation units pass with GCC/Qt 6.4.2, playback and
duplex disabled and warnings as errors. All 21 catalogs compile, 181 scoped keys
and global translation extraction pass, and documentation, generated offline
guide, source layout and all 221 registered-command CLI documentation/routing
checks pass with private settings. Verification helpers, exact results and
source/binary/render hashes are retained in
`.agents/tmp/audio-daw/recording-comp-verification.json`.

The full DAW goal remains active. This closes basic recording-section assembly,
not the entire comping gate: dedicated alternate take lanes, audition inside
Review and reopening an editable comp recipe remain open. Open-ended recording,
long-media streaming, MIDI/instruments, plugins and the other capability gates
also remain open. Linux evidence is compilation only; physical-device timing,
dropouts/reconnect, power-loss, macOS runtime and native keyboard/screen-reader
acceptance remain unverified. Tests use fake devices and direct Qt APIs, without
physical device use, input injection or OS capture. No new unrelated issue was
found; earlier package diagnostic failures were not retested.

### Recording audition checkpoint, 2026-10-06

Record Tracks review now auditions the current section or the complete reviewed
import before changing the session. A shared service rechecks plan, receipt and
take hashes and builds the same clip, replacement and crossfade snapshot used by
import. Playback spans the selected clips and handles without added preroll or
tails. Include backing clips defaults on; isolated audition removes other clips
while retaining mixer routing, inserts, automation, mute and solo. It is not a
dry bypass. Audition leaves source journals, session state and undo history intact.

The native Audition tab provides independent output selection, buffer size,
listening volume, repeat, pause/resume, frame seek and visible playback diagnostics.
Each play waits for existing browser, waveform and session output to stop, then
verifies and prepares the snapshot asynchronously. Edits invalidate playback;
Stop, Record, Import and Close cancel pending work or wait for output shutdown.
Late and duplicate handoff callbacks cannot restart a cancelled audition. Playback
requires no microphone permission. Missing playback support still permits reviewed
import. An early-stop/device-discovery race in the shared playback status was also
fixed so an available output no longer remains labelled unavailable.

`asset audio-recording preview SESSION --review PLAN.json --output PREVIEW.wav`
exports the same finite snapshot through the existing mixdown service without a
device. `--isolated`, dry run, overwrite and session-digest guards are supported.
Preview retains the input session, review, journals and source media; float32 WAV
uses session levels rather than GUI listening volume or device clipping. Existing
v1/v2/v3 review plans and native session schema 7 are unchanged. No dependency or
borrowed code is added.

All 38 selected Windows Audio suites pass with warnings as errors. Exact sample
checks cover section and comp audition, backing isolation, mixer settings,
partial device writes, repeat, pause/seek, CLI WAV output and original-state
retention. Further tests cover cancelled verification, stale receipts, unavailable
and failed devices, delayed/duplicate handoffs, output shutdown before recording
or session adoption, and one-step import undo. Twelve selected Audio compilation
units pass with GCC/Qt 6.4.2, playback and duplex disabled and warnings as errors.
Linux evidence is compilation only.

All 21 catalogs compile; 219 scoped translation keys, global extraction,
documentation, generated offline guide, source layout and all 221 registered CLI
command documentation/routing checks pass. Thirty-three direct widget renders
cover dark/high-contrast, 100–200% scale and expanded RTL forms. The final Audition
and Review layouts were visually inspected. Verification helpers, exact results
and source/binary/object/render hashes are retained in
`.agents/tmp/audio-daw/recording-audition-verification.json`.

This closes audition inside recording Review. The full DAW goal remains active:
dedicated alternate take lanes, reopening an editable comp recipe, open-ended
recording, long-media streaming, MIDI/instruments, plugin hosting and the other
capability gates remain open. Physical device timing, dropout/reconnect,
power-loss, macOS runtime and native keyboard/screen-reader acceptance remain
unverified. Tests use fake devices and direct Qt APIs, without physical device
use, user input control or OS capture. No new unrelated issue was found; earlier
package diagnostic failures were not retested.

### Saved recording review checkpoint, 2026-10-06

Record Tracks now saves and reopens editable recording review recipes through
the existing v1/v2/v3 JSON format. Open Saved Review restores every ordered cut,
pass, channel map, target, placement, replacement flag, interruption choice,
grouping option and crossfade length. Use queued take selections preserves
non-comp reviews with multiple passes from one arm as well as repeated-pass
comp sections. Draft fields enter the active queue through Add/Update. Checked
whole-take selection remains available when queue mode is off.

The shared C++ service reads bounded 128 KiB JSON, resolves the recording folder
relative to the review file, independently verifies retained journals and
preflights the full selection against the current session. GUI Open/Save run on
cancellable workers; failed or cancelled operations retain the prior queue and
saved file. Save and Save As use the shared atomic output publisher, protected
session/media/recording paths and loaded SHA-256 conflict guards. Relative paths
allow a review and its recordings to move together. No device or session edit
is created. CLI `asset audio-recording save-review` uses the same verification,
dry run and guarded writer; the saved files feed existing import and preview.

The native file/status label reports saved and unsaved choices. Close, Open and
Record offer Save/Discard/Cancel before replacing edited choices. Cancel is the
default, and Save must finish successfully before the requested action proceeds.
A failed save keeps the review open. Save is unavailable while work is active.
If cancellation arrives after publication, the UI retains the actual saved
identity rather than claiming the file was not written. Confirmation and save
dialogs explicitly inherit the editor's layout direction.

All 39 selected Windows Audio suites pass with warnings as errors. New fixtures
cover exact comp and legacy review round trips, audition sample parity, moved
bundles, output conflicts, cancellation, missing tracks, malformed/oversized JSON,
protected recordings and source/session retention. CLI fixtures cover path
rebasing, preview reuse, dry runs, overwrite and session revision guards. Native
control tests cover complete queue restoration, unsaved state, stale-save
failure, Cancel, successful Save-before-Close and explicit Discard-before-Record.
The final RTL dialog-direction refinement passes both affected UI/audition
suites again. Nine selected Audio units compile with GCC/Qt 6.4.2, playback and
duplex disabled and warnings as errors; Linux evidence is compilation only.

All 21 catalogs compile; 232 scoped translation keys, global extraction,
documentation, generated offline guide, source layout and all 221 registered CLI
command documentation/routing checks pass. Thirty-six direct widget renders
cover dark/high-contrast, 100–200% scale and expanded RTL forms, including the
unsaved-review confirmation. Verification helpers and exact results with
source/binary/object/render hashes are retained in
`.agents/tmp/audio-daw/recording-review-verification.json`.

The full DAW goal remains active. Saved recipes are separate files and require
their retained recording journals and matching destination tracks. They do not
identify and revise an earlier import automatically. Dedicated alternate take
lanes and comp revisions embedded in native sessions remain open, along with
open-ended recording, streaming, MIDI/instruments, plugins and the other gates.
Physical audio devices, native keyboard/screen-reader behavior, macOS runtime,
dropout/reconnect and power-loss acceptance remain unverified. Tests use fake
devices and direct Qt APIs, without user input control or OS capture. No new
unrelated issue was found; earlier package diagnostic failures were not retested.
