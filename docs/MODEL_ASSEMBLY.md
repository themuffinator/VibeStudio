# Model Assemblies

Input [collision boxes](MODEL_COLLISION.md) remain in component mesh sources.
Pose and animation baking report their omission. Fit or author collision on the
baked mesh when the composed prop needs a separate level collision handoff.

**Models > Assemble** links existing models through named attachment tags. The
selected package model starts as the root when one is selected; otherwise the
editor opens an empty recipe. It uses the same software viewport, material
resolver, package snapshots and cancellable model worker as the mesh editor.
No AI connector or additional dependency is required.

An `.assembly.json` source stores links, local transforms and independent
animation settings. It does not modify, embed or merge its referenced models.
Keep these sources together in the project. Save As rewrites file references
relative to the new recipe directory. Package references resolve against the
current package or staged plan; the recipe does not remember a package path.

## Authoring

1. Choose **Add Part**, enter a unique ID and select **File** or **Current
   package**. **Browse…** selects a loose model or a supported package
   entry. OBJ, MDL, MD2, MD3 and editable `.mesh.json` models are supported.
2. The first part is the root. For a child, choose its parent and one of the
   parent's tags. Tags can also be entered by name when repairing missing
   references. Create or repair the tag in **Edit Mesh > Animation**.
3. Set local translation, X/Y/Z rotation and positive uniform scale. Parent
   transforms and scale apply to the complete descendant branch.
4. Choose inclusive first/last frames, FPS, phase in frames, looping and
   interpolation for this part. **Last available** follows the model's final
   frame. Zero FPS holds the chosen phase. A non-looping part stops at its last
   frame. Each part samples the common time independently.
5. Choose **Apply** to commit the form. Select parts in the hierarchy or
   preview; the corresponding geometry is highlighted. **Remove Branch** removes
   descendants too. Undo/redo restores links, settings and selected part.
6. Save the assembly source. **Reload Inputs** reads fresh model and linked-skin snapshots.
   Staging and palette changes refresh the current package context.

**Play** uses elapsed time; the time field also samples exact times while paused.
Reduced motion disables automatic playback and retains manual sampling. The
displayed time always describes the completed composed pose. Reflected tags
preserve orientation and correct winding. Interpolating between opposite tag
handedness, invalid normals or collapsed geometry fails with a diagnostic;
stored poses remain available where valid.

The **Details** drawer lists source paths, input sizes, SHA-256 fingerprints,
per-part frame samples, bake omissions and material diagnostics. Material
previews resolve from the current package; embedded skins show their first
image. Full shader effects and native MDL skin-group schedules are not simulated
by assembly playback.

Missing files or tags clear the preview and disable baking but leave the recipe
editable and saveable. Cancellation leaves the previous document and preview
unchanged. Reload a repaired reference to rebuild the preview. Malformed recipe
syntax or invalid graph edits never replace the current document.

## Linked Skins

Each part's **Materials** control chooses **Model materials**, **Skin file** or
**Package skin**. **Choose Skin…** selects a local Quake III `.skin` file or an
exact occurrence in the current staged package. **Apply** commits the reference
and resolves its surface-to-shader assignments on the assembly worker. The
original model and skin stay unchanged. Choose **Model materials** to remove
the override; undo/redo restores both the reference and selected part.

