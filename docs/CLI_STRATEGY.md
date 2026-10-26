# CLI Strategy

`model curve-transfer` copies or reverses selected parent-local channels over
an inclusive source range within the current rig. It shares automatic capture,
exact boundary cropping, rate/time scaling, destination interval replacement,
combined rebaking and change reports with **Transfer Animation Range**. Output,
dry-run and overwrite use normal model services. See
[range automation](MODEL_ANIMATION_CURVES.md#automate-range-transfers).

`model curve-keys` exposes explicit sample/range selection across stored joint
channels, shared retiming/duplication/removal/reduction, continuous scalar change
reports and guarded dry-run/output behaviour. See
[key-group automation](MODEL_ANIMATION_CURVES.md#automate-key-groups).

`model curves` shares persistent skeletal channel capture, key/tangent editing,
whole-group replacement and explicit baking with **Animation Curves**. It uses
normal document validation, schema-9/10 persistence, guarded output and non-writing
dry runs. See [animation curves](MODEL_ANIMATION_CURVES.md#automate-curve-edits).

`model weight-transfer` shares mirrored and spatial weight transfer with the
Mesh Editor: bounded vertex/triangle correspondence, joint maps, destination
locks, guarded outputs and dry-run validation. See
[weight transfer](MODEL_WEIGHT_TRANSFER.md#automate-the-workflow) for the contract.

`model weight-paint` shares replace/add/subtract/smooth strokes, exact joint locks,
normalised remainders, vertex coverage and atomic pose rebaking with the review
window. It accepts explicit vertex lists or bounded versioned stroke recipes,
with guarded `.mesh.json` output and a non-writing dry run. See
[weight painting](MODEL_WEIGHT_PAINT.md) for the complete command contract.

`model skinning` shares influence inspection/selection, rigid or distance binding,
explicit rebinding, reduction and unbinding with **Skin Weights and Binding…**.
Edits require an explicit surface target and guarded `.mesh.json` output, with
dry-run, overwrite, strict option checks and text/JSON reports. Inspection reports
raw combined weights and issue flags; selection emits sorted vertex indices.
See [Skin weights and binding](MODEL_SKINNING.md#automate-the-workflow).

`model rig` shares atomic joint/weight authoring with **Edit Skeleton**, strict
operation options, guarded `.mesh.json` outputs, explicit overwrite, dry-run
validation and text/JSON inspection. Hierarchy edits retain poses; weight edits
reject independent frame changes. See [Skeleton authoring](MODEL_RIG.md).

`model pose` adds strict, indexed skeletal clip and pose operations through the
same candidate transaction: bind positioning, creation/duplication/rename/delete,
tail resizing, timing, joint transforms, full joint-pose copying, parent-space
interpolation and `--operation working-set`, which chooses the clips baked into
editable frames. Inspection returns clip metadata, preview ranges, the
`workingSet` report and model-space joint matrices. New displayed clips must
fit the full editable frame budget; no authoring operation silently skips its
requested output. `model import` re-bakes a skeletal model over the editable
budget into a working set and reports `rebaked` and `workingSet`; `--clips`
chooses the working set by name or index, and a clip that matches nothing is a
usage error (exit 2) in both commands. Model inputs named relative to the
working folder are resolved before decoding, so loose companions such as
md5anims are found as they are for absolute paths. Every skeletal writer
reports the source it wrote as `outputSource` (`bytes`, `encoding`,
`limitBytes`), as `model import` and `model edit` do in `source`. See
[the working set](MODEL_SKELETAL_WORKING_SET.md).

`map clip` shares the editor's atomic brush-cut service. It accepts either
`--axis` with `--at` or three `--points`, and repeatable `--object`. Unknown,
duplicate and incomplete options fail before saving. Whole brushes on the
discarded side are removed, empty brush owners are removed, and any unsafe
brush refuses the whole operation. `clipped` counts all changed/removed
brushes; `pieces` lists the remaining selection and `skipped` stays empty for
schema compatibility. See [the level manual](manual/levels.md#clip-brushes-across-views).

Editor profiles include `q3radiant` (`q3-radiant`, `quake3-radiant`,
`quake-iii-radiant`). `editor select`, `editor controls` and `editor gestures`
use its canonical preference store. The shared gesture format also exposes
`camera.driveButton`, `camera.driveModifiers`, `camera.discreteDriveKeys` and
`plan.fixedCameraSteps`; dry-run, validation and atomic import/export behavior
are unchanged. Choosing the profile never changes map formats or game targets.

`asset audio-session meters SESSION [--start-frame F] [--end-frame E]
[--block-frames B]` analyzes pre/post track, bus and master sample peak, RMS,
held maxima, over-range counts and stereo correlation through the shared
renderer. Bounds default to the complete session; B defaults to 4096 and must
be 1–65536. A nonempty range is required. This read-only command rejects
`--output`, `--overwrite`, `--dry-run`, `--loop` and unrelated options; it opens
no audio device. JSON `meters.strips` follows session order then master, with
stable IDs, `pre`/`post`, frame counts and `channels` ordered left/right.
`maximum` and `integratedRms` summarize the range; `peak` and `rms` are the final
24 dB/s and 300 ms envelope readings. Correlation has final-envelope and full-range
values. dBFS silence and undefined correlation are null. Over-range counts use
strict magnitude >1. These are sample-peak measurements; see
[Session Meters](AUDIO_EDITOR.md#session-meters).

`asset audio-session media SESSION --operation OP` shares the session media
manager's validation. `inspect` is read-only and rejects output, overwrite and
dry-run options. Its `media` JSON lists source IDs, names, paths, channels,
frames, embedded `sampleBytes`, clip counts, required source frames and track
IDs; totals include `unusedSampleBytes`. Availability IDs are `embedded-only`,
`available`, `missing`, `not-file` and `unreadable`. They report filesystem
availability, not content verification; embedded audio remains playable.

All other operations require `--output NEXT.vssession` and accept the normal
`--dry-run`/`--overwrite` guards:

| Operation | Options and scope |
| --- | --- |
| `rename` | Exactly one `--source ID` and `--name NAME` (1–256 characters). |
| `relink` | Exactly one `--source ID`, `--input FILE`, optional `--expected-sha256 HEX`; requires identical rate, channels and decoded sample bits. |
| `replace` | Same selectors as relink; optional `--resample` explicitly converts to session rate. Channel count must match, and audio must cover every referencing clip. |
| `remove` | Repeated, distinct `--source ID` selectors, all unused. IDs are literal and are not comma-separated. |
| `prune` | No selectors; remove every unused embedded source. |

`mediaEdit` JSON reports operation, added/removed source IDs, affected clip IDs,
whether original decoded samples were identical, whether conversion occurred,
and `inputSha256`. A replace/relink dry-run supplies the digest for a later
`--expected-sha256` application; malformed or stale digests fail before output.
Replacement creates a new immutable source identity while retaining every clip
ID, source offset, timing, fade/group and automation setting. The source label
is retained; native input markers/metadata follow the replacement. Relink changes
only provenance. Inputs are bounded regular files, and the reviewed resolved
path and bytes are checked again before adoption. Native v7 needs no new schema.

These commands never modify or delete source files. Output guards protect the
original session, every original source path (including sources removed or
relinked by the command) and the reviewed input. Updating the session in place
requires `--overwrite` plus the normal matching on-disk revision.

`asset audio-session range SESSION --operation OP --start-frame F --end-frame E
--all-tracks --output NEXT.vssession` edits a nonempty half-open sample range.
Use `--all-tracks` or repeated `--select-track ID` options, never both. IDs must
be distinct and existing; group links never add unchecked tracks. Operations:

| Operation | Behavior |
| --- | --- |
| `clear` | Remove clip portions and leave the gap; every automation lane stays unchanged. |
| `ripple-delete` | Remove the range and shift later material earlier by its length. |
| `insert-silence` | Insert the range length at its start, splitting crossing clips and shifting the tail. |
| `repeat` | Insert a copy immediately after the range and shift the original tail later. |

Time edits default to `--follow-automation true`, including track gain/pan and
effect lanes. `false` retains authored frames. Master effect automation follows
by default for all-track time edits; `--master-automation false` preserves it.
Explicit master following requires all tracks and track automation following.
Clear rejects both automation options. Inserted time holds boundary values;
cut curves preserve their original interpolation domain. Tempo/meter maps stay
unchanged. Limits include generated clip fragments and automation boundaries.

JSON `rangeEdit` reports operation, canonical track IDs, resulting first/end
frames and added clip IDs. Ripple deletion collapses the resulting range at its
start; repeat selects the new section. All normal dry-run, output, overwrite,
revision and media-source protections apply. Native v7 save/recovery retain
cut domains; rendered mixdown, stems and waveform handoff share the evaluation.

`asset audio-take inspect TAKE --json` returns verified metadata, frames, prefix
SHA-256, completion state and recovery diagnostics without opening audio input.
`asset audio-take export TAKE` requires `--expected-prefix-sha256`,
`--start-frame`, `--end-frame`, one-based `--channels` and a separate `--output`.
`--format project|wav` selects lossless `.vsaudio` (default) or float32 WAV;
incomplete prefixes require `--allow-incomplete`. `--dry-run` validates without
writing; `--overwrite` permits a guarded replacement. Shared readers enforce the
waveform sample limit and preserve source provenance. The resulting project
enters `asset audio-session import` or existing analysis/game delivery. There
is no CLI device-record command or implicit microphone permission request.

`asset audio-recording inspect FOLDER.vsrecord --json` inspects a recording
pass plan, final receipt and independently verified per-arm `.vstake` prefixes.
JSON `recording` exposes plan/receipt validity, `receiptMatches`, digests, timing
and each take's verified frames/completion. A valid plan returns exit 0 even
with missing/interrupted receipts so recovery diagnostics remain available;
inspect the fields rather than treating exit 0 as complete capture. Invalid
plans return exit 3, unsupported/duplicate options exit 2. This is read-only
and opens no audio device. `asset audio-recording import SESSION --review
PLAN.json --output SESSION.vssession` rechecks reviewed hashes, explicit take
ranges/channels and destination tracks, then commits one session snapshot.
The versioned plan also specifies grouping, replacement, placement and explicit
interrupted-prefix acceptance. Supports `--expected-session-sha256`, `--dry-run`
and `--overwrite`; in-place updates recheck the input identity. Parse, import
and write failures return 2, 3 and 4. See [the recording contract](AUDIO_DUPLEX.md).
The GUI's live input/output meter history is transient and is not reconstructed
by receipt inspection. Reviewed take export and waveform/session analysis provide
the existing device-free route to inspect recorded levels.
Loop recordings use version-2 plans with `loopPasses`; per-arm inspection reports
complete passes and partial-pass frames. Version-2 import review JSON adds a
one-based `loopPass` to every selection, with `first`/`end` local to that pass.
It accepts up to 128 distinct arm/pass pairs within session budgets; version-1
review files retain their original first-pass meaning. Raw `audio-take export`
ranges remain journal-relative. Version-3 review adds required `comp: true`
and `crossfadeFrames`, permits repeated arm/pass pairs, and plans non-overlapping
comp sections per target track. Zero makes hard cuts; at least two frames makes
complementary linear fades after adjacent cuts, requiring outgoing handles and
sufficient incoming length. Every selection requires one-based `loopPass`.
Handles count toward session limits. Import remains atomic, hash-checked and
compatible with dry-run/guarded writes.

`asset audio-recording preview SESSION --review PLAN.json --output PREVIEW.wav`
exports the reviewed span as stereo float32 WAV without adopting or altering
the source session. `--isolated` excludes backing clips while retaining mixer,
effects, automation, mute and solo. The range covers selected clips and handles,
with no extra preroll or tails. It shares review hashes, relative-folder
resolution, `--expected-session-sha256`, `--dry-run` and `--overwrite`. Source
media, recordings, session and review files remain protected. JSON includes
`first`, `end`, `frames`, `sampleRate`, `includeBacking`, `peak` and
`samplesAboveFullScale`. Output omits GUI listening volume and device clipping.
No CLI device start is introduced.

`asset audio-recording save-review SESSION --review PLAN.json --output REVIEW.json`
rechecks the recording and all selections, then saves the editable review without
adopting session clips. The existing v1/v2/v3 schema and 128 KiB bound are retained;
recording paths are rebased relative to the output file. `--dry-run`, `--overwrite`
and `--expected-session-sha256` apply. Updating the input review requires
`--overwrite` and uses its read SHA-256 as the output conflict guard. Protected
session/media/recording files cannot be outputs. JSON reports `review`, `output`,
`written`, `dryRun`, `reviewSha256` (empty for dry runs) and `sourceSessionSha256`.
Usage errors return 2, review/session read errors 3 and verification/save errors 4.
The GUI opens the same files as editable queues, including multiple passes from
one arm in a non-comp review; import/preview retain their existing error codes.

`editor gestures [profile]` reads or edits per-profile pointer, editing and camera-key preferences.
JSON reports defaults, effective values, available choices, field types and overrides. Repeated
`--set field=choice` forms a validated batch (`default` removes one override).
`--reset`, `--input FILE`, and `--output FILE` are separate operations;
`--dry-run` previews a preference change, and export replacement requires
`--overwrite`. Unknown/duplicate options, invalid fields, conflicting gestures,
wrong-profile imports and protected settings are refused. The GUI and CLI share
the versioned VibeStudio file parser, settings transaction and validation.
`editing.nudgeAfterDuplicate=on|off` controls ordinary GUI duplicate placement;
TB and NRC default to `off`. It uses the same per-profile persistence and portable
format. `map duplicate` continues to use its explicit offsets.
`editing.gridChangeEnablesSnap=on|off` decides whether a GUI grid command or
grid-list choice turns Snap on; NRC and NetRadiant default to `on`. `editor
controls --json` reports it as `controls.gridChangeTurnsSnapOn`, next to
`controls.gridSizes` (0.125 to 1024). The working grid is a view setting, so no
`map` command sets it; map commands take an explicit `--grid`, fractions
included (`map render --grid 0.5`); `map snap` refuses a fractional grid on
binary Doom and Hexen maps, which store whole units.

`map duplicate --make-unique` uses NRC's separate unique-copy semantics: selected
children receive a new entity owner, copied namespace properties are remapped
together against the destination, and every array copy reserves fresh names.
`--copies`, `--delta`, texture lock, dry-run and save-as protections use the shared
placement service. The `clone` alias accepts the same options. JSON
adds `cloneMode` (`ordinary` or `make-unique`); a recorded single unique copy uses
the strict version-1 Repeat action kind `duplicate-unique`, with `doomAllowed`
false. Ordinary duplication and native Doom thing arrays retain their behaviour.
Key fields use `camera.flyKeys.*`, `camera.driveKeys.*` and
`camera.lookToggleKey`, `camera.lookHoldKey` and `plan.panHoldKey`; direction
and hold values are single unmodified portable keys,
toggle values may include modifiers, and `none` disables a key. The
`shortcutWarnings` array reports potential command overlaps without rewriting
command shortcuts. Escape/Tab and ambiguous movement bindings are refused.
Fly/drive sets include `pitchUp` and `pitchDown`; `hold-key` is a distinct JSON
field type. `netradiant` and `sledge` have independent canonical settings and
aliases such as `xonotic-netradiant` and `sledge-editor`. The controls JSON
also exposes `mouseLookHoldKey` and `planPanHoldKey`.
`editor controls` and `editor current` include effective gesture overrides;
`editor profiles` describes built-in defaults. CLI writes take effect in an open
GUI when that profile/preferences are next loaded. See
[Gesture customization](EDITOR_PROFILES.md#gesture-customization).

## Workspace and shared asset diagnostics

`workspace create <file.vibeworkspace>` saves portable project and module
references. Options include `--project`, `--package`, `--map`, `--map-name`,
repeated `--code`, `--current-code`, `--active-module`, and `--from-session`.
Use `--dry-run` to validate the proposed workspace or `--overwrite` to replace
an existing validated workspace. `workspace inspect <file.vibeworkspace>`
validates the document and reports missing references without opening assets.
Both commands support text and `--json` output.

`asset audio-session new|inspect|import|edit|arrange|range|automation|effects|effect-automation|presets|tempo-map|position|mixdown|stems|recover|transport [session]` manages
multitrack `.vssession` documents. Creation, edits and mixdown require `--output`
and accept `--dry-run`/`--overwrite`; inspect reports the existing session.
Import takes `--input` with optional track and frame position, and requires
explicit `--resample` when source rates differ. Edits select `--operation` and
track/clip IDs; automation takes comma-separated `frame:value[:linear|step|smooth]` gain or pan
points. Mixdown writes WAV with optional frame bounds and `--wav-format`.
Mixdown and stems accept `--dither none|tpdf` (default none) and an unsigned
64-bit `--dither-seed` when TPDF is enabled; floating output rejects dither.
The seeded noise sequence continues across processing blocks.

`asset audio-session stems SESSION --stem ID|master --output DIR` exports
selected track/bus outputs and an optional master mix. Repeat `--stem` for each
selection; `id:` escapes a literal ID named master. Optional `--tap pre|post`
(default post) chooses before/after strip fader, pan and inserts; master always
includes master processing. `--respect-solo true|false` defaults false, while
mutes always apply. `--start-frame`/`--end-frame` give every file the same range;
the default end includes the session tail. `--name PREFIX` supplies a sanitized
filename prefix. Stable session ordinals distinguish filenames. The existing
folder receives WAVs and a version-1 `delivery.stems.json` manifest. Every
destination is preflighted and existing revisions are guarded under explicit
`--overwrite`; files appearing later are preserved. Each WAV commits atomically,
but the batch can finish partially. The manifest records in-progress, complete,
failed or cancelled state and per-file hashes; JSON error results retain batch
details. A dry run renders every selection and writes no WAV, manifest or lock.
See [stem delivery](AUDIO_EDITOR.md#stem-delivery) for routing/summing semantics.
The command supports text and `--json` diagnostics. `recover` reads a reviewed
`.vssession-recovery` using `--expected-sha256` and writes a separate native
session, preserving the copy and recorded original. `asset audio-recoveries`
lists waveform and session kinds in one budgeted inventory. Exact session
discard needs `--kind session`, the reviewed digest and explicit `--write`;
without `--write` it remains a read-only dry run. `transport SESSION --frames N`
processes up to 16,777,216 frames without device or file output. Optional frame
bounds, `--block-frames` (1–65,536) and `--loop true|false` exercise the shared
sample clock. JSON includes rendered frames, cursor, loops, peaks, over-range
count and the float32 little-endian sample digest. Loops retain DSP history
and wrap sources, latency-corrected automation and future lookahead context;
the sample digest matches continuous session playback. Finite exports remain
a single pass through their requested range.

Mixer edits also support `--operation add-bus --name NAME`, `routing`, `send`
and `remove-send`. Routing takes `--track ID`, optional `--output-bus ID|master|none`,
`--invert-left true|false`, `--invert-right true|false` and
`--swap-channels true|false`. `none` disables the current output while retaining
its destination. `send` creates or updates the selected track's `--target-bus ID|master`
with optional `--db`, `--pan`, `--pre-fader true|false` and `--enabled true|false`;
omitted fields preserve an existing send, with unity/post-fader/enabled defaults
for a new CLI send. `remove-send` requires the track and target. Prefix a literal
bus ID with `id:` to disambiguate IDs named `master` or `none`. Validation rejects
missing destinations, repeated send destinations and cycles, including disabled
edges. Removing a referenced bus fails until its incoming routes are changed.
All these edits retain the normal output, dry-run, overwrite and conflict guards.
Versions 1–6 remain readable; saves write version 7 with routing, effects,
automation domains, tempo/meter maps, clip groups and inherited fade segments.

`asset audio-session arrange SESSION --operation OP --select-clip ID --output NEXT.vssession`
edits a selection transactionally. Repeat `--select-clip` for distinct IDs;
IDs may contain punctuation and are not comma-split. `--linked-groups true|false`
defaults true and expands selected group members across tracks. Operations are:

| Operation | Required values | Behavior |
| --- | --- | --- |
| `move`, `duplicate` | `--offset-frames N` | Signed integer delta; preserve relative spacing/tracks; duplicate groups get new identities. |
| `split` | `--at-frame N` | Split targets crossing the cursor, preserving inherited fade envelopes. |
| `group` | `--name NAME` | Create/merge a group or rename the exact existing membership. |
| `ungroup`, `remove` | None | Release links or remove effective clips; prune empty/singleton groups. |
| `gain` | `--db N` | Set absolute clip gain in −96…+24 dB. |
| `fades` | `--fade-in N --fade-out N` | Set whole-frame fade lengths over each current clip length, resetting inherited segments. |
| `mute` | `--mute true|false` | Set the effective clips' mute state. |

A bad target or value rejects the whole edit. Track/effect automation stays at
its authored frames. JSON returns `selection`, `addedClipIds`, and `groupId`
when grouping. After copy/split, selection contains the new pieces. Native
save/recovery retains groups and original fade segments; WAV rendering uses the
same audible envelope. Existing output/dry-run/overwrite/revision/source guards
apply. `edit --operation region` remains a precise single-clip edit and accepts
`--reset-fades true|false`; changing fade lengths also resets the segment.

`asset audio-session tempo-map SESSION --output NEXT.vssession` replaces the
supplied musical timing fields: `--tempo BPM`, `--meter numerator/denominator`,
`--tempo-points tick:bpm,...` and `--meter-points bar:numerator/denominator,...`.
Omitted fields are preserved; `none` clears either change list. `new` accepts
the same options. Changes must be strictly ordered, after tick 0/bar 1, and
within the session frame limit, with at most 4,096 per list. BPM is 20–400
quarter notes/minute, resolution 960 ticks/quarter; meters use 1–32 beats and a
power-of-two denominator 1–32. The map edit shares undo/recovery/native validation
with the GUI and retains source samples and every clip/automation frame.
Normal dry-run, overwrite, source-protection and digest guards apply.

`asset audio-session position SESSION` is read-only and requires exactly one of
`--at-frame N`, `--at-tick N` or `--at-position bar.beat.tick`. Bars/beats start
at 1; ticks within the notated beat start at 0. JSON `musicalPosition` reports
the resolved frame, tick, position, BPM, meter and resolution. Optional
`--snap bar|beat|half-beat|quarter-beat|beat-triplet` adds the nearest grid frame
and position, plus strictly previous/next grid frames (the current frame at a
timeline boundary with no further grid line). Ties snap later. Low sample rates
can map several ticks to one frame; the returned position is the nearest tick
to that frame. Tempo is stepped; meter changes occur at bar starts. Ramps,
audio time stretching and external clock synchronization are not implemented.

`asset audio-session effect-automation SESSION --track ID|master --effect ID
--parameter KEY` edits a numeric effect lane. Supply `--points
frame:value[:linear|step|smooth],...` (or `none` to clear), `--enabled true|false`,
or both. Omitted curves are linear; an enabled nonempty lane replaces the static
parameter and holds its endpoints. Invalid targets, bounds and duplicate frames
are rejected. The normal required `--output`, `--dry-run` and `--overwrite`
guards apply. Inspect exposes curves and enabled states. Track gain/pan
`automation` also accepts the optional third curve field. Presets contain static
values only; replacing a chain removes its old automation.

`asset audio-session effects SESSION --track ID|master --operation OP --output FILE`
edits an ordered insert chain. Prefix a literal track ID with `id:` when it is
named `master`; an empty literal ID is rejected. Supported operations are:

- `add --type TYPE`: append a processor, or insert at zero-based `--index N`.
  Optional `--parameters key=value,...` and `--enabled true|false` override defaults.
  JSON returns the stable `addedEffectId`.
- `set --effect ID`: update only supplied parameters or enabled state.
- `remove --effect ID`, `move --effect ID --index N`, or `clear`: remove one,
  reorder one, or empty the selected chain.
- `tail --tail-seconds N`: change the global 0…60-second session tail. The
  `--tail-seconds` option also accompanies other effects operations.
- `preset --preset ID` or `preset --preset-file FILE.vsfx`: replace the selected
  chain with a factory or file preset. Choose exactly one input. IDs are renewed;
  values must fit the destination sample rate. The tail becomes the longer of
  the current and preset tails unless `--tail-seconds` explicitly overrides it.
- `save-preset --name NAME --output FILE.vsfx`: save the selected chain, rate and
  tail as a version-1 preset without editing the input session. Optional
  `--tail-seconds` changes only the saved preset's suggestion.

`asset audio-session presets [--sample-rate RATE]` lists nine factory recipes;
JSON also exposes all 18 processor schemas, rate-specific parameter bounds and
the `automatable` flag. `lookahead-limiter` adds ceiling/release/attack controls
and fixed 0…20 ms lookahead. `lookaheadMs` is structural and cannot have an
automation lane, including a disabled lane.
It accepts no session path or output options and opens no device. Preset files
are bounded to 64 KiB, preserve physical parameter values, and reject unknown
types/parameters and incompatible destination rates. Preset publication shares
the atomic output, dry-run, explicit overwrite and source/hard-link guards.

See [Session Effects](AUDIO_EDITOR.md#session-effects) for processor types,
parameter keys, limits, insert placement and fresh-state range behavior. Repeated
parameter keys and unsupported options fail. These edits preserve the normal
dry-run, overwrite, source protection and conflict guards; rejected changes do
not publish a file. Inspect exposes all chains and the global tail. Transport,
mixdown and GUI waveform handoff process the same stateful chains.

Session results expose `processingLatency` separately from serialized session
state: total frames/milliseconds, master input/insert frames, compensation bytes
and per-strip input/insert/output timing plus each output/send delay. The renderer
aligns parallel routes and trims processing latency from the delivered timeline.
Transport and mixdown JSON and individual stem manifest entries include
`processingLatencyFrames`. These are processing frames, not hardware latency.
Explicit ranges prime from their requested start with later session context;
frame counts remain unchanged. For example, add a staged 5 ms limiter with
`effects SESSION --track master --operation add --type lookahead-limiter
--parameters lookaheadMs=5,ceilingDb=-1 --output NEXT.vssession`.

`asset formats [--module <id>]` lists shared format capabilities, available
runtime codecs and export profiles. `asset route <path>` reports the owning
module and recognized format from the filename; the owning editor validates
content when opened. Both inspection commands support `--json`.

```sh
vibestudio --cli workspace inspect ./work.vibeworkspace --json
vibestudio --cli asset formats --module textures --json
vibestudio --cli asset route textures/wall.dds --json
```

`model collision` shares mesh validation/edit history and level placement with
the GUI. Operations inspect, add, fit, fit-animated, animate, freeze, update,
transform, duplicate, delete, export-map and
place have strict applicable options, text/JSON diagnostics and guarded outputs.
Mutations support dry runs; placement writes a separate map and protects source
inputs. Target selection and Quake III shader dependency are explicit. See
[Model Collision CLI](MODEL_COLLISION.md#cli) for the full contract.
Collision transforms use world translation/rotation, box-local scaling and
explicit pivots/snapping, matching viewport and numeric controls.
Animated value updates/transforms require explicit `--frame all|N`; omitted
update fields keep their values in each pose. Inspection accepts a stored frame.
Map export/placement of animated boxes requires `--frame N`, reports the sampled
pose and emits static brushes. Track data remains in source schema 7.

Native `model import`/`model build` share the GUI's face-orientation conversion:
editable/OBJ counter-clockwise faces become clockwise MDL/MD2/MD3 records and
return unchanged in a native round trip. Existing editable files retain their
stored triangles. [Model engine acceptance](MODEL_ENGINE_ACCEPTANCE.md) documents
older-source review and the optional CLI/compiler/dedicated-server proof.

Package temporary-copy preparation in the GUI shares `extractPackageEntries`
with `package extract`, including streamed verification and safe output paths.
Drag selections retain exact reader indexes; CLI `--index` and `--as` provide
explicit occurrence selection and output mapping. Temporary GUI batches apply
their own admission limits and expose no paths after partial failure; ordinary
CLI extraction retains its documented per-file completion report.


`editor scene list|select|translate|create|rename|move|assign|move-to-layer|visibility|lock|remove|reset|save|create-linked-duplicate|select-linked-groups|update-linked-groups|separate-linked-groups|extract-linked-groups <map>` shares
the map document's scene, validation and serialization services. Mutation output
is explicit (`--output`), supports `--dry-run`, and requires `--overwrite` for an
existing destination. WAD operations require `--map-name`. JSON includes stable
node UUIDs, memberships, visibility, local/inherited locks, links with each linked
group's affine `frame` and `outOfSync` mark, and preserved-metadata diagnostics.
`lock --id <UUID> --locked true|false` changes protection; ordinary map edits use
the same lock boundary and refuse output publication on a protected edit.
Linked groups take TrenchBroom's command names.
`create-linked-duplicate --id <UUID> [--offset x,y,z]` duplicates a group and links
the two, reporting `createdId`; `select-linked-groups --id <UUID>` reports the
selection of its whole link set; `update-linked-groups --id <UUID>` makes the
other unlocked members match it and reports `updated`; `separate-linked-groups
--id <UUID,UUID...>` takes groups out of their sets, keeping several chosen from
one set linked with each other, and reports `separated`. The earlier names
`link`, `update-links`, `unlink` and `extract-linked` remain accepted. Ordinary
`map` edits keep linked groups in step through the same service as the studio,
and an edit whose linked updates could not be made reports `linkedGroupNotices`;
saving warns about groups still out of sync. Read-only `select --objects <refs>`
expands closed groups, with optional `--selection` and `--mode replace|add|toggle`.
`select --expand-layers` then adds the visible, unlocked objects of the picked
objects' layers, reporting `layers` and `skippedGroups`, through the service the
studio's **Expand Selection to Layers** uses. `move-to-layer --id <layer UUID|default>
--objects <refs>` is **Move to Layer**: closed groups and brush entities move whole
in one scene undo step, and locks or a group id refuse it before any output.
`translate --objects <refs> --offset x,y,z` applies those units with texture lock.
Both accept a per-invocation `--context <group UUID>`; selection ignores outside
hits and translation refuses them atomically. Context is not written to the map.
Other low-level `map` commands retain explicit primitive targeting. See
[Level scene organization](LEVEL_SCENE.md#cli) for operation options.

`package groups <wad-or-draft>` inspects semantic WAD groups and emits a schema-1
`inventory` with IDs, planned ranges, blockers and a content/plan fingerprint.
New source-free WAD drafts support the same group edits immediately. WAD saves
and subsets retain the reviewed planned order, including namespace anchors; new
binary/GL runs are assembled before inspection and ambiguous ownership is blocked.
Staging routes accept repeated `--rename-group <id>` / `--group-to <label>` or
`--delete-group <id>`, with one `--groups-fingerprint <fingerprint>`. Groups cannot
overlap or mix with individual entry/folder edits. Maps include matching GL
companions, namespace deletion retains whole regions, and local texture tables
form a unit. GUI and CLI share atomic validation, undo and draft replay. See
[Edit WAD Groups](PACKAGE_MANAGER.md#edit-wad-groups) for limits and exit codes.

`map materials <wad> --map-name MAP01 --package <resources> --geometry --json`
resolves Doom/Hexen flats and composite wall textures and inspects the shared
camera geometry. JSON adds `geometry` counts, `truncated`, `cancelled` and
warnings; geometry omissions or truncation return validation failure. Material
`inputs` include snapshot entry indexes, directory occurrences, namespaces,
roles, paths and layers. Plain `map materials` retains its material-only scope.
`map dependencies` reuses these exact Doom inputs and reports
`exportSupported: false`: a portable subset still needs texture-table and WAD
namespace closure. Full package saves are unaffected. See
[Doom Camera Editing](LEVEL_EDITOR.md#doom-camera-editing).

`map copy-surface <map> --target face:brushId:faceNumber --output <surface.json>`
exports a version 1 material/mapping/flag definition. `map paste-surface <map>
--clipboard <surface.json> --target face:brushId:faceNumber|brush:brushId
--output <map>` applies it through the shared atomic surface service. Paste
targets repeat; face numbers are one-based. `--mode parameters` requires matching
mapping families; `--mode project` preserves world projection and `--mode seamless`
wraps UVs around the planes' intersection. Both accept
`--texture-size W,H` for unit conversion plus explicit `--allow-valve220` when
needed. That permission covers consistent map-wide conversion while preserving
unpasted surfaces and respecting their locks. JSON includes `convertedFaces`. Both support dry-run, overwrite and JSON; map paste uses normal source
protection and backups. See [Surface clipboard](LEVEL_EDITOR.md#surface-clipboard)
for bounds, format rules and GUI integration.

`--mode radiant-values` retains Valve axes while transferring native values.
Both parameter modes also accept `--target patch:id` (material only) and repeated
`--object brush:id|patch:id|entity:id` to include a selected set. Selected brush
flags stay unchanged except on explicit hits. `--mapping-only` keeps each target's
material and flags in all modes. Matrix transfers require source and target image
sizes: use source `--texture-size W,H` and repeated `--material-size material=W,H`
for retained targets. The latter also supplies a source size when no override is
given. JSON includes `changedPatches`, `mappingOnly` and `selectedObjects`.
`--mode radiant-project` accepts the same selected set, copies classic/Valve
parameters and projects primitive matrices and patch UVs. It requires matching
brush families, normalizes matrix shifts by whole repeats and reports permitted
edge-on brush mappings as `edgeOnFaces`. Patch geometry and subdivisions remain
unchanged; mapping-only preserves materials. This mode rejects `--allow-valve220`.

`--stroke` treats individual face/patch targets as an ordered traversal, up to
4,096 hits; brush-wide targets are rejected. Selection applies only on the first
hit. Seamless steps advance the source plane and use destination `--material-size`
entries for subsequent source dimensions. The original `--texture-size` applies
only before the first wrap. The complete traversal uses one transaction; a bad
later hit publishes no output. JSON adds `strokeHits`, `sourceAdvanced` and the
portable `finalSource` definition. Input definitions stay unchanged. Omitting
`--stroke` preserves independent batch paste from the original clipboard.
The CLI uses one mode/policy for the traversal; mixed-mode stroke-file replay
remains open, while the core transaction supports the camera's modifier changes.

`map paint-material <map>` and `map sample-material <map>` share the level
camera's atomic material service. Paint takes repeated `--target`, required
`--texture` and `--output`, plus `--dry-run`/`--overwrite`. Sample takes one
target without write options. WADs require `--map-name`. Face selectors use
one-based face numbers; patches, sidedef parts and sector flats are supported.
See [Material Painting](LEVEL_EDITOR.md#material-painting) for syntax, JSON and exits.

`editor bookmarks list|import|export|rename|remove <map>` manages saved level views
through the GUI's validated storage service. Import uses `--input` and requires
`--replace` for a nonempty list; export uses `--output` and explicit `--overwrite`.
Rename/remove take `--id`; rename also requires `--name`. WADs require `--map-name`.
The CLI never changes map bytes or an open GUI camera. See
[Saved Level Views](LEVEL_EDITOR.md#saved-level-views) for schema, limits and errors.

`editor layout [profile|single-2d|single-3d|camera-and-plan|four-views|camera-above-plans|camera-beside-plans]` reads or
sets the level editor layout independently of its controls. JSON includes
`preference`, `effectiveLayout` and `profileId`; omitted preference is read-only.
`--panes top,front,side` (any non-empty subset, each once, or `all`) saves the
plan panes the three-plan layouts show, the GUI's **Show Top/Front/Side View**
choice; JSON adds `planPanes` (shown pane ids in pane order) and
`planPanesApply` (whether the effective layout uses them). The layout id and
pane list are both validated before either preference is written.
Unknown/duplicate options, unknown IDs and extra positionals return 2, settings
write failures return 1, and successful reads/writes return 0. Standard settings
and locale overrides apply. See [Four-View Workspace](LEVEL_EDITOR.md#four-view-workspace).

The GUI's temporary Maximize/Restore Active View does not change this persisted
preference or saved-view metadata. `editor controls <profile> --json` reports
its profile keys and the Equalize View Sizes binding where assigned. The CLI
does not manipulate the panes of a running GUI process.

`editor clipper` reads or updates the same `levels/clipper` preferences as
**Clipper Options…**. Repeat `--set field=value` for material
(`inherit|current|custom`), `customTexture`, `resetPoints`, `resetSide`,
`twoPointsInPlan`, `wrapMerge` (`on|off`) and `doubleClick` (`none|clip|split`). `--reset`
restores defaults; `--dry-run` previews a change without writing settings.
All fields validate before persistence. `map clip --cut-texture <name>` uses
the shared cut service with an explicit face material; omission inherits the
brush majority independently of GUI settings. JSON includes `cutTexture`.
`map clip --wrap-merge --wrap-texture <name>` accepts one to three actual
`--points`; two points use `--view-axis x|y|z` (default z). It explicitly permits
NRC-style convex wrap on a one-point/degenerate plane or a no-change cut.
Geometry or scene failures never trigger fallback. JSON names `operation` and
reports `changed`, `inheritedFaces`, `newFaces` and `wrapTexture`; `clipped` is
zero for a wrap. Axis/position planes cannot request wrapping.
Two-point cuts also work without wrapping when `--view-axis x|y|z` is explicit;
`--view-sign 1|-1` selects the extrusion direction (default `1`). Alternatively,
`--camera-eye x,y,z --camera-forward x,y,z` calls the shared NRC dominant-view,
parallel-segment and outer-face plane service using the selected bounds. Both
camera options are required, the forward vector must be non-zero, and camera
options cannot mix with explicit axis/sign. Only two actual points accept these
options. Two-point JSON adds `planeAxis`, `planeDirection` and `cameraPlane` for
exact replay; malformed, non-finite or degenerate camera inputs never save.
`editor gestures netradiant-custom --set camera.clipViewAxis=off` changes the
two-point plane policy; `--set camera.clipDragStyle=surface|world|view` independently
chooses the camera point-movement policy. Both use the normal typed per-profile
settings, dry-run and portable import/export. Numeric `map clip --points`
reproduces the final freely moved points through the same geometry service.
`map wrap-merge <map> --texture <name> --output <map>` invokes standalone
wrapping. Repeat `--object brush:ID|entity:ID` for whole-object replacement or
`--set brush:ID/vertex|edge|face/IDs` / `patch:ID/vertex/IDs` to retain sources
and add a component hull. The modes cannot be mixed. Up to three repeated
`--point x,y,z` options add actual points. The last selected object supplies
the destination entity and scene group; all new component-hull faces use the
explicit material, default UVs and zero flags. JSON reports `sourcesRetained`,
`brushId`, `sourceBrushes`, `sourcePatches`, `vertices`, `faces`, `inheritedFaces`
and `newFaces`. Strict parsing, `--dry-run`, overwrite policy, scene protection
and linked propagation share the normal map services.
See [clipping](manual/levels.md#clip-brushes-across-views) for cut flags,
undo and cancellation rules.

`editor view-links` reads the GUI's navigation defaults. Optional `--centers`,
`--zoom` and `--follow-camera` accept `on|off` and retain omitted values. JSON
returns boolean fields in `links`: `centers`, `zoom`, `followCamera`. Parsing
validates all arguments before writing; invalid/duplicate/unknown options and
extra arguments return 2, failed writes return 1, and success returns 0. Read-only
settings still allow inspection. See [Linked Navigation](LEVEL_EDITOR.md#linked-navigation).

`map export-prefab`, `map inspect-prefab`, and `map insert-prefab` share Levels'
versioned prefab service, target-link remapping, texture lock and persistence.
Complete closed groups retain nested hierarchy through export and insertion.
Grouped assets use version 2; flat version 1 assets remain supported. Inspection
JSON includes `groups` and `linkedGroups`. Portable link sets receive fresh
identities, and a lone linked group becomes independent. Linked groups in a prefab
turn and move their frames with its placement, at any rotation. Native clipboard
metadata is validated before insertion. `map duplicate` and its arrays preserve
complete group hierarchies beside their source, keeping existing link sets;
partial selections retain ordinary object-copy membership.
Export accepts repeated `--object`, a required `--name`, optional `--description`
and `--anchor x,y,z`. Insert requires `--prefab` and either `--position x,y,z`
or repeatable `--replace kind:id`: the objects the prefab takes the place of,
removed in the same undo step with its anchor on their bottom centre (JSON lists
them as `replaced`). Optional `--rotation x,y,z`, `--texture-lock on|off` and
`--target-prefix` apply to both.
`--entry` reads from a package, and `--package` supplies dependency assets.
Writes require `--output`; `--dry-run` creates no files and overwrites are explicit.
Unknown/duplicate options and extra positional arguments fail. JSON contains the
`prefab` report and save result, including backup/recovery information. See
[Reusable Prefabs](LEVEL_EDITOR.md#reusable-prefabs) for defaults, limits and examples.

`map add-brush` shares the GUI primitive builder: `--shape box|wedge|cylinder|cone|sphere`,
`--axis x|y|z`, radial `--sides`, and sphere `--bands`, with the existing required
bounds, material and output path. Geometry is validated in the destination map's
current face dialect before one undoable insertion. See
[Brush Primitives](LEVEL_EDITOR.md#brush-primitives) for detail limits and JSON fields.

- `package recoveries [--directory <store>]` lists bounded local checkpoint
  metadata and logical storage totals in `inventory`, with configured `limits`;
  payloads are verified only during restore.
- `package draft-recover <id> --expected-sha256 <manifest-hash> --output
  <new.vibepackage>` verifies a selected checkpoint and restores its complete
  content/history to a new draft. `--dry-run` creates no files or locks.
- `package recovery-discard <id> --expected-sha256 <manifest-hash>` previews
  removal; alternatively, `--expected-storage-sha256 <storage-hash>` also supports
  incomplete copies with no manifest. The two checksums are mutually exclusive.
  `--write` commits after session, path and checksum checks. Both
  modifying routes also accept `--directory`; existing outputs are protected.
  Exit codes: 0 success, 2 usage, 4 verification/operation failure. Listing also
  returns 4 for a damaged or truncated inventory. See
  [Automatic Recovery](PACKAGE_MANAGER.md#automatic-recovery) for limits and JSON.
- `package draft-storage <draft.vibepackage>` fully verifies history and payloads,
  then returns retained/unused storage, review checksums and configured limits.
  `package draft-compact <draft.vibepackage> --expected-storage-sha256 <hash>`
  previews reclamation. `--write` excludes participating document/worker readers
  before removing only reviewed unused files. Default dry run creates no locks and
  reports `readerExclusion: false`. JSON retains partial removal counts on failure.
  Both reject unknown/repeated options and extra arguments; compact rejects
  `--write --dry-run`. Exit 0 succeeds, 2 means usage error, 4 means review or
  maintenance failure. Draft saves use `packages/draftMaximumMiB` (32,768) and
  `packages/draftMaximumFiles` (200,000), including unused objects and peak metadata
  space; quota failures preserve the prior commit. See
  [Saved draft storage](PACKAGE_MANAGER.md#saved-draft-storage).
- `package groups <source.wad|draft.vibepackage> --json` inspects Doom WAD map,
  GL, namespace and texture-table groups, returning snapshot group IDs and a review
  fingerprint without changing the package.
- `package copy-limits` inspects the per-window temporary-copy policy. Optional
  `--max-mib`, `--max-files`, `--max-entries` and `--max-batches` propose changes;
  `--write` saves them, while inspection and default/explicit `--dry-run` leave
  settings and storage untouched. Unknown/repeated/range-invalid options fail
  with exit 2; settings write failures use exit 1. JSON distinguishes proposed
  and stored limits, states its window scope, and reports live usage unavailable.
  GUI reservations share the same settings and preserve existing copies when
  limits are lowered. See [Temporary copies](PACKAGE_MANAGER.md#temporary-copies-and-drag-out).
- `package copy-store-limits [--directory <store>]` inspects shared initial
  reservations across windows/processes using that physical store. The four
  `--max-*` options propose limits; only `--write` commits `limits.json` without
  creating/migrating preferences. Default/explicit `--dry-run` creates no files.
  Optional `--expected-policy-sha256` binds a proposed change to an earlier review;
  stale policy writes fail. Schema 1 returns scope `managed-copy-store`, proposed
  and stored limits, `dryRun`, `updated` and a complete/partial `quota` including
  reserved resources, accounted sessions, policy checksum and errors. Strict
  option errors use exit 2; incomplete inspection, stale review or write failure
  uses exit 1. Valid policy edits can succeed with incomplete legacy accounting,
  which still blocks new copies. See [Shared copy storage limits](PACKAGE_MANAGER.md#shared-copy-storage-limits).
- `package copy-sessions [--directory <store>]` inspects managed temporary-copy
  sessions without creating files or preferences. JSON schema 1 retains partial
  inventory and errors with `complete: false`. Actual payload usage is a
  snapshot; the separate `quota` reports shared initial reservations and its own
  completeness. Sessions include `reservationKnown` and `reserved` counters.
- `package copy-discard <id> --expected-storage-sha256 <hash> [--directory <store>]`
  defaults to dry run; `--write` removes only the unchanged reviewed session.
  Both modes require native exclusion of live owners and create no lock files.
  Tokens cover storage metadata and session-record bytes, not payload hashes.
  Cancellation/failure can leave partially removed files, requiring refresh.
  Unknown/repeated/conflicting/malformed arguments use exit 2; review, ownership
  or cleanup failures use exit 1; successful review/discard uses exit 0.
  See [Retained copy sessions](PACKAGE_MANAGER.md#retained-copy-sessions).
- `package working-imports [--directory <store>]` returns bounded working-session
  `inventory` and configured `limits` without writing. It reports actual and reserved
  payload bytes/file counts, session leases and `storageSha256` review tokens.
  The separate `locks` array exposes recognized lock paths, bounded metadata
  review errors and native-identity/content `lockSha256` tokens.
- `package working-unlock <relative-lock-path> --expected-lock-sha256 <hash>
  [--directory <store>]` defaults to dry run; `--write` releases the unchanged
  reviewed lock only after native owner exclusion. Both modes check exclusion,
  create no locks and report `ownerExclusionChecked`, `dryRun` and `released`.
  Recognized store/session lock and stale-removal-guard names only; payloads
  remain for separate review. Exit codes are 0 success, 2 usage, 4 refusal.
- `package working-discard <id> --expected-storage-sha256 <hash> [--directory <store>]`
  defaults to dry-run review. `--write` proves the session lease is idle and removes
  only the reviewed files. JSON includes `dryRun`, `discarded` and `leaseChecked`;
  dry runs create no locks and leave `leaseChecked: false`. Unknown/repeated options
  and combined `--write --dry-run` fail. Working-store routes use exit codes 0 success,
  2 usage, 4 incomplete inventory or operation failure. The shared preferences
  `packages/importMaximumMiB` / `packages/importMaximumFiles` default to
  8,192 MiB / 50,000 files. See [Working import storage](PACKAGE_MANAGER.md#working-import-storage).
- File imports for actual create/draft-save/save-as commands retain independent
  working copies. `stage`, `manifest`, staged comparison and `--dry-run` use
  verified source references and need no writable temporary import storage.
  Staging file-operation JSON includes `contentStorage` (`owned-temporary` or
  `verified-file`) while preserving original source paths and logical timestamps.
- `package create <output>` creates an archive or `.vibepackage` document without
  a source. Filename inference supports PAK/ZIP/PK3/WAD; draft `--format` defaults
  to PK3. `--wad-magic PWAD|IWAD|WAD2|WAD3` chooses a new WAD variant. Existing
  outputs require `--overwrite`; `--dry-run` writes nothing.
- Every staging route accepts `--mkdir <path>`, paired `--rename-folder <path>` /
  `--folder-to <unused-path>`, and `--delete-folder <path>`. Folder edits are
  atomic and preserve descendant identity through grouped undo and draft replay.
  WAD remains flat; PAK export refuses explicit empty folders. See
  [Package Manager](PACKAGE_MANAGER.md#new-packages-and-folders) for ordering,
  collision rules, format limits and JSON results.
- `package draft-save <source-or-draft> <output.vibepackage>` persists base
  payloads, staging and undo/redo history. Staging options form one edit group;
  replacing a draft requires `--overwrite`. `--dry-run` checks all reachable
  payloads, redo history, metadata and destination constraints without creating
  directories, objects or lock files; JSON reports `saved: false` and `dryRun: true`.
  A repaired plan may preserve an unreadable deleted/replaced original as an
  explicit version-3 unavailable-history record. Current content and existing
  verified objects remain strict. Undo restores its metadata with export blocked;
  Redo restores the repair. Manifest rows expose `contentAvailable: false`, null
  `sha256` and `unavailableReason`; recovery inventory adds `unavailableBaseCount`.
  Complete drafts still use version 2. See [Drafts and edit history](PACKAGE_MANAGER.md#drafts-and-edit-history).
- `package draft-info <draft.vibepackage>` verifies and reports a draft without
  writing it. `package draft-undo` and `package draft-redo` explicitly change and
  atomically save its history position. They reject unknown/empty options and
  extra arguments. JSON exposes revision, undo/redo availability, operation/file
  counts, and export readiness. Exit codes are 0 success, 2 usage, 1 load/save
  failure, and 4 unavailable undo/redo. A valid draft may contain staging
  conflicts; export readiness is reported separately from persistence success.
- `package stage`, `package manifest` and `package save-as` accept a draft source
  through the shared staging service. Move the whole `.vibepackage` directory,
  including `document.json` and `objects/`. See [Package Manager](PACKAGE_MANAGER.md)
  for storage limits and lifecycle boundaries.
- `package info`, `list`, `preview`, `extract` and `validate` accept saved drafts
  through the shared planned-view reader. Added and renamed paths are present;
  deleted paths are absent. Unfiltered `list --json` adds `entryIndex` for the
  current snapshot and `sourceOrdinal` for source occurrences (-1 for new entries
  without a source occurrence). Ambiguous path previews fail; extraction refuses blocked plans.
- `package preview <path> --entry-index N` reads one exact snapshot row.
  Preview sampling shares the GUI's streaming service: full samples require
  final size/checksum success, while truncated samples do not validate the
  unread tail. Use `package validate` for complete integrity verification.
  `package extract` accepts repeated `--entry-index N` and optional paired
  `--as <relative-path>` overrides. Indexes come from the current `list --json`;
  supply one override per index or none. Mapped names pass the same namespace,
  traversal, source-protection and atomic-publication checks as path selectors.
- Edits use `--replace-ordinal N --replace-file <file>`, `--rename-ordinal N
  --to <path>`, or `--delete-ordinal N`. These zero-based source ordinals persist
  through other edits; each operation also verifies its expected path on replay.
  Replacement/rename groups cannot mix path and ordinal selectors. Draft version
  2 retains occurrence identity; version 1 remains readable.

- `project files <folder> --where "kind=image" --json` lists source, media,
  maps, packages and studio documents through the shared Go to File catalog.
- `asset audio-export <input> --preset wav|doom|quake|quake2|quake3 --output <path>`
  delivers a separate WAV or Doom DMX sound. It supports final dither, source
  protection, `--dry-run` and `--json`; see the Audio editor documentation for
  preset details.
- `asset audio-generate --prompt <description> --game doom|quake|quake2|quake3|generic
  --output <project>` makes sound effects: the synthesizer by default (no AI;
  the same sound for the same description and `--seed`), or `--source ai` for
  the configured sound model (ElevenLabs, or a custom endpoint). `--kind`
  chooses one of fifteen kinds, `--duration` the length in seconds, `--loop
  on|off` a seamless loop (ambience and alarms loop by default), `--variants`
  up to four, `--name` and `--folder` where Quake-family sounds go under
  `sound/`, `--wad` the PWAD for Doom lumps, and `--influence` how closely the
  model follows the description. `--preview <wav>` writes the working sound
  to listen to first; `--overwrite` replaces an existing file or lump. AI
  rules follow `ai ask`: `--dry-run`, `--yes`, `--provider`, `--model`,
  `--endpoint`, `--timeout-ms`.

`map repeat <map> --recipe <JSON> --object kind:id --output <map>` replays the
recorded object sequence through the same atomic service as the GUI. Repeat
`--object` for several targets, optionally open `--context <group UUID>`, and use
the normal `--texture-lock on|off`, `--allow-valve220`, `--map-name`, `--dry-run`
and `--overwrite` policies. **Copy Repeat Recipe** and the JSON map result's
`repeatRecipe` export version 1 actions. Recipes are limited to 4 MiB and reject
unknown fields, non-finite or singular transforms and inconsistent rotation
metadata. Session repeat history is not saved in maps. See [Repeat](LEVEL_REPEAT.md).

`map sweep` uses the shared selected-face sweep service with repeated
`--set brush:ID/face/IDs`, a material and explicit output. The
[sweep manual](manual/levels.md#sweep-selected-faces) documents path, taper,
section and viewport controls. Its existing validation and save safeguards also
apply to CLI-generated sweeps.

`map rotate --degrees <angle>` uses the GUI's shared atomic rotation service.
It accepts repeatable `--object`, `--axis x|y|z`, optional `--pivot x,y,z`,
`--texture-lock on|off` and explicit `--allow-valve220`. Required map-wide
dialect conversion is authorized through that option. `--turns` uses the same
texture-lock policy; it cannot accompany `--degrees`. Save As, dry-run, overwrite,
JSON and quiet contracts remain available. See [rotation limits](LEVEL_EDITOR.md#numeric-rotation-and-texture-lock).
`map rotate --euler x,y,z` turns about world X, then Y, then Z in one atomic
step (NetRadiant Custom's Arbitrary Rotation, the GUI's modeless **Rotate
Selection…**) and cannot accompany `--axis`, `--degrees` or `--turns`; JSON
reports `eulerXyz`. `map resize --factors x,y,z` scales about the centre or
`--pivot x,y,z` through `scaleLevelMapSelection` (NetRadiant Custom's
Arbitrary Scale, the GUI's modeless **Scale Selection…**); negative factors
mirror with winding repair, and JSON reports `factors` and `mirrored`.
`map rotate --pivot-grid <units>` turns about the selection's centre rounded to
that grid, the quick-turn pivot of the NetRadiant Custom and TrenchBroom
profiles (`core/level_quick_transform.h`); it cannot accompany `--pivot`, and
JSON reports the resolved `pivot` and `pivotGrid`. The GUI's world-axis
**Rotate X**, **Rotate Y** and **Rotate Z** are `--axis x --turns -1`,
`--axis y --turns 1` and `--axis z --turns -1`; **Flip X/Y/Z** are
`map flip --axis x|y|z`. `editor controls` reports each profile's
`quickTurnPivot` (`grid` or `centre`), and `editor gestures <profile> --set
editing.quickTurnSnapsPivot=on|off` changes it.

`map move`, `map flip` and `map resize` also accept `--texture-lock on|off` and
`--allow-valve220`. Move/flip default to locked; resize defaults to retained
source parameters. Locked resizing stretches brush UVs with the geometry.
Required dialect conversion, geometry and texture changes share one atomic edit.
JSON reports `textureLockPolicy` and `allowValve220`; a repeated or invalid lock
value is a usage error. These options concern Quake-family brush faces, not Doom
wall offsets. See [transform controls](LEVEL_EDITOR.md#transform-texture-controls).

Existing UDMF maps also use `map move`, `rotate`, `flip`, `snap` and `resize`.
They preserve fractional XY coordinates and untouched TEXTMAP bytes. Thing moves
support fractional height; `snap --grid` accepts fractional XY steps. Sector
heights use `map edit-udmf`. X/Y mirrors retain native sides and require complete
connected geometry, optionally expanded by `--connected`. Geometry edits require
rebuilt nodes; ordinary-thing transforms retain valid nodes. See
[UDMF transforms](LEVEL_EDITOR.md#udmf-native-transforms).


- `map new --game quake3 --preset room --output <map>` creates a new level.
  `--game` also accepts quake, quake2, doom and hexen; `--preset empty` omits
  starter geometry. `--map` selects the Doom marker; `--texture`,
  `--floor-texture`, and `--ceiling-texture` set asset names.
- `map export-prefab <map> --object <selector> --name <name> --output <file.vprefab>`
  captures selected map objects and their ownership with an anchor; supports
  `--dry-run` and JSON reporting.
- `map inspect-prefab <file-or-package> [--entry <path>] [--package <assets>]`
  inspects prefab content and optional asset dependencies.
- `map insert-prefab <map> --prefab <file> --position x,y,z --output <map>`
  places prefab content with optional `--rotation x,y,z` and
  `--texture-lock on|off`, remapping internal target links; `--replace kind:id`
  instead of `--position` puts it where those objects stand and removes them.
- `map recoveries <directory>` lists recovery checkpoint metadata.
- `map recover <checkpoint.vsrecovery> --output <map>` verifies and restores a
  snapshot. Map creation and recovery support `--dry-run`, `--overwrite`, and
  `--json`; normal saves share source-conflict checks, atomic output and backup
  behavior with Levels. See [Level Editor](LEVEL_EDITOR.md) for recovery
  storage, source fingerprints, and limits.


`texture create` and `texture edit` provide the raster authoring automation
surface. Create accepts `--size`, `--color`, and optional `--operations`; edit
accepts an input image or `--package` / `--entry` and a required operations JSON
file. Both accept `--output <file.png|file.vtexture>`, `--palette`, `--dry-run`, `--overwrite`,
and `--json`. The shared texture document validates an ordered recipe before
any output is committed. See [Texture Editor](TEXTURE_EDITOR.md) for the recipe
schema, limits, provenance, and downstream package/map handoff.
Recipes include brush shape and alpha mode, inclusive line/rectangle/ellipse
endpoints, seed-relative fill tolerance, edge wrapping and cyclic offsets. They
use the same pixel rules and cancellation checkpoints as the authoring canvas.
`canvas-size` pads/crops all layers at an anchor or explicit offset without
resampling. `resize-selection` and `rotate-selection` transform the active
layer's selected pixels at one of nine anchors, refusing out-of-canvas results.
The existing `resize` and `rotate` operations affect the entire canvas.
`texture inspect <file.vtexture>` validates a layered project and reports layers, metadata and a
source fingerprint. Native writes preserve layers and palette metadata and
check conflicts when updating an opened project. PNG exports are composites.
`texture profiles` lists nine output profiles and versioned defaults.
`texture validate <input>` encodes without writes; `texture export <input> --output <path>`
shares guarded publication, dry runs and explicit overwrites. `--profile` and
`--export-options <json>` select native metadata, alpha, dithering and mip rules.
Inputs can be projects, loose images or `--package`/`--entry`. Saved settings and
palette provenance survive exports. `--palette-file` or `--palette-root` supplies
actual colors; generated fallback colors require `--allow-generated-palette`.
Reports distinguish encoding validation from `written`, with hashes, changed-pixel
counts, mip dimensions, palette source and engine warnings. See the complete
[texture profile contract](TEXTURE_EDITOR.md#export-profiles-and-previews).

`texture stage <input> --target-package <archive|folder|draft.vibepackage>
--target-entry <path|lump> --output <draft.vibepackage>` runs the same encoders
and package staging service. WAD2 miptexture types and Doom flat/patch namespaces
survive grouped undo and draft reopening. `--replace-texture` approves replacing
an entry; `--overwrite` separately approves replacing the output draft. A target
package palette takes precedence unless a palette file/root was supplied.
Dry runs write nothing. JSON distinguishes the draft's `written` status from
`packageWritten: false` and includes the planned operation manifest. Publish
through the normal package writer after review.

Texture-to-map handoff uses `map apply-texture` with explicit selectors. For
Quake II WAL and Quake III PNG/TGA paths, stage under `textures/` with the
profile's lower-case extension and omit that prefix/extension in the map token.
Quake WAD2 miptextures use their matching lump/embedded name, with the saved
WAD configured in the map/compiler search path. Restaging pixels under a name
already used by the map needs no additional map edit. The GUI's Stage and Apply
validates these same format/path combinations and commits both in-memory edits
only after both succeed; disk publication remains explicit for each document.

`texture recoveries <directory>` lists bounded local checkpoints with envelope
validity and individual errors. `texture recover <checkpoint> --output
<new.vtexture>` restores layers and metadata to a new project; `--dry-run` is
write-free and `--overwrite` is refused. Both accept `--json`, share the GUI's
recovery service, and never follow source paths stored inside checkpoints.

The VibeStudio CLI is a first-class interface, not a debugging afterthought.
Every workflow that can reasonably run headless should have a stable command
surface using the same core services as the GUI.

`code text-info <file>` reports encoding, BOM, line-ending counts, editability,
and SHA-256. `code text-save <file> --input <edited-file>` previews a save using
the target's format. Writing requires `--write --expected-sha256 <source-hash>`;
`--dry-run` wins, and stale hashes block writes. Both support `--json` and share
the Code editor's bounded text-document service. See [Code Editor](CODE_EDITOR.md).

`code text-create <file>` previews an empty UTF-8 file or imports `--input`.
`code text-save-as <source> --output <file>` previews a format-preserving copy,
optionally with edited `--input`. `code recoveries --directory <folder>` inspects
local text checkpoints; `code text-recover <checkpoint> --output <file>` previews
exporting one. Writes need `--write`; replacing an existing destination also
needs its `--expected-sha256` from the preview. These operations share Code's
destination checks and retain the original source or recovery copy.

## Goals
- [x] Use the same project, package, compiler, validation, and task services as the GUI.
- [x] Provide human-readable output by default.
- [x] Provide JSON output for automation.
- [x] Use stable exit codes.
- [x] Support reproducible command manifests.
- [x] Support CI, release, and batch workflows.
- [x] Keep help text aligned with README and generated user docs.

## Command Architecture
- [x] Lightweight in-process router for early subcommand families.
- [x] Global `--json` output mode for router-backed project, package,
  installation, asset, map, shader, sprite, code, extension, compiler,
  localization, accessibility, diagnostics, AI, and exit-code commands.
- [x] Stable exit-code contract exposed through `--exit-codes` and
  `cli exit-codes`.
- [x] Schema-versioned command manifest writer for compiler command plans.
- [x] Schema-versioned command manifest loader and re-run path for compiler runs.
- [x] Shared Advanced Studio command services for shader scripts, sprite plans,
  code indexing, extension manifests, and reviewable AI creation proposals.
- [x] Evaluate CLI11 and adopt an in-process command registry that exposes
  testable command metadata through `cli commands`; keep the external CLI11
  parser dependency deferred until generated help, validation, and shell
  completion justify it.

## Command Families

### About And Credits
- [x] `--about`
- [x] `--credits`
- [x] `about`
- [x] `about show`
- [x] `credits validate`

### Project
- [x] `--project-init <path>` scaffold project manifest creation/update.
- [x] `--project-info <path>` scaffold manifest and health summary output.
- [x] `--project-validate <path>` scaffold project health validation.
- [ ] `project create`
- [x] `project init`
- [x] `project info`
- [x] `project files <project-root> [--where <query>] [--max-files <count>]`: bounded
  Go to File discovery of source and media candidates. Metadata includes `kind`;
  `--json` returns `projectFiles`, and partial catalogs return exit 4.
  See [Code Editor](CODE_EDITOR.md#go-to-file-across-the-studio).
- [x] `project validate`
- [ ] `project set-install`
- [ ] `project list-mounts`

### Installations
- [x] `--installations-report` scaffold report for saved manual installation profiles.
- [x] `--add-installation <root>` scaffold manual profile creation.
- [x] `--select-installation <id>` scaffold selected-profile persistence.
- [x] `--validate-installation <id>` scaffold read-only validation.
- [x] `--remove-installation <id>` scaffold profile removal without touching files.
- [x] `--detect-installations` read-only Steam/GOG candidate detection.
- [x] `install list`
- [x] `install detect`
- [x] `install add`
- [x] `install validate`
- [x] `install select`
- [x] `install remove`
- [x] `install register build <installation> [--package <path>]... [--dry-run]`: reads the
  installation's stock packages (or the given packages and folders) into the game asset index and
  saves it as `asset-registers/<id>.json` beside the settings. JSON: `register` (summary),
  `path`, `written`, `bytesRead`, `warnings`. Exit 4 when a package cannot be read.
- [x] `install register info <installation>`: the index's sources, counts and freshness under
  `status`; exit 4 when it is missing or out of date, listing each `staleReasons` entry.
- [x] `install register check <installation> <game-path>...`: whether each path is a stock file or a
  shader the game declares, with its package; `--file <local> --as <game-path>` compares a local
  file and reports `match` as `identical`, `different` (the file would replace the game's) or `none`.
- [x] `install register export <installation> --output <file>`: writes the index to a file for
  `release plan --register` on a machine without the game.
  See [Project releases](PROJECT_RELEASES.md#the-game-asset-index).

### Packages

All commands that open an on-disk archive or folder use the shared index limits:
250,000 records including skipped records and implied folders, 128 path components,
64 MiB of logical index metadata and 64 MiB of source chunk fingerprints. `info`,
`list` and `validate` return exit 1 with a diagnostic when admission fails; JSON
contains no partial listing. There is no CLI override. Narrow the source instead.
Archive writes also enforce this opening policy, including implied folders,
encoded directories, ZIP64 metadata and output fingerprint chunks. An over-limit
output in `create`, `save-as` or subset export returns its existing write-failure
exit 4 with `outputCommitted: false`, preserving output, backup and draft history.
Staging options that exceed plan/view admission fail earlier with the existing
invalid-plan/argument exit 2, including `create` and `draft-stage`.
`--dry-run` uses the same admission without temporary output or locks; it remains
usable without writable temporary storage. The default archive-byte ceiling is
128 GiB; format-specific limits can be lower. See
[Export and reopening limits](PACKAGE_MANAGER.md#export-and-reopening-limits).
Shared plan preparation also supports cancellation during replay, folder work,
sorting and WAD assembly, preserving document history and a retryable cache.
Plan preparation admits 250,000 live entries/conflicts, 500,000 path/parent
keys and 128 MiB of text per representation before container growth. The manifest
reports the fixed caller policy under `summary.planLimits` with decimal-string
`maximumRecords`, `maximumIndexKeys` and `maximumMetadataBytes`. A resource
refusal blocks publication without partial rows; Undo/Redo and draft history
remain available. Individual edits check plan and browser metadata before
adoption; a CLI command's staged group checks its final view once before commit.
Refusal leaves its saved draft and redo unchanged. `summary.viewLimits` reports
decimal-string `maximumEntries`/`maximumMetadataBytes` and numeric
`maximumPathDepth`. This adds no CLI flag or terminal progress protocol.
Archive info JSON adds `package.summary.totalSizeOverflow` and the decimal
string `totalSizeBytesExact`. Oversized totals set the flag and make both the
exact and legacy numeric byte fields null; readable metadata is still returned.
Human output explicitly names overflow. Staging manifests add availability and
before/after overflow flags plus `beforeBytesExact`/`afterBytesExact`; composition
buckets add `sizeOverflow` and `sizeBytesExact`. Prefer these strings for exact
large aggregates. This does not change individual-entry number fields or bypass
manifest payload verification. Oversized planned totals block writing through
the existing failure path. See [Summary totals](PACKAGE_MANAGER.md#summary-totals-and-composition).
Package preview JSON includes decimal-string `bytesReadExact` and
`totalBytesExact` with `totalBytesKnown`. Unknown entry metadata has a null exact
total; a known empty entry has `"0"`. Read failure/cancellation after resolving
an entry preserves its declared total and reports zero discarded sample bytes.
Legacy numeric counts remain available but may round large integers. Text
output uses exact bytes above 2^53. These fields do not change preview read
budgets, integrity checks or exit codes. Preview JSON adds `audioCodec`,
`audioChannels`, `audioSampleRate`, `audioBitsPerSample`, `audioDurationMs`,
decimal-string `audioDurationMsExact`, and `audioPlaybackCandidate`. Zero duration
means unknown/unavailable or sub-millisecond; exact encoding does not imply timing
accuracy. Ogg timing is header-derived and remains unknown for partial,
inconsistent or out-of-range pages. A positive result is an estimate that can
include codec trimming or stream offsets. Candidate status does not guarantee
decodability or playback. See [Entry previews](PACKAGE_MANAGER.md#entry-previews).
Combined sessions and the `map textures` multi-folder lookup share these
budgets across at most 64 layers/roots, including overridden source records.
Staged documents also bound retained generated content to 256 MiB and payload
fingerprints to 128 MiB across base, operations and undo/redo. Draft object
deduplication shares that allowance; rejected edits preserve the document.
`package manifest` includes decimal byte counters and limits under
`summary.retainedContent`. Retained index/text metadata also has a 128 MiB
budget and 1,000,000-record cap across base, edits and history. The manifest's
`summary.retainedMetadata` reports decimal-string `records`, `metadataBytes`,
`maximumRecords` and `maximumMetadataBytes`. Group/edit refusal preserves prior
state; draft metadata is checked before payload reads. Reader projections also
admit 250,000 physical/diagnostic/implied-folder records, 64 MiB of metadata text
and 128 path components. A refused draft view reports its limit without partial
JSON entries; `package draft-undo` remains available to restore the view while
preserving redo. Legacy draft history may still exceed a view policy; Undo/Redo
remain unrestricted for recovery. Model-package handoffs share the same diagnostic.
Aggregate process memory remains open.
Persisted history counters refuse edits before source reads or output creation
when the next operation/revision would exceed `2^64 - 2`. A refused staging option
returns the existing invalid-plan/argument exit 2 and preserves the input draft.
Draft save without edits, Undo/Redo and archive export remain available; export
and reopen a package to begin fresh history. See
[Opening limits](PACKAGE_MANAGER.md#opening-limits),
[Retained document content](PACKAGE_MANAGER.md#retained-document-content) and
[Retained document metadata](PACKAGE_MANAGER.md#retained-document-metadata) and
[Reader snapshot limits](PACKAGE_MANAGER.md#reader-snapshot-limits) and
[Edit history counter limits](PACKAGE_MANAGER.md#edit-history-counter-limits).
- [x] `--package-formats` scaffold report for package/archive interface descriptors.
- [x] `--check-package-path <path>` scaffold validation for normalized safe package virtual paths.
- [x] `--info <path>` scaffold package summary for folders, PAK, WAD, ZIP, and PK3.
- [x] `--list <path>` scaffold package entry listing for folders, PAK, WAD, ZIP, and PK3.
- [x] `--preview-package <path> --preview-entry <virtual-path>` scaffold package
  entry text, image metadata, and binary preview output.
- [x] `--extract <path> --output <folder>` safe selected/all package extraction
  with repeatable `--extract-entry`, `--dry-run`, and explicit `--overwrite`.
- [x] `--validate-package <path>` read-only streaming integrity validation.
- [x] `package info`
- [x] `package list`
- [x] `package preview`
- [x] `package extract` shares bounded streaming and whole-selection preflight
  with the GUI. Repeated/case-folded output names, file/directory conflicts,
  links and source destinations are refused. Each file commits only after its
  content checks pass; already completed files remain after a later failure.
  JSON includes `bytesRead`, committed `totalBytes`, `entryIndex` and
  `sourceOrdinal`. Empty selectors, unsupported/repeated options, extra
  positional arguments and selectors combined with `--extract-all` return `2`.
  No selectors means all entries; `--entry`/`--extract-entry` are repeatable.
- [x] `package validate` streams every physical file, checks sizes and ZIP CRCs,
  and reports SHA-256 hashes, per-entry failures, warnings, and unchecked files.
  ZIP/PK3 opening checks end-record counts/ranges, ZIP64 local sizes and data
  descriptors. Names follow UTF-8, checksum-matched Unicode Path or CP437
  metadata. Invalid global metadata fails opening; invalid local records remain
  listed with a diagnostic and cannot pass validation. Digital-signature
  authentication and regional code-page overrides are not provided.
  `--max-entry-bytes <n>` optionally limits work per file; the positive limit
  leaves larger files unchecked. Exit `0` requires a complete, warning-free
  pass, `4` indicates invalid or unchecked content, `2` invalid arguments,
  `3` missing input, and `1` an archive that cannot be opened. JSON preserves
  `validation.usable` as an alias of the stricter `validation.valid` result.
- [x] `package manifest`
- [x] `package interrupted-saves [--directory <folder>]` discovers bounded journal
  metadata, defaulting to the current folder. Repeat `--directory` up to 32 times.
  The shared GUI/CLI service scans at most 100,000 members and 256 journals, with
  a 64 KiB journal limit. JSON `inventory` distinguishes metadata validity from
  unverified payloads, reports incomplete scans and supplies `journalSha256`.
  Exit `0` requires complete valid metadata, `2` invalid arguments, `4` incomplete
  or invalid discovery. No payload hashing, save locks or preferences are written.
- [x] `package recover <journal> [--finish]` inspects a save journal and verifies
  its recorded file hashes. Read-only inspection is the default. `--finish`
  completes backup publication and cleanup only when the replacement is already
  installed; it never installs pending output or rolls back later edits.
  An unfinished backup outside the output directory requires explicit
  `--finish --backup <recorded-path>` after inspection.
  Optional `--expected-sha256 <journal-hash>` rejects a changed review. JSON includes
  schema version, state, `canFinish`, `finished`, `journalSha256`, `cancelled`,
  `backupVerified`, paths and errors.
  Exit `0` means inspection/completion succeeded, `2` invalid or repeated options,
  `3` a missing journal, and `4` invalid or unsafe recovery.
- [x] `package compare` to diff two archives entry by entry, reporting
  added, removed, changed, case-only, and identical members. Paths are
  matched case-folded, and repeated Doom lump names are paired by source
  order rather than collapsed, so one WAD's second map is compared against
  the other WAD's second map. `--metadata-only` compares paths and sizes without
  a per-entry content comparison; opening still fingerprints the source files.
  `--include-directories` brings
  directory records into the comparison (off by default),
  `--max-entry-bytes <n>` sets the per-entry I/O budget (default 256 MiB),
  and `--against <path>` replaces the second positional path. An entry that
  is unreadable or above the budget has status `uncompared` and blocks a content
  match. Repeated WAD names are read positionally. Equal-size files are hashed
  with SHA-256; matching stored CRC metadata does not bypass byte verification.
  Archives, saved drafts and staged content stream through the same bounded
  readers without allocating an entire payload or creating comparison artifacts.
  JSON includes `completed`, `cancelled`, and per-entry evidence. Interrupted
  rows contribute neither hashes nor byte totals to a partial result.
  Reader warnings prevent a match. Exit codes: `0` for a complete match, `4` for
  differences, blocked plans, or unchecked content, `3` when either package
  cannot be opened, and `2` for invalid arguments. `--metadata-only` explicitly
  allows a name/size match without checking content.
  With one source path, `--staged` compares against the result of the usual
  `--add-file`/`--as`, `--replace-file`/`--replace-entry`, `--rename`/`--to`, and
  `--delete` options. It never writes an archive. Combining `--staged` with a
  second package, or staging options without `--staged`, is a usage error.
- [x] `package stage`
- [x] `package save-as`
  Existing outputs require explicit overwrite and retain their original bytes
  in a backup. New outputs publish without clobbering a concurrently created
  file; replacements keep the original present until atomic commit. JSON
  `outputCommitted` remains true when later backup/manifest bookkeeping warns.
  `recoveryPaths` identifies retained journals and verified recovery copies.
- [x] `package subset <source> <output>` exports files from an archive, folder or
  `.vibepackage` draft using repeatable `--entry-index <n>`, `--entry <path>`,
  `--prefix <folder>`, and/or `--where <query>`. Indexes are zero-based rows from
  `package list --json` for the same snapshot. Indexes, entries and prefixes form
  a union, then the query filters it; a query alone searches all files. Prefixes
  match folder boundaries case-insensitively. Queries/prefixes preserve repeated
  occurrences; ambiguous path-only selectors are refused. Unknown fields,
  nonexistent selectors and empty results fail closed.
  Alternatively, exclusive `--map-input <map>` selects resolved level dependencies
  with `--engine` and `--map-name`. The source map and BSP are not added automatically.
  Doom WAD subsets expand complete binary/UDMF map groups, matching GL groups,
  namespace boundaries and local texture name tables. Invalid/ambiguous groups or
  skipped WAD records return `4`; WAD2/WAD3 select exact texture records.
  `--dry-run --json` exposes a schema-1 `subset` review alongside `write` and
  `staging`, including explicit versus required entries, source ordinals, snapshot
  indexes and decimal-string byte sizes. Dry runs write no output or preferences.
  Format, compression, manifest and overwrite options use the shared writer.
  All archive writer commands stream verified entry payloads. ZIP compression
  uses a read-only measurement pass followed by a verified write pass; dry runs
  create no payload spool. Requested in-place manifests are hashed before the
  source is replaced. Existing JSON report and exit-code contracts are unchanged.
  `--output` can replace the positional destination. Extra/empty/repeated singleton
  or unknown options return `2`. In-place and staging-edit options are refused;
  prepare a draft before selecting a subset. See [Package Manager](PACKAGE_MANAGER.md#export-a-selected-subset)
  for grouping boundaries and the distinction from complete game dependency closure.

### Level Dependency And Package Loop

```sh
vibestudio --cli map dependencies ./maps/arena.map --engine idTech3 --package ./assets --json
vibestudio --cli map materials ./maps/arena.map --engine idTech3 --package ./assets --json
vibestudio --cli package subset ./assets ./arena-assets.pk3 --map-input ./maps/arena.map --engine idTech3 --dry-run --json
vibestudio --cli package subset ./assets ./arena-assets.pk3 --map-input ./maps/arena.map --engine idTech3 --compression best
vibestudio --cli package subset ./assets ./textures.pk3 --prefix textures/arena --where "ext=tga" --json
```

`map materials` uses the camera's bounded resolver to report decoded images,
shader editor/stage previews, static model skins, original/preview dimensions,
source paths and limitations under the JSON `materials` key. It requires
`--package <archive-or-folder>` and accepts `--palette`. Incomplete or unavailable
previews return validation failure; it writes no images. See
[Level Editor](LEVEL_EDITOR.md#material-camera-and-package-assets).

`map dependencies` reports schema version 1 JSON under `dependencies`, including
`files`, unique byte totals, per-reference status/candidates/object selectors,
`requiredBy`, warnings, and coverage limits. Exit `0` means the supported scan
completed without unresolved references; `4` means dependency problems or an
incomplete scan. Usage errors return `2`; input-load failures return `1`.
`--installation <id>` checks references against that installation's asset index: references the
game provides get the status `stock` and the report a `stock` count, and they are not problems.
Its `--package` input accepts an archive, folder or `.vibepackage` draft. Drafts
are inspected through their planned contents, including staged additions,
replacements, renames and deletions. Review does not publish or change the draft;
an invalid draft fails to load instead of exposing its storage files as assets.
The report's `package` path identifies the selected draft, rather than its
underlying base archive or an empty path for a newly created package.
`package subset --map-input` returns `4` without writing when the dependency
report cannot be exported. File-selection usage errors return `2`.

The dependency resolver checks explicit map textures, `model`/`model2`,
`noise`/`noise1`–`noise4`, `sound`, and `music`. Quake III declarations take
precedence over a same-named image, and their image references include
`map`, `clampMap`, `animMap`, `videoMap`, `qer_editorimage`, `q3map_lightimage`,
and six faces for each nonempty `skyParms` box. Duplicate declarations are
ambiguous. Inline model identifiers and engine sound identifiers are accounted
for separately. Quake sky/liquid textures and Quake III common shaders still
need their actual source assets; a special-looking name is not proof of presence.

This is a source-asset inventory, not a complete playable-mod dependency proof.
It follows MDL/MD2/MD3 material references, including Quake III shader images.
It does not expand external `.skin` overrides, Doom composite texture definitions, secondary
shader references, game-code assets, or dynamically selected files. The export
retains whole referenced shader scripts but resolves images only for shaders
used by the map. File resolution is metadata-based; payload reads and archive
integrity checks happen during export. Each reference records up to 64 use sites.
Shader scanning is bounded to 2,048 scripts, 4 MiB per script, 64 MiB total,
65,536 declarations, and 65,536 unique dependency references. Exceeding a bound,
malformed script data, or cancellation marks the report incomplete. Model scans
are bounded to 128 files, 8 MiB per file, and 64 MiB total. Unsupported or
malformed model data also makes the report incomplete. GUI audits include the
current conflict-free staging plan; generated model bytes are retained in the
snapshot used for subset export.

Package output must be outside an input folder and separate from imported source
files. Manifest and backup paths must also be separate from source content and
the archive output. These checks run before writing; they are path-based guards,
not a guarantee against concurrent filesystem replacement by another process.

### Project Releases

```sh
vibestudio --cli install register build quake3-games-quake3
vibestudio --cli release catalog ./mymod --json
vibestudio --cli release plan ./mymod --map maps/arena1.map --json
vibestudio --cli release notes ./mymod --map maps/arena1.map --readme
vibestudio --cli release changelog ./mymod --add "New arena: The Pit" --category added
vibestudio --cli release publish ./mymod --map maps/arena1.map --release-version 1.0.0 --dry-run --json
vibestudio --cli release history ./mymod --json
```

- [x] `release plan [<project>]`: what a release ships and what the game provides. The project
  defaults to the working folder, which must then hold a manifest; a named folder without one uses
  folder defaults with a warning. `--map`, `--model` or `--texture` (repeatable, one kind per run)
  choose items; none releases the whole project, and `--scope project|maps|models|textures` states
  it explicitly. JSON: `project`, `manifest`, `game`, `installation`, `stock` (`available`,
  `description`, `warnings`, `register`) and `plan` (entries with `path`, `role`, `bytes`,
  `requiredBy`, `replacesStock`, `loose`; `stock` references; `problems` with `kind` and `blocking`;
  `maps` with their build state; `warnings`, `limitations`, `canPublish`, `stockChecked`, `files`,
  `package`). Exit 4 when something blocks publishing.
- [x] `release notes [<project>]`: the generated Markdown notes, or the readme with `--readme`.
  JSON adds `notes`, `readme`, `changes` and `compared` (the inventory diff with the previous
  release). Exit 4 when the plan cannot publish.
- [x] `release publish [<project>]`: writes the package, readme, `RELEASE_NOTES.md` and
  distribution archive into `--output` (default `<release.outputFolder>/<name>-<version>`), records
  `.vibestudio/releases/<version>.json` and moves the changelog's Unreleased changes under the
  version. `--notes-file` replaces the generated notes; `--no-readme`, `--no-notes`,
  `--no-archive`, `--no-record` and `--no-changelog` skip those outputs; `--compression` applies
  to ZIP and PK3; `--date` sets the release date. A released version is refused unless
  `--overwrite` replaces it, keeping `.bak` copies. `--dry-run` reports every path without writing.
  JSON: `publication`. Exit 4 when the plan blocks or the output is refused before writing; exit 1
  when writing fails afterwards.
- [x] `release changelog [<project>]`: the unreleased changes and released versions; `--add <text>
  --category added|changed|deprecated|removed|fixed|security` records one (`--dry-run` previews).
- [x] `release history [<project>]`: published releases from `.vibestudio/releases`, with their
  package, hash, size, file count and maps.
- [x] `release catalog [<project>]`: the maps (with `built` and `stale`), models and texture
  folders a project can release.

Every planning command accepts `--installation <id>`, `--register <file>` (an exported index, which
wins over the installation's), `--no-stock`, `--game <key>`, `--release-version`, `--title`,
`--format pk3|pak|wad|zip`, `--package-name` and `--include-sources`. The project's linked
installation is used by default, then any saved installation of the project's game. Options that
only one command reads are refused by the others with exit 2. See
[Project releases](PROJECT_RELEASES.md).

### Assets
- [x] `asset inspect`
- [ ] `asset preview-export`
- [x] `asset convert`
- [x] `asset audio-wav` — separate PCM16 output from supported WAV/DMX, MP3,
  native FLAC and Ogg Vorbis; shared bounded decoder, source protection, atomic
  writes and JSON conversion details.
- [x] `asset audio-edit` — shared PCM effects, paste/mix, silence insertion, whole-document `resample --sample-rate`, exact frame ranges, atomic WAV/native output, JSON, dry runs, and source preservation; see [Audio Editor](AUDIO_EDITOR.md).
- [x] `asset audio-new` — create empty native documents or silent audio with an explicit sample rate, channel count, and frame count.
- [x] `asset audio-project` — inspect/import/copy/recover lossless `.vsaudio` documents and export PCM WAV; edits can also output `.vsaudio` without intermediate quantization.
  MP3, FLAC and Vorbis imports use the same device-independent decoder as the GUI.
  Audio project, edit, analysis, marker and delivery reports include import warnings;
  native projects persist them in metadata. See the compressed format bounds in
  [Audio Editor](AUDIO_EDITOR.md#formats-and-limits).
- [x] `asset audio-export` — shared WAV/DMX game sound presets, mono/resampling reports, deterministic dither/dry runs, explicit overwrite, and native provenance protection.
- [x] `asset audio-generate` — sound effects from a description by the synthesizer or the sound model, delivered through the same presets, with seamless loops, variants, and a provenance record per sound.
- [x] `asset audio-analyze` — read-only exact-range sample/true peak, BS.1770 integrated loudness, RMS, DC, full-scale counts/runs, per-channel absolute positions and versioned JSON. Explicit surround `--channel-map` or `--no-loudness`; unavailable measurements and silent dBFS/dBTP use null. See [analysis conventions](AUDIO_EDITOR.md#analysis).
- [x] `asset audio-markers` — inspect cues/forward loop or replace them from bounded
  `--markers <JSON file>` into a separate `--output <.vsaudio|.wav>`, with package
  `--entry`, dry runs, explicit overwrite, source/manifest protection and shared
  GUI validation. Structural edits and SRC transform the same markers.
- [x] `asset audio-recoveries [--directory <folder>]` verifies bounded local copies
  and reports individual errors. Reviewed `--discard <id> --expected-sha256 <hash>`
  defaults to a read-only dry run; `--write` commits under session/file/folder
  locks. Source paths remain provenance only. Limits are 32 copies / 512 MiB,
  with no automatic eviction. See [Audio Editor](AUDIO_EDITOR.md).
- [ ] `asset graph`
- [x] `asset find`
- [x] `asset replace`
- [x] `asset search` through the `asset find` alias.

`asset find` and `asset replace` share the Code workspace's search service and
UTF-8/BOM-marked UTF-16 codec. CLI commands read saved files; GUI snapshots of
open documents remain in the running editor. Use `--case-sensitive`,
`--whole-word`, `--include "*.qc;*.qh"`,
`--exclude "generated/*"`, and `--extensions` to restrict matching. Replacements
default to previews; `--write` applies the prepared bytes after source-hash
checks. `--delete-matches` explicitly removes matches and cannot accompany
`--replace`. JSON reports completeness, skipped files, before/after snippets,
actual replacement counts, saved paths, and per-match encoding, source and text
snapshot hash. Incomplete scans return validation
exit code 4 and cannot write. See [Project Search](PROJECT_SEARCH.md) for exact
bounds, encoding behavior, batch cancellation, and concurrency limits.

### Maps
- [x] `map generate --prompt <description> --output <map-or-folder>` plans,
  lays out and builds a sealed, playable level for Quake, Quake II, Quake III
  (`.map`) or Doom (a PWAD holding `MAP01`; build its nodes before playing).
  The description is read for the game, mode, theme, rooms, verticality,
  liquid, players and seed; `--game`, `--mode`, `--theme`, `--rooms`, `--size`,
  `--verticality`, `--liquid`, `--monsters`, `--players`, `--seed`, `--title`
  and `--wad` set them outright. `--planner rules` (the default) plans on this
  machine, the same way for the same description and seed; `--planner ai`
  asks the configured text model for the plan as JSON, checked against the
  plan schema and repaired, with `--revisions` correction rounds (default 1)
  before the rules step in. `--plan <file>` builds a saved or hand-edited
  plan; `--save-plan`, `--preview <png>` and `--report <json>` keep the plan,
  a top-down layout picture and the full report. `--texture role=name`
  (wall, floor, ceiling, trim, liquid) and `--textures-from <package>` choose
  textures the project has. AI rules follow `ai ask`: `--dry-run`, `--yes`,
  `--provider`, `--model`, `--endpoint`, `--timeout-ms`.
- [x] `map generate --generator quake-mapgen` builds the level with
  Quake-MapGen (`src/mapgen`) for Quake, Quake II or Quake III. `--mode` also
  takes `ctf`; `--format q1-valve220|q2-valve220|q3-legacy|q3-brush-primitives`
  picks the map dialect (default: the game's); `--intensity quiet|balanced|intense`,
  `--rooms` (2 to 32), `--players`, `--seed`, `--verticality` and `--title`
  override the description; `--candidates` (1 to 64, default 6) and
  `--repair-iterations` (0 to 64, default 8) bound ranking and repair; and
  `--switch <name>=on|off` (repeatable) sets a stage switch. `--planner ai`
  plans through the configured text model with `--revisions` corrections
  (0 to 8, default 2) before the rules step in; a connector failure or
  cancellation fails the command. `--report` writes the studio report with
  Quake-MapGen's schema v4 report under `quakeMapgen`, also when validation
  fails. `--verbose` prints each analysis finding. The studio generator's
  `--theme`, `--size`, `--liquid`, `--monsters`, `--wad`, `--texture`,
  `--textures-from`, `--plan` and `--save-plan` are refused with this
  generator.
- [x] `map plan --prompt <description>` prints the room-and-connection plan
  without building it, by `--planner rules` or `ai`; `--output` writes it as
  JSON for review, editing, and `map generate --plan`. With `--generator
  quake-mapgen` it prints Quake-MapGen's semantic plan instead.
- [x] `--ranker scalar|ggml` (Quake-MapGen, default `scalar`) picks the
  candidate ranker; `ggml` needs a build with `-Dggml=enabled` and gives the
  same scores.
- [x] Every text AI command (`ai ask`, `ai test-connection`, `map generate`
  and `map plan` with `--planner ai`, and `map ai-edit`) takes
  `--provider claude-code|codex|custom-command|local-gguf` besides the HTTP
  connectors; image and sound commands do not. For a harness, `--endpoint`
  is the program (or the custom command line) and `--model` the tool's model,
  `claude-opus-5-5` by default for Claude Code; for `local-gguf`, `--model`
  is the `.gguf` file.
  Harnesses are cloud requests: they need cloud connectors allowed and
  `--yes`, and `--dry-run` prints the exact command, files and input.
  `ai test-connection` checks a harness's login first and reports it under
  `login`; `ai status` lists the harness programs found and the GGUF runtime
  under `runtimes`.
- [x] `map ai-edit <map> --prompt <instruction>` asks the configured text
  model for edits to a Quake, Quake II, or Quake III `.map`, in a fixed set
  of actions (add an entity, set or remove a key, add a box brush,
  retexture, move, delete) answered against a JSON Schema. The model sees a
  summary of the map (entities and keys, brush bounds and textures, the
  selection given by `--select entity:3,brush:12`), with the home folder
  shortened. Each action is checked against the map and listed as ready or
  blocked, with why; nothing changes unless `--output <map>` is given,
  which writes the map with the ready actions applied through the editor's
  own edits (`--only 1,3` picks them by number; `--overwrite` replaces an
  existing file). `--save-proposal <json>` keeps the proposal and
  `--proposal <json>` reviews or applies a saved or hand-written one with
  no model at all. AI rules follow `ai ask`: `--dry-run`, `--yes`,
  `--provider`, `--model`, `--endpoint`, `--timeout-ms`. Exit code 4 when
  the answer holds no proposal or `--output` has nothing ready to apply.
- [x] `map inspect-udmf <wad> --map-name <marker>` reports the selected UDMF
  map's namespace, globals, object selectors and property literals; `--json`
  includes source locations.
- [x] `map edit-udmf <wad> --map-name <marker> --object global|type:index
  --set key=literal --output <wad>` applies property changes as one transaction.
  Repeat `--set` and `--remove key` as needed; values use UDMF literal syntax.
  `--dry-run` verifies without writing, and `--overwrite` permits an existing
  destination. Inspection and editing reject maps that are not UDMF.
- [x] `map inspect` for Doom WAD map lumps and Quake-family `.map` files.
- [x] `map place-sound <map> --package <archive-or-folder> --sound sound/name.wav
  --game quake2|quake3 --origin x,y,z --output <separate.map>` validates a package
  WAV and places a speaker in a separate map output. `--mode` accepts `loop-on`,
  `loop-off` or `triggered`; inactive sounds require `--targetname`. Use `--dry-run`
  to inspect the plan without writing; map and package inputs are protected.
  With `--json` it lists every object the editor shows: entities, brushes,
  patches, target links (with both ends' anchor points), and Doom vertices,
  linedefs, sidedefs, sectors, and things.
  Patches include row/column identities, XYZ/UV control values, and fixed
  subdivision settings, so automation can inspect a grid before editing it.
- [x] `map edit` for entity key/value edits with non-destructive `--output`,
  and on Doom and Hexen maps for a record's fields: `--select sector:N` (heights,
  flats, light, special, tag), `--select linedef:N` (flags, special, tag or
  `arg0` to `arg4`, and `front.` or `back.` with a side field, a shared sidedef
  copied first), `--select sidedef:N`, or `--select thing:N` (a thing's type,
  angle, position, and flags, as whole numbers the WAD can hold); and on
  Quake-family maps a brush face's texture and alignment with `--select brush:N
  --set faceK.field=value` (`texture`, `shiftx`, `shifty`, `rotation`, `scalex`,
  `scaley`, faces counted from 1). The report then describes the record edited:
  a brush's faces as `faceK: texture=... shiftx=...` lines, and `map inspect
  --json` gives each brush a `faces` array with the same fields, and Hexen lines
  and things their `args`. With `--where "<query>"` in place of `--entity` or
  `--select`, every entity (on a Doom map, every thing) the query keeps takes
  each `--set` as one undo step, as the Levels inspector sets a key on several
  selected entities: `map edit start.map --where class=light --set light=300`.
- [x] `map move` for Doom vertices/linedefs/things and Quake entities.
- [x] `map align-textures <map>` for batch brush and patch surface alignment.
  Select repeatable `--object brush:id|patch:id|entity:id` or
  `--face brushId:faceNumber` (one-based). Choose exactly one of `--shift U,V`,
  `--scale U,V`, `--scale-step U,V` (NetRadiant Custom's additive scale step
  about the texture origin, flipping sign at zero), `--degrees N` (with
  `--anchor centre|origin`; `origin` is Radiant's turn about the texture
  origin), `--clockwise N` (degrees clockwise as seen from each face's front,
  about the texture origin, whatever the face format or a mirrored texture:
  TrenchBroom's Rotate Textures turns, `ClockwiseRotate` in
  `core/level_surface.h`; negative turns counter-clockwise), `--fit U,V` (a
  zero keeps that axis),
  `--align U,V` with `keep|minimum|center|maximum` modes, `--natural U,V` or
  `--face-aligned U,V` (map units per texel, Radiant's Natural and a projection
  along each face's own plane), `--flip u|v`, or, as a camera at
  `--camera-yaw` and `--camera-pitch` degrees (default 0) sees each face,
  `--camera-shift RIGHT,UP` (texels across and up the view, TrenchBroom's
  camera texture moves) or `--camera-flip horizontal|vertical`, or a reset in
  one pass for every face format: `--reset-alignment U,V` (TrenchBroom's Reset
  Texture Alignment, keeping each face's projection kind; `Reset`) or
  `--radiant-reset U,V` (NetRadiant Custom's TextureReset/Cap: the plane's own
  basis for Valve 220 and brush-primitive faces, capped patches;
  `RadiantReset`). Reset Textures mode (bobToolz) replaces the operation with
  absolute `--set-scale-u`, `--set-scale-v`, `--set-shift-u`, `--set-shift-v`
  and `--set-rotation` values and `--rename NAME`, on the selection or the
  `--whole-map` (visible, unlocked surfaces in the open group), optionally
  `--only-texture NAME`, through `prepareLevelTextureReset`.
  Resolve original image sizes through `--package <archive-or-folder>` and
  optional `--palette`, or override with `--texture-size W,H`. Required unknown
  dimensions fail; no size is guessed. Requires `--output`, supports
  `--overwrite`, `--dry-run`, `--json`, and reports changed face/brush counts.
  All faces retain their dialect and share one undo transaction. `map edit`
  also accepts `faceN.matrix00` through `faceN.matrix12`, or an atomic
  `faceN.matrix=a,b,c,d,e,f`. See [Surface Alignment](LEVEL_EDITOR.md#surface-alignment).
- [x] `map copy-surface <map> --target face:brushId:faceNumber --output surface.json`
  copies one face's material, mapping and flags to a versioned JSON clipboard.
  Face numbers are one-based. `map paste-surface <map> --clipboard surface.json`
  accepts repeated `--target face:brushId:faceNumber|brush:brushId` and requires
  `--output`. Default `--mode parameters` preserves the mapping family;
  `--mode project` preserves world UVs; `--mode seamless` wraps around the shared
  plane edge. Both may need `--texture-size width,height`
  and explicit `--allow-valve220`. Both commands accept `--dry-run` and
  `--overwrite`; an output path is required even for dry runs.
  `radiant-values` retains Valve axes. Parameter modes accept `patch:id` targets
  and repeatable `--object` selections. `--mapping-only` keeps materials/flags;
  repeatable `--material-size material=W,H` supplies retained target dimensions.
  `radiant-project` projects brush/patch selections with native parameters,
  source/target dimensions and an `edgeOnFaces` report.
  `--stroke` replays up to 4,096 ordered face/patch hits with one transaction,
  first-hit selection and an advancing seamless source. JSON includes the final
  portable source; source and clipboard files remain protected.
- [x] `map place-sound <map> --package <archive|folder|.vibepackage>
  --sound sound/name.wav --origin x,y,z --output <separate.map>` validates one
  exact-case mono 22050 Hz PCM16 sound and adds a Quake II/III `target_speaker`.
  `--game quake2|quake3` selects an explicit target when needed; mismatched targets
  fail. `--mode loop-on|loop-off|triggered` defaults to `loop-on`; the other modes
  require `--targetname`. Shares placement/path/format validation with Audio's
  Stage & Place action, supports deterministic `--dry-run`, `--json` entity
  properties and explicit `--overwrite` for separate outputs, and protects inputs.
  Unknown/repeated/missing options return 2; invalid/missing assets return 4.
  No sound bytes or package source are changed by this command.
- [x] `map add-entity` to add a point entity to a Quake-family `.map`, with
  `--class`, `--origin x,y,z`, and repeatable `--set key=value`.
- [x] `map delete` to delete entities, brushes, and patches from a
  Quake-family `.map` by repeatable `--object kind:id`. An entity takes its
  brushes and patches with it; `worldspawn` is refused, and on a Doom map only
  things can go, since vertices, linedefs, and sectors hold the geometry.
- [x] `map duplicate` to copy entities, brushes, and patches of a Quake-family
  `.map`, moved by an optional `--delta x,y,z`. A copy keeps the original's
  exact text format; the new selectors are reported as `copies`. On a Doom or
  Hexen map `map duplicate` and `map delete` take `thing:N` selectors.
  Duplication locks brush textures by default; `--texture-lock off` retains the
  source frame. Doom/Hexen copy coordinates round to native whole units.
  `--copies N` (1–256, default 1) creates additional copies at `delta`,
  `2*delta`, through `N*delta` from the original objects, avoiding cumulative
  rounding drift. JSON reports `copyCount` and every new selector in `copies`.
  Arrays use one atomic edit and undo command; a failed copy preserves the
  source and output. Multiple-copy arrays are limited to 32,768 added native
  records (including Doom entity mirrors), 262,144 brush faces/patch points,
  and the scene membership budget. Owners, scene memberships and entity target
  names retain ordinary duplicate behaviour; use prefabs for independent
  target-name remapping. UDMF things support the same command and arrays with
  fractional XYZ positions. Copies retain native block comments, unknown
  properties, IDs and action arguments; originals remain byte-for-byte intact.
  Ordinary things retain valid nodes; copied polyobject controls require a
  rebuild. UDMF geometry duplication and thing deletion remain unsupported.
  Snap, duplicate and paste share the GUI's isolated placement preparation
  service before the CLI's guarded save or dry run. CLI invocation remains
  synchronous; no terminal cancellation or progress-stream option is added.
- [x] `map paste <path> --from <map-text-file> --output <path>` inserts UTF-8
  map text with an optional `--delta x,y,z`. Brush textures lock by default;
  `--texture-lock off` retains source parameters. Supports `--dry-run`,
  `--overwrite` and `--json`; JSON reports the added selectors as `pasted`.
  Input is limited to 8 MiB / 4,194,304 UTF-16 characters; malformed UTF-8, NUL,
  excessive geometry nesting, incompatible brush dialects and patches in
  non-Quake-III maps are refused. Standard `--engine`/`--engine-hint` resolve
  ambiguous text-map targets. See [placement](LEVEL_EDITOR.md#placement-and-grid-alignment).
- [x] `map add-brush` to add an axis-aligned box brush (`--mins`, `--maxs`,
  `--texture`) to worldspawn, written in the map's own face format.
- [x] `map insert-vertices <map> --brush <id> --position x,y,z
  [--position x,y,z ...] --output <map>` adds corners through the shared hull
  service; optional `--grid` snaps new coordinates and `--allow-collapse`
  permits existing corners to disappear. Every new corner must survive.
- [x] `map remove-components <map> --brush <id> --kind vertex|edge|face
  --component <id> [--component <id> ...] --output <map>` removes incident
  vertices and requires a valid three-dimensional result. Both topology
  commands retain source mapping/flags/dialect, honour scene locks and normal
  dry-run/overwrite protections, and report inserted/removed counts in JSON.
  Component commands reject unknown, irrelevant, duplicate singleton and
  missing-value options before editing.
- [x] `map split-components <map> --brush <id> --kind edge|face
  --component <id> [--component <id> ...] --delta x,y,z --output <map>` adds
  a corner at each midpoint/centre plus the delta. Optional `--grid` snaps
  final coordinates; `--allow-collapse` explicitly permits old corners to
  disappear. Every new point must survive. Uses the same atomic geometry,
  scene lock and persistence services as GUI splitting, with standard dry-run,
  overwrite and strict-option checks. Re-query component IDs after each edit.
- [x] `map brush-components <map> --brush <id>` reports solved zero-based vertex,
  edge and face IDs, positions and face materials. JSON includes a `brush`
  object with `vertices`, `edges` and `faces` arrays. Re-query IDs after editing.
- [x] `map extrude-faces <map> --face <brush:face> [--face <brush:face> ...]
  --distance <units> --output <map>` moves selected planes along the first
  face's normal, preserving every original face. One face per brush and
  parallel normals are required. `--split` creates a separate section and
  requires normals pointing the same way. Optional `--grid` chooses the nearer
  distance-step or vertex-to-grid-plane snap; without it distances are exact.
  Texture lock is on unless `--no-texture-lock` is supplied. Shared validation,
  scene locks, linked groups, source/UV preservation and one undo transaction
  match the GUI. JSON includes `changedBrushes`, `createdBrushes`, `distance`
  and `activeFaces`; re-query IDs after saving/reopening. Standard dry-run,
  overwrite and strict-option checks apply.
- [x] `map move-components <map> --brush <id> --kind vertex|edge|face
  --component <id> [--component <id>...] --delta x,y,z --output <map>` reshapes
  the selected components through a convex hull. `--grid` snaps final positions
  and can be used without a delta. `--allow-collapse` accepts disappearing or
  merged vertices; invalid/flat solids are always rejected. Materials and flags
  retain their source syntax; deformed faces do not have texture lock. Bounds
  are 128 faces, 256 vertices and ±32768 units. Supports `--dry-run`, `--json`
  and `--overwrite`; JSON includes before/after counts and `collapsedVertices`.
- [x] `map transform-components <map> --set brush:0/face/4 --operation rotate
  --axis z --angle 15 --output <map>` transforms picked brush components and
  patch points atomically. Repeat `--set object:id/vertex|edge|face/ids` once per
  object; patches require `vertex` and row-major point IDs. `move` uses `--delta`,
  `scale` uses `--factors`, and `shear` uses distinct `--axis`, `--along` and
  `--factor`. The shared pivot defaults to selected-corner bounds; `--pivot`
  overrides it. Optional `--grid`, `--allow-collapse` and `--uv-lock off|on|fit`
  (default off) share GUI validation; `fit` keeps three corners of a face an
  exact lock cannot hold instead of refusing, and JSON reports `uvLockMode`
  and `fittedFaces`. `--allow-valve220` requires UV lock and explicitly permits
  whole-map mapping conversion. Supports 256 object sets,
  4,096 IDs per set, finite coordinates within ±32768, `--dry-run`, `--overwrite`
  and JSON counts/pivot/UV policy. Duplicate, unknown and irrelevant options
  are rejected before writing. Inspect topology again after editing it.
  Repeated `--face face:brushId:faceNumber` (one-based, the `faces` that
  `map select-textured` prints) transforms independently picked faces without
  selecting their brushes, alone or with `--set`, through the same service as
  the GUI's Edit Faces mode; `--context GROUP` opens a group first. Missing,
  hidden, locked or closed-group picks and `snap` are refused with exit code 2.
  JSON adds the remapped `faces`, `brushGeometrySha256`, `retiredFaces` and
  `parentBrushesSelected: false` for chained edits.
- [x] `map select-components <map> --set brush:0/face/4 --operation convert
  --to vertex` queries component selection without changing geometry. Convert
  supports `--coverage complete|touching`; Grow/Shrink support `--steps 1..256`;
  Connected follows the entire object graph. Patch points use four grid
  neighbours and remain vertices when converting brush modes. Repeat `--set`
  for up to 256 sets; same-kind sets on one object are merged, and stale IDs
  fail atomically. JSON includes sorted component sets, counts and replayable
  `selectors` for `map transform-components`. No `--output` is accepted.
- [x] `map add-patch` to add a Quake III plane, cylinder, square cylinder, cone,
  sphere, bevel or end cap using `--shape`, `--size x,y,z`, `--origin x,y,z`,
  `--texture`, and optional grid dimensions; `--axis x|y|z` turns it,
  `--segments N` (3 to 15) sets the spans round a cylinder, cone or sphere and
  `--bands N` a sphere's spans from pole to pole.
  `map edit-patch` selects `--patch <id>` and zero-based `--point row,column`
  values, with `--delta`, `--grid`, `--uv`, `--texture`, `--subdivide rows|columns`,
  or `--invert`. Both require an input map and `--output` and support `--dry-run`,
  `--overwrite`, and `--json` through the existing map save service.
- [x] `map patch-matrix` to change the control grids of repeatable `--patch`
  ids as Radiant's Patch menu does, with `--op insert-columns`, `add-columns`,
  `insert-rows`, `add-rows`, `delete-first-columns`, `delete-last-columns`,
  `delete-first-rows`, `delete-last-rows`, `transpose`, `invert`,
  `redisperse-columns`, `redisperse-rows`, `smooth-columns` or `smooth-rows`,
  as one undo step; JSON reports the changed patches and their grids.
- [x] `map thicken-patch` to give repeatable `--patch` ids a back
  `--thickness` units behind them (negative: in front), along each point's
  normal or `--axis x|y|z`, with walls along the open edges unless
  `--no-seams`, as NetRadiant Custom's Thicken does; the new selectors are
  reported as `created`.
- [x] `map add-thing` to add a thing to a Doom or Hexen map by DoomEd number
  (`--type`), at `--origin x,y`, facing `--angle`, on every skill.
- [x] `map snap` to move objects onto a `--grid` (16 by default), each by its
  own amount; the moved selectors are reported as `snapped`.
  Owners and selected children move once as assemblies. Doom/Hexen line/sector
  selectors expand to unique boundary vertices; binary fractional grids and
  collapsed lines are refused. UDMF retains fractional grids. Brush textures
  lock unless `--texture-lock off` is supplied.
- [x] `map rotate` to turn objects by `--turns` quarter turns (negative for the
  other way) about `--axis x|y|z` through their centre. Quarter turns are
  exact, so four of them write the source back unchanged.
- [x] `map flip` to mirror objects along `--axis x|y|z` through their centre,
  faces keeping their outward winding. Doom/Hexen/UDMF supports X/Y geometry
  and things; affected linedefs reverse endpoints and retain native side ownership,
  offsets, flags and actions. `--connected` explicitly expands vertices/lines/sectors
  through shared vertex IDs, preserving selected things. An attached partial
  geometry selection is refused without this expansion; Doom Z reflection remains
  unsupported. Geometry invalidates nodes; unrelated WAD entries are retained.
  JSON includes `connectedGeometry` and reports `textureLockPolicy: native-offsets`
  for Doom/Hexen; dry runs perform validation without output. Shared map save
  reports expose a structured `staleLumps` array for compiler automation, alongside
  localized warnings. Obsolete node payloads are cleared on geometry saves;
  `map inspect --json` reports persistent `nodeBuild` state after reopening.
- [x] `map apply-texture` to put one `--texture` on every face of the given
  brushes (a brush entity's included) and on the given patches; the count is
  reported as `applied`.
- [x] `map paint-material <map> --target <selector> --texture <name>
  --output <path>` paints exact surfaces with repeatable targets. Selectors are
  `face:brushId:faceNumber` (one-based face number), `patch:id`,
  `side:id:upper|lower|middle`, or `sector:id:floor|ceiling`. WAD input requires
  `--map-name`; text maps reject that option. `--dry-run` and `--overwrite`
  follow the shared map save service; even a dry run requires an output path.
- [x] `map sample-material <map> --target <selector> [--map-name <name>]`
  reads one surface material using the same selectors and optional JSON output.
  It requires exactly one target and rejects texture, output and write options.
- [x] `map clip`, `map hollow`, and `map carve` list any selected brush they
  leave as it was, with the reason, as `skipped` (and "Left as it was:" lines
  in text output), so a partly done operation never passes for a whole one.
- [x] `map split-linedef` and `map flip-linedef` for Doom and Hexen maps
  (`--map` names the map in the WAD): a split puts a vertex at the linedef's
  middle and gives the second half its own copies of the sides; a flip swaps
  the ends, and the sides of a two-sided line. The resulting linedefs are
  reported as `linedefs`. Geometry saves clear obsolete node data and report
  the affected names in `save.staleLumps`; reopening retains the rebuild requirement.
- [x] `map draw-sector` to draw a sector on a Doom or Hexen map from
  `--points "x,y x,y x,y"`, its corners in order: a corner on a vertex joins it,
  one on a linedef splits it, an edge along a linedef shares it, and the rest
  become new linedefs, two-sided inside a sector and walls in the void. The new
  sector is reported as `sector` and the linedefs it added as `newLinedefs`; a
  shape that crosses a linedef or itself is refused with exit code 1.
- [x] `map connect` (`map connect-entities`) connects the selected owners to the
  last one. The default keeps multi-source, destination-first naming and `t<N>`
  allocation. `--profile netradiant-custom` requires two distinct owners, uses
  source-name priority and allocates a classname-based name in the NRC namespace.
  `--kill-target` uses that two-object policy with `killtarget` for Quake, Nexuiz
  or Xonotic; `--game` overrides built-in map inference. JSON includes
  `targetname`, `property`, `game`, `profile`, `connectionMode`, resolved source
  and target entities, and the directly changed count. Save As, dry run,
  selection validation, locks and cancellation share the GUI service. Repeated
  selectors count once at their last occurrence; unknown/repeated singleton
  options and ambiguous inputs are rejected.
- [x] `map shift-sectors` to raise or lower the `--object sector:N` sectors'
  `--field floor`, `ceiling`, or `light` `--by` a whole number (negative
  lowers), within the lumps' ranges, and `map gradient-sectors` to spread a
  field evenly over three or more sectors from the first given to the last;
  both report `changed` and `field`, and neither marks the nodes stale.
- [x] `map make-door` to make the `--object sector:N` sectors doors, as Doom
  Builder's Make Door does, with `--door-texture` (BIGDOOR2), `--track-texture`
  (DOORTRAK), an optional `--ceiling-flat`, and `--keep-offsets`; the count is
  reported as `doors`.
- [x] `map join-sectors` and `map merge-sectors` to make the `--object
  sector:N` sectors one, the last given; merging also takes away the lines
  that divided them (not ones carrying a special or tag), reported as
  `removedLinedefs`, with the sector kept as `sector`.
- [x] `map merge-vertices` to join the `--object vertex:N` vertices into the
  last one given, dropping lines whose ends meet and stitching lines left over
  each other into one; the count is reported as `merged` and the vertex kept,
  under its id after renumbering, as `vertex`.
- [x] `map delete` takes Doom vertices, linedefs, and sectors as well as
  things: a sector takes its sides, a vertex between two linedefs joins them,
  and records nothing uses any more are dropped and the rest renumbered.
- [x] `map carve` to carve the `--object` brushes out of every other brush they
  overlap, as CSG subtraction does; the carving brushes stay, and the number of
  brushes cut is reported as `carved`.
- [x] `map add-shape <map> --shape box|wedge|cylinder|cone|sphere|arch|ring|stairs|room
  --texture <name> --output <path>` adds a shape filling `--mins x,y,z` and
  `--maxs x,y,z`, or replaces the `--object brush:N` world brushes with one
  filling their bounds, as the Levels Shapes tab does. Each shape reads only its
  own settings: `--axis x|y|z`, `--sides`, `--bands`, `--thickness`, `--arc`,
  `--start`, `--steps` and `--rise auto|+x|-x|+y|-y`; any other is a usage
  error. Arches, rings, stairs and rooms are several brushes, one undo step in
  the editor. JSON reports `shape`, the new `brushes` and the `replaced` ones.
- [x] `map align <map> --object brush:3 --object brush:4 --axis x|y|z --edge
  min|centre|max --output <path>` lines the objects up on the minimum, centre or
  maximum of their combined bounds, as Hammer's Align Objects does: entities
  with their brushes, world brushes and patches, and Doom things (no Z). One
  undo step in the editor; reports `moved`.
- [x] `map replace-key <map> --key name --find value --replace value [--partial]
  [--object entity:N ...] --output <path>` replaces a key's value on every
  entity whose value matches `--find` (the whole value, or with `--partial`
  every occurrence within it), or only on the `--object` entities, as one undo
  step; the key matches in any case. Reports `replaced`.
- [x] `map shear <map> --object brush:3 --axis x|y|z --along x|y|z (--factor f |
  --angle degrees) [--about value] [--texture-lock on|off] [--allow-valve220] --output <path>`
  slants the objects about their centre, or with `--about value` about the line at
  that coordinate along `--along`, as TrenchBroom's shear tool does:
  points slide along `--axis` by the factor times their distance along
  `--along`. Doom maps shear in X and Y only.
- [x] `map curve-linedefs <map> [--map-name MAP01] --object linedef:4 --segments N
  --bulge units --output <path>` bends Doom linedefs into N pieces along an arc
  bulging towards their front sides (negative: the back), as Doom Builder's
  curve mode does; texture offsets carry on along the pieces. Reports `curved`
  and the new `pieces`; rebuild the nodes before playing.
- [x] `map make-sector <wad> [--map-name MAP01] --at x,y --output <path>` makes a
  sector of the existing lines around the point, islands of lines inside it
  included, as Doom Builder's Make Sectors mode does; a point outside every
  closed shape of lines is refused. Reports `sector`; rebuild the nodes before
  playing.
- [x] `map align-walls <wad> [--map-name MAP01] --sidedef N [--part upper|middle|lower]
  [--axes x|y|xy] [--width texels] [--object linedef:N ...] --output <path>` lines
  up the textures of the walls joined to a side that show the same texture, as
  Doom Builder's auto-align does: X offsets run on by each wall's length (within
  `--width` when given) and Y offsets follow the Doom renderer's pegging, so rows
  stay level as floors and ceilings change. `--object` keeps the walk to those
  linedefs. Reports `aligned`. Binary Doom and Hexen maps.
- [x] `map draw-stairs <wad> [--map-name MAP01] --from x,y --to x,y [--steps N]
  [--step-height units] [--rise auto|+x|-x|+y|-y] --output <path>` draws a row
  of step sectors across the footprint, each floor higher than the last, and
  `map draw-grid <wad> --from x,y --to x,y --columns N --rows N --output <path>`
  cuts it into a grid of sectors, as Doom Builder's stair builder and grid
  drawing do, through Draw Sector's service. Both report the new `sectors`.
- [x] `map region <map> (--mins x,y,z --maxs x,y,z | --object ...) [--start
  x,y,z] [--texture name] --output <path>` writes a region of a Quake-family map
  as a map of its own, as Radiant's regions and Hammer's cordon do: world
  brushes and patches touching the box, point entities inside it and brush
  entities touching it stay; the box is sealed by six brushes (default
  `common/caulk`) and gets an `info_player_start` at `--start` (or its middle)
  when it has none. `map cordon` is an alias. Reports `kept`, `removed`,
  `sealBrushes` and `playerStartAdded`.
- [x] `map intersect` to replace the `--object` brushes (two or more, of one
  entity) with the one brush where they all overlap, as TrenchBroom's CSG
  Intersect does; each face takes the texture of the face it came from. Brushes
  that do not overlap fail and write nothing; the result is reported as
  `result`.
- [x] `map tie-entity <map> --object brush:12 --class func_door [--key k=v]...
  --output <path>` makes brushes and patches (or the brushes of selected brush
  entities) into a new brush entity, the way Radiant's entity menu and Hammer's
  Tie to Entity do; an entity left without brushes goes. `map move-to-world`
  gives the `--object` brush entities' brushes back to worldspawn. Both are one
  undo step in the editor and report `entity` or `moved`.
- [x] `map create-entity <map> --class <classname> [--definitions <path>]
  (--origin x,y,z | --camera x,y,z | --plane-point ... | --ray-origin ... |
  --object brush:12...) [--key k=v]... [--context <layer|group>] --output
  <path>` is TrenchBroom's generated Create <class> command: the class must be
  a point or brush class of the definitions (or the map's game's built-in
  ones). A point class goes at the exact origin or is placed against the map
  through `core/level_paste.h`'s cursor placement with optional `--grid`; a
  brush class takes the `--object` brushes and patches as `map tie-entity`
  does. One undo step through `core/level_entity_create.h`, shared with the
  Levels page's Create commands; reports `className`, `kind`, `entity`,
  `commandId`, `origin`/`placement` or `brushes`/`patches`, and refuses
  undefined classes, locked, hidden or out-of-group brushes and a locked layer
  (exit 4) without writing.
- [x] `map move-to-entity <map> --object brush:12 --object brush:7 [--target
  last|first|entity:N] --output <path>` moves the `--object` brushes and patches
  (and those of given brush entities) into an existing entity, which keeps its
  keys: by default the entity of the last `--object`, as NetRadiant Custom's
  Move Primitives to Entity (Ctrl+M) does. Entities left empty go; brushes keep
  their ids, groups and visibility. `map move-to-world --selected-only` moves
  only the given primitives to worldspawn (NRC Ungroup Primitives), and
  `map detail --structural --policy trenchbroom` is TrenchBroom's Make Structural,
  which also takes the brushes out of brush entities. `map explode-entity <map>
  --object entity:4 [--rename-targetnames] --output <path>` gives every brush and
  patch of the given brush entities an entity of its own with identical keys, as
  bobToolz's Explode Entity does. Each is one undo step through
  `core/level_entity_ownership.h`, reports `target`, `moved`, `removedEntities`,
  `createdEntities` or `movedToWorld`, and refuses locked or linked content
  without writing.
- [x] `map entity-colour <map> --object entity:3 [--object brush:12]...
  --colour "r g b"|#rrggbb | --normalise [--each] [--where <query>] [--key
  _color] --output <path>` writes the `_color` of the given entities and of the
  entities holding the given brushes and patches, as NetRadiant Custom's Set
  Entity Colour (K) and Normalise Entity Colour do: one colour in each key's own
  0-1 or 0-255 form, or the last object's colour scaled so its brightest
  channel is 1 (black becomes white); `--each` normalises every entity's own
  colour. World brushes never colour worldspawn and hidden entities are left
  out. One undo step through `core/level_entity_colour.h`, shared with the
  Levels page and the Inspector's colour swatches; reports `mode`, `value`,
  `source`, `entities`, `changed`, `skipped`, `worldPrimitives` and `hidden`,
  and refuses locked content or a missing colour without writing.
- [x] `map select-region <map> --object brush:12 --mode
  inside|touching|complete-tall|partial-tall [--axis x|y|z]` queries profile-specific
  selection through `--policy bounds|trenchbroom|radiant`. An optional `--output`
  writes the prepared result, including consumed source volumes; `--dry-run`
  validates without writing. `--axis` is the projection axis for Tall modes.
  See [Select by brush volume](manual/levels.md#select-by-brush-volume).
- [x] `map filters <map> [--off kinds] [--saved] [--invert] [--objects] [--package path]` lists the
  Levels view filters the map's format offers, with counts, shown state and the
  toggling command, through `core/level_view_filters`, and reports the objects a
  switched-off list (the View tab's saved one with `--saved`) would hide, after
  Invert Filters with `--invert`, plus the caulk faces the camera leaves out one
  at a time (`faceCount`, `filteredFaces`). `--package` reads a Quake III map's
  shader scripts through the material library for the material kinds
  (Translucent, Liquids, Sky, Clip and its parts, Bot Clip, Nodraw) and reports
  them as `shaders`. Nothing is written. See the
  [view filters CLI manual](manual/cli.md#query-view-filters).
- [x] `map camera-focus <map> [--object selector...] [--level]` reports where
  **Level Camera** and **Focus Camera on Selection** would put the 3D camera,
  through `core/level_camera_focus`: `--camera-yaw`, `--camera-pitch`, `--fov`,
  `--aspect` and `--offset` (free-focus wheel notches) describe the camera,
  `--camera-position` where it stands when nothing is focused, and `--context`
  the open group. Hidden objects are refused; nothing is written. See the
  [camera focus CLI manual](manual/cli.md#query-a-level-camera-focus).
- [x] `map visibility <map> --mode hide|show-all|isolate` queries temporary
  visibility using repeated `--object` and `--hidden` selectors and optional
  `--context`. JSON reports explicit/effective hidden selectors, selection and
  source hash; no map or undo session is written. See the
  [visibility CLI manual](manual/cli.md#query-temporary-level-visibility).
- [x] `map select-type <map> --object brush:12` queries all selected owner
  classes. `--material` supplies the material fallback or face query source;
  `--mode faces` returns stable brush-face sets and component selectors without
  changing object picks. Vertex/edge modes are no-ops. `--context` and repeated
  `--exclude` share the selection boundaries. JSON reports matched classes,
  fallback, component-change intent, selectors and source hash; no map is
  written. See [Select all of a type](manual/levels.md#select-all-of-a-type).
- [x] `map select-connected <map> --object patch:2` adds one hop of direct
  incoming/outgoing `target` and `targetname` matches to the supplied selection.
  Brush and patch owners supply seed names; unanchored entities can match.
  `--context` and repeated `--exclude` share the selection boundary service.
  JSON includes original/result selectors, added count, exact connection keys
  and `transitive: false`; the query never writes a map. See
  [connected selection](manual/levels.md#select-connected-entities).
- [x] `map select-type <map> --object brush:12` queries NRC Select All of Type
  using exact entity classes or the `--material` fallback. `--mode faces`
  selects matching brush faces; vertex/edge mode keeps the existing selection.
  Optional `--context` and repeated `--exclude` restrict the query; no map is
  written. Face results include zero-based component `selectors` and one-based
  `surfaceTargets`, directly reusable with surface copy/paste and material
  **--target** options.
- [x] `map select-siblings <map> --object brush:12` queries selectable siblings
  through brush-entity ownership, scene layers and closed/open groups.
  `map select-lines <map> --line "42,57"` resolves positions in the loaded source
  text, including brush-entity property fallback. Both accept `--context` and
  repeated `--exclude`; JSON returns `selection`, `sourceSha256`, `queryOnly`
  and `mapWritten`. They never write a map. See the
  [source/hierarchy selection manual](manual/levels.md#select-by-hierarchy-or-source-line).
- [x] `map detail <map> --object brush:3 [--structural] --output <path>`: with
  Quake II or Quake III face flags, sets or clears the detail content bit on
  every face; in Quake maps, ties the brushes to `func_detail` or moves
  `func_detail` brushes back to the world. Reports `changed` and `faceFlags`.
- [x] `map drop-to-floor <map> --object entity:7 [--definitions <path>]
  --output <path>` moves point entities straight down onto the highest brush or
  patch top beneath them, keeping each class's definition height above the
  floor (from `--definitions`, else the built-in catalogue of the map's game).
- [x] `map merge-brushes <map> --object brush:0 --object brush:1 --output <path>`
  joins an exact convex union using shared GUI/core validation and undo.
  Gaps, cavities, concavity, mixed owners and mixed dialects fail. Conflicting
  materials, UVs or flags require repeated `--face-source outputFace=brushId:sourceFace`
  choices from the JSON report; all IDs are zero-based. Unresolved conflicts
  write nothing and return a validation failure with `merge.faces` candidates.
  Supports `--dry-run`, `--json` and explicit `--overwrite`. See
  [Brush Merging](LEVEL_EDITOR.md#brush-merging).
- [x] `map cap-patch <map> --patch ID --boundary first-row --boundary last-row
  --output <path>`: add caps to one or more distinct curved boundaries, in the
  source entity. Boundary tokens also accept first-column and last-column.
  Optional `--texture`, `--uv planar|boundary`, `--units-per-tile N` (default 128),
  `--center X,Y,Z` (one boundary) and `--invert` match GUI authoring. Planarity,
  angular order and source ownership are validated before any edit. JSON includes
  a `cap` report. `--dry-run` writes nothing; existing outputs need `--overwrite`.
  Usage returns 2, geometry/save failure 4, source-read failure 1 and success 0.
  See [Patch Caps](LEVEL_EDITOR.md#patch-caps).
- [x] `map stitch-patches <map> --first patchId:boundary --second patchId:boundary
  --output <path>` joins two patch edges with exact grid refinement and one shared
  document operation. Boundary tokens are `first-row`, `last-row`, `first-column`
  and `last-column`. Optional `--max-gap N` (default 8), `--direction
  auto|forward|reversed`, `--target first|second|average`, `--uv
  preserve|first|second|average` and `--match-tangents` match the GUI. JSON reports
  both IDs, resulting grids, direction, maximum gap/movement and warnings.
  Supports `--dry-run`, `--json` and explicit `--overwrite`; validation errors
  and save errors return 4, usage errors 2 and source-read failures 1. See
  [Patch Stitching](LEVEL_EDITOR.md#patch-stitching).
- [x] `map hollow` to turn brushes (`--object` brushes or brush entities) into
  walls `--thickness` units thick, one per face, each textured like the face
  it grew from; the new selectors are reported as `walls`. `--mode overlap`
  (the default) lets the walls meet at the edges, `touch` cuts them back so
  they meet without overlapping, and `room` builds them outside the brush, as
  NetRadiant Custom's Make Room does. `--caulk` (the game's caulk) or
  `--caulk-texture NAME` puts caulk on every wall face but the room's, and
  repeated `--open-face face:brushId:faceNumber` leaves faces open, the walls
  beside them stopping flush; JSON adds `caulkTexture`, `caulkedFaces`,
  `wallCount`, `openFaces` and `openFaceCount`
  (`cli/level_hollow_options.cpp`, `core/level_hollow.h`).
- [x] `editor make-room` to read or change the `levels/makeRoom/*` options
  that **Make Room Options…** edits and the Levels editor's **Make Room**
  uses: repeat `--set` with `caulk=on|off`, `caulkTexture=NAME` (nothing after
  `=` for the game's own caulk) and `openPickedFaces=on|off`; `--reset`
  restores NetRadiant Custom's defaults (caulk on, picked faces open) and
  `--dry-run` previews. Both surfaces validate through
  `updateLevelRoomPreferences` and `levelRoomPreferencesProblem`; JSON reports
  `values`, `applied` and `dryRun`. A bad field, value or material name returns
  2 and a settings write failure 1. `map hollow` does not read these options;
  it takes them as flags.
- [x] `map hollow --mode diagonal|extrude|wrap|pull`, `--keep-inner`,
  `--exclude-face`, `--only-face` and `--exclude-facing x|y|z|dx,dy,dz`: the
  Levels **CSG Tool**'s hollows and face policies, after NetRadiant Custom's
  CSG Tool (`hollowLevelMapSelection`, `levelCsgExcludedFaces`,
  `levelMapFacesFacingAxis`); JSON adds `keepInner` and `innerKept`.
- [x] `map shrink-brushes` and `map expand-brushes` move every face of the
  `--object` brushes `--thickness` units in or out, except those
  `--exclude-face`, `--only-face` and `--exclude-facing` leave alone, as one
  undo step (`offsetLevelMapSelectionFaces`); `--context`, `--dry-run` and
  `--overwrite` as for other map commands. JSON reports `operation`,
  `thickness`, `offset`, `changed`, `movedFaces`, `excludedFaces` and
  `skipped`; usage errors return 2, a shrink no brush can take 1 and save
  errors 4 (`cli/level_hollow_options.cpp`).
- [x] `map snap-vertices` to round every vertex of the `--object` brushes, or
  of entities' brushes, onto `--grid` and rebuild them, as TrenchBroom's Snap
  Vertices does; a brush the grid would collapse is left as it was and listed
  with its reason under `skipped`.
- [x] `map nudge <map> --object brush:12 --direction depth-up --view front --output <map>`
  moves the `--object` selection, or repeated `--set` components, `--steps`
  grid steps of `--grid` units towards `--direction left|right|forward|back|
  depth-up|depth-down|z-up|z-down`, resolved as the Levels **Nudge** commands
  resolve it for a `--view top|front|side` plan view or a camera named by
  `--camera-yaw` and `--camera-pitch` (core/level_nudge.h). Objects keep
  `--texture-lock on|off`; components take `--uv-lock off|on|fit`,
  `--allow-collapse`, `--allow-valve220` and `--context GROUP`. Doom maps
  nudge along X and Y only. One undo step; `--output`, `--dry-run`,
  `--overwrite` and `--json` (`axis`, `delta`, counts, save fields).
  With `--duplicate` the `--object` selection is copied first and the copies
  move, as the Levels **Duplicate and Move** commands do (TrenchBroom's
  Ctrl+arrows): one duplicate at the nudge offset, the copies listed under
  `copies`.
- [x] `map move <map> --object brush:3 --camera 128,0,64 --grid 16 --output <map>`
  centres the repeatable `--object` selection on the camera position by Paste
  to Camera's translation snapped to `--grid` (default 0, none), sizing point
  entities from optional `--definitions`, as **Move to Camera** does
  (core/level_paste.h): one move that keeps object identities and source
  lines. `--texture-lock on|off`, `--allow-valve220` and `--context GROUP`;
  Quake-family maps only. JSON reports `camera`, `delta`, `grid` and the save
  fields.
- [x] `map repeat-transforms <map> --object brush:4 --rotate z,15 --translate 64,0,0 --output <map>`
  copies the `--object` selection where it is and applies NetRadiant Custom's
  repeatable transforms to the copies about their centre or `--pivot x,y,z`,
  or applies them to repeated `--set` components (core/level_repeat.h):
  `--scale x,y,z`, `--shear AXIS,ALONG,FACTOR`, `--rotate AXIS,DEGREES` and
  `--translate x,y,z`, in that order. Objects keep `--texture-lock on|off`;
  components take `--uv-lock off|on|fit` and `--allow-collapse`;
  `--allow-valve220` and `--context GROUP`. Quake-family maps only. One undo
  step; `--output`, `--dry-run`, `--overwrite` and `--json` (`transforms`,
  `mode`, `pivot`, `copies`, counts, save fields).
- [x] `map view-transform <map> --object brush:12 --transform roll-clockwise --view front --output <map>`
  turns the `--object` selection 90 degrees or flips it as the Levels
  page's view-relative commands do: `--transform roll-clockwise|
  roll-counterclockwise|yaw-clockwise|yaw-counterclockwise|pitch-clockwise|
  pitch-counterclockwise|flip-horizontal|flip-vertical`, resolved to one world
  axis for a `--view top|front|side` plan view or a camera named by
  `--camera-yaw` and `--camera-pitch` (core/level_quick_transform.h). The
  pivot is the exact centre, `--pivot x,y,z`, or the centre rounded with
  `--pivot-grid N` (to N for turns, N/2 for flips, as the TrenchBroom profile
  does). `--texture-lock on|off`, `--allow-valve220` and `--context GROUP`;
  Doom maps flip along X or Y and turn about Z only. One undo step;
  `--output`, `--dry-run`, `--overwrite` and `--json` (`transform`, `frame`,
  `axis`, `degrees`, `mirror`, `pivotPolicy`, `pivot`, save fields).
- [x] `map round-to-grid <map> --object brush:12 --grid 8 --output <map>`
  rounds each brush vertex, patch control point and point-entity origin of the
  `--object` selection onto the grid on its own, as Radiant's Snap To Grid
  does, through the service behind **Round Selection to Grid**. Brush entities
  round their brushes and patches and keep their origin key; hidden objects and
  brushes the grid would collapse are left as they were and listed under
  `skipped`. Repeated `--set brush:ID/vertex|edge|face/IDs` or
  `patch:ID/vertex/IDs` rounds only those components instead, all or nothing,
  with optional `--allow-collapse`. `--uv-lock off|on|fit`, `--allow-valve220`
  and `--context GROUP` follow `map transform-components`. One undo step;
  `--output`, `--dry-run`, `--overwrite` and `--json` (counts, `fittedFaces`,
  `skipped`, save fields) as other map edits.
- [x] `map clip` to cut brushes (`--object` brushes or brush entities) with a
  plane: `--axis x|y|z --at <units>`, whose front is up the axis, or
  `--points "x,y,z x,y,z x,y,z"`, whose front is (b - a) x (c - a).
  `--keep back|front|both` (`below` and `above` read naturally with
  `--axis`) chooses the part kept; each cut brush gains a face on the plane in
  its own format, and the new selectors are reported as `pieces`.
- [x] `map resize` to fit objects to new bounds (`--mins x,y,z` and
  `--maxs x,y,z`) or a new `--size x,y,z` from their lower corner. Points and
  planes map from the old box to the new one; source texture parameters remain
  unchanged by default. `--texture-lock on` stretches with the brush and
  `--allow-valve220` permits required conversion. Two numbers leave z as it is.
  `--factors x,y,z` scales about the centre or `--pivot` instead; a negative
  factor mirrors.
- [x] `map replace-texture` to replace every use of `--from` with `--to` on
  brush faces, patches, or Doom wall textures and flats, across the map, only
  the `--object` selectors given, or only repeated `--face` picks (with
  optional `--context`); the count is reported as `replaced`, hidden and locked
  uses as `skipped`, with `scope` and `targets`. Levels' modeless **Find and
  Replace Textures…** shares the core service (`core/level_material_find.h`),
  and `map select-textured --within-selection` is its Find.
- [x] `map compile-plan` for profile-backed compiler command review.
- [x] `map render` for deterministic SVG pictures of Doom and Quake-family maps,
  usable from CI and documentation without a display; `--leak` adds a compiler
  leak trail and `--links` adds entity target links. Both are opt-in, so a
  picture made without them stays byte-identical.
- [x] `map textures` to check every texture a map references against the
  textures a package or folder actually provides, separating genuinely missing
  names from the ones the engine supplies itself; `--uses <texture>` lists the
  objects that use one texture, as the Levels Textures tab selects them.
  Repeated `--root` and expanded search paths admit at most 64 folder roots with
  shared entry/metadata/fingerprint budgets. Over-limit or unreadable indexes
  publish no partial catalog. Missing roots allow diagnostic results from the
  available folders, with `textures.sourceIndexComplete: false` in JSON. An
  incomplete source audit returns exit 4 even for a map with no texture references.
  `--package` also accepts a saved `.vibepackage` draft and inspects its current
  planned contents. JSON includes `textures.complete` and `textures.cancelled`;
  failed shader reads/parsing, collection limits or requested image decoding
  make the audit incomplete and return exit 4. Known unreadable image rows are
  unresolved; shader declarations precede same-named images, matching dependency
  export. Shader input is capped at 64 MiB total and 16 MiB per script, with at
  most 128 diagnostics plus a truncation notice. Partial counts cannot prove
  complete resolution. `--no-decode` still checks references and shader data
  without decoding images.
- [x] `package list --where <query>` to list only the entries a query
  matches, with the Packages filter's syntax over each entry's `path`, `name`,
  `ext`, `folder`, `type`, `storage`, `size`, `packed`, and `kind`. Sizes take
  suffixes counted in 1024s (`--where "ext=wav size>1mb"`).
- [x] `map find` to list the objects a query matches, with the same syntax as
  the Levels Objects filter: `key=value`, `key:text`, `key!=value`, `key<n`,
  `key>n`, and plain words, all of which must hold (`--where "class=light
  light>200"`). Entities answer to their own keys, brushes and patches to
  `texture`, Doom things to `type` and `name`, linedefs to `special` and `tag`,
  and sectors to `floor`, `ceiling`, `light`, and `tag`. `--number 0,12`
  instead finds the object a compiler names as "Entity 0, Brush 12": entities
  and, within each, brushes and patches together are counted from 0 in file
  order, and an entity number alone names the entity.
- [x] JSON output for map statistics, entities, brushes, textures, validation,
  preview lines, selection, properties, and save reports.

### Entity Definitions
- [x] `entity definitions` to load Radiant `.def`, QuakeC `/*QUAKED` blocks,
  Valve `.fgd` (including `@include` and `base()` inheritance) and Quake III
  `.ent` catalogues, and list the classes they declare. `--class <classname>`
  narrows the text output to that one class summary, and adds a `className`
  field beside the catalogue in JSON.
- [x] `entity validate` to check a map's entities against a catalogue: unknown
  classnames, undeclared and mistyped keys, unknown spawnflag bits, point/brush
  misuse, and the `target`/`targetname` graph in both directions.
- [x] `--strict` on `entity validate` fails on warnings as well as errors.
  Without it only errors fail, because an unknown classname is a warning: a
  map may legitimately use an entity a mod ships no definition for, and
  `--strict` is for the build script that refuses that anyway.
- [x] `--no-recursive` keeps folder paths from being walked into subfolders.
  A folder contributes the files whose suffix is `def`, `fgd`, `ent`, or `qc`.
- [x] JSON output for the loaded catalogue and for the validation report.

Both commands resolve definition paths the same way, accumulating from every
source below before falling back:

- Repeatable `--definitions <path>`, `--definition <path>`, `--path <path>`,
  and `--paths <path>`.
- `--definition-paths "<a;b;c>"`, split on semicolons.
- Trailing positional paths: every token after the action for
  `entity definitions`, every token after the map path for `entity validate`.
- Only if all of those are empty, `--project-root <path>` falls back to the
  conventional per-project folders, in this order: `.vibestudio/definitions`,
  `definitions`, `defs`, `scripts`, `base/scripts`, `entities`. They are
  probed, not required to exist.

Each resolved path may be a definition file or a folder of them.

| Command | Exit | Condition |
| --- | --- | --- |
| `entity definitions` | 0 | The catalogue loaded. |
| `entity definitions` | 2 | No definition path resolved. |
| `entity definitions` | 3 | The catalogue reported an error, or `--class` named a class it does not declare. |
| `entity validate` | 0 | The report has no errors, and no warnings when `--strict` is passed. |
| `entity validate` | 1 | The map path exists but the map could not be loaded. |
| `entity validate` | 2 | No map path, or no definition path resolved. |
| `entity validate` | 3 | The map path does not exist, or the catalogue declared no classes. |
| `entity validate` | 4 | The report has errors, or `--strict` and the report has warnings. |

### Models

- [x] `model assembly` inspects or edits linked `.assembly.json` sources and
  explicitly bakes composed poses or sampled animations. Independent playback settings, nested tags,
  package/draft references, input fingerprints, source protection and dry runs
  share the GUI services. `--operation inspect|add|update|remove|bake|bake-animation`, part fields
  and worked examples are documented in [Model Assemblies](MODEL_ASSEMBLY.md).
- [x] `model assembly --operation bake-animation` requires `--frames` and
  `--sample-fps`, with optional `--time` and `--clip-name`. It writes a sampled
  `.mesh.json`, MD2 or MD3 with the same storage, topology, cancellation and
  destination guards as the GUI. JSON reports exact sampling times and export
  losses. Mesh sources retain clip FPS; native game timing stays external.
- [x] `model edit --operation set-clip-fps --clip N --clip-fps RATE` and optional
  `add-clip --clip-fps RATE` persist zero (unspecified) or 0.001–1,000 FPS in the
  editable source. `model animations` reports `framesPerSecond`; schema 6 stores
  positive rates and preserves optional MDL/collision metadata.
- [x] `model assembly --operation recoveries|recover|discard` shares verified
  assembly recovery storage with the GUI. Recover/discard require a reviewed UUID
  and SHA-256; recover writes a different source and retains unavailable links,
  while discard checks active-session locks. Both support read-only dry runs.
- [x] `model assembly --operation players --package <pk3|folder>` and
  `--new --from-player NAME` share the GUI's Quake III player listing and import:
  three package parts, the linked skin, native animation from the package's
  `animation.cfg` and the `player-review` options to publish it again. The package
  is never written.
- [x] `model recoveries [--directory <folder>]` lists bounded recovery headers.
  JSON distinguishes header validity from payload verification. `model recover
  <copy.vsmeshrecovery> --output <new.mesh.json>` verifies the payload and writes
  a new source, with `--dry-run` and JSON support. Existing files and original
  source paths are protected; `--overwrite` is rejected. Invalid contents return
  `4`, destination/read/write failures `1`, usage errors `2`, and success `0`.
- [x] `model repair-import <source> [--output new.mesh.json]` prepares explicit
  index/collapsed-face, unusable-normal and orphaned-seam repairs before strict
  editable admission. No output means read-only review; `--dry-run` requires an
  output and validates without writing. Existing files and changed inputs are
  protected, with no overwrite mode. Text/JSON report exact original indices,
  pose scope and normal fallbacks. JSON includes the input SHA-256 and groups
  changes by surface and normal pose. Usage errors return 2, IO failures 1, invalid or
  unsupported source data 4, and success 0. See [Repair Import](MODEL_MESH.md).
- [x] `model intersections <source> [--frame all|N] [--surface all|N]`
  inspects face crossings and coplanar overlaps across stored poses and surfaces.
  Both scopes default to all; a surface filter retains contacts with other
  surfaces. Shared boundaries and isolated point contacts are allowed. The
  command is read-only, reports text or JSON, and fails bounded inspections
  without a partial report. It does not inspect motion between stored poses.
  JSON includes `complete`, `findings` with exact pose/surface/face indices and
  `crossing` or `coplanar-overlap` kinds, plus workload counts. Scope `-1` means
  all. Findings return success; usage errors return 2, read failures 1, and
  invalid sources or exhausted limits 4. Unknown, repeated and write options
  are refused. See [geometric inspection](MODEL_MESH.md) for limits and contact rules.
- [x] `model topology <source> [--surface N]` lists canonical indexed edges and
  their incident faces as text or JSON. `model edit --operation split-edges
  --edges a:b,c:d` splits selected edges across every pose. `--operation weld`
  accepts vertices, edges, or faces with `--weld-distance`; seams stay protected
  unless `--merge-seams` is explicitly supplied. Both use document validation,
  dry-run, and normal source-save conflict protection.
- [x] `model topology` also reports duplicate-face groups, unused vertices,
  disconnected fans, winding conflicts, nonmanifold edges, boundaries and edge-
  connected face components. JSON preserves the edge list and adds `health`
  with exact component indices. Findings do not make inspection fail.
  `model edit --operation remove-duplicate-faces|remove-unused-vertices|split-disconnected-fans|split-nonmanifold-edges|orient-faces`
  repairs the whole `--surface` across every pose through the document service.
  Component selectors and a single-frame scope are rejected as usage errors.
  Nonmanifold splitting preserves existing two-face connections and copies
  affected endpoints into distinct face fans without deleting faces or moving
  any animation sample. Its deterministic cuts can create open boundaries.
  Normal dry-run/output/overwrite checks apply; unrepairable or unchanged
  candidates return validation failure without writing. See [mesh health](MODEL_MESH.md).
- [x] `model edit <source> --operation fill-boundary-loops --edges a:b,c:d
  --output <new.mesh.json>` expands each chosen boundary edge to its complete
  loop and fills the holes through the shared document service. `--surface N`
  defaults to 0; `--source-frame N` chooses the triangulation pose (default 0).
  Every animation pose must support the cap without new intersections on that
  surface. Existing positions, normals, UVs and material bindings remain intact.
  Use `model topology --json` to inspect endpoint pairs. Faces/vertices and
  single-frame scope are usage errors; normal dry-run and overwrite protections
  apply. Geometry failures return 4 without writing. See [boundary filling](MODEL_MESH.md)
  for conservative geometry checks and workload limits.
- [x] `model edit <source> --operation bridge-boundary-loops --edges a:b,c:d
  --output <new.mesh.json>` joins exactly two disjoint closed boundaries on the
  active surface, including unequal vertex counts. `--source-frame N` selects
  the alignment pose (default 0); `--bridge-twist N` shifts the second anchor
  from the closest pair and wraps by its loop length (default 0, range -1023 to
  1023). Both controls accept one integer value; twist is exclusive to bridging.
  The shared service checks every stored pose before committing and retains
  existing attributes. Use `--frame all` or omit it, and select only edges.
  Dry-run and overwrite protection use the normal document path. Explicit
  `--overwrite` permits a fingerprint-checked save back to the editable input.
  Usage errors return 2 and invalid geometry returns 4 without
  writing. See [boundary bridging](MODEL_MESH.md) for candidate search, finishing
  and geometry/workload limits.
- [x] `model uv <source> [--surface N]` inspects UV islands, their bounds,
  component indices and marked seams without writing. Surface indices are
  zero-based and default to 0. Text reports summarize islands; JSON includes
  their face/vertex indices, bounds and seam edge pairs. Usage errors return
  `2`, source-read failures `1`, invalid sources/topology `4`, and success `0`.
- [x] `model uv-density <source> --texture-size WIDTHxHEIGHT` measures complete
  UV islands against the selected reference pose. `--target` with `--output`
  previews or writes a density adjustment; `--mode individual|group` chooses
  per-island or combined scaling. See [texture density](MODEL_UV_DENSITY.md)
  for face expansion, validation, dry-run and output requirements.
- [x] `model edit --operation uv-mark-seams|uv-clear-seams --edges a:b` edits
  source seam marks. `uv-detach --faces ...` splits shared face corners without
  moving UVs or poses. `--uv-islands` expands component selection for UV
  transform/project/detach/unwrap/pack. Transform/project accept `--uv-pivot-mode
  origin|selection|custom|islands`, `--uv-pivot u,v` for custom pivots, and `--uv-grid N`
  for offset snapping. These use the same candidate validation/history semantics
  as the GUI. New editable sources use schema 3; schemas 1 and 2 remain readable.
  `islands` rotates/scales each complete selected chart around its own bounds;
  projection uses the chosen reference pose. Partial charts or mixed components
  return validation failure (`4`); `--uv-islands` explicitly expands components
  first. Shared corners split across every pose without changing geometry,
  normals or unselected UVs. Repeated pivot-mode flags and conflicting custom
  coordinates return usage error (`2`).
- [x] `model edit --operation uv-unwrap|uv-pack --faces all` generates or repacks
  a UV atlas through the same offline document service as the editor.
  `--uv-atlas-size N` selects a square (default 512); `--uv-atlas-size WIDTHxHEIGHT`
  selects a rectangle, for example `512x128` or `320x200`. Both axes accept
  32–4096 pixels. `--uv-padding N` defaults to 4 (0–64, less than one eighth of
  the smaller dimension). Repeated or malformed options are usage errors (2).
  `--frame N` chooses unwrap geometry;
  the default uses pose 0. All pose geometry remains intact. `--uv-islands`
  expands vertex/edge/face selection first. Dry-run validates without writing;
  normal output/overwrite/conflict rules apply. Unselected UVs remain fixed
  and are not packing obstacles. Atlas edits do not create/resize/repaint images
  or change MD2 export skin dimensions. Pixel-space proportions and ordinary
  source/history/recovery behavior are shared with the GUI.
- [x] `model edit --operation uv-pack-around --faces 0,1 --uv-islands` packs
  complete selected islands around fixed unselected faces and other surfaces
  sharing any normalized, nonempty material slot path. The existing atlas-size
  and padding flags apply. `--uv-pack-scale fit|preserve` defaults to uniform
  fitting; preserve keeps current UV scale and fails if it cannot fit. Islands
  keep orientation and relative density. Fixed UVs outside the 0–1 tile are
  refused. The deterministic search is bounded and does not prove optimal fit.
  Cancellation, refusal, dry-run, source protection, undo/recovery and native
  exports use the normal shared document contract. Repeated, unknown or
  unrelated scale options return usage error (`2`). Images are not rebaked.
  Atlas dimensions, padding and scale accept both `--option value` and
  `--option=value`; mixing the forms still counts as a duplicate.
- [x] `model tags <source> [--frame all|N] [--tag name]` reports named origins
  and local orientation axes without writing. Attachment edits add, duplicate,
  rename and delete identities in every pose; origin, orientation reset,
  move/rotate and pose copying use all/current-frame scope. `--tag` selects an
  existing attachment, `--tag-origin` supplies absolute coordinates and
  `--source-frame` selects the copy source. Rigid transforms share the mesh
  pivot/move/angle-snap service and reject scale. See [tag CLI](MODEL_MESH.md#cli).
- [x] `model materials <source> --package <archive-or-folder> [--palette <id>]`
  resolves each surface with the editor's bounded shader/image lookup. Text/JSON
  reports include source paths, dimensions, warnings and static-preview limits.
  Returns 0 when resolved, 1 for source-read failure, 2 for usage, 3 for an
  unavailable package, and 4 for invalid source or material problems. No writes.
- [x] `model slots <source.mesh.json>` lists ordered external bindings and shares
  set/insert/remove/move/replace/clear operations with the Surface review dialog.
  Edits require an explicit surface and output. Repeated `--material` values
  preserve exact order and duplicates for Replace. `model materials --surface N
  --material-slot N` previews an alternate without editing the source. See
  [Material Slots](MODEL_MATERIAL_SLOTS.md) for argument and write rules.
- [x] `model edit --operation transform --surfaces all|0,2` transforms complete
  selected surfaces, including unused vertices, around one pivot. The selector
  is exclusive with `--surface` and component selectors and cannot be repeated.
  Current/all-frame scope, snapping, pivots and atomic output rules are shared
  with the GUI. JSON reports sorted `selectedSurfaces`; normal sources omit
  selection. See [surface transforms](MODEL_SURFACES.md#select-and-transform-surfaces).
- [x] `model surfaces <source.mesh.json>` lists indexed geometry and material
  slots. `--operation rename|separate|move|duplicate|delete|join` uses the shared
  all-pose document service with explicit source, face and target selections.
  Different ordered material lists require `--adopt-target-materials` for Move
  or Join. Edits use `.mesh.json` output, `--dry-run` and normal overwrite/source
  protection. On skeletal models each surface lists `skinned`, edits report
  `skinProvenance`, and mixing skinned and unskinned vertices in one surface is
  refused. [Surface Authoring](MODEL_SURFACES.md) documents exact selectors,
  partition semantics, selection remapping, limits and exit codes.
- [x] `model skinning <source.mesh.json>` inspects skeletal influences. Its
  `--operation select|bind|rebind|unbind|prune` routes through shared model
  services; edits require `.mesh.json` output and support `--dry-run` and
  `--overwrite`. Binding accepts rigid, joint-distance or bone-distance methods;
  pruning accepts influence-count and threshold limits. See command help for
  surface/vertex selectors, weight filters and replacement controls.
- [x] `model weight-paint <source.mesh.json>` paints selected skeletal vertices
  using replace, add, subtract or smooth strokes, or replays `--stroke-file`.
  Joint locks, a remainder joint, strength and opacity control normalised
  weights. Edits preserve bind geometry and update retained poses atomically;
  `.mesh.json` output, `--dry-run` and `--overwrite` follow normal model saving.
  See registered command help for selectors and value limits.
- [x] `model tool <source.mesh.json> --tool <name> --output <new.mesh.json>` runs one
  edit-mode mesh tool through the same document service as the Mesh Editor:
  rotate-edges, merge, dissolve-vertices, dissolve-faces, poke, beautify,
  make-face, extrude-edges, inset, shrink-fatten, smooth, transform
  (proportional and mirrored), shade-flat, shade-smooth, auto-smooth, bisect,
  symmetrize, loop-cut, edge-slide, add (primitives), decimate, bevel-vertices, solidify
  and the UV projections uv-cube, uv-view, uv-cylinder and uv-sphere. Components use
  `--surface`, `--faces`, `--vertices` and `--edges`; each tool refuses options
  it does not take. `--dry-run`, `--overwrite`, text/JSON results and exit
  codes (2 usage, 1 read/write, 4 invalid edit, 0 success) match `model edit`.
  JSON includes the resulting selection for chaining, and on skeletal models a
  `skinProvenance` report of kept, interpolated, mirrored, rebound, merged and
  bound weights. Symmetrize on skinned surfaces takes
  `--joint-map detect|left=right,...` and `--keep-joint-names`; `add` takes
  `--new-surface NAME` and, on skeletal models, `--bind-joint NAME` or
  `--bind nearest-bones` (required on a skinned surface) and reports its
  `binding`. `model edit` adds the same `skinProvenance` report, and its frame
  operations refuse the bind frame and skeletal clip frames with exit code 4.
  See
  [Mesh Tools](MODEL_TOOLS.md) and
  [skin weights through mesh edits](MODEL_SKIN_PROVENANCE.md).
  [Edge Slide](MODEL_EDGE_SLIDE.md) takes `--edges` and exactly one of
  `--slide` (signed fraction) or `--distance` (reference-pose units), with
  `--frame all|N`, `--reference-frame` and optional `--angle` for quad pairing.
- [x] `model select <source> --select <operator>` is read-only: all, none,
  invert, linked, more, less, loop, ring, path, similar, non-manifold, loose,
  boundary, sharp, random, checker, side, facing and mirror, in face, vertex
  or edge mode. It prints the selected indices as ready-to-use `--faces`,
  `--vertices` or `--edges` values. Loop and ring without `--edge` grow every
  edge in `--edges` (or between two `--vertices`) through
  `selectModelEdgeLoops`, the service behind the editor's **Select Edge
  Loops/Rings of Selection**, and report refused seeds under `refused`;
  `--extend` gives the editor's result. See [Mesh Tools](MODEL_TOOLS.md#selection).
- [x] `model lod <source> --output <name.md3>` writes the base MD3 and Quake III
  detail levels `name_1.md3`, `name_2.md3` beside it (`--levels`, `--ratio`,
  `--target-triangles`, `--level-source LEVEL=PATH` for hand-made levels, or the
  source's stored levels; `--save-recipe` stores them; `--target` for the MD3
  delivery target, which sets how many levels the game loads), each generated
  level a quadric decimation of the previous one that keeps UV seams and every
  pose. `model lod --inspect <base.md3>` (or `--package P --entry E`) checks an
  existing set's frames, surface names and limits and exits 4 on an error. See
  [Model Detail Levels](MODEL_LOD.md). The whole set is prepared, checked against the target and
  reviewed before any file is written; levels that would land on the source are
  refused, existing files need `--overwrite`, and JSON reports each file's
  `committed` state, `exportNotes` and `exportOmissions`, with `existing`,
  `committed` and every omission once for the set. `--strict` exits 4, writing
  nothing, when a file would leave data out. See
  [Mesh Tools](MODEL_TOOLS.md#quake-iii-detail-levels).
- [x] `model targets [<source>]` lists the delivery targets (engines and source
  ports with their model limits and the sources they were read from), or checks
  a model against them, reporting every limit it exceeds with the surface, value,
  maximum and fix. `--format` and `--target` narrow the list; a named `--target`
  the model does not fit exits 4, and an id shared by several formats (`fte`,
  `ioquake3`, `q2pro`, `gzdoom`, `doom3`) needs `--format` to check a model
  (exit 2 without it). Issues name the faces or vertices concerned.
  `--project DIR` shows each format's target in a project with its origin, and
  `--save` / `--clear` keep or remove the project's choices and strict setting.
  `model build --target ID` runs the same
  preflight before writing (the project's saved target, else the format's
  default, when omitted) and exits 4 with `targetIssues`. See
  [Model Delivery Targets](MODEL_TARGETS.md).
- [x] `model prepare <source> --format <format> --output <new.mesh.json>` applies
  the fixes a delivery target's preflight suggests (split, join, decimate, fit,
  influences; `--skip` leaves kinds out) through the shared document edits and
  reports each change and what still blocks; `--dry-run` reviews only, and it
  exits 4 when limits remain. See
  [Prepare For Target](MODEL_TARGETS.md#prepare-for-target).
- [x] `model skin <source.mesh.json> --file <file.skin> --output <source.mesh.json>`
  applies complete Quake III surface-to-shader assignments through the shared
  document transaction. Package/folder/draft imports use `--package` with
  `--entry` or exact `--entry-index`; both together guard the selected path.
  Text/JSON reports include before/after mappings, unused bindings and ignored
  markers; JSON records the input hash and occurrence. `--dry-run` validates the
  edit and destination, and `--overwrite` is required for an existing output.
  Inputs are protected. Usage/protection errors return 2, read/write errors 1,
  invalid skin data/edits 4 and success 0. See
  [skin assignments](MODEL_MESH.md#quake-iii-skin-assignments).
- [x] `model skin-variants <source.mesh.json>` lists the named skin variants a
  source owns; `--operation add|rename|remove|set` with `--name`, `--to` and
  repeated `--surface-material SURFACE=MATERIAL` edits them through the shared
  document transaction (one undo step) and requires `--output .mesh.json`.
  `--dry-run` and `--overwrite` behave as for `model skin`.
- [x] `model skin-export <model> --output <file.skin>` writes the Quake III `.skin`
  of the primary materials or `--variant NAME` for an editable source or native
  model, or with `--format doom3` a Doom 3 skin declaration (`--model-path`,
  `--decl-name`). Both writers are the Mesh Editor's; refusals exit 4 without
  writing. See [Skin Variants](MODEL_SKIN_VARIANTS.md).
- [x] `model import <input> --output <source.mesh.json>` creates an editable
  mesh source from polygonal OBJ, MDL, MD2, MD3, or a baked primitive design. `model edit <source>
  --operation <name> --output <source.mesh.json>` applies a validated component,
  UV, material, or frame operation. Both support `--dry-run`, `--overwrite`, and
  text/JSON results. See [Editable Meshes](MODEL_MESH.md) for selectors, transform
  options, bounds, preservation guarantees, and import limitations. Usage errors
  return `2`, invalid operations/imports `4`, read/write failures `1`, and success `0`.
- [x] `model build` also accepts `.mesh.json`, preserving animation frames and
  tags in MD3 or all poses/skin slots in MD2; `--frame N` chooses the OBJ frame. Its JSON includes frame/tag
  counts in addition to the existing geometry and write status.
- [x] `model animations <model.mesh.json> [--clip N] [--json]` lists saved clips
  with zero-based indices and inclusive first/last frame ranges. `--clip N`
  selects an existing clip; invalid indices return usage error `2`. The command
  is read-only and an empty clip list leaves all model frames available.
- [x] `model edit --operation add-clip|rename-clip|set-clip-range|delete-clip`
  authors named inclusive ranges through shared validation/history/recovery.
  Add/range use `--first-frame N --last-frame N`; rename/range/delete select
  an existing `--clip N`; add/rename use `--name`. `insert-inbetweens --frame N`
  generates poses between N and N+1 with optional `--insert-count` (default 1)
  and `--name` prefix (default `blend`). `copy-frame-pose --frame N --source-frame M`
  copies a full pose into N. Both pose edits include every surface and attachment,
  preserve other poses, and reject partial component/surface scope. MD3/OBJ build
  results report omitted custom clip metadata in `exportNotes`; mesh sources retain it.
- [x] `model import <source> --output <model.mesh.json>` imports polygonal OBJ, MD2/MD3 or
  bakes a primitive design into an editable mesh source. Use `--dry-run --json`
  to inspect the import before writing.
  OBJ uses one pose, preserves UV/normal seams and direct package material paths,
  and triangulates bounded planar polygons. Material libraries, non-polygon
  records, invalid indices and lossy conversions fail with a line diagnostic.
  See [OBJ interchange](MODEL_MESH.md#obj-polygon-interchange).
- [x] `model edit <model.mesh.json> --operation <operation> --output <path>`
  applies validated mesh, UV, material or animation-frame edits. For example,
  `--operation transform --faces all --offset 0,0,8` moves all selected geometry;
  optional `--snap-grid N` rounds the translation delta in the chosen axes, matching
  the viewport and numeric editor. Zero disables it; positive steps range from
  0.000001 to 1,000,000 model units. `--snap-angle N` snaps degrees (positive
  range 0.000001–180); `--snap-scale N` snaps factors relative to 1 (positive
  range 0.000001–10,000). Zero disables either step. `--pivot-mode
  origin|selection|custom` chooses the pivot, with legacy default custom 0,0,0;
  `--pivot x,y,z` supplies custom coordinates. `--pivot-frame N` chooses the
  selection-bounds reference pose for all-frame transforms only (default 0).
  A frame-local transform uses the edited pose. Other operations reject these
  pivot and snapping flags; contradictory pivot options are usage errors.
  `--dry-run --json` reports the proposed result without writing it.
- [x] Mesh/tag transforms, face extrusion/duplication and collision transforms
  accept `--transform-space world|selection|custom` (default world). Custom
  accepts `--axis-rotation X,Y,Z`, using XYZ Euler degrees (default 0,0,0).
  Selection derives axes from the first usable selected/touching triangle, the
  active whole surface, or the selected tag/box. `--axes-frame N` chooses a
  fixed reference pose for selection axes; default is the edited frame, or
  `--pivot-frame`/pose 0 for all-frame edits. The pivot remains in model coordinates.
  Axis angles require Custom; the reference requires Selection. Repeated,
  malformed, out-of-range or inapplicable axes options are usage errors (exit 2).
  Collision size remains intrinsic to the box under every axes mode. No output
  is written on failure or dry run. For example:
  `model edit prop.mesh.json --operation transform --faces all --transform-space selection --axes-frame 1 --offset 0,0,8 --output raised.mesh.json`.
- [x] Viewport free trackball rotation emits ordinary XYZ angles in the chosen
  transform basis. Reproduce its preview through mesh/tag `model edit` or
  `model collision --operation transform` using `--rotate X,Y,Z`, the same axes,
  explicit pivot and frame scope, with `--snap-angle 0` (or omitted). Free
  rotation already snapped its total angle while preserving its axis; applying
  numeric Euler snapping again would produce a different rotation. This adds
  no screen-coordinate CLI or alternative mutation service.
- [x] `model edit --operation md2-skin-size --skin-size width,height` saves MD2
  dimensions through shared validation/history/recovery. `model build` infers
  MD2 from `.md2` or `--format md2`; dry runs report target limits and maximum
  position/normal/UV quantization errors. JSON includes `exportNotes`,
  `md2SkinSize`, ordered `skinSlots`, `storedVertices`, `maxPositionError`, `maxNormalAngleDegrees`,
  and `maxUvError`. Use matching external PCX skins and review package materials.
- [x] `model build <design.model.json> --output <model.md2|model.md3|model.obj>` builds
  static primitive designs through the shared authoring service. Schema-2
  part rotations and UV transforms are applied exactly as in the GUI;
  version-1 designs remain readable with identity defaults for the new fields.
  The [model design schema](MODEL_DESIGN.md) defines required fields and limits.
  Optional `--format md2|md3|obj` overrides extension inference; `--dry-run` performs no
  writes, and `--overwrite` allows replacing an output. The design source and
  symbolic-link destinations are protected. JSON includes output path, format,
  part/vertex/triangle counts, bytes, materials, and write/dry-run state.
  Exit `0` means success, `2` missing arguments, `4` unsupported export format
  or geometry unsupported by the target format, and `1` an invalid input or write failure.
- [x] `map place-model <map> --package <source> --entry <model.md3>
  --origin x,y,z --output <map>` adds a `misc_model` to a Quake III map and
  saves through the shared map service. Use `--engine idTech3` where needed.
  The model must be readable, unambiguous MD3 geometry without parser warnings,
  and at most 8 MiB. `--dry-run` and `--overwrite` follow map save-as behavior. Model
  validation failures return `4`, missing arguments `2`, and load/edit failures
  `1`; save failures follow the existing map command contract.

See [Model Design And Level Handoff](MODEL_DESIGN.md) for a complete design,
package, and level CLI example and the editable JSON schema.

- [x] `model inspect` to decode MDL, MD2, MD3 and polygonal OBJ geometry and report surfaces,
  frames, animations, tags and skins. MDC, MDR and IQM report their header
  counts only; no geometry is decoded for them.
- [x] `model export` to write one frame as a Wavefront OBJ for an external
  modeller. `--frame <n>` is zero-based and defaults to 0, `--material <name>`
  sets the OBJ `usemtl` name, and `--output <path>` names the file. Without
  `--output` the OBJ text goes to stdout, or to the JSON `obj` field.
  `--overwrite` is required to replace an existing output, and `--dry-run`
  prepares and validates the same output without creating directories, locks or
  files. Guarded writes review the destination before serialization, check its
  identity again at commit, and verify each write. Neither overwrite nor dry run
  bypasses source protection: export outside the input package/folder/draft and
  its retained payload storage. Loose and package model inputs and OBJ output are bounded to
  64 MiB. JSON `notes` describe omitted poses, tags and native skin data; notes go
  to stderr when text stdout carries OBJ. Invalid or missing `--frame` values are
  usage errors, as is an explicitly blank `--output`.
- [x] Both commands accept either a package plus an entry (`--package <path>`
  and `--entry <virtual/path>`, or the same two as positional tokens) or a
  single model file on disk (`--file <path>`, or a lone positional path with
  no entry after it). Package inputs include archives, folders and portable
  `.vibepackage` drafts; the latter read the current planned bytes and names.
- [x] `--palette <id>` selects the palette used for the indexed skins MDL
  embeds.
- [x] `model mdl <source.mesh.json>` reports native MDL settings, skin members,
  frame groups and cumulative times. `--operation` edits indexed skins, member
  durations, groups, header fields or the 768-byte preview palette through the
  normal document service. Mutations require `--output`, support `--dry-run`
  and `--overwrite`, and reject repeated/inapplicable arguments. JSON contains
  palette provenance and a hash, without raw images. Read-only `--time` sampling
  accepts `--native-frame`, `--skin`, `--timing stored|glquake` and `--sync-phase`;
  it reports the same exact pose/member as native GUI preview without writing.
  Skin edits also accept `--package <archive|folder|draft.vibepackage>` and one
  unique `--entry` or exact `--entry-index`, instead of `--image`. Optional
  `--palette` selects package palette paths. The shared indexed reader preserves
  pixels, rejects palette/dimension mismatches and reports a source receipt;
  output cannot replace package/draft inputs. Draft edits remain unchanged.
  See the complete
  [native MDL operation table](MODEL_MESH.md#native-mdl-cli).
- [x] `model import <native.mdl> --output <source.mesh.json>` retains all native
  data. Loose imports use a generated preview palette until explicitly changed.
  `model build <source.mesh.json> --output <model.mdl>` applies original Quake
  limits (or another MDL delivery target with `--target`) and reports stored vertices, native frame/skin counts, precision errors
  and compatibility notes. It does not embed the source palette into the MDL.
- [x] `model formats` lists every model format the studio reads: suffixes,
  engine families (`idtech1` to `idtech4`, `goldsrc`, `interchange`), games,
  whether it is skeletal or animation only, the companion files it reads, whether
  it is also written, and what the decoder keeps. `--json` adds `exportFormats`,
  the identifiers `model build` accepts. See [Native Model Formats](MODEL_FORMATS.md).
- [x] `model build <source.mesh.json> --output <model.md5mesh|model.md5anim|model.iqm|model.ase>`
  writes skeletal and idTech 4 formats through the same export service as the
  Mesh Editor. Skeletal output re-binds vertices moved in the bind pose first. For
  `.md5anim`, `--clip N|NAME` chooses the clip; `--frame N` selects the clip that
  frame belongs to, as the editor does; a model with several clips and neither
  option exits 2, and a model without a clip exits 4. `--md5-frame-rate N`
  writes a whole rate in place of the clip's own; a fractional clip rate without
  it exits 4. `--all-clips --output-dir DIR` writes one `<clip>.md5anim` per clip,
  checking every destination before writing (existing files need `--overwrite`).
  JSON adds `clip` (index, name, frames, `frameRate`), `exportOmissions`, and for
  IQM `influencesReduced`, `maxDroppedWeight`, `maxWeightError`, the largest
  joint translation, rotation and scale quantisation, `maxPositionError`
  (vertex movement over the clips with the stored weights and channels, a note)
  and `maxLayoutError` (the same before rounding; beyond 1/64 unit it is an
  omission). `--strict` exits 4 without writing when any format would leave data
  out; `--all-clips --strict` prepares every clip first and its JSON lists each
  clip's `exportOmissions` and `refused`, every omission once, and
  `sharedNotes`. MDL, MD2 and MD3 omissions are the clips the frame names do not
  rebuild, authored frame origins (MDL, MD2, OBJ) and a skeleton. A model without
  joints writes a one-joint MD5 mesh or a static IQM, with a note. Options may
  come before the source path.
- [x] `model profiles` lists the modeller's controls profiles (`studio`,
  `blender`, `3ds-max`, `milkshape-3d`) with layout, transform style, sidebar
  family, selection-mode names, what each changes and where it differs from the
  editor it follows. The current profile is marked.
- [x] `model controls [--profile ID]` prints a profile's navigation, selection,
  transform, layout and key bindings, with saved changes unless `--defaults`.
  `--section navigation|selection|transform|layout|keys` narrows the output;
  `--check` exits 4 when two gestures or keys collide; `--export <file>` (with
  `--overwrite` for an existing file) writes a `vibestudio.modeller-controls`
  file; `--import <file>` saves its changes for the profile it names; `--select`
  makes the profile current; `--reset` drops saved changes; `--settings-file`
  works on a separate settings store. The keys section lists Ctrl+Tab and
  Ctrl+Shift+Tab (Control+Tab and Control+Shift+Tab on macOS), which every
  profile keeps for moving focus between panels;
  saved changes or an imported file that give them to a command are applied
  without them and reported under `warnings`. See [Modeller Profiles](MODELLER_PROFILES.md).

| Command | Exit | Condition |
| --- | --- | --- |
| `model inspect` | 0 | The model decoded. |
| `model inspect` | 1 | The loose source exceeds 64 MiB or fails bounded reading. |
| `model inspect` | 2 | No package path and no model file path was given. |
| `model inspect` | 3 | The model file or the package could not be opened. |
| `model inspect` | 5 | The payload is not a recognized idTech model. |
| `model export` | 0 | The OBJ was written, printed, or reported as a dry run. |
| `model export` | 1 | Bounded input/output, source protection, destination review, serialization or guarded writing failed; no completed output is claimed. |
| `model export` | 2 | Missing input, invalid `--frame`, or an explicitly blank `--output`. |
| `model export` | 3 | The model file or the package could not be opened. |
| `model export` | 5 | The input decodes no exportable geometry. |
| `model controls` | 0 | The controls were printed, checked without problems, exported, imported, selected or reset. |
| `model controls` | 1 | A controls file could not be read or written, or the settings store refused the change. |
| `model controls` | 2 | An unknown profile, section or option, an existing `--export` file without `--overwrite`, or a controls file for a different `--profile`. |
| `model controls` | 4 | `--check` found colliding gestures or keys, or an imported controls file is malformed. |

### Compiled Artifacts

- [x] `bsp inspect` for Quake, Quake II, and Quake III BSP lump tables, entity
  and texture lumps, counts, and any `.pts`/`.lin` leak or `.prt` portal file
  written beside the map.

### Build Pipelines And Launch
- [x] `build prepare <map> --package <archive-folder-or-draft> --output <new-directory>`
  captures a Quake-family map and complete asset snapshot with verified input hashes.
  `--target quake|quake2|quake3` selects the game and defaults the engine hint
  appropriately; with `--engine idTech2`, classic maps default to Quake unless
  they carry the Quake II marker.
  Optional `--name` is a portable 1–64 character map name; `--engine` defaults to
  `idTech3` when no target is supplied; `--max-bytes` defaults to 4 GiB (hard cap 1 TiB / 100,000 assets).
  `--dry-run` reads and hashes without creating the workspace. JSON returns
  `workspace`, including inventory, dependencies, omitted stale outputs and warnings.
- [x] `build run-prepared <directory>` verifies the inventory before/after the
  shared pipeline. `--pipeline` supports `quake3-full` (default) and
  `quake3-bsp-only` for Quake III. Quake/Quake II use `quake-full` (their default),
  `quake-fast` or `quake-bsp-only`. Repeat `--tool <id>=<executable>` for distinct
  compatible IDs: `vibemap3` for Quake III, or `vibemap2-bsp`, `vibemap2-vis`,
  `vibemap2-light` for Quake/Quake II. The pre-VibeMap ids (`q3map2`,
  `ericw-qbsp`, `ericw-vis`, `ericw-light`) are refused. `--timeout-ms`, one
  `--stage-args <stage>=<arguments>` per stage, and repeated `--disable-stage`
  control execution. Workspace game/filesystem and output-path flags cannot be
  overridden (`-lightmapdir`, `-tempname` and `-rename` are refused).
  `--dry-run` verifies inputs and plans stages without running compilers.
  JSON returns `pipeline` and `workspaceDirectory`; no additional banner or
  compiler stdout is interleaved. Exit codes: 0 success, 1 read-only settings,
  2 usage, 3 source/draft load failure, 4 preparation/verification/build failure.
  Unknown, repeated or inapplicable options fail. Existing workspaces are never
  overwritten by preparation. Full BSP runs retain previous outputs outside the
  compiler search path, then record a new output inventory; BSP-disabled runs
  require an existing verified successful inventory. Review successful outputs
  with `build artifacts` before publication.
- [x] `build artifacts <directory>` verifies captured inputs and the last successful
  compiler output inventory. JSON returns `artifacts`, including `recordSha256`;
  failed, interrupted or changed runs cannot be published.
- [x] `build publish-prepared <directory> --output <package.pak-or-pk3>` publishes captured
  assets, BSP, generated shaders and external lightmaps through the shared package
  writer. The target fixes the PAK/PK3 format; Quake `.lit`/`.lux` are runtime files.
  `--include-source` includes the source map and generated Quake WAD; diagnostics stay in the
  workspace. `--dry-run` verifies without writing; `--overwrite` permits replacing
  an existing output. PAK stores files without compression; PK3 `--compression`
  accepts store/fast/default/best and optional
  `--expected-output-sha256` binds the reviewed output record. JSON returns
  `publication`; exit 0 succeeds, 1 means read-only settings, 2 means invalid
  arguments and 4 means verification/publication failure. Replacements preserve
  the previous package in a verified `.bak`. Output and backup paths must stay
  outside the workspace and original source assets. The expected receipt hash
  accepts 64 hexadecimal digits. Both commands reject unknown, repeated and
  inapplicable options. Publication JSON includes reviewed artifacts, package
  paths, output/backup paths, bytes, hash and committed/dry-run state.
- [x] `build list` for chained pipelines and their stages.
- [x] `build deploy-plan <directory>` verifies a prepared build and reviews its
  installation target. `--installation <id>` selects a saved profile; otherwise
  the selected/first saved installation is used. `--mod <folder>` overrides the
  saved launch folder or game default. The profile must match the captured Quake,
  Quake II or Quake III target (custom profiles need the matching engine family).
  The folder must be a single portable name inside the root. For classic targets,
  `--pak-slot N` selects a numbered PAK explicitly; omitting it reuses this map's
  remembered slot or chooses the first available loadable slot. Quake supports
  0–999 with a consecutive prefix; Quake II supports 0–9 with gaps. Quake III
  rejects the PAK option.
  JSON returns `deploymentPlan` with output/backup paths, existing package hash,
  artifact receipt, `pakSlot`, `pakReceiptPath`, receipt/layout hashes,
  `reviewSha256`, warnings and windowed launch command. It writes nothing and
  rejects publication flags.
- [x] `build deploy-prepared <directory>` uses the same target options and writes
  `pak<N>.pak` or `vibestudio_<map>.pk3` containing the captured assets and verified runtime
  outputs. `--allow-test-assets` grants one-operation permission for a read-only
  installation; it never changes the saved profile. `--overwrite` requires a
  verified backup of an existing package. `--include-source`, `--compression`
  (store/fast/default/best) and `--expected-output-sha256` match publication.
  `--expected-package-sha256 <64-hex-digits|missing>` binds a prior destination
  review. `--expected-deployment-sha256 <64-hex-digits>` binds the complete review,
  including the chosen path, selected PAK receipt, other numbered PAK inventory,
  output record and executable. `--launch` starts the reviewed executable only
  after verified publication, with engine-specific windowed/base/game/map
  arguments (and home lookup for Quake III). `--dry-run`
  verifies without creating folders, publishing or launching; it needs no
  installation write consent. JSON returns `deployment`, including the plan,
  publication, committed state, process ID, launched/cancelled state and errors.
  PAK slot receipts are written after package commit; receipt-write failure is
  a warning and retains the deployed PAK. A launch failure may follow successful publication: inspect `committed` before
  retrying. Exit codes: 0 success, 1 read-only settings, 2 usage, 3 missing saved
  installation, 4 verification/publication/launch failure. Unknown, repeated or
  inapplicable options fail. Protected source/workspace/registered-package paths
  and links are refused. See [deployment limits](LEVEL_EDITOR.md#prepared-builds-with-current-assets).
- [x] `build plan` to resolve stage inputs, outputs, and tool availability
  without running anything.
- [x] `build run` to run every enabled stage in order with streamed logs,
  diagnostics, hashes, and per-stage command manifests.
- [x] `launch plan` to build a reviewable engine command line.
  Doom WAD plans inspect the selected map's node records and refuse missing or
  malformed data. JSON includes `nodeBuild`, `validatedArtifactPath`, and
  `validatedArtifactSha256`; `launch run` rechecks that hash before starting.
  Relative `--bsp` WAD paths become absolute launch arguments, so the engine's
  working folder cannot redirect the checked input. Numeric MAP shorthand and
  episode/map pairs resolve to their native marker for validation.
  Unsupported node formats remain explicit warnings, not validated records.
- [x] `launch run` to start the configured game installation.
- [x] `--deploy` on both to copy the built map (`--bsp`) into the game folder the
  engine loads maps from; `--allow-test-maps` lets a run save permission on a
  read-only installation profile, the choice the Build page asks for once.

### Textures And Palettes
- [x] `texture decode` for idTech textures, flats, Doom patches, WAL, MIP, LMP,
  PCX, TGA, and sprites, with optional PNG export.
- [x] `texture palette` to resolve the palette used for indexed art and report
  whether it came from the package or is a generated stand-in.
- [x] `texture generate --prompt <description> --game <game>` draws
  `--count` variants with the configured image model, or takes
  `--from-image <picture>` with no AI, and makes each game-ready: cropped,
  blended to tile (unless `--no-seamless`), resampled with wrap-around,
  converted to the game's format and palette (`--palette`, `--palette-file`,
  `--palette-root`, or `--package`; a generated stand-in is used and warned
  about otherwise), then written where the game reads it under `--folder`:
  a WAD2 lump (Quake, `--wad`), a WAL (Quake II), a TGA and, for glows and
  liquids, a shader (Quake III), a flat or patch in a PWAD (Doom, Heretic,
  Hexen), or a PNG (generic). `--surface` (wall, floor, ceiling, trim, panel,
  liquid, sky), `--style`, `--size WxH`, `--name`, `--directory`, `--dither on`,
  `--fullbrights` and `--seam-blend` shape it; `--companions` adds the maps
  source ports read beside it. `--source` restyles a picture through the
  model, and `--preview <png>` writes the variants tiled 2x2. A provenance
  record goes to `.vibestudio/generated/textures/<name>.json`.
- [x] `texture derive --input <picture> --game quake|quake2|quake3|generic`
  writes the normal, gloss, and glow companion maps for a texture, with no AI:
  `_norm`, `_gloss`, `_glow` for DarkPlaces, FTE and QuakeSpasm-family ports,
  or `_n`, `_s`, `_glow` for ioquake3. `--normal-strength`, `--glow-threshold`,
  `--no-seamless`, `--output <folder>` and `--dry-run` shape it.

### Shaders
- [x] `shader inspect` for idTech3 `.shader` parsing, stage graphs, stage
  previews, raw text detail, and mounted-package texture-reference validation.
- [x] `shader set-stage` for non-destructive stage directive edits with
  `--output`, `--dry-run`, `--overwrite`, and JSON save reports.

### Materials
One family for every idTech material, shared with the **Materials** page
(`core/material_*`; design in `docs/MATERIALS.md`). A source is a package,
folder or WAD, or a loose `.shader` or `.mtr` script; a loose script's images
and Doom 3 tables come from the folder above its `scripts/` or `materials/`
folder, or from `--package`. `--base` adds the game's own package or folder
for images a mod borrows. `--material` names the material (it may be left
out when the source defines only one), `--engine doom|quake|quake2|quake3|doom3`
narrows or overrides the engine, and `--palette` picks the palette for
paletted images when the package has none.
- [x] `material list` prints every material with its engine, kind, stages,
  traits and source line, including Quake III implicit shaders, Doom walls and
  flats, Quake WAD textures and Quake II WALs. `--where` takes the studio query
  language over `name`, `engine`, `kind`, `source`, `stages`, `images`,
  `image`, `frames`, `blend`, `sort`, `cull`, `errors` and `warnings`, the
  yes/no traits `animated`, `sky`, `light`, `fog`, `translucent`, `rejected`
  and `shadowed`, and each surface parameter, keyword flag or Quake II flag
  (`slick=yes`, `surfaceparm=nolightmap`); `--limit` caps the rows and
  `--no-implicit` leaves out images no script names.
- [x] `material inspect` shows one material: its summary, every image and
  where it resolved (or that it is missing), diagnostics, why the engine would
  drop it, its node graph and, with `--show-source`, its text.
- [x] `material validate` checks materials the way each engine loads them:
  script syntax, keywords the engine rejects (Quake III drops the shader,
  Doom 3 defaults the material), missing images, definitions another one
  shadows, and Doom lump problems. It exits 4 on errors, or on warnings with
  `--strict`; `--no-images` skips image lookups and `--where` narrows the set.
- [x] `material render` draws a material the way its engine draws it on the
  GPU, with the same OpenGL or Vulkan renderer as the **Materials** page, and
  writes a PNG to `--output` (or reports with `--dry-run`). The JSON names the
  renderer that drew it; `--renderer automatic|opengl|vulkan` picks one for
  the run, and the command exits 5 when no renderer can start. `--shape wall|floor|cube|sphere|cylinder|room`, `--size WxH`,
  `--time`, `--yaw`, `--pitch`, `--zoom`, `--fov`, `--orthographic`, `--tiling`
  and `--swatch` frame it; `--frames N --fps F --columns C` writes an animation
  sheet. Lighting follows the engine: `--lightmap`, `--flat-lightmap`,
  `--no-overbright` and `--context world|model|2d` (Quake III);
  `--doom-light`, `--doom-extralight`, `--no-doom-distance`,
  `--no-fake-contrast`, `--boom-wrapping` and `--sky` (Doom);
  `--quake-renderer glquake|modern|software`, `--quake-style`,
  `--quake-lightmap` and `--alternate` (Quake); `--quake2-intensity` and
  `--sky` (Quake II); `--light-color`, `--light-angle`, `--ambient`,
  `--no-specular`, `--doom3-shading vanilla|bfg`, `--parms 0=1,4=2` and
  `--sound` (Doom 3). `--filter`, `--background`, `--no-checker`,
  `--editor-image`, `--ignore-rejection` (draw the stages even where the
  engine draws its fallback) and `--developer-default` complete it.
- [x] `material graph` prints the material's node graph (nodes, properties
  and links) and the nodes that can be added. With `--edits <json>` it applies
  graph edits (`set`, `add`, `remove`, `move`, `wrap`, `unwrap`, `expand`) to
  the material's text, checks the result by parsing it again, and writes it to
  `--output` (or shows it with `--dry-run`). `--text-kind` picks the text for
  classic materials: `swantbls`, `animdefs` or `wal-json`; a `wal-json` edit
  written to a `.wal` file rewrites that WAL's header.
- [x] `material edit` applies text edits from `--edits <json>` (`set`, `add`,
  `remove`, `add-stage`, `remove-stage`, `move-stage`, `move`,
  `replace-definition`, `add-definition`, `remove-definition`, `rename`) to a
  Quake III shader or Doom 3 material script: the loose script, `--script` in a
  package, or the script that defines `--material`. It writes `--output` with
  `--overwrite` for the same file, or reports with `--dry-run`; `--strict`
  refuses to write a script with errors.
- [x] `material templates` lists the starting points for `material new`, by
  engine.
- [x] `material new --template <id> --name <material>` writes a new material,
  with `--image` for its main image, to the console, to a new `--output`
  script, or to the end of an existing one with `--append`.
- [x] `material doom-tables` prints a Doom package's animation and switch
  tables (Boom `ANIMATED`/`SWITCHES` or the vanilla tables) as SWANTBLS text,
  or reads SWANTBLS text, and with `--output-dir` writes `ANIMATED.lmp`,
  `SWITCHES.lmp` and `SWANTBLS.txt`.
- [x] `material wal` shows a Quake II WAL header (a `.wal` file, or
  `--material` in a package) as ericw-tools `.wal_json`; `--output` writes that
  sidecar, and `--metadata <wal_json> --output <wal>` rewrites the header's
  flags, contents, value and next frame without touching the pixels.

### 3D Rendering
Every 3D view and material preview draws on the GPU through one renderer
(`core/render_device`; design in `docs/ARCHITECTURE.md`) with an OpenGL and a
Vulkan backend. The choice is the saved preference (**Settings** >
**Appearance and Language** > **3D Rendering**), overridden for a run by
`VIBESTUDIO_RENDER_BACKEND` and, ahead of that, by `--renderer`. Commands
that draw get a windowless GUI application so OpenGL is available to them; on
Linux without `DISPLAY` or `WAYLAND_DISPLAY`, and under the offscreen
platform, only Vulkan can draw.
- [x] `render backends` starts each renderer and reports its API version,
  device, driver, kind of device, largest texture and start-up time, or why it
  is unavailable, then which one 3D views use. It exits 5 when the choice
  leaves no working renderer.
- [x] `render test` draws a known image on each available renderer (or only
  `--renderer opengl|vulkan`) and checks every pixel. It exits 4 when one
  draws wrongly and 5 when none can start.
- [x] `render set automatic|opengl|vulkan` saves the preference the studio and
  the CLI share; `--settings-file` keeps scripts away from the user's own.

### Sprites
- [x] `sprite plan` for Doom lump naming, Quake `.spr` sequencing, palette
  preview notes, frame rotations, and package staging paths.

### Code IDE
- [x] `code files <project-root> [--where <query>] [--max-files <count>]` for the
  same bounded filename/metadata catalog as the Files panel. Queries use `path`,
  `name`, `ext`, `folder`, `size`, and `language`. JSON reports complete/partial
  state, counts, exclusions and warnings; partial results exit 4, invalid arguments
  or unknown fields exit 2, and failed roots exit 1. The default is 4,000 files;
  `--max-files` accepts 1–20,000. Content decoding is deferred until opening.
- [x] `code language-server <file> --server <absolute-executable>` runs the shared
  local stdio client against a saved source. Optional `--root`, `--language`,
  `--server-args` (JSON array), `--timeout-ms`, and one-based `--line`/`--column`
  configure the check and definition lookup. `--completion` selects validated,
  read-only completion proposals at that position instead; `--references` selects
  semantic locations, with optional `--exclude-declaration`; `--hover` returns
  symbol documentation and validated source ranges. Hover, completion and reference
  queries are mutually exclusive and require `--line`. `--json` returns `languageServer`
  diagnostics, version provenance, definitions, completion edit proposals, reference
  previews/ranges/source hashes, hover content, logs and errors. Complete reports
  without errors exit 0; invalid arguments exit 2; source errors, missing/limited
  diagnostics, incomplete/limited/skipped completion, reference or hover results and server failures
  exit 4. Formatting is a separate mutually exclusive query: `--format-document`
  or `--format-range --line N --column N --end-line N --end-column N` previews
  edits and normalized output; `--tab-size 1..16` and `--insert-spaces` set options.
  `--write` requires `--expected-sha256` from the preview's `sourceFileSha256`;
  exact-range atomic saves preserve encoding, BOM and untouched separators.
  Formatting does not wait for diagnostics: exit 0 proves the formatting
  preview/write, not source correctness; invalid edits, stale files and failed
  writes exit 4. `--rename <new-name> --line N --column N` previews semantic
  project edits independently of diagnostic publication. A write requires the
  reviewed `--expected-plan-sha256`; changed plans fail before any file write.
  Full edit ranges, source hashes, written files and partial failure are exposed
  in JSON. See [Local Language Services](LANGUAGE_SERVICES.md).
- [x] The same `code language-server` command supports `--code-actions --line N
  --column N` with optional `--end-line`/`--end-column` selection endpoints.
  Listing is read-only; `--action-index N` resolves and previews one action.
  `--write` requires the reviewed `--expected-plan-sha256`, binding the action
  title, provider and complete edit plan. Malformed or partial lists, unavailable
  selections, stale plans and failed writes exit 4; invalid options exit 2.
  Core editing remains available without a server or AI connector.
  Diagnostic queries negotiate push or document pull automatically. JSON records
  `diagnosticOrigin`, `diagnosticsError` and `diagnosticsSkipped`; invalid ranges,
  partial reports, timeouts and provider failures return exit 4. CLI checks are
  read-only and emit no save notification. Explicit guarded writes run after the
  short-lived connection closes.
- [x] `code language-server --completion --resolve-completion N` resolves a
  one-based item index (1–500), preserving provider data and returning validated
  detail, documentation and related edits. JSON records `needsResolve`, item
  indices, `resolvedIndex` and `resolveReceived`. Resolution remains read-only;
  changed identities, unsupported commands/edits, stale versions and timeouts
  fail explicitly, preserving the completion diagnostic exit contract.
  Snippet entries expose `snippet` and `tabStops` alongside expanded edits.
  Each stop records its number, absolute UTF-16 offset/length, parent occurrence
  and choices. Related imports are included in these positions, and `caret`
  identifies the final snippet stop. Unsupported transforms and malformed/cyclic
  snippets are omitted with the existing skipped-result exit 4 contract.
- [x] `code language-server --signature-help --line N --column N` inspects call
  overloads and active arguments without writing. JSON includes `signatureHelpReceived`,
  normalized signatures, zero-based active indices, UTF-16 parameter label ranges,
  documentation and source provenance. This query is mutually exclusive with other
  semantic queries and retains the diagnostic exit contract: valid empty replies
  can succeed; malformed, shortened/skipped or unsupported replies and timeouts
  exit 4. See [Local Language Services](LANGUAGE_SERVICES.md).
- [x] `code index` for source tree discovery, language hook descriptors,
  diagnostics, symbol search, compiler task suggestions, and launch profiles.
  Symbols include QuakeC functions (`void() name = ...`) and entity classes
  from `.def` and `.fgd` files, the same scanner the editor's Go to Symbol
  uses.
  The bounded scanner accepts strict UTF-8 and BOM-marked UTF-16, prunes generated
  subtrees, skips links, and reports partial scans. `--max-files` accepts 1–20,000
  (default 4,096). JSON adds `complete`, `cancelled`, `filesSkipped`, `entriesVisited`
  and `bytesRead`; source records identify `fromBuffer` (false for CLI disk scans).
  An incomplete scan exits 4. See [Code Editor](CODE_EDITOR.md) for all bounds and
  the GUI's live-buffer indexing behavior.

### Compilers
- [x] `--compiler-registry` scaffold compiler registry and executable discovery.
- [x] `compiler list`
- [x] `compiler profiles`
- [x] `compiler plan`
- [x] `compiler set-path`
- [x] `compiler clear-path`
- [ ] `compiler detect`
- [ ] `compiler probe`
- [x] `compiler run`
- [x] `compiler rerun`
- [x] `compiler manifest`
- [x] `compiler copy-command`
- [ ] `compiler explain-log`

Tool and profile ids name VibeStudio's own compilers: `vibemap2-bsp`,
`vibemap2-vis`, `vibemap2-light`, `vibemap2-bspinfo`, `vibemap2-bsputil` (with
the `vibemap2-bsputil-check`, `-extract-entities` and `-extract-textures`
profiles) and `vibemap2-hub` for VibeMap2; `vibemap3` and the `vibemap3-probe`,
`-bsp`, `-vis`, `-light`, `-convert` and `-pk3` profiles for VibeMap3. The ids
used before the move to VibeMap2 and VibeMap3 (`ericw-qbsp`, `ericw-lightpreview`,
`q3map2`, `q3map2-bsp` and the rest) are refused; the `compiler` commands and
compiler plans name the new id in their error. See [Compiler Integration](COMPILER_INTEGRATION.md#renamed-ids).

### Editor Profiles

The shared catalog contains 18 schemes. `editor select` and `editor controls`
accept documented family aliases (`hammer++`, `worldcraft`, `udb`, `slade3`,
`db2`); persistence uses the canonical ID. Profile JSON adds `aliases`,
`adaptations` and `referenceUrl`; controls JSON adds `mouseLookToggleKey`.
Adaptations describe differences from upstream editors and do not change map
format or engine support. See [Editor Profiles](EDITOR_PROFILES.md).

- [x] `--editor-profiles` report for routed editor interaction profiles.
- [x] `--set-editor-profile <id>` selected profile persistence.
- [x] `editor profiles`
- [x] `editor controls` prints a profile's Levels layout, grid, 2D and 3D
  gestures, and keys (`--json` for the same rows and the scheme's facts).
- [x] `editor view-links` reads linked plan-centre, plan-zoom and camera-follow
  defaults; `--centers on|off`, `--zoom on|off` and `--follow-camera on|off`
  update them. `--json` reports the resulting values and `--settings-file`
  isolates automation settings.
- [x] `editor layout [profile|single-2d|single-3d|camera-and-plan|four-views|camera-above-plans|camera-beside-plans]`
  reads the current level-view layout, or saves the supplied preference. `profile`
  follows the selected interaction profile's default. JSON returns `preference`,
  `effectiveLayout` and `profileId`; `--settings-file` isolates automation settings.
  `--panes top,front,side|all` saves which plan panes three-plan layouts show;
  JSON adds `planPanes` and `planPanesApply`.
  Invalid identifiers, repeated/unknown options or excess arguments exit 2;
  settings write failures exit 1 and successful reads/writes exit 0.
- [x] `editor current`
- [x] `editor select`

### Shell UI Semantics
- [x] `--ui-primitives`
- [x] `--ui-semantics`
- [x] `ui semantics`

### Localization And Diagnostics
- [x] `--localization-report`
- [x] `localization targets`
- [x] `localization report`
- [x] `accessibility report`: every accessibility preference, the speech engine
  and how many voices it has, the system's languages and the target they
  resolve to, and the setup review's warnings; reads the store without
  writing (`--json` for the same fields).
- [x] `accessibility voices`: the speech engine's voices, with the ids
  `--set-tts-voice` takes; exits 5 (unavailable) without an engine.
- [x] `accessibility speak <text>` / `accessibility speak --test`: speaks and
  waits, with the saved voice, rate, pitch, and volume, or `--voice`, `--rate`,
  `--pitch`, and `--volume`; no text exits 2 and no engine exits 5.
- [x] `diagnostics bundle`
- [x] `diagnostics crashes`
- [x] `editor keys`

### AI Automation
- [x] `--ai-status`
- [x] `ai status`: the settings, connectors, credentials, and where text,
  images, and sounds would go now (`textConnection`, `imageConnection`,
  `soundConnection`), with what stops each; `projectAiFree` says when the
  open project's manifest turns AI off. As text it prints `Text:`, `Images:`
  and `Sounds:` lines saying what each feature would use now, the GGUF
  runtime and the Claude Code and Codex programs found, and a `Connectors`
  list with each connector's `Status: Available` or `Status: Planned`.
- [x] `ai connectors`: every connector with its capabilities, notes and
  `Status: Available|Planned`.
- [x] `ai tools`
- [x] `ai explain-log`
- [x] `ai propose-command`
- [x] `ai propose-manifest`
- [x] `ai package-deps`
- [x] `ai cli-command`
- [x] `ai fix-plan`
- [x] `ai asset-request`
- [x] `ai compare`
- [x] `ai shader-scaffold`
- [x] `ai entity-snippet`
- [x] `ai package-plan`
- [x] `ai batch-recipe`
- [x] `ai review`
- [x] `ai ask`: asks the text model the Assistant uses, with `--context-file`
  files as context (paths inside the project and home folder shortened, key-
  shaped text removed). `--dry-run` prints the request without its key, each
  message's text as written; with `--json` it adds the raw body.
  An endpoint off this machine needs `--yes`, since a script has nobody to
  show the request to. `--provider`, `--model`, and `--endpoint` override the
  settings for one run; `--max-tokens` and `--timeout-ms` bound the answer.
- [x] `ai test-connection`: asks the model to reply OK and reports who
  answered, from where, and how fast; nothing from the project is sent. For
  Claude Code and Codex it checks the login first, stops when the tool is
  signed out, and says when the tool is signed in with an API key, which is
  then billed.
- [x] `--set-ai-cloud on|off` allows connectors that send requests off this
  machine, after the user confirms each one; `--set-ai-agentic` stores the
  opt-in for the planned agentic workflows, which nothing uses yet.
- [x] `--set-ai-local <connector>`, `--set-ai-model <connector>=<model>`, and
  `--set-ai-endpoint <connector>=<url>` configure the text connectors without
  the GUI; an empty value clears the setting. `--set-ai-local` accepts only
  the connectors that run on this machine (`local-offline`, `local-gguf`).
  For `claude-code`, `codex` and `custom-command`, `--set-ai-endpoint` takes
  the program or command line. A rejected id prints the ids the option
  accepts. After saving, the command prints `Text:`, `Images:` and `Sounds:`
  lines saying what each feature would now use.
- [x] `--set-ai-image <connector>`, `--set-ai-image-model <connector>=<model>`,
  and `--set-ai-image-endpoint <connector>=<url>` do the same for the image
  connectors the Texture Generator and `ai image` use.
- [x] `--set-ai-audio <connector>`, `--set-ai-audio-model <connector>=<model>`,
  and `--set-ai-audio-endpoint <connector>=<url>` do the same for the sound
  connectors (ElevenLabs, custom) the Sound Generator and
  `asset audio-generate --source ai` use.
- [x] `ai image`: draws pictures with the configured image model (OpenAI's
  Images API, Gemini's generateContent, or a local Stable Diffusion web UI) and
  writes them to `--output` (a file, or a folder for several). `--source`
  edits or restyles a picture (`--strength` for the web UI's img2img);
  `--count`, `--size WxH`, `--quality`, `--negative`, `--seed`, `--steps`,
  `--transparent`, and `--tileable` shape the request. `--dry-run` prints the
  request without its key and with picture data shown by size; an endpoint
  off this machine needs `--yes`. `--provider`, `--model`, and `--endpoint`
  override the image settings for one run.
- [ ] `ai apply-staged`

### Extensions
- [x] `extension discover` for `vibestudio.extension.json` manifests across one
  or more roots.
- [x] `extension inspect` for manifest, trust, sandbox, command, capability,
  and staged generated-file reporting.
- [x] `extension run` for command-plan review, dry-run-first execution, extra
  arguments, and staged generated-file summaries.

### Release And QA
- [x] `scripts/validate_samples.py`
- [x] `scripts/validate_packaging.py`
- [x] `scripts/validate_release_assets.py`
- [x] `scripts/validate_docs.py`
- [x] `scripts/validate_source_layout.py`
- [x] `scripts/validate_cli_docs.py`
- [x] `scripts/validate_credits.py`
- [x] `scripts/extract_translations.py`
- [x] `scripts/package_portable.py`
- [ ] `qa smoke`
- [ ] `qa support-matrix`
- [ ] `release manifest`
- [x] `credits validate`
- [x] `docs validate`

## Output Contract
- [x] `--json` emits stable machine-readable output for supported command families.
- [x] `--quiet` suppresses non-error narration.
- [x] `--verbose` includes diagnostics and timing.
- [x] `--manifest <path>` writes compiler command manifests.
- [x] `--dry-run` shows planned package extraction writes, package save-as
  writes, model frame exports, shader save reports, extension command plans,
  and compiler command runs without touching files.
- [x] `--watch` streams compiler task log entries while long-running process-backed commands are active.
- [x] `--task-state` adds automation-friendly task-state objects to JSON output where supported.
- [x] `--renderer automatic|opengl|vulkan` picks the 3D renderer for one run of a
  command that draws (`material render`, `render backends`, `render test`),
  ahead of `VIBESTUDIO_RENDER_BACKEND` and the saved preference.
- [x] Non-zero exit codes distinguish usage errors, not-found cases,
  validation failures, operation failures, and unavailable workflows.

### Exit Codes

`--exit-codes` and `cli exit-codes` print this contract from the same table
the command handlers return.

| Code | Id | Meaning |
| --- | --- | --- |
| 0 | `success` | The command completed successfully. |
| 1 | `failure` | The command was understood but the operation failed. |
| 2 | `usage-error` | Arguments were missing, malformed, or incompatible. |
| 3 | `not-found` | A requested project, package, entry, installation, or tool was not found. |
| 4 | `validation-failed` | Validation completed and found blocking problems. |
| 5 | `unavailable` | The workflow is recognized but no capable implementation or tool is available yet. |

A finding is reported with `4`, not `1`: `entity validate` and
`package compare` both complete normally and exit `4` so a build script can
gate on the result.

## UX Rules
- [x] Every active destructive command must have a dry-run or staged mode.
- [x] Every active command that writes files must report exact output paths.
- [ ] Commands should accept project-relative and absolute paths.
- [ ] Commands should work with spaces and Unicode in paths.
- [x] Help examples should cover Windows PowerShell and POSIX shells where syntax differs.

## Current Examples

PowerShell:

```powershell
vibestudio --cli package validate "C:\Games\Quake\id1\pak0.pak" --json
vibestudio --cli package save-as ".\mod-folder" ".\build\mod.pk3" --format pk3 --add-file ".\autoexec.cfg" --as "scripts/autoexec.cfg" --manifest ".\build\mod.manifest.json"
vibestudio --cli map edit ".\maps\start.map" --entity 1 --set targetname=lift --output ".\maps\start-edited.map"
vibestudio --cli map edit ".\maps\start.map" --where class=light --set light=300 --output ".\maps\start-bright.map"
vibestudio --cli map add-entity ".\maps\start.map" --class light --origin 64,0,96 --set light=300 --output ".\maps\start-lit.map"
vibestudio --cli map delete ".\maps\start-lit.map" --object entity:3 --object brush:12 --output ".\maps\start-trimmed.map"
vibestudio --cli map duplicate ".\maps\start.map" --object brush:12 --delta 128,0,0 --copies 4 --output ".\maps\start-more.map"
vibestudio --cli shader set-stage ".\scripts\common.shader" --shader "textures/base/wall" --stage 1 --directive blendFunc --value "GL_ONE GL_ONE" --output ".\scripts\common-edited.shader" --json
vibestudio --cli sprite plan --engine doom --name TROO --frames 2 --rotations 8 --palette doom --json
vibestudio --cli entity validate ".\maps\start.map" --definitions ".\defs\quake.def" --strict --json
vibestudio --cli model export ".\id1\pak0.pak" progs/player.mdl --frame 0 --output ".\out\player.obj" --dry-run
vibestudio --cli package compare ".\release-1.pk3" ".\release-2.pk3" --json
vibestudio --cli compiler run vibemap2-bsp --input ".\maps\start.map" --watch --manifest ".\build\start.run.json"
vibestudio --cli ui semantics --json
vibestudio --cli localization report --locale ar --json
vibestudio --cli diagnostics bundle --output ".\diagnostics"
vibestudio --cli extension discover ".\extensions" --json
vibestudio --cli ai explain-log --log ".\build\qbsp.log" --json
vibestudio --cli ai review --kind shader --prompt "glowing gothic wall" --json
vibestudio --cli --set-ai-free off --set-ai-local local-offline --set-ai-model local-offline=llama3.2
vibestudio --cli ai ask --prompt "why does qbsp report a leak?" --context-file ".\build\qbsp.log" --dry-run
vibestudio --cli map generate --prompt "gothic castle with lava pits, 8 rooms, seed 42" --game quake --output ".\maps\castle.map" --preview ".\maps\castle.png"
vibestudio --cli map plan --prompt "quake 3 duel arena" --planner ai --dry-run
vibestudio --cli map ai-edit ".\maps\start.map" --prompt "add a light above each player start" --save-proposal ".\edits.json"
vibestudio --cli map ai-edit ".\maps\start.map" --proposal ".\edits.json" --only 1,2 --output ".\maps\start-lit.map"
vibestudio --cli asset audio-generate --prompt "heavy metal door slam" --game quake --output ".\mymod" --preview ".\door.wav"
vibestudio --cli asset audio-generate --prompt "distant reactor hum" --game quake2 --loop on --source ai --dry-run
vibestudio --cli texture generate --prompt "rusted riveted metal plate" --game quake --palette-root ".\id1" --companions --dry-run
vibestudio --cli texture derive --input ".\textures\metal1_1.png" --game quake --json
vibestudio --cli ai image --prompt "slipgate chamber concept art" --output ".\concepts\" --dry-run
```

POSIX shells:

```sh
vibestudio --cli package list './baseq3/pak0.pk3' --json
vibestudio --cli package stage './mod-folder' --add-file './autoexec.cfg' --as 'scripts/autoexec.cfg' --json
vibestudio --cli map inspect './maps/start.map' --select entity:0 --json
vibestudio --cli entity definitions './defs' --json
vibestudio --cli model inspect './id1/pak0.pak' progs/player.mdl --json
vibestudio --cli shader inspect './scripts/common.shader' --package './baseq3' --json
vibestudio --cli code index './mymod' --find monster --json
vibestudio --cli compiler plan vibemap2-bsp --input './maps/start.map' --dry-run
vibestudio --cli ui semantics
vibestudio --cli localization targets
vibestudio --cli accessibility report --json
vibestudio --cli accessibility speak --test --rate 2
vibestudio --cli diagnostics bundle --output './diagnostics'
vibestudio --cli extension run './extensions/sample/vibestudio.extension.json' make-file --dry-run --json
vibestudio --cli ai propose-command --prompt 'build quake map maps/start.map with qbsp'
vibestudio --cli ai batch-recipe --prompt 'convert doom sprites to indexed png' --json
```

## Workspace and format commands

- `workspace create <file.vibeworkspace>` writes portable references. Options:
  `--project`, `--package`, `--map`, `--map-name`, repeated `--code`,
  `--current-code`, `--active-module`, `--from-session`, `--overwrite`, `--dry-run`.
- `workspace inspect <file.vibeworkspace>` validates without opening assets and
  reports its revision and missing references.
- `asset formats [--module <id>]` reports suffixes, owning modules, import/native
  export/conversion capabilities, limitations and runtime Qt codec availability.
- `asset route <path>` resolves a filename's owning module; content validation
  belongs to the normal reader. It does not open or execute the path.

All accept `--json` and normal quiet/verbose/isolated-settings options. See
[Workspaces](WORKSPACES.md) for the schema and exit codes. `texture export
--profile dds|ftx --output <file>` and `asset convert --format dds|ftx` share the
new native encoders. SWL is input-only. No compressed DDS writer is claimed.

## Quake III Native Animation

Per-part skin references are available through `model assembly` add/update with
`--skin PATH`, optional `--skin-kind file|package`, and package-only
`--skin-entry-index N`. Update's `--clear-skin` removes the link. Inspection and
bakes share verified model/skin snapshots and source guards; JSON includes each
skin's digest, occurrence and material assignments. See
[Linked Skins](MODEL_ASSEMBLY.md#linked-skins) for working-directory rules,
package ambiguity, dry runs and schema compatibility.

`model assembly` exposes native Quake III configuration through `animation-set`, `animation-export` and `animation-clear`. New bindings use `--config` (or `--config-entry PATH` from `--package`, with `--config-entry-index N` for a repeated path), `--lower-part`, `--upper-part` and optional exact `--lower-animation` / `--upper-animation` identifiers. All require `--output`; dry runs validate all native slots and input protections without publication. Saved clip choices also drive ordinary inspect and bake operations. See [examples and limits](MODEL_ASSEMBLY.md#quake-iii-native-animation).

## Native Player Package CLI

`model assembly <source.assembly.json> --operation player-review` resolves a
native player and lists its owned files, hashes, dependency closure and limits.
`player-export` uses the same preparation followed by the deterministic atomic
PK3 writer. Both require `--player-name`, `--head-part`, `--icon` and `--package`;
optional `--skin-name`, `--icon-kind file|package` and `--icon-entry-index` select
native naming and exact package image identity. Export requires `--output` and
supports `--dry-run` and `--overwrite`; review rejects write-only flags. Portable
`.vibepackage` inputs include staged material edits. Export failure JSON retains
commit state, backup/recovery paths and blocked messages. More skins come from
`--skin-variants a,b`, `--team-variants` (red and blue), repeated
`--variant-skin NAME:ROLE=PATH` and `--variant-icon NAME=PATH`; detail levels
from `--lod-levels 1-2 --lod-ratio R` or `--lod-recipes`; custom sounds from
repeated `--sound NAME=PATH`. `package:PATH` names a package entry. Review
refuses an upper part without `tag_weapon`, team skins identical to the model's
own materials and skins that miss a surface. See the full
[contract and examples](MODEL_ASSEMBLY.md#native-player-packages).

`model assembly --operation players --package PATH` lists the Quake III players
of a package or folder from entry metadata. `model assembly --new --from-player
NAME --package PATH --output FILE` opens one through the same
`importModelQ3Player` service as the GUI's **Open Player…**: lower, upper on
`tag_torso` and head on `tag_head` as package parts, the `--skin-name` skin linked
to each, and native animation from the package's `animation.cfg`. It accepts
`--name`, `--dry-run` and `--overwrite`, refuses part-editing options and
`--operation`, and writes nothing when the parts do not resolve. JSON `player`
carries the listing, notes and `publication.arguments`, the `player-review`
options that publish the player again. The package is read only. See
[opening a Quake III player](MODEL_ASSEMBLY.md#opening-a-quake-iii-player).

`map insert-vertices` accepts repeated distinct `--brush` IDs. Every brush
receives the repeated `--position x,y,z` list in one atomic scene transaction;
a refused hull or locked brush prevents all output. Existing single-brush JSON
fields remain, with `brushIds`, `brushes` and aggregate inserted/collapsed counts
for batches. Grid snapping, optional old-corner collapse, dry runs and protected
output paths use the existing services. GUI direct insertion consumes the same
batch geometry service and adds stale-source/preview guards before publication.

`editor gestures <profile> --set plan.componentAxisDrag=on --set
camera.componentAxisDrag=on` selects world-axis component dragging independently
of insertion chords. Both fields accept `on`/`off`, support dry runs and portable
profile import/export, and default on for TB. CLI geometry commands continue to
accept explicit transforms and grid policy; they do not simulate GUI gestures.

`map transform-components <map> --operation snap --set brush:ID/vertex/ID
--target brush:ID/vertex/ID --output <map>` snaps one geometric brush vertex
exactly to another. Repeated source sets must identify coincident positions.
The shared service computes displacement, enables merging and keeps ordinary
UV, scene-lock, save and dry-run checks. Grid, pivot and delta options are
refused for snap. JSON includes the target, exact displacement and collapsed
corner count. Component move displacements may span ±65536 units; resulting
coordinates remain within ±32768. Two independent gesture fields,
`plan.componentQuickSnapModifiers` and `camera.componentQuickSnapModifiers`,
use the existing choice/import/export/reset interface (TB default `shift+alt`,
otherwise `none`). The gesture schema now contains 95 fields.

### Clipboard target placement

`map paste` retains original/`--delta` insertion and adds `--camera`,
`--plane-point` with `--plane-normal`/`--ray-direction`, and `--ray-origin` with
`--ray-direction`/`--view-direction`. Ray targets accept `--orthographic` for a
line through the orbit focus plane, with frontmost-surface placement on either
side and focus-depth fallback. Target modes accept `--grid` (0 disables snapping)
and `--definitions` for point bounds. `--context UUID` chooses a layer
or opens a group. JSON reports the resolved `delta` and placement mode. All
variants share bounded parsing, native insertion, scene locks, texture mapping,
dry-run and overwrite validation. See [Clipboard placement](LEVEL_PASTE.md).


### Face clipboard attributes

`map copy-surface --format map` exports ordered raw faces from repeatable targets;
JSON remains the single-face default. `map paste-surface` detects raw face text
and defaults to `attributes`, retaining target contents and omitted surface
flags/value. Explicit mode selection and normal dry-run/overwrite checks apply.
The same strict parser and transaction service serve normal GUI paste and the
Surfaces panel. See [Face clipboard](LEVEL_PASTE.md#face-clipboard).

### Object clipboard export and cut

`map copy <map> --object entity:id|brush:id|patch:id --output fragment.map` and
`map cut <map> --object ... --clipboard fragment.map --output remaining.map`
share the editor's private preparation, selection validation and ownership
serializer. Selectors repeat. Dry runs validate without writing; overwrite is
explicit for either existing output. Parents must exist and cannot traverse
links. The fragment cannot overwrite either map path. In-place map saves retain
backups. Two-file cut publishes the fragment first and reports `clipboardWritten`
and `mapWritten` on success or failure; failed map saving retains the source.
See [clipboard limits and examples](LEVEL_PASTE.md#cli). **Status: Partial.**


### Two-bone inverse kinematics

`model ik` shares the modeller's two-bone solver over an inclusive clip sample
range. Model-space target/pole inputs, fixed bone lengths, optional nearest
reachable clamping and end-joint following use the shared service. Normal
`--dry-run`, `--output .mesh.json` and `--overwrite` publication rules apply.
See the [CLI manual](manual/cli.md).


### Joint animation curves

`model curves` inspects or edits a clip's stored joint curves through the shared
modeller service. Capture, key insertion/removal/movement, group replacement and
baking accept the same channels and interpolation/tangent modes as the editor.
Writes use `--output .mesh.json`, with `--dry-run` validation and explicit
`--overwrite` for replacement. See the [CLI manual](manual/cli.md).


### Temporary visibility queries

`map visibility` runs session Hide, Show All and Isolate without writing native
map or scene metadata. `--policy inherited` uses TB-style group containers;
`--policy flat` targets existing members. `--hidden` and `--shown` accept object
selectors and `group:<UUID>` identities, rejecting contradictory states. JSON
separates explicit states from `effectiveHidden` object selectors for selection
query handoffs. GUI and CLI share the resolver, cancellation and validation.

`map visibility --mode inspect` reports supplied states unchanged. Read-only
`move --node GROUP --parent NODE`, `assign --object SELECTOR --parent NODE`,
and `duplicate --object SELECTOR --offset x,y,z` modes prepare authoring effects
through the scene/placement services and return the resulting `scene` alongside
visibility. They preserve source bytes and hashes and do not publish map files.

`--profile ID` supplies the shared insertion policy and default flat/inherited
hiding policy. An explicit `--policy` changes hiding only. JSON reports both the
resolved profile and insertion policy; invalid profiles fail before loading or
writing. NRC duplicate previews create fresh inherited states, while TB/default
previews preserve explicit descendants and reveal inserted roots.

### Additive material surface query

`map select-textured` requires `--material` and preserves repeated `--object`
and one-based `--face` seeds. It returns independent brush faces and matching
whole patches, canonical `surfaceTargets`, source hash and exact ordered brush
geometry fingerprints. It shares cancellable preparation, group/visibility/lock
boundaries and complete-result validation with the forthcoming GUI action.
No map or package is written. The GUI command, independent-face state and NRC
shortcut are not yet wired; this route remains Partial. See the
[CLI manual](manual/cli.md#query-textured-level-surfaces).