The [shared skin reader](MODEL_MESH.md#quake-iii-skin-assignments) requires every
model surface to have a binding and uses Quake III's surface-name normalization.
Only primary materials change; alternate slots, geometry, every pose, tags and
collision remain in the model snapshot. Embedded MDL skins require their native
inspector and cannot be replaced through this control. **Details** shows the
skin's path, byte count, SHA-256, package index, before/after materials, ignored
attachment markers and bindings for other surfaces. Textured preview and material
diagnostics use the existing package resolver.

Local references follow the recipe directory and are rebased by Save As.
Package references use the current package or staged plan. **Skin entry** is a
zero-based occurrence index; **Unique path** (`-1` in the recipe) requires exactly
one matching entry. An explicit index must also match the saved path. Reordering
a package can require choosing the skin again, especially for repeated names.
Package changes refresh the input snapshot. **Reload Inputs** rereads loose-file
changes; a previously resolved snapshot remains stable until then. Missing or
invalid skin bytes clear the preview and block baking while leaving the recipe
editable for repair. Cancellation preserves the previous document and preview.

Linked skins use assembly schema 3, including assemblies with native animation.
Each part may carry `skin: {"source": "lower_default.skin", "kind": "file",
"entryIndex": -1}`. Kind is `file` or `package`; file references cannot name an
entry index. Removing the last skin returns to schema 2 when native animation is
present, otherwise schema 1. Older schemas cannot contain skin fields. Source
history and recovery preserve references, not copies of external skin bytes.
Each skin is limited to 64 KiB and shares the 256 MiB assembly input-read budget.

Pose and animation bakes write the resolved shader assignments into the composite.
Generated surface names use `<part>_s<index>` so Quake III's suffix normalization
cannot collapse different surfaces from one part into the same native name.
Their normal mesh/native export, package staging and level-placement workflows
then apply. Bakes do not publish a companion `.skin` file or retain a live skin
link. Original model, skin, package and recipe inputs are protected by write
guards. Player-bundle publication, live skin selection in ordinary model browsers
or level instances, source-port hiding directives and full shader effects remain
separate work.

`add` and `update` accept `--skin PATH`, optional `--skin-kind file|package`
(default `file`), and `--skin-entry-index N` for a package occurrence. A new
reference without an index uses a unique path. Omitted skin options retain the
current reference; `update --clear-skin` removes it. Kind/index require `--skin`,
and `--clear-skin` cannot be combined with it. CLI relative file paths resolve
from the working directory; saved recipe references resolve from its directory.
JSON adds each resolved input's `skin` receipt, including original content
identity and complete assignments. All ordinary dry-run and overwrite rules
apply; invalid or missing linked skins fail before a CLI output is written.

```sh
vibestudio --cli model assembly ./player.assembly.json --operation update \
  --part lower --skin ./lower_blue.skin --output ./player-blue.assembly.json --json
vibestudio --cli model assembly ./player-blue.assembly.json --operation update \
  --part upper --skin models/players/custom/upper_blue.skin --skin-kind package \
  --package ./assets.vibepackage --output ./player-blue.assembly.json --json
vibestudio --cli model assembly ./player-blue.assembly.json --operation update \
  --part lower --clear-skin --package ./assets.vibepackage \
  --output ./player-original-lower.assembly.json --json
```

## Quake III Native Animation

**Native Animation…** authors or imports a player `animation.cfg`. Bind distinct
lower and upper assembly parts, select the lower/upper clips to preview, then
choose **Apply**. The ordinary timeline, pose bake and animation bake consume
those selections. Native playback replaces the bound parts' ordinary range,
FPS, phase and loop settings; their interpolation switches still apply. Other
parts keep their own settings. Clearing **Use native animation** removes the
binding in one undoable edit. Renaming a part updates the binding; remove or
rebind native animation before deleting a bound branch.

The fixed list contains all 31 authored Quake III slots. Each slot has a native
first frame, count, loop-tail count, FPS and reverse switch. **Model frames**
shows the adjusted range that will index the actual model. Quake III subtracts
`LEGS_WALKCR.first - TORSO_GESTURE.first` from rows `LEGS_WALKCR` through
`LEGS_TURN`; BOTH and torso rows remain unadjusted. Consequently the adjusted
`LEGS_WALKCR` start always equals `TORSO_GESTURE`'s start. Changing either anchor
can move every leg range. **Use Selected Clip** maps a saved mesh clip's local
range and FPS into the selected native slot and rejects an incompatible anchor.
All slots, including unselected ones, must fit their bound models. BOTH slots
must fit both models.

A zero loop tail holds the final playback pose. A positive tail plays the
introduction once, then loops the last N playback steps. Reverse applies to the
complete sequence, including the tail. Preview uses the engine's integer
millisecond period: 15 FPS becomes 66 ms per pose. Time zero is the first pose
of the selected clip. This is steady clip sampling; transition blends, haste,
procedural body aiming, footsteps and voice playback are outside the preview.
Head offset, sex/voice set, footsteps and fixed-body flags are preserved in the
configuration for the game. They do not alter assembly placement.
Geometry and attachment interpolation retain the studio's existing assembly
rules. In particular, its shortest-arc tag rotation differs from the
[original renderer’s normalized basis-vector blend](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_model.c);
the configuration check does not
certify renderer equivalence.

**Import…** reads a loose `animation.cfg` into the pending dialog. Import
accepts whitespace, quoted tokens, line/block comments, supported header
directives and 25–31 complete animation rows. Missing legacy torso gestures
are filled from TORSO_GESTURE with forward playback, and zero FPS becomes 1;
both normalizations are reported. Unknown or repeated directives, extra or
partial rows, invalid ranges and unsafe numeric values fail without replacing
the draft. Source-port extensions require their own explicit support. Limits
are 19,998 bytes, 1–1,024 frames per clip, 0–count loop frames, 0.001–1,000 FPS
and adjusted ranges within the modeller's 1,024-pose limit.

**Apply and Export…** commits the configuration, then writes a `.cfg` derivative
using the normal overwrite and input-protection guards. Export retains settings
but normalizes whitespace/comments and writes all 31 rows in native order.
Canceling the file chooser retains the applied, undoable configuration. The
assembly and all source models remain separate. Sources with native animation
use assembly schema 2 (schema 3 with linked skins): `q3Animation` contains lower/upper part IDs, the selected
native slot names and canonical native text in `config`. Ordinary recipes still
write schema 1. Schema 2 is retained by undo, source save/reopen and recovery;
older applications must reject it rather than discard the configuration.

Export the original lower/upper models with matching poses and attachment tags
for use with this configuration. A composed bake has different frame numbering
and must not reuse the original configuration automatically. Package the native
configuration through the existing file import/staging workflow, or publish the
complete [native player package](#native-player-packages). Direct config import
from a package entry, generated flag and
backward-walk runtime slots, source-port dialects, Quake/Quake II game-code
configuration and original-engine gameplay acceptance remain separate work.

```sh
vibestudio --cli model assembly ./player.assembly.json --operation animation-set \
  --config ./animation.cfg --lower-part lower --upper-part upper \
  --lower-animation LEGS_RUN --upper-animation TORSO_ATTACK \
  --output ./player-native.assembly.json --json
vibestudio --cli model assembly ./player-native.assembly.json \
  --operation animation-export --output ./out/animation.cfg --json
vibestudio --cli model assembly ./player-native.assembly.json \
  --operation animation-clear --output ./player-ordinary.assembly.json --json
```

`animation-set` requires `--config`, `--lower-part` and `--upper-part` for a new
binding. For an existing binding, supply only the configuration or choices to
change; slot names are exact native identifiers. `inspect --time`, `bake` and
`bake-animation` use the saved native selections. JSON retains configuration,
input fingerprints, actual frame samples and normalization notes. Native
inspection also includes structured `nativeAnimation.clips` with native/model
first frames, count, loop tail, reverse, FPS and effective millisecond period.
Native operations share package contexts, dry runs and guarded writes with other
assembly operations. The loose `--config` path resolves from the CLI working
directory.

## Native Player Packages

**Player Package…** publishes a non-team Quake III player from a resolved assembly.
Configure **Native Animation…**, then use exactly three distinct parts: lower as
the root, upper on lower's `tag_torso`, and a single-pose head on upper's `tag_head`.
Merge accessories into those parts first. Choose a player ID, skin ID, head part
and icon, then **Review Package**. The review lists every virtual path, role, byte
count and SHA-256, followed by target limitations. Selecting a file with the
keyboard or pointer shows its complete path and hash in Details. Long names keep
both ends visible when elided. Changing an option clears the
review; **Export PK3…** becomes available after another successful review.

Review runs on a cancellable worker and writes no package. Publication uses the
same atomic, reproducibility-verified package writer as the Package Manager. An
assembly or package-context change requires a new review. Export uses the captured
bytes, so later external file edits cannot silently change a reviewed package.
Reload Inputs and review again when those changes should be included. Source
models, linked skins, the recipe, icon and input package/folder are protected,
including output backup paths. An approved overwrite retains the verified old
package in the writer's backup. Publication reports commit state and any recovery
or finalization warning instead of claiming that an already committed file vanished.

The package contains:

- `models/players/<player>/lower.md3`, `upper.md3` and `head.md3`, retaining all
  native poses, surface materials and complete attachment tracks. Local transforms
  and inherited scale are baked into each part's vertices, normals, frame origins
  and tags. These remain separate animated models; no sampled composite is used.
- `lower_<skin>.skin`, `upper_<skin>.skin`, `head_<skin>.skin` with normalized native
  surface names and the resolved primary assignments, including linked `.skin`
  overrides. Alternate material slots remain in the models.
- `animation.cfg` with the original frame mapping, and `icon_<skin>.tga`, converted
  through the Texture Editor's export service without changing the input image.
- Referenced shader scripts and material images/videos, collected from every
  retained material slot through shared dependency validation. The captured bytes
  are checked again for a complete dependency closure before publication.
- `vibestudio/player-bundle.json`, a deterministic receipt of source identities,
  native roles, export reports and generated file hashes. It contains no machine
  paths or timestamps; its file list excludes the receipt itself.

Open the project asset package/folder before review. CLI `--package` also accepts
a portable `.vibepackage` draft so staged material edits use the same workflow.
Icons can be loose files or exact package entries; duplicate paths require an
explicit matching entry number. Icons must decode as one image of at most
1024 × 1024 pixels. They are converted to bottom-left, true-color TGA pixels.

Player and skin IDs use 1–24 ASCII letters, digits, underscores or hyphens and
start with a letter. Generated game paths must fit 63 characters; unsafe filesystem
names are rejected on every platform. Dependencies must use native `.tga`, `.jpg`,
`.shader` or `.roq` paths within that limit, using printable ASCII. Native image
lookup uses explicit stage filenames and the original TGA-to-JPG fallback; it
does not substitute PNG, extensionless stage images or JPG-to-TGA alternatives.
Implicit model image shaders default to TGA; shader declaration lookup follows
the original first-dot extension stripping. TGA materials must be uncolormapped
RGB/RGBA or grayscale with bottom-left origin: the original game's loader ignores
both orientation flags. Convert other image variants in the Texture Editor first.
Unresolved, duplicate, unreadable or truncated dependencies block review. Limits
are 4,096 output entries and 256 MiB total; individual source reads use the normal
64 MiB modeller limit and shader scanning keeps its shared bounds.

Select the result in non-team play as `<player>/<skin>`. Team variants, custom
sounds, generated LODs and gameplay validation are separate work. The game supplies
default player sounds. Whole referenced shader scripts are retained; review their
other declarations for conflicts in the target game. Shader-language behavior,
video decoding, procedural aiming and runtime tag interpolation are not certified
by dependency closure. Editable collision boxes and custom clip metadata stay in
the source project, with normal exporter notes in the review. The package can be
opened in the Package Manager for normal inspection and deployment workflows;
publication does not modify the active project package or launch a game.

```sh
vibestudio --cli model assembly ./player.assembly.json --operation player-review \
  --player-name myplayer --skin-name default --head-part head \
  --icon ./icon.png --package ./assets --json
vibestudio --cli model assembly ./player.assembly.json --operation player-export \
  --player-name myplayer --head-part head --icon textures/player/icon.tga \
  --icon-kind package --icon-entry-index 7 --package ./assets.vibepackage \
  --output ./myplayer.pk3 --dry-run --json
```

`player-review` accepts no output/overwrite/dry-run options. `player-export` requires
`--output` ending in `.pk3`; omit `--dry-run` to publish and use `--overwrite` only
for an intended replacement. CLI loose icon paths resolve from the working
directory; GUI relative paths resolve from the assembly directory. The default
skin is `default`, icon kind is `file`, and omitted package index means a unique
path. Text and JSON include dependencies and per-file hashes; export JSON includes
`write.outputCommitted`, `dryRun`, `determinismVerified`, output hash, backup and
recovery information, including publication failures. Each CLI invocation prepares
its own current snapshot; a prior `player-review` is not a durable authorization token.

## Baking And Studio Handoff

**Bake Pose to Mesh** explicitly creates one composed pose in the normal mesh
editor. That document supports topology/UV edits, source save/recovery, native
export, package staging and Quake III level placement through existing services.
Close another mesh document before opening a bake. The assembly stays open with
all its links and playback settings.

**Export Pose…** writes OBJ, MD2 or MD3 using the normal format limits and guarded
writer. The review explains that a bake omits hierarchy, independent animation,
attachment tags and native skin metadata. It retains positions, normals, indexed
UVs, seam marks and surface material paths in the transient mesh; native formats
may impose additional losses. Embedded images are not written as texture files.
The original assembly and model sources are the editable originals.

**Bake Animation…** reviews a clip name, start time, frame count and sampling FPS,
then opens the complete sequence in the Mesh Editor. **Export Animation…** uses
the same review and worker to write `.mesh.json`, MD2 or MD3. Frame `i` samples
the assembly at `start + i / FPS`, independently applying every part's rate,
phase, looping and interpolation. The last sample is `start + (count - 1) / FPS`;
the clip lasts `count / FPS`. No duplicate endpoint is appended. Choose an interval
whose endpoints make sense for looping; smooth preview can blend the last pose
back to the first. The review reports sample times, duration and omissions.

The baked mesh retains every composed position and normal, ordered surfaces,
UVs, seam marks and external material slots. It stores one named clip with its
sampling rate; editable mesh schema 6 retains that rate through save, undo and
recovery. Selecting the clip uses the saved rate for preview. Links, tags,
independent playback, collision and native skin metadata stay in the originals.
Embedded images are not emitted as textures. Native MDL group timing does not
replace the assembly's explicit per-part frame settings.

Sampling accepts 1–1,024 frames and 0.001–1,000 FPS, with every time within
0–1,000,000 seconds. The combined output must fit 65,536 vertices and 1,048,576
stored frame vertices. Capacity is checked before sequence allocation. Every
sample must validate and retain identical topology, winding, UVs and materials;
changing attachment handedness cannot silently change triangle order. Failure
or cancellation publishes no partial sequence. Native format limits still apply:
MD2 needs one surface and at most 512 frames. Bake to mesh and explicitly join
compatible surfaces when needed. OBJ is intentionally unavailable for animated
export. MD2/MD3 do not store this clip's FPS; configure target-game timing
separately and retain the editable source.

The optional [baked-animation engine check](MODEL_ENGINE_ACCEPTANCE.md#baked-assembly-animation)
authors a nested animated chain through the CLI and checks its MD2/MD3 exports
against independent geometry calculations and FTE render-target images. It
covers sampled composition, native vertex blending and external textures in a
small original fixture. Target-game timing, live attachments, full shaders,
maximum-assembly performance and original-engine gameplay still need their
own acceptance.

Existing outputs require explicit overwrite. Assembly, model and linked-skin inputs, active
package sources and portable-draft assets are protected. Saving back to the
loaded recipe requires the exact reviewed on-disk bytes; even overwrite cannot
bypass an external change. New outputs use no-replace publication and all writes
check destination identity again at commit time.

## CLI

```sh
vibestudio --cli model assembly --new --name Player --part root \
  --model ./models/lower.md3 --fps 10 --output ./models/player.assembly.json
vibestudio --cli model assembly ./models/player.assembly.json --operation add \
  --part upper --model ./models/upper.md3 --parent root --tag tag_torso \
  --first-frame 0 --last-frame 20 --fps 12 --output ./models/player.assembly.json
vibestudio --cli model assembly ./models/player.assembly.json --time 0.5 --json
vibestudio --cli model assembly ./models/player.assembly.json --operation update \
  --part upper --translation 0,0,2 --rotation 0,0,15 --scale 1 \
  --phase 0.25 --loop on --interpolate on --output ./models/player.assembly.json
vibestudio --cli model assembly ./models/player.assembly.json --operation bake \
  --time 0.5 --output ./out/player.obj --dry-run --json
vibestudio --cli model assembly ./models/player.assembly.json --operation bake-animation \
  --time 0 --frames 30 --sample-fps 30 --clip-name walk \
  --output ./out/player.mesh.json --dry-run --json
```

Operations are `inspect` (default), `add`, `update`, `remove`, `bake` and `bake-animation`.
`--new` starts with `add`; it accepts `--name`. `add` requires `--part` and
`--model`; child additions also need `--parent` and `--tag`. `update` changes only
provided fields; `--rename-to` updates child links. `remove --part <id>` removes
the branch. All mutations and bakes require `--output`.

`bake-animation` requires explicit `--frames` and `--sample-fps`; `--time`
defaults to zero and `--clip-name` defaults to `assembly`. These settings do not
rewrite per-part `--fps`. JSON adds `sampling` with start, last sample, count,
FPS, clip name and duration, plus an `export` report with stored vertex count.
`quantizationMeasured` is true for MD2, whose position/UV/normal errors are
reported; other formats omit unmeasured error fields. Input frame samples
describe the start time. Dry runs sample and validate the entire sequence.

Use `--kind package --model models/upper.md3 --package ./assets.pk3` for package
references. `--package` also accepts a folder or `.vibepackage` draft and must be
supplied whenever a recipe has package parts. `--palette` selects a package
palette. File `--model` values resolve relative to the process working directory;
file references already in a recipe resolve relative to that recipe.

`--first-frame`, `--last-frame` (`-1` means last available), `--fps`, `--phase`,
`--loop on|off` and `--interpolate on|off` control each part. Time is in seconds.
`--translation` and `--rotation` use comma-separated X,Y,Z values, with rotations
in degrees. JSON includes the recipe, dependency fingerprints, sampled frames,
geometry counts, notes and explicit `written`/`dryRun` fields. Inspection and
dry runs leave files unchanged. CLI mutations validate referenced inputs before
writing; use the GUI to save an intentionally incomplete recipe for later repair.

`--dry-run` runs the same resolution, sampling and destination guards as a real
write. `--overwrite` permits existing derivative or Save As destinations; source
conflict and input-protection rules still apply. Unknown, repeated or irrelevant
options are rejected. Exit codes are `0` for success, `2` for usage, `4` for
recipe/model/write validation errors, and `1` for package-open failures.

## Recovery

**Keep local recovery copies** defaults on and shares the Mesh Editor's
`model/recoveryEnabled` preference. Committed edits queue an atomic background
checkpoint; every five seconds a dirty document also captures its selected part
and completed timeline position. One active write and one replaceable pending
snapshot keep disk work off the UI thread. Turning the preference off pauses
checkpoints and retains the existing copy. Brief storage-lock contention is
retried on the worker; persistent contention reports a failure and preserves the
preceding copy. Save, approved Discard and undo back
to a clean source retire the current session's copy after any active write ends.
Older interrupted copies remain until explicitly discarded.

**Recoveries…** lists verified copies and invalid entries with diagnostics.
**Restore as Draft** preserves the complete recipe, selection, time and original
source fingerprint, and starts a dirty document with no save binding. The original
source remains protected even with overwrite enabled; choose a different Save As
path. An empty recovered recipe is also dirty. Missing inputs leave the recipe
repairable; package parts use the currently open package or staged plan.
Restoration preserves the reviewed recovery copy, including when recovering from
the current editor's copy. A new dirty session receives a new recovery identity.

```sh
vibestudio --cli model assembly --operation recoveries --json
vibestudio --cli model assembly --operation recover --recovery UUID \
  --sha256 REVIEWED_SHA256 --output ./out/recovered.assembly.json --dry-run --json
vibestudio --cli model assembly --operation discard --recovery UUID \
  --sha256 REVIEWED_SHA256 --dry-run --json
```

Use the UUID and SHA-256 returned by `recoveries`. `--directory` selects another
recovery folder. `recover` accepts `--overwrite` for an existing destination other
than the original source. It does not resolve or require available inputs; inspect
the saved recipe with its intended package afterwards. JSON reports the recovered
selection and time; `.assembly.json` itself stores only the authoring recipe.
`discard` requires the exact reviewed bytes and refuses an active editor's copy.
Its dry run verifies the digest without writing lock files; locks are checked on
commit. Unknown, duplicate and irrelevant options remain errors.

Copies use schema 1 `.vsassemblyrecovery` records in `assembly-recovery` beneath
the application data directory, or beside an explicit settings file. They contain
linked references and source paths, not model/texture/package bytes. Each copy is
limited to 2 MiB, automatic storage to 32 copies and 32 MiB, and a scan to 128
entries and 32 MiB of reads. Invalid entries count against storage limits; full
storage reports a failure and never prunes older work. A Qt session lock protects
live writers, and checksums, strict validation and guarded publication detect
corruption or changed copies. Folder links and linked recovery files are refused.

Recovery covers applied recipe edits. Unapplied inspector text, undo history,
unsaved input-model edits and prior package contents are not included. Save the
assembly and its referenced assets normally for durable project storage.

## Bounds And Release Work

All source versions use `format: "vibestudio-model-assembly"`, a name and a part
array. Ordinary recipes use schema 1, native animation uses schema 2, and linked
skins use schema 3 with optional native animation.
Unknown keys, duplicate IDs ignoring case, missing parents and cycles are
rejected. Nonempty graphs have exactly one root. IDs use an ASCII letter followed
by letters, digits, underscores or hyphens, up to 32 characters. Sources are
bounded to 1 MiB and 32 parts. Each model read is at most 64 MiB; the combined
read budget is 256 MiB. Geometry is bounded to 32 surfaces, 65,536 vertices,
131,072 triangles and 4,194,304 stored frame vertices across inputs. Individual
inputs also satisfy the editable mesh limits. Time is bounded to 1,000,000
seconds; FPS to 1,000; local scale to 0.000001–1,000. Composed coordinates and
accumulated scale are checked before publishing a pose. History retains at most
64 edits under a conservative 16 MiB serialized-state estimate.

Source references are live references, not immutable project locks. Each resolve
captures verified bytes and fingerprints; preview/export share that snapshot
until reload. Distribute the models and linked skins with the recipe and supply the intended
package context when reopening it.

Quake III native configuration has the bounded workflow above. Other game
configuration, original-engine gameplay, assembly collision tracks and
maximum-assembly performance remain open. Skeletal models remain outside this
workflow. The complete [modeller release gate](MODELLER_RELEASE.md)
also requires native platform and physical accessibility acceptance.
