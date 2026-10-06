# Level Editor Quality Plan

The target is a full-featured, professional-standard editor for the supported
idTech1, idTech2, and idTech3 workflows. A working subset or passing smoke suite
does not establish completion. Each area below needs working authoring controls,
shared service/CLI behavior where meaningful, persistence, and direct evidence.
Changes must preserve the module-harmony rule in `AGENTS.md`.

## Generated levels

**Generate** on the Levels header (or `map generate`) builds a sealed,
playable level from a description for Quake, Quake II, Quake III, or Doom,
planned by deterministic rules or by the configured text model, and opens it
here as a new, unsaved map once reviewed. Its rooms are plain brush boxes on
a 32-unit grid (Doom: sectors from the same cells), so every editing tool
applies to them. See `docs/AI_AUTOMATION.md` for the pipeline and limits:
stairs, pits, pillars and daises only; no doors, lifts, or teleporters yet;
links that cannot be routed are reported and left out; Doom maps need their
nodes built before they are played.

## Edit with AI

**Edit with AI** on the Levels header (or `map ai-edit`) takes an
instruction for the open Quake-family map; the configured text model
proposes actions (add an entity, set or remove a key, add a box brush,
retexture, move, delete) that are checked against the map and listed for
review before any is made. Applied actions are ordinary editor edits, each
its own undo step, and the selection can be named in the instruction ("the
selected brushes"). See `docs/AI_AUTOMATION.md`.

## Familiarity coverage and navigation

The [profile catalog](EDITOR_PROFILES.md) contains 19 working schemes spanning
classic brush editors, Doom editors and modern scene editors. QuArK now has
four-view controls; Hammer-family profiles connect familiar editing keys and
F9 to existing build/test services. Modern profiles support held-button flight,
and Godot/Blender distinguish middle orbit from Shift+middle pan. Focus loss,
hidden views and profile changes end navigation. Settings, layout overrides,
Controls help, CLI aliases and adaptation notes share the same catalog.
Standalone NetRadiant and Sledge have separately audited defaults. Sledge's
temporary hold-Space navigation, pitch keys, arrow translation and mouse-button
pan share configurable navigation services; standalone NetRadiant keeps its
own 8-unit grid, 110-degree FOV, zoom/deletion and clone bindings.

This advances familiarity coverage without closing the professional-grade
target. Complete component manipulation, Doom modal/visual editing, additional
Radiant variants, native preference import, native
input/accessibility acceptance, production-map performance and cross-platform
execution still need evidence.
Profiles do not change supported formats, scene/undo semantics, asset paths,
compiler targets or deployment permissions.

Per-profile gesture overrides now share validated settings, GUI editing,
portable VibeStudio import/export and `editor gestures` CLI diagnostics. The
60 supported settings, including fly/drive, pitch and hold/toggle navigation keys, route through the
same plan/camera controls and update the
Controls reference. Applying a draft preserves authoring state and the current
view workspace, including a bookmarked camera projection and temporary pane
maximization. Core/CLI tests cover all profile defaults, atomic rejection, round trips,
dry runs and protected settings. The shell suite exercises staging, apply,
profile reselect, all plan panes, camera state, undo/redo and 100%/200% text
scaling, including high-contrast RTL renders with expanded translations. Wrapped
label allocation and accessible status updates have regression checks. Native
keyboard/mouse and screen-reader acceptance remain open. See
[Gesture customization](EDITOR_PROFILES.md#gesture-customization).

The shared camera-key resolver also retains vertical drive in free look and
makes Radiant's duplicated fly/drive turning keys strafe consistently there.
Reserved keys, duplicate directions, conflicting always-active fly/drive keys
and inaccessible fly bindings are rejected. Modal fly bindings can override drive
keys during mouse look, and required held-look modifiers work with movement.
Potential command overlaps remain visible in GUI details and CLI diagnostics.
Physical movement is not simulated by the acceptance suite.

## Camera brush creation

Quake-family maps offer **Draw Brush** in the camera tool selector, Edit menu,
context menu and command palette. Choose XY, XZ or YZ, then left-drag a footprint
and release to create a box. The shared grid/snap setting and the profile's
plan square/cube modifiers apply. Wheel steps during the drag adjust depth;
**Base** and **Depth** supply exact coordinates. **Use Work Zone** takes the
last selection's hidden-axis range, falling back to 0–64 in an empty map.
Document, list and camera selection updates now refresh that work zone too.
**Numeric Brush…** opens the existing primitive preview with the same material,
plane and depth for keyboard editing or a different primitive shape.

The construction grid works in empty maps. A dashed box and live dimensions
show the camera draft; all three plan panes show corresponding outlines without
adding geometry to their documents or worker indexes. The shared material
picker receives texture-browser choices and sampled materials and supplies both
plan and camera creation. Empty maps display their default material; a cleared
picker uses the same fallback in numeric and drawn creation. Release uses the normal brush service, creation-layer
membership, inherited locks, selection, one-step undo/redo, recovery and save.
The resulting map follows the existing build, dependency and package workflows.
Camera gestures add no new file dialect or cloud requirement.

Escape cancels the draft, then leaves the tool. Focus loss, camera/plane/grid,
selection, material, source and tool changes cancel pending creation; a changed
creation destination is rejected on release. Invalid, backward or grazing rays,
collapsed boxes and coordinates outside ±32768 cannot create geometry. Drafts
must have at least one unit on every axis. Paint/Sample and navigation retain
their profile behavior outside the explicit drawing gesture. No default shortcut
is reserved; `map.draw-brush` is available for custom bindings.

`box-draw-smoke` checks ray/grid/constraint math. Camera UI tests cover all 19
profiles on all three planes, cancellation and enlarged high-contrast RTL
renders. The shell suite checks empty-map creation, linked draft outlines,
work-zone/material/numeric handoff, scene destinations/locks, exact undo/redo and
save/reload. `level-camera-brush-latency-smoke` measures 64 draft proposals on
complete 1,000/10,000-brush scenes and verifies no geometry render rebuilds;
`VIBESTUDIO_LEVEL_BRUSH_MAX_CALL_MS` can enforce a local GUI-call budget.
Tests use semantic Qt calls and widget render targets, not physical input.

Surface-aligned/slanted construction planes, native input and screen-reader
acceptance, production-map and full-shell creation latency remain open.
The camera draft is an outline rather than a textured solid; committed geometry
uses the normal material renderer. CLI `map add-brush`, including its `--shape`
option, uses the same creation services without a camera gesture.

### Cancellable brush insertion

Plan and camera release, and the public shell primitive operation, prepare new
brushes on the shared placement worker. Longer preparations show **Add Brush**
with a named geometry/insertion/history phase and Cancel; fast operations avoid
a flashing dialog. Cancellation discards the private candidate, preserving the
source, selection, creation layer and Undo/Redo. Geometry construction and map
scans include cooperative cancellation checks. Allocation and native library
calls are not individually interruptible.

**Add Brush… / Numeric Brush…** uses that same cancellable preparation for its
coalesced previews. Apply publishes the exact validated preview without building
the edit again. Publication checks source identity/revision, selection, creation
destination, save state, package revision and palette. A stale draft stays open
with an error and cannot overwrite the current map. All paths retain one undo
step, scene locks, native face dialect, materials and normal save/build/package
handoff. The CLI's existing `map add-brush` contract is unchanged and invokes
the same underlying authoring service.

`level-brush-jobs-smoke` covers all five primitive shapes in four face dialects,
destination membership, exact Undo/Redo and save round trips, inherited locks,
invalid requests, cancellation through final preparation and inside geometry
construction. The UI suite checks worker cancellation and progress at 100% dark
and 200% high-contrast RTL with expanded text, plus actual-shell plan/numeric
publication and stale-source/selection/destination refusal. The latency variant
separately measures complete shell Apply and event-loop gaps on generated maps.
These semantic Qt tests do not exercise physical input or screen readers.
Publication and workbench refresh still run on the GUI thread; preparation
timings alone do not establish full-editor responsiveness.
Material summaries now count surfaces directly and normalize each distinct
spelling once. Selected Doom linedefs gather their sidedefs in one pass, with
shared sides counted once. The material picker, texture tiles and CLI reports
share these counts; the brush inspector and map statistics share the sorted
name list. A full workbench update computes statistics once for its summaries
and health state; viewport selection also refreshes the inspector once after
selection has settled. `level-texture-summary-smoke` covers ownership, sparse IDs, shared
sides, case handling and palettes up to 50,000 distinct names.
The shared textual preview now formats only its displayed prefixes (24 brushes
or lines, 12 patches or sectors, 16 entities or things), retaining the same
total-count notices and CLI text. Boundary tests compare both map families with
complete small references and 10,000-record inputs. Full-shell publication and
refresh latency remain performance acceptance gates.

### Object list and queries

The Objects list uses a snapshot-backed Qt model. Ordinary layout and painting
format the requested rows instead of constructing a widget item and text for
every object. An identity index resolves selection directly, including sparse
IDs after deletion. The native list retains map order, extended selection,
primary-object framing, context menus and hidden-object marks. Rows use two
elided lines that follow text scaling; tooltips and accessible text retain the
complete field content, including source line breaks.

Text and property filters run on a coalesced worker. **Objects (filtering…)**
marks pending results; the filter field remains available, and clearing it
cancels and restores the complete list. Only results for the current query,
document and visibility snapshot are applied. Enter during filtering selects
the completed current result; another query, clearing or replacing the map
discards that pending selection. Filtering alone preserves the document's
selection, including members outside the filtered rows. Select Matching keeps
temporarily hidden and scene-hidden objects out, as before. The property index
and comparisons remain shared with CLI `map find`; cancellation discards an
incomplete index. The CLI's query contract is unchanged.

Creation, undo, save, diagnostics and the plan/camera panes use the same object
identities. Brush insertion now records the previous selection for undo and
the new brush for redo. Core and semantic Qt suites cover source replacement,
query cancellation/coalescing, sparse IDs, hidden state, selection handoff,
native Doom rows, accessible metadata and enlarged RTL rendering. Native input,
screen readers, other operating systems and realistic release-scale latency
remain separate acceptance work.

## Camera selection resizing

Quake-family selections expose six labelled face handles in the camera: X−/X+,
Y−/Y+ and Z−/Z+. Drag a square or its label with an unmodified left button.
The opposite face stays fixed; the moved face follows the shared grid/snap
setting. Crossing the opposite face clamps the size instead of mirroring or
collapsing geometry. Near-parallel or indistinguishable handles are hidden;
orbit the view or use **Resize Selection** for numeric keyboard editing.
This explicit handle gesture is available with all 19 profiles, including
orthographic orbit cameras, and appears in Controls and `editor controls`.
Paint/Sample and mouse-look keep their own input while active.

Brushes and patches preview their affine geometry on the existing background
camera renderer. Selected point entities/models and Doom things change their
spacing without stretching their visuals. Live dimensions appear in the camera
status and accessible description. Labels remain separate at enlarged text
scales, retain left-to-right axis identifiers in RTL layouts, and connect to
their handles with leader lines. Escape, focus loss, camera/grid changes,
selection changes, loading or replacing the document cancel the gesture.
An unchanged selection refresh preserves it. No-op drags create no undo step.

Release uses the same cancellable resize service as plan handles, numeric
editing and `map resize`. Source revision/load/selection checks prevent stale
publication. Scene locks, geometry validation, Texture Scale Lock and explicit
Valve 220 conversion policy still apply. Success creates one shared undo step,
refreshes sibling panes and saves through the normal map writer. Bounds include
hidden primitives owned by selected entities, matching numeric editing.

Doom camera handles currently support thing spacing in XY. UDMF geometry
resizing remains in the native plan/numeric tools: its shared vertices and
adjacent surfaces need a topology-aware camera preview. Brush UVs retain their
current mapping during the drag; the committed preview applies the actual
texture-lock policy. Live unlocked UV projection, surface-aligned brush drawing, production
map latency, physical input and native assistive-technology acceptance remain
open. Semantic tests compare live geometry with separately rebuilt committed
geometry, exercise every profile, and cover shared undo, pane refresh and save.
`level-camera-resize-latency-smoke` queues 64 proposals over complete 1,000- and
10,000-brush synthetic scenes (12,000 and 120,000 triangles). It records GUI-call
and event-loop delays plus completed worker pictures; the optional
`VIBESTUDIO_LEVEL_RESIZE_MAX_CALL_MS` environment setting enforces a local budget.
This measures preview responsiveness, not large-map transaction or native-input
latency. Single-line readouts use font-based heights so changing dimensions
cannot shift a splitter by a pixel and cancel its camera gesture.

## Prepared Builds with Current Assets

For a Quake-family map, **Build > Prepare Build Workspace** captures the current
document, including unsaved geometry and entity edits, with the complete open
package or draft. Generated models, staged textures, shaders and sounds use the
same package paths as Levels. Preparation checks explicit dependencies, streams
independent asset copies, records SHA-256 hashes and publishes a new directory
only when all inputs succeed. The original map and draft remain unchanged.

**Use in Build** pins the captured map as the Build input. It refuses a handoff
if the live map or package changed during preparation. Later edits need a new
workspace. Existing ericw-tools/q3map2 pipelines, logs, cancellation, diagnostics, artifact
validation and command manifests remain the execution path. Every run verifies
the captured inputs before starting a compiler and checks them again afterwards.
Missing, changed or unexpected asset files fail the build, including failures
before the first stage. **Use Open Map** returns to the normal saved-file loop.

**Build > Publish Prepared Build** verifies and reviews the captured assets,
compiled map and generated runtime files before writing a PAK (Quake/Quake II)
or PK3 (Quake III). The review lists up to 300 files per page, supports PK3
compression and optional build-source inclusion,
and requires explicit overwrite with a verified `.bak` of the previous package.
Hashing and publication run on a cancellable worker; changed or failed outputs
cannot be published. A new build invalidates the previous review.
**Build > Open Prepared Assets** opens the captured game asset folder in
Packages, using the normal pending-draft review, for general inspection/editing.
**Build > Deploy Prepared Build** reviews a complete PAK or PK3 for a matching
Quake, Quake II or Quake III installation. Choose one portable game folder inside
its root. Quake III uses `<game-folder>/vibestudio_<map>.pk3`; classic targets use
a numbered `pak<N>.pak` selected by the **PAK slot** control. The review includes the existing package
hash, file inventory and windowed launch command. Read-only installations need
**Allow this asset deployment** for this operation; saved permissions do not
change. Replacement requires the existing overwrite-with-backup control.
Changing the game folder or PAK slot invalidates the review. Hashing, packaging and launch
preparation run on the worker. Cancellation, a changed receipt/destination, or a
changed executable prevents the next publication/launch step. A launch failure
after publication explicitly reports the package that was already deployed.

**Build and Launch** runs the captured build, then opens this deployment review.
**Launch Game** also uses the review for a prepared input. The map/profile are
fixed to the captured map and target engine, while the target folder is editable.
The single-BSP **Add to Package** shortcut remains disabled for prepared inputs.
CLI equivalents are `build prepare`, `build run-prepared`, `build artifacts`,
`build publish-prepared`, `build deploy-plan` and `build deploy-prepared`; see
[CLI Strategy](CLI_STRATEGY.md).

Deployment rejects links/junctions, protected source/workspace paths and
registered base/mod package destinations, including the backup path. The shared
atomic publisher checks the reviewed destination hash under its save lock.
Launch is optional and uses the matching engine profile. Quake uses `-basedir`,
`-game`, `+map` and `-window`; Quake II uses early `+set basedir`, `+set game`,
`+map` and `+set vid_fullscreen 0`. Quake III uses the installation as base/home,
`sv_pure 0`, `r_fullscreen 0`, the chosen `fs_game` and captured `devmap`.
All use the installation as working directory. This may write engine configuration/logs in
the selected installation. Other packages or loose map files can still affect
engine search precedence; the review warns when it finds them in the target
folder. It does not prove runtime dependency closure, resolve all search-order
conflicts, manage running engine instances or undo engine-created files.
Other idTech3 games require their own compatible layout/profile work.

For PAKs, **Automatic** first reuses a matching map/engine receipt in the chosen
folder, otherwise it chooses the first available loadable slot. Quake requires
every earlier numbered PAK to exist; VibeStudio supports slots 0–999. Quake II
checks slots 0–9 even when earlier slots are absent. An explicit slot can review
replacement of an unregistered package, with the same permission and backup
requirements. Registered base/mod PAKs are always protected. Nonportable slot
names, links, invalid receipts, ambiguous remembered slots and Quake numbering
gaps are refused. A changed remembered PAK requires an explicit slot review.
Other numbered PAKs can override the map/assets; this is reported as a warning.

The bounded `.vibestudio-pak<N>.json` receipt records the engine, map, slot and
published PAK hash. It is a destination hint, never overwrite permission, and
reserves the slot if the PAK is removed. A new workspace for the same map can
reuse it. A game-folder lock serializes VibeStudio PAK deployments; other PAK
names/sizes/modification times and the selected receipt are checked again before
publication. The PAK itself uses the shared atomic publisher and verified backup.
Receipt publication follows the PAK commit: a crash or receipt-write failure can
leave a committed PAK without current metadata. Such failure reports a warning;
inspect the receipt and use an explicit slot review when needed. The two files
are not one atomic transaction. Other applications do not share the studio lock.
Before launch, the PAK, executable and other numbered PAK inventory are checked
again. These checks do not validate every installed PAK, force every source port
to ignore user search paths, or prove runtime search precedence.

CLI `--pak-slot N` provides the same explicit selection. `deploy-plan` JSON
returns the resolved slot/receipt and `reviewSha256`; pass that token through
`deploy-prepared --expected-deployment-sha256 HASH` to bind the complete review
across processes, including the selected destination, output record and launch
executable. Dry runs create neither a game folder, lock, PAK nor receipt.

The target selector distinguishes Quake and Quake II's shared MAP grammar and
honors a saved Quake II target marker. Quake uses `game/id1`, Quake II uses
`game/baseq2`, and Quake III uses `game/baseq3` with an isolated home folder.
Quake/Quake II use `quake-full`, `quake-fast` or `quake-bsp-only`, with separate
ericw-tools executables, explicit asset lookup and per-stage logs. Quake II adds
`-q2bsp`; Quake III retains its q3map2 BSP/full pipelines and base/home flags.

Quake captures native loose `.mip` textures and WAD2 texture lumps into
`maps/<name>.wad`, preserving animation frames and liquid names. Only the private
map copy's `wad` and `_wad` properties change. Paths named by the original map
are never opened: the selected package supplies all bytes. Direct WAD2 packages
and drafts work too. WAD3, compressed/malformed miptextures and ambiguous names
are refused. Texture scanning and retained mip data each have a 256 MiB limit,
with at most 65,536 textures; the overall workspace byte limit still applies.
All visible WAD files in a Quake asset tree must be WAD2. Quake II validates WAL
mip data and follows animation references (64 MiB per file / 256 MiB aggregate).
Both targets refuse external-map prefab references and unsupported custom flags;
merge prefabs into the document before capture. Scene locks remain on the live
document and do not prevent preparing its private compiler copy.
On Windows, final directory publication tolerates a brief external handle by
retrying up to six times over 310 ms. Every attempt rechecks cancellation,
ancestor links and destination absence; a newly appeared destination is kept.

Quake `.lit` and `.lux` files are runtime outputs and publish alongside the BSP.
Portal/leak files, ericw texture/content metadata, visibility state and per-stage
logs stay diagnostic. **Include build sources** includes the map and generated
Quake WAD; these two inputs are otherwise omitted. PAK uses the existing portable
name/size checks and stores files without compression. Package review keeps a
compact status and file list visible; **Details** reports the warning count and
opens the full review in a bounded, selectable text view. Prepared deployment
supports numbered PAKs for Quake/Quake II and named PK3s for Quake III.
Doom node-builder layouts remain open. The complete package is captured deliberately: explicit dependency checks
do not prove all runtime or secondary shader references. Existing shader lists
are preserved; a warning explains that the compiler honors `shaderlist.txt`
whereas dependency preview scans all scripts. Nested compiler-mounted PAK/PK3/DPK
files must first be mounted into the visible asset namespace.

Successful runs record the map's `.bsp`, generated `scripts/q3map2_<map>.shader`,
standard `maps/<map>/lm_0000.tga` external lightmaps (including numbered deluxe
maps), and diagnostic `.prt`, `.lin`, `.pts`, `.srf`, `.log`, `.lit` and `.lux`
companions. `build-outputs.json` binds output paths, sizes and hashes to the
captured input inventory and a build UUID. It allows at most 100,000 outputs /
16 GiB, with a bounded 32 MiB record reader. Publication includes all captured
assets and generated runtime files; it omits diagnostics and, by default, the
current source map. It rechecks identities while streaming the archive.

A full BSP rebuild moves previous outputs into `history/<new-run-id>/` before
compilation so stale shaders/lightmaps leave the compiler search paths. History
is retained without automatic pruning and can grow across rebuilds. Failed or
cancelled runs stay inspectable but cannot publish; skipping BSP requires a
previous verified successful build and retains its outputs for incremental use.
Workspace locks exclude simultaneous studio build/publication operations. Dry
runs write neither locks nor records/history. Output path overrides
(`-lightmapdir`, `-tempname`, `-rename`) and unrecognized output formats still
require another output model. Verification detects changes at its checkpoints;
it does not lock files against external processes or prove the authenticity of
a record. Compiler
diagnostics refer to the exact prepared map; remapping them to a structurally
edited original document remains a separate acceptance gate.

`level-build-workspace-smoke`, `level-build-workspace-cli-smoke` and
`level-build-workspace-ui-smoke` cover complete draft snapshots, unsaved map
capture, persistent hashes, changed/extra inputs, source protection, size limits,
cancellation, actual shell handoff and failure reporting, and offscreen Widgets
at 100%/200% text scale with high contrast, RTL and translation expansion.
`level-build-engines-smoke` adds Quake/Quake II layouts, native WAD2 capture,
WAL validation/animation closure, target compatibility, direct texture-WAD
inputs, PAK output and CLI compiler overrides. The same GUI suite covers both
classic targets and their PAK review/publication controls. On 2026-10-05,
`level_build_engines_compiler_workflow.py` passed 25 steps through installed
ericw-tools 2.0.0-alpha8: generated texture authoring, real QBSP/VIS/LIGHT,
independent BSP texture checks, every PAK payload, MD2/PCX handoff, deterministic
backups, incremental builds and changed-input rejection. The Quake III artifacts
proof also passed its 24-step regression. Evidence is under
`.agents/tmp/level-build-engines/`; no game or native input was used.
`level_build_workspace_compiler_workflow.py` independently generates a sealed
room, stages a generated MD3 into a draft, compiles BSP/VIS/light with q3map2,
checks baked model surfaces and sound references, and verifies the resulting
PK3 bytes. `level-build-artifacts-smoke` and `level-build-package-ui-smoke` cover
output receipts, failed/cancelled builds, malformed records, concurrent writes,
source changes during package writing, backups, stale review, paged accessible
controls, worker responsiveness and the real shell build-to-PK3 handoff.
`level_build_artifacts_compiler_workflow.py` runs real q3map2 external-lightmap
and generated-shader publication, then switches back to internal lightmaps and
checks that stale runtime files are retained only in history. Evidence is in
`.agents/tmp/level-build-workspace/` and `.agents/tmp/level-build-artifacts/`.
`level-build-deployment-smoke` adds core/CLI destination freshness, permission,
backup, cancellation, complete asset delivery and a recorder executable that
verifies the PK3 before recording launch arguments. The corresponding UI suite
exercises review controls and the actual shell Prepare → Build and Launch →
Deploy handoff. `level_build_deployment_compiler_workflow.py` extends the real
q3map2 proof through installation deployment and recorder launch; evidence is in
`.agents/tmp/level-build-deployment/`. No game, native
input, screen reader, cross-platform execution or production-scale performance
acceptance is implied by these tests.

`level-classic-deployment-smoke` covers engine-specific numbering, repeated map
slots, fresh-workspace reuse, explicit recovery, reserved/missing slots, full
Quake II folders, protected package/receipt paths, stale CLI review hashes,
deployment locking, cancellation and post-commit metadata/search changes. Its
recorder checks the actual numbered PAK search and published map bytes before
recording arguments. `level-classic-deployment-ui-smoke` covers both engines'
slot controls and shell Prepare → Build and Launch → Deploy handoff, plus 100%/
200% text, dark/high-contrast themes, RTL, expanded labels and live worker UI.
The real ericw workflow accepts `--recorder builddir/src/level_classic_deployment_smoke_test`
to extend texture/model/audio capture and complete PAK verification through
installation deployment, stable-slot backup, review-token refusal and recorder
launch. On Windows use the `.exe` suffix. Evidence for this round belongs under
`.agents/tmp/level-classic-deployment/`.
On 2026-10-05 the extended real ericw proof passed 43 steps for both classic
targets, and all 11 final Meson regression suites passed. Test binary hashes
stayed unchanged; the tested production binary matched the real compiler proof.
The inventory and validation summary distinguish recorder/UI evidence from
the remaining native runtime and accessibility acceptance work.

## Scene Organization

The Scene tab now provides named layers and nested groups, undoable assignment,
selection, inherited visibility, editing locks and creation destinations. Native map metadata,
save/recovery, CLI operations, structural edits and package grouping share the
same document service; see [Level scene organization](LEVEL_SCENE.md). Reusable
linked instances and native accessibility acceptance remain open.

## Maps Opened from Packages

Opening a `.map` entry prepares a verified temporary copy and loads it into
Levels. Save opens Save As, suggesting the active project or package directory
before Documents/home. Saving into this window's temporary-copy storage is
refused, including paths reached through directory aliases. Cancellation and
refusal preserve the working document and undo history. A successful save
adopts the independent file; packaging that map or its compiled output remains
an explicit staging action. The source package is unchanged.

## Background Map Opening

Open and Reload read, tokenize, parse, solve brush geometry, run map health
checks and fingerprint the source on a worker. The window-modal dialog shows
the current phase and its progress, rather than an estimated overall duration.
Cancel and window close request cancellation and keep the dialog open until
the worker acknowledges it. The current map, undo history and recovery remain
intact on cancellation or failure. A document edited through another callback
while opening cannot be overwritten by the older request.

The shared loader publishes a complete document only on success. Its optional
progress/cancellation callbacks run on the calling thread, so the same core
service serves the GUI, CLI and package workflows. Checkpoints cover chunked
reads and hashing, text scanning, object parsing, individual brush solving and
preflight checks. The GUI hands the worker's bounded geometry cache to the plan
view; unchanged valid brushes do not need another solve during adoption.
Reloading the same file refreshes the plan even when its revision key matches,
preserves navigation, and clears stale visibility and drawing/clip anchors.

Changing a path does not read its WAD. Marker choices populate from the loaded
archive; an explicit marker can also be typed. File routing and session restore
inspect only the WAD directory, retaining eleven records of lookahead instead
of reading lump payloads. Doom/Hexen saves still own their full source archive
snapshot, preserving other maps and resources. WAD payload expansion is checked
before copying: both source bytes and the sum of copied lump bytes are bounded
to 512 MiB, including overlapping directory entries.

Source size and modification time are captured before reading and compared
before adoption. A detected external change requires reopening the file.
This is a metadata check, not a filesystem lock; changes that preserve both
values may escape it. Blocking OS reads, allocation/decoding calls and final
GUI model adoption remain bounded-call cancellation/latency limits. Total
parsed-document memory is not limited to 512 MiB.

`level-map-load-smoke` covers phase cancellation, unchanged document/history,
long tokens, single-brush interruption, preflight cancellation, source changes,
WAD expansion and Doom/Hexen/UDMF detection. `level-map-load-ui-smoke` exercises
the actual worker, shell reload, recovery preservation, stale-result rejection,
accessible roles and offscreen renders at 100%/200% text with high contrast,
RTL and expanded labels. These tests use semantic Qt calls, not input injection.

`level_map_load_benchmark` accepts 1–10,000 synthetic brushes and prints JSON
phase timings, GUI heartbeat gaps, prepared-cache adoption and cancellation
acknowledgement. Build with the normal preset and run, for example,
`builddir/src/level_map_load_benchmark 10000` (`.exe` on Windows), with temporary
storage under `.agents/tmp`. Its timings are local evidence, not CI thresholds
or proof of production performance. It does not measure a full shell refresh,
native input, software rendering or asset decoding.

## Geometry Reuse and Scale Measurements

Quake-family plan views reuse projected brush wires and bounded images of the
brushes and patch borders. Pan, zoom, pane size, display scale and contrast changes
redraw those images; grid, member markers, labels, links, camera markers and editing
previews have separate layers. Changes to source geometry, visibility or projection retire the drawing
data. Same-projection siblings can share an immutable drawing snapshot.

Larger scenes prepare projected wires and raster images on a cancellable worker
per pane. Each pane keeps one active request and replaces its pending request with
the newest view. Navigation can reuse completed projection work; changes to
source, visibility, selection or projection retire the affected results. The HUD
and accessible description show **Updating view…** while the current image is
pending. The previous navigation image follows the new pan/zoom until the exact
view finishes; newly exposed areas may be empty during this interval. Source or
visibility changes clear that image, and selection changes clear old highlights
immediately. Picking and authoring continue to use current complete geometry.
No automatic animation, profile gesture, preference or CLI change is introduced.

Member selection rings use up to 512 distinct visible positions. Coincident
members share a ring, and offscreen members do not consume the drawing limit.
The primary object keeps its separate ring, crosshair and label. The HUD and
editing commands still use the complete selection.
Selected Quake brushes and patches also carry thicker dashed outlines of their
actual projected geometry. Selecting an entity highlights its visible owned
brushes and curved patch borders, including entities without an origin. Selecting
an owner and its child together draws that child once. Hidden children stay out
of the outline layer; full-source resize/work-zone bounds retain their existing
ownership semantics. Invalid brushes keep their warning crosses inside the
highlighted diagnostic perimeter. Point entities keep their diamond and selection
markers. Doom geometry still uses the existing selection-marker feedback.

The selected outlines use a separate bounded wire/image cache, so changing the
selection does not rebuild the base brush image. Selection, source, visibility
and projection changes retire this overlay. Pan/zoom, physical display scale,
pane size and palette changes refresh its image, with the same complete drawing
fallback as ordinary wires. All editor profiles share this behavior.

Exactly coincident edges draw once within each consecutive world/entity pen run.
Nearby edges retain their original coordinates, and ownership colors retain
their original drawing order. Invalid brushes retain their dashed warning boxes
and crosses. Picking, selection cycling, undo, map saving and compiler inputs
continue to use complete objects; drawing reuse never merges map objects. Edge
coverage uses the same antialiased CPU renderer as model previews, so stroke
brightness no longer builds up from repeated coincident faces in a pen run.

Wire data has a 32 MiB payload ceiling, 262,144 unique-edge ceiling, 65,536-batch
ceiling and 2,097,152-source-edge work ceiling. A wire image has the shared
8,388,608-physical-pixel ceiling (32 MiB); a previous and replacement image can
coexist during a redraw. These are cache bounds, separate from documents, solved
geometry and other panes. The base and selected-outline layers each have these
limits. A wire-budget rejection uses complete ordinary painting on the worker.
Physical targets above the image ceiling or failed image allocations use complete
ordinary painting on the GUI thread. No geometry is omitted or downsampled to
satisfy a cache limit. Small scenes also render immediately to avoid queuing
overhead. Solving/adopting a scene, Doom drawing, picking and live overlays remain
separate responsiveness limits. Cancellation polls between geometry/raster
batches; one patch tessellation and image allocation remain bounded calls.
Real-project and native frame-time acceptance remain open.

`level-plan-wires-ui-smoke` checks known projected boundaries, oblique and nearly
coincident edges, pen ordering, invalid-brush diagnostics, budget fallback,
independent picking identities, pane sharing and physical high-DPI rendering.
`level-selection-outlines-ui-smoke` checks dashed actual edges, curved patch
borders, owner/child deduplication, hidden children, sparse identifiers, unchanged
source/history, sibling adoption, edits, undo/redo, deletion, reload and close.
It also exercises physical high DPI, 200% text, high-contrast RTL with expanded
labels, outlines beyond the member-marker limit, cache rejection and the full
over-limit image fallback. Tests call
semantic methods and render widgets into images; native input acceptance remains
open.
`level-plan-worker-ui-smoke` checks direct/background image parity, coalesced
navigation, cancellation, wire-budget fallback, GUI event-loop progress, selection
replacement, hiding, projection changes, same-path reload, close and independent
panes. It also checks the translated updating state at high contrast, enlarged
text, RTL and 2× physical scale. Its synthetic timings are measurements, not
portable latency thresholds.
`level-plan-scale-ui-smoke` compares the widget's geometry pixels with direct
physical-pixel presentation at 125%, 150% and 175%, using odd pane dimensions in
all three projections, for both immediate and background rendering.
`model-wireframe-smoke` also checks the shared compositing entry point and its
existing independent stroke-coverage references. The plan benchmark includes
unselected renders, selection/framing samples, separately warmed selected renders
and changing-pan/zoom renders; `--staggered` uses original rotated boxes at varied
heights to exercise distinct front/side geometry. Selection/framing samples
include initial outline preparation and the first redraw after Frame Selection;
they must not be interpreted as stationary cache timings.
The benchmark now separates GUI paint time from completion time, waits for the
current image before collecting pixels/statistics, and records navigation
event-loop heartbeat gaps. A quick paint of the interim image is not counted as
a completed redraw.

The 2026-10-06 Windows Clang debug comparison used 1,000 and 10,000 brushes with
three samples per projection. For 10,000 flat-grid boxes, changing-pan/zoom
medians fell from 980/4,454/4,780 ms (top/front/side) to 67/22/24 ms. For 10,000
rotated boxes at varied heights they fell from 1,588/4,876/4,789 ms to 74/176/574
ms. Selection/framing sample medians were 23–142 ms in the larger cases. This
comparison predates the selected-outline layer. Other builds
were active on the same machine. These are scoped synthetic debug measurements,
not release frame-rate guarantees; especially the larger front/side redraws and
cold preparation still need work. Logs, source/binary hashes and widget render
targets are retained under `.agents/tmp/level-plan-render/`.

The selected-outline follow-up records its source/binary hashes, 15 passing
integration suites and original synthetic widget captures under
`.agents/tmp/level-selection-outlines/`. In its separately warmed 10,000 rotated
brush run, selected redraw medians were 58/95/93 ms (top/front/side); changing
pan/zoom medians were 132/909/835 ms. The additional geometry feedback therefore
still leaves large-scene navigation outside the intended interactive budget.
Other builds were active and some samples varied sharply, so these results are
debug evidence, not a controlled release-performance comparison. They predate
the background preparation/redraw path; real-project frame-time acceptance
remains open.

The background-render follow-up records 16 passing integration suites and five
affected viewport suites rerun after a fractional-scale correction (17 distinct
suites) under `.agents/tmp/level-plan-worker/`. The independent scale test first
reproduced edge shifts, then verified physical-pixel placement at 125%, 150% and
175%. All 12 stationary selected benchmark images matched the preceding renderer
exactly. In the 10,000 rotated-brush debug run, top/front/side navigation paint
medians were 115/107/107 ms, versus the previous blocking 132/909/835 ms. Complete
image medians were 492/949/996 ms, including the test helper's interim/final
repaints. The GUI heartbeat continued while work ran, but its largest gaps were
223–236 ms. This moves expensive redraw work off the GUI thread; it does not yet
meet a smooth-frame budget or establish production performance. Those results
prompted the paint-stage profiling below. Source/binary hashes identify each run;
the workspace also contains concurrent unrelated changes.

Primary-object labels now stay inside the pane and choose the nearest available
position outside the crosshair and status tags. Reading direction controls their
preferred side and elision; the accessible selection description retains the
full identity. A theme-matched background keeps text legible over dense selected
geometry, with an opaque outlined background in high contrast. Offscreen anchors
and panes without space for a line omit the
label. This correction covers primary selection labels; other authoring captions
remain separate layout work.

Unchanged grids reuse a native Qt image with the same 8,388,608-physical-pixel /
32 MiB ceiling. Pan, zoom, grid units, palette, size and physical scale refresh it.
Oversized targets and unsupported transforms use complete ordinary grid lines.
For larger Quake scenes and selections above 64 objects (including Doom), a
separate worker prepares grid and member images. Each pane admits one active
request and one latest pending request, independently of brush/patch preparation.
Cancellation polls between grid batches, member lookups and native ellipse draws.
Only complete current results are adopted. Compatible previous images follow
navigation until replacement; newly exposed areas may be empty and strokes may
temporarily scale. Primary rings/crosshairs/labels and editing handles stay live.
Selection and visibility changes immediately clear old member highlights.
Grid units, palette and physical scale reject incompatible old pixels. The HUD
and accessible **Updating view…** status aggregate geometry and overlay work.
Returning to an already presented view cancels its obsolete replacement.
Every member image has the same 32 MiB ceiling as the grid; previous and
replacement images may coexist. These are per-image budgets, not total memory
limits. Unsupported targets/transforms and allocation failures preserve complete
ordinary painting. Small scenes/selections keep the immediate path.

Split-pane offsets preserve the exact physical-pixel origin at 125%, 150%,
175% and other fractional scales. Brush and selection strokes, patch borders,
invalid-brush warnings, grids and member images account for the same offset as
live primary markers and handles. A move that changes this fractional origin
refreshes the image; a whole-physical-pixel move can reuse it. The leading and
trailing coverage pixels count against the unchanged image ceiling. Rotation,
shear and nonuniform target transforms retain complete ordinary drawing.
Selected dashes in the shared Levels/Models CPU renderer use a stable endpoint
anchor independent of the scan axis; rounding near a diagonal no longer reverses
the pattern. This changes the dash origin of steep falling edges without changing
their stroke geometry, picking, source or selection identities.

Immediate/fallback member rings reuse exact physical subpixel phases, without quantizing positions,
within an 8 MiB charged-payload / 512-entry cache per pane. A temporary atlas of
at most 8 MiB shares painter setup when phases are missing; allocation or budget
failure draws the affected rings normally. The distinct-visible-marker limit,
primary ring/crosshair, source selection and complete picking are unchanged.
Closing a map releases both caches; hiding the grid releases its image.

`viewport-overlay-smoke` compares native grid pixels exactly and member rings
against independent ordinary QPainter draws, allowing at most four channel
levels for intermediate-image compositing. It covers fractional display scale,
overlap, clipping, opacity, unsupported transforms, eviction and bounded storage.
`viewport-hud-smoke` checks label bounds, reading direction, HUD avoidance and
prepared-layout parity. `level-viewport-overlay-ui-smoke` renders the actual
widget at every edge with 100–300% text, RTL and fractional physical scale,
checking complete label painting, accessible identity and unchanged authoring
state. Paint profiling is opt-in and disabled in ordinary editing; the benchmark
separates grid, geometry, markers, handles and HUD within GUI paint time.

`level-overlay-worker-ui-smoke` compares background member pixels with independent
native draws across projections and 100/125/150/175/200% physical scales. Actual
widget checks cover odd-sized panes, grid alignment, live primary markers,
retired selections, rapid navigation, source replacement, visibility, independent
panes, closing and oversized-image fallback. Sparse Doom/brush/patch positions,
late visible members beyond offscreen/coincident references, worker cancellation,
GUI event-loop progress and enlarged high-contrast RTL status are also covered.

The 2026-10-06 overlay follow-up passed 20 integration suites, with seven affected
checks rerun after the label-contrast correction. Native grid image comparisons
are exact, and ring references have a maximum channel difference of 2/255.
Evidence, build/source hashes and original widget images are under
`.agents/tmp/level-plan-overlays/`; `reviewed-*` identifies the accepted benchmark
run. On the 10,000 rotated-brush Windows Clang debug fixture, warm selected
medians changed from 49/115/94 ms to 8/23/15 ms (top/front/side). Cold navigation
paint changed from 48/101/102 ms to 52/122/97 ms, while completed-image medians
changed from 212/979/941 ms to 147/905/739 ms. Maximum GUI event gaps changed from
103/211/237 ms to 72/159/154 ms. Cache construction remained a cold navigation
cost and prompted the background-overlay follow-up; the warm improvement must
not be read as a frame-rate guarantee.
Other builds shared the machine, and production maps/native platforms remain
unverified. Specialized grid compositing experiments were rejected in favor of
native Qt grid pixels and a simpler cache.

The background-overlay follow-up passed 21 integration suites on the Windows
Clang debug build. Its `final-*` benchmark, source/binary hashes, pixel comparisons
and original Qt widget images are retained under `.agents/tmp/level-overlay-worker/`.
In the 10,000 rotated-brush fixture, top/front/side cold-navigation paint medians
changed from 49/150/133 ms to 13/9/31 ms. Completed-image medians changed from
135/1,227/1,107 ms to 97/758/982 ms; maximum event-loop gaps changed from
103/190/237 ms to 26/58/72 ms. All 12 final selected images match the inspected
candidate images exactly; differences from the preceding marker-cache renderer
are at most 4/255 per channel from intermediate-image compositing. Rendering
sources and binaries stayed stable during verification; unrelated Meson entries
changed during the integration suite. Other builds shared the machine, so these
are scoped debug measurements, not release latency guarantees. Overlapping
geometry redraw, source adoption, ordinary fallbacks, real projects and native
platform acceptance remain open. Repository-wide documentation/translation validation is separately recorded
in the evidence report and must not be inferred from these passing editor suites.

The child-pane follow-up reproduced shifted brush edges and synchronous overlay
fallback at fractional display scales. `level-pane-phase-ui-smoke` renders a
translated child through its parent into Qt images, comparing grids, markers,
brushes, selection, patches and warning/fallback paths with direct drawing.
It covers all three projections, 100/125/150/175/200% physical scales, small and
worker-sized scenes, phase-changing moves and whole-pixel reuse. The pure overlay
test also checks negative device offsets, unsupported transforms and phase edge
pixels at the allocation limit. Evidence is retained under
`.agents/tmp/level-pane-phase/`; these semantic offscreen checks do not replace
native monitor-migration, keyboard or screen-reader acceptance.

This follow-up passed 22 integration suites on the Windows Clang debug build.
All 30 child-pane brush comparisons are exact; the 60 selection, patch, warning
and fallback comparisons differ by at most 1/255 per channel. Grid/member samples
and pane moves match their direct references exactly. Nine of 12 benchmark
images are unchanged; the other three reflect the corrected selection-dash
anchor on rotated edges. On the 10,000 rotated-brush fixture, warm repaint
medians are 6/4/18 ms, navigation paint 13/9/33 ms and completed images
100/701/945 ms (top/front/side). Maximum event gaps are 28/79/72 ms. Concurrent
work and the debug build prevent treating these as release latency acceptance.
Rendering sources and binaries stayed stable throughout the suite and benchmark;
an unrelated Meson test registration changed during the suite. Full results,
source hashes and before/after pixel evidence are in the task directory above.

Plan selection lookups use scene indexes shared by sibling panes. Projected
selection extents and the shared service's complete transform bounds are reused
until the selection, source document, visibility or projection changes. Edits,
undo/redo, deletion, same-path reloads and closed maps invalidate that state.
Resize/work-zone bounds still include hidden geometry owned by a selected entity;
framing uses the visible selection, including brush entities without an explicit
origin. Owner bounds are resolved together through the shared service, not once
per entity. Multiple point objects and straight lines
contribute to the combined extent, including in front and side projections.
No new preference, document metadata or CLI dialect is introduced.

`level-plan-selection-ui-smoke` compares warmed views against fresh widgets,
including sparse IDs, shared panes, hidden children, source changes, Doom
geometry and point selections at 100%/200% with high contrast, RTL and expanded
text. `level_plan_selection_benchmark` measures selection, repeated metrics,
painting and framing separately for 1–10,000 original synthetic brushes in all
three projections. `--sparse` exercises IDs left after editing/deletion. For
example, run `builddir/src/level_plan_selection_benchmark 10000 --sparse` with
temporary storage under `.agents/tmp`. The benchmark's JSON timings are local
evidence rather than CI thresholds; overlapping wireframe painting and full
shell adoption remain separate performance work.

The earlier 2026-10-06 selection-index Windows Clang debug run used three
repetitions per projection, before the wire-renderer change described above.
For 10,000 brushes, repeated framing medians fell from 297–347 ms to
0.08–0.33 ms; selecting sparse IDs fell from 219–458 ms to 4.6–10.1 ms. All 18
before/after brush-render comparisons (100/1,000/10,000 brushes, dense/sparse IDs,
three projections) had identical pixels. Front/side painting of heavily
overlapping wireframes still took several seconds. Other builds were running
on the same machine, and these synthetic timings do not establish release,
real-project or native-platform performance. Detailed samples and captures are
retained under `.agents/tmp/level-plan-selection/`.

Quake-family plan edits and package-backed camera previews reuse the solved
polygons of unchanged brushes. The active plan shares its cache with sibling
panes, preserving local navigation and tools. Moving, undoing, changing a plane
or painting a new material invalidates the affected brush. UV edits and package
image replacements reuse polygons while recalculating texture coordinates from
the live map and image dimensions. Cached data never becomes map, model,
package, compiler or recovery content.

Each cache retains at most 64 MiB of estimated payload and 32,768 brushes, with
a 1 MiB input-key ceiling per brush. The document, visible scene, decoded assets
and assembled camera mesh consume separate memory. Invalid brushes and brushes
with diagnostics are solved again so translated warnings stay current. Exceeding
a cache bound reduces reuse; it never hides or simplifies geometry. The camera
keeps its existing cancellation and triangle budget, and discarded requests
cannot publish their cache.

Cache keys use one bounded allocation and compare every plane input, precision
setting and complete UTF-16 material name, including changes through retained
mutable references. Read-only plan fitting, geometry weighting and sibling-pane
updates keep shared document arrays intact instead of copying the full scene.
Scene visibility skips ownership expansion when nothing is hidden and uses
numeric owner IDs when expanding hidden entities into brushes and patches.
Hidden geometry, inherited layer visibility and independent pane navigation
retain their existing behavior. Cache and scene regressions cover 10,000
brushes, sparse IDs, material-name boundaries and exact solver results.

A workbench refresh shares one material-name/statistics summary between the
overview, Details and inspector, and one usage-count result between the paint
picker and texture tiles. These values last only for that refresh, so edits,
undo, save/reload and package context changes use current map data. Recent
materials remain available with their actual current usage, including zero.
Large palettes use set membership when merging recent and map names.
Read-only inspector, camera-highlight and resize refreshes preserve the document
arrays shared with the Objects panel and preview snapshots. They do not copy
all brushes or entities just to inspect their current state.

Details keeps the section being inspected when selection or map edits change.
Its text updates when that section's content changes; unchanged text retains
the reading position and text selection. This behavior uses the shared drawer
also used by package, build and asset diagnostics.

`level_geometry_benchmark` is an explicit developer harness, separate from CI
timing thresholds. It generates original box geometry, round-trips map text,
opens three real plan widgets, renders to a widget target, moves/undoes one
brush, selects all brushes, and builds package-textured meshes through the real
preview worker. It accepts a brush count from 1 to 10,000 and prints JSON
timings, solve/reuse counts and cache payload estimates. Build with the normal
Meson/Ninja preset, then run `builddir/src/level_geometry_benchmark 10000`
(`.exe` on Windows). Set `TEMP`/`TMP` to a disposable directory under
`.agents/tmp` when collecting evidence. No game data, input injection, OS
capture, network service or AI connector is needed.

These synthetic boxes test geometry reuse, not complete production performance.
The harness's camera time includes the existing 60 ms debounce and mesh/UV
assembly, but excludes software rasterization. The first widget render includes
font/platform initialization. Cold cache creation can cost more than a fresh
uncached solve. Parser throughput, complex brushes, patches, Doom sectors,
large material sets, realistic projects, memory peaks and native platforms
still need separate measurement and improvement. Timings are machine/build
evidence, not portable pass/fail limits.

## Doom Camera Editing

Binary Doom and Hexen maps now render floors, ceilings, solid wall parts and
both sides of masked middle walls. Sector surfaces preserve concave outlines,
holes and disconnected islands. Same-sector internal lines do not become
boundaries. Open, crossing or degenerate boundaries produce a Details warning;
the camera does not invent a floor for them. The geometry worker has a shared
triangle cap and a four-million edge-check budget, and supports cancellation.

Floors and ceilings have world-aligned UVs and independent sector material
targets. Walls retain exact sidedef/upper/lower/middle ownership. Horizontal
offsets, vertical offsets, upper/lower unpegging, exposed step heights and
single-height masked-wall clipping use the original image dimensions.
Backface culling permits editing from inside a room or through its outside
ceiling. Paint, Sample, Targets, undo, recovery and normal WAD saves all use the
same material transaction; painting does not invalidate Doom node lumps.

Open the resource WAD or asset folder in Packages. Flat and wall namespaces are
resolved separately, including when a flat and a patch have the same name.
The resolver composes classic `TEXTURE1`/`TEXTURE2` entries through `PNAMES`,
preserving patch order, signed origins and transparent gaps. It reads the
selected package's `PLAYPAL`, reports generated fallback palettes, and accepts
direct images in `flats/`, `textures/` and the matching WAD namespaces. Package
drafts use exact entry occurrences; changing a patch updates its wall image
without replacing a flat with the same name.

The Levels texture list labels flat and wall tiles separately, with independent
usage counts, images and preserved selection. **Show in Package** reveals the
exact flat or texture-definition occurrence; **Show Material Inputs** exposes
the composition inputs. **Select Objects Using It** respects the tile's namespace.

Definitions are bounded to 65,536 names/definitions/patch references, 4,096
patches per texture and 4,096 pixels per dimension. Decoding retains the shared
16-megapixel per-image bound. Composition stops after 64 megapixels of patch
work per request, with a 16 MiB patch cache in addition to the normal preview
image budget. Invalid offsets, patch references, duplicate definitions and
ambiguous namespace entries withhold the affected material. Definition-table
errors withhold composite lookup until it is reliable.

Material Details and JSON record each input's namespace, package layer and WAD
directory occurrence. Dependency review consumes this same evidence, including
texture tables, patch names, patch images, flats and palettes. Doom asset subset
export remains unavailable: exporting whole texture tables also requires their
unused definitions and namespace groups to be preserved or rewritten. The
complete package save workflow remains available.

```sh
vibestudio --cli map materials room.wad --map-name MAP01 --package resources.wad --geometry --json
vibestudio --cli map dependencies room.wad --map-name MAP01 --package resources.wad --json
vibestudio --cli map paint-material room.wad --map-name MAP01 --target sector:0:floor --texture FLOOR2 --output painted.wad
```

`--geometry` adds the camera's geometry counts, truncation state and warnings
to `map materials`; incomplete geometry returns validation failure. The camera
is an authoring preview: engine sky effects, sector lighting/COLORMAP effects,
animated textures, sprites/things, advanced UDMF effects, Strife texture tables and ZDoom `TEXTURES`
definitions remain outside this implementation. Automatic resource merging
from installed games or multiple project packages remains open.

`doom-preview-smoke` covers topology, UV anchors, material namespaces,
composition, staged occurrences, malformed data, limits, undo and actual CLI
processes. `doom-preview-ui-smoke` loads the real shell, paints and samples
floor/ceiling hits, saves/reopens the WAD and exercises dependency review at
100%/200% text scale, high contrast and RTL. Fixtures contain original generated
art only. Semantic Qt calls and widget rendering do not establish native input,
screen-reader, cross-platform or production-scale acceptance.

## Material Painting

The Levels material strip offers **Navigate**, **Paint** and **Sample**. Choose
a map material, type a name, or use **Use for Map Painting** on a Textures asset.
**Use for Painting** is also available in the Levels texture menu. The command
palette exposes both tools without taking existing editor profile shortcuts.

An unmodified left drag collects brush faces, patches or rendered Doom wall
parts. Pending surfaces are highlighted and counted in text. Release commits
the entire stroke as one undo step. Repeated hits are deduplicated; unchanged
materials add no history. Escape cancels a stroke, or leaves the tool when no
stroke is pending. Focus loss, hiding/disabling/rebuilding the preview, camera
changes, material changes and map edits cancel unfinished strokes. Other camera
gestures retain the chosen profile's behavior. Sampling chooses a material
without changing geometry, selection, undo or model source assets.

Q3Radiant and GtkRadiant also offer **middle click** to sample the material name
and **Shift+middle click** to paint one source surface. NetRadiant and NetRadiant
Custom offer middle-click sampling. These are immediate actions from Navigate,
Paint, Sample or an idle Draw Brush tool; the current tool and selection stay in
place. A paint commits on press through the same material service, respects scene
locks, and adds one undo step only when the material changes. Sampling updates
the shared picker used by painting and new brushes. Package/staging previews,
recovery, dependency review, compilation and saving then follow the normal map
workflow. Placed models report their Models authoring route and remain unchanged.

**Controls > Customize Gestures… > 3D Camera** exposes sample, paint and surface
paste button/modifier pairs for every profile. The CLI uses `camera.materialSampleButton`,
`camera.materialSampleModifiers`, `camera.materialPaintButton` and
`camera.materialPaintModifiers`; for example:

```sh
vibestudio --cli editor gestures q3radiant --set camera.materialSampleModifiers=alt --set camera.materialPaintModifiers=ctrl+shift --dry-run --json
```

Pairs match exactly; extra modifiers and multi-button presses cannot invoke an
instant action. Material gestures require a current presented preview and idle
navigation/manipulation. They cannot cancel a pending paint stroke, brush draft,
resize or transform. Free look and held camera movement suppress them. Missing
hits and unavailable previews produce text feedback. The explicit Paint/Sample
tools and keyboard target controls remain available independently of the profile.

**Paint** transfers material names and retains the target's UVs and flags.
Sampling a brush also captures its complete definition for the
[surface clipboard](#surface-clipboard). Q3Radiant/GtkRadiant Ctrl+middle pastes
onto the hit brush; Ctrl+Shift+middle pastes onto the hit face. Patch/Doom sampling
chooses the name and clears the brush clipboard. NetRadiant Shift+middle pastes
parameters onto one face; NetRadiant Custom Ctrl+middle wraps the brush mapping.
Custom Shift+middle includes selected brushes and patch materials; Alt retains
materials and flags for value paste and wrap. Other native selected-set behavior,
sampled brush depth, light color and drag-to-paint variants remain
separate work. Patch/Doom support uses the shared studio material service.

**Targets** reveals native keyboard-accessible Paint Targets/Sample Target
controls. Selectors are `face:brushId:faceNumber` (face numbers start at 1),
`patch:id`, `side:id:upper|lower|middle` and `sector:id:floor|ceiling`. Commas
separate GUI targets; the CLI repeats `--target`. Sampling accepts one target.
The target field records the last touched surface.

The shared service validates every target before editing, checks source identity
and revision at commit, and preserves UVs, patch controls, flags, selection and
source layout. Doom/Hexen names are uppercased printable ASCII, at most eight
characters. Text-map names are bounded to 1024 printable characters, use forward
slashes and cannot contain tokenizer delimiters or comment openers. Each
operation accepts at most 16384 targets.
Normal map save, undo, recovery, dependency and compiler services consume the
result; package drafts remain independently staged.

Triangles retain exact source identities after grouping by material. Stepped
Doom walls now identify the side facing the exposed step, including the back
side when appropriate. Placed model triangles are skipped; their materials are
authored in Models. Doom floors, ceilings and middle walls are also rendered and
paintable; see [Doom Camera Editing](#doom-camera-editing). Full engine shader
effects and interpolation between pointer samples remain open. Image refresh uses the asynchronous
package/staging material service after commit; pending highlighting identifies
the surfaces rather than displaying a speculative material replacement.

```sh
vibestudio --cli map paint-material arena.map --target face:0:1 --target patch:0 --texture studio/metal --output painted.map --dry-run --json
vibestudio --cli map paint-material doom.wad --map-name MAP01 --target side:0:middle --target sector:0:floor --texture STONE --output painted.wad
vibestudio --cli map sample-material arena.map --target face:0:1 --json
```

Paint requires `--texture` and `--output`, including dry runs. Existing output
requires `--overwrite`; normal conflict checks, atomic saves and backups apply.
WADs require `--map-name`; text maps reject it. Unknown/inapplicable options,
duplicate singleton options, missing values and malformed selectors return 2;
missing input returns 3; validation/save failures return 4. Success returns 0.
JSON includes map, marker, targets and material; painting adds unique target and
changed counts, output, backup, dry-run state, warnings and stale Doom lumps.

`level-material-paint-smoke` covers four text dialects, exact undo/redo bytes,
mixed face/patch edits, bounds/stale plans, Doom/Hexen wall/flat persistence,
preview provenance, package resolution and actual CLI calls.
`level-material-paint-ui-smoke` exercises real camera hits, staged strokes,
cancellation, history, sampling, explicit controls and save/reload at 100%/200%
text, high contrast and RTL expansion. It uses semantic Qt APIs and widget
rendering. `level-material-gestures-ui-smoke` adds live switching across the
four Radiant profiles, exact hit/patch routing, locked/model/empty targets,
unchanged tools/selection, no-op history, undo and pending-work guards. It checks
the real preference controls at 100%/200% with high contrast, RTL and expanded
labels. `level-gestures-smoke` checks portable files, old navigation preferences,
conflicts and actual CLI calls. Native input, screen readers, macOS/Linux
execution and large-map latency remain unverified.

## Saved Level Views

The Levels **Saved Views** menu and **View > Save Current Level View** name the
current camera and all three plan views. **Saved Level Views** manages capture,
update, rename, removal, navigation, import and export. Next/Previous Saved Level
View commands cycle the list and are available in the command palette and key
binding settings without reserving a profile's existing keys.

A bookmark retains the effective layout, active plan, camera visibility, each
plan's projection/centre/zoom, and the perspective or orbit camera's pose, field
of view and scale, plus the three navigation-link choices. Restore changes navigation immediately; it leaves selection,
hidden objects, grid, snapping, geometry and undo intact. It cancels unfinished
local gestures and clip/draw modes. The saved layout applies to the current
workspace without rewriting the global layout preference. Choosing a layout
ends that temporary layout override. Reapplying editor preferences restores the
configured layout and profile camera projection.
Restoring a bookmark also leaves the saved navigation defaults intact. Its exact
pane centres and scales are restored before linking resumes; even a camera-follow
bookmark does not immediately recenter those panes. Opening another map uses the
user's navigation defaults again.
An early restore survives the asynchronous arrival of the first camera mesh;
any subsequent camera navigation takes precedence when that preview completes.
Capture waits for the current preview rather than saving a previous map's pose.

Manager list edits apply with **Save**; **Cancel** discards those edits but does
not reverse navigation already performed with **Go to View**. **Reload Saved
List** discards list edits and reconciles changes made by another window or the
CLI. Failed saves retain the draft for export or correction. Imported `.vviews`
JSON replaces the manager's draft list; Save commits that replacement. Export
includes view data only, without local source paths or asset payloads. Files can
be shared alongside project authoring assets; they are not required by compilers
or games and are never automatically staged into a package.

Views persist per canonical map path. WAD identity additionally includes the
map marker, so MAP01 and MAP02 have independent lists. A new unsaved map keeps
its list in memory until its first save. Save As copies the list to the new
identity; the original retains its views. An existing destination list or failed
metadata write does not undo a successful map save: the status bar and activity
log report the problem, and the current list remains available to export. Reload
adopts the destination's stored list before further edits. Recovery copies and
package-extracted session maps have their own identities; views are not currently
embedded in map recovery records or keyed by the original archive entry.

Storage uses small atomic JSON files below the application configuration
directory's `level-views` folder. `--settings-file file.ini` isolates them under
`file.ini.level-views`. The service locks writes and compares exact content
revisions, refusing stale updates, malformed stores and linked destination files.
Read-only settings permit navigation and export but refuse collection edits.
There is no new dependency or online service.

```sh
vibestudio --cli editor bookmarks list arena.map --json
vibestudio --cli editor bookmarks export arena.map --output arena.vviews
vibestudio --cli editor bookmarks import arena.map --input arena.vviews --replace
vibestudio --cli editor bookmarks rename arena.map --id <uuid> --name "North gate"
vibestudio --cli editor bookmarks remove arena.map --id <uuid>
vibestudio --cli editor bookmarks list doom.wad --map-name MAP01 --json
```

Import requires `--replace` when a stored list is nonempty. Export requires
`--overwrite` to replace an existing valid view file; unrelated or malformed
files are preserved even with that flag. WAD commands require `--map-name`;
text maps reject it. The CLI validates the map and shares the exact GUI codec
and storage service. It does not control an open GUI camera. Options must apply
to the selected operation; duplicates, missing values, unknown flags and extra
arguments fail. Exit codes are 0 success, 1 read-only settings, 2 usage, 3 missing
map/input/bookmark, and 4 invalid map/view data, write failure or revision conflict.
JSON includes `operation`, `map`, `mapName`, `storage`, `revision` and `views`.

The version-2 `VibeStudioLevelViews` envelope contains `format`, `version` and
`bookmarks`. Each entry has a canonical UUID `id`, a unique trimmed `name` of
1–128 UTF-16 characters, and a `view` object. A collection holds at most 128
entries and its file at most 1 MiB. Unknown fields and non-finite numbers are
rejected. Positions are bounded to ±10 million units, plan zoom to 0.0025–64,
orbit scale to 0.002–512, perspective pitch to ±89°, orbit pitch to ±90°, FOV to
15–150°, focus distance to 1–10 million and yaw to ±360000°. Four-view layouts
require top/front/side order. Orbit targets use world coordinates so resizing
does not reinterpret a stored pixel offset.
Each `view.links` object contains boolean `centers`, `zoom` and `followCamera`
fields. Version-1 files remain readable and default all three links off; new
writes and exports use version 2. Unsupported versions and malformed link fields
are rejected without changing the current collection.

`level-navigation-smoke` covers malformed state, durable revisions, concurrent
writers, portable files and the actual CLI. `level-bookmarks-ui-smoke` exercises
the real shell/manager, asset-backed preview, asynchronous restore, Save As,
unsaved Doom maps, selection/undo isolation, and Qt-rendered manager layouts at
100%/200%, high contrast and RTL expansion. Native input, screen-reader operation,
macOS/Linux execution and realistic large-map performance remain unverified.

The 2026-10-04 Windows debug run passed the navigation and bookmark suites plus
four-view, shared model viewport, command semantics and editor-profile checks.
The two UI suites passed again after the preview-race fix; the CLI suite passed
after diagnostic localization. Build records, validation logs, source/binary
hashes and reviewed widget renders are retained under `.agents/tmp/level-bookmarks/`.

## Four-View Workspace

The Levels **Layout** menu and View commands choose a single plan, a single
camera, camera beside plan, or four views: camera/top above front/side. **Follow
Editor Profile** restores the profile's default arrangement. Layout is an
independent, persistent preference; changing it leaves that profile's mouse,
keyboard, grid and camera controls intact. Splitter sizes are retained per layout.
A fresh settings store uses the selected profile's grid; an explicitly saved
grid takes precedence. Grid and snap changes apply to all three plan panes.

**Layout > Maximize Active View** temporarily fills the viewport area with the
focused camera or plan. The same command becomes **Restore View Layout** and
returns the original visible panes and splitter proportions. **Equalize View
Sizes** restores the workspace and divides available space equally, subject to
pane minimum sizes. These commands are also in View, command search and keyboard
settings. Hammer/Worldcraft and J.A.C.K. use Shift+Z; NetRadiant Custom uses F12;
VibeStudio Default uses Ctrl+Space. Classic Hammer uses Ctrl+A to equalize.
Shortcuts require viewport focus; menu invocation uses the last focused visible
pane. Inspectors and other studio surfaces retain their normal shortcuts.

Expansion leaves authoring controls, assets and inspectors visible. It preserves
map bytes, selection, undo, camera/plan navigation, the persistent layout and
its saved splitter sizes. Edits and undo continue in the expanded view. Saved
views capture the underlying workspace, not temporary visibility. Restoring a
bookmark, choosing a different layout/profile, switching to another pane, or
opening/closing a map ends expansion. A tool that needs a hidden plan or camera
restores the workspace before using that surface. One-view layouts disable
expansion and equalization because the pane already fills the viewport area.

`level-view-workspace-ui-smoke` checks all four panes, unequal-size restoration,
bookmark state, editing/undo, hidden-camera preservation, shared command scopes,
profile/layout/map transitions and Qt renders at 100% and 200% high-contrast RTL
with expanded translations. Native input and assistive-technology acceptance
remain separate checks.

Plan and camera status labels share a bounded layout. The projection or camera
identity leads, long fields wrap at field boundaries to at most two rows, and
remaining text uses an ellipsis. Tags follow the pane's text direction; optional
map/model counts appear only when they fit beside the view state. This keeps
narrow split panes usable at large text scales without placing text outside the
view. Full status data remains available to the existing inspector; accessible
descriptions retain unelided scene and control summaries. The shared Models
viewport uses the same layout.
`viewport-hud-smoke` covers 240–1600-pixel panes at 100–300% font scales, Arabic
and Latin labels, RTL/LTR alignment, short/collapsed panes and bounded painting.

The three orthographic panes share one document, selection, hidden set, leak
trail and solved geometry. Selecting or editing in any pane updates the others,
the outliner, inspector and shared Models camera. Package materials, placed
models, undo, saving, recovery, dependency review and compilation continue to
use the same map. A layout or visibility change does not edit saved map data.
Each pane retains its navigation through edits. Frame Selection and Zoom
to Fit frame the visible panes together. Opening a different map resets all
panes, including hidden objects and old leak trails.

Click or focus a plan pane to make it active. Its border and **Active** label
identify the plane used by toolbar commands, placement, duplicate offsets,
rotation and clipping. Top/Front/Side commands and the projection chooser
activate the corresponding pane in four-view mode. Leaving a pane cancels its
unfinished local gesture and exits clip/draw mode. The camera marker appears
in each plan when the profile uses a perspective camera. Existing camera
preview limits, including static model frames and incomplete shader simulation,
still apply. Navigation starts independent; the optional links below keep
neighbouring plan panes together.

```sh
vibestudio --cli editor layout four-views --json
vibestudio --cli editor layout --json
vibestudio --cli editor layout profile
```

Accepted preferences are `profile`, `single-2d`, `single-3d`, `camera-and-plan`
and `four-views`. Omitting the identifier reads without changing the preference.
JSON reports `preference`, `effectiveLayout` and `profileId`. Unknown identifiers,
options and extra positional arguments fail with usage code 2; failed settings
writes return 1. `--settings-file` selects an isolated store for automation.
Layout changes apply to the next GUI start; this CLI does not control an already
running window. A hand-edited invalid preference falls back to the profile.

`level-views-ui-smoke` covers shared geometry/visibility, edits from different
panes, exact undo/redo, package-backed camera assets, layout persistence, CLI
errors and settings protection, Quake-to-Doom document switching, and Qt widget
renders from shells opened at 100%/200% text with high contrast and RTL
translation expansion. This does not measure live whole-studio theme changes.
These direct Qt calls do not establish native keyboard, screen-reader or
cross-platform acceptance. Persistent groups/layers and
production-scale profiling remain separate acceptance requirements.
The 2026-10-04 focused Windows run passed `level-views-ui-smoke`,
`level-prefab-ui-smoke`, `editor-profiles-smoke` and `studio-semantics-smoke`.
Final widget renders and validation records are under `.agents/tmp/level-views/`.

## Linked Navigation

The **Layout** menu includes independent **Link Plan Centres**, **Link Plan Zoom**
and **Plan Views Follow Camera** choices. All three default off and persist in
local settings, independently of interaction profiles and layout. The same
commands appear in the command palette and can receive user key bindings.

Linked centres keep the top, front and side panes on one world point. Navigating
a pane supplies its two visible axes and retains the remaining axis from another
pane. The side view displays Y horizontally and Z vertically. Linked zoom shares
pixels per map unit; it can be enabled without linking centres. Hidden panes
receive the same updates, so reopening a layout retains a useful reference.
Updates do not change the active pane, its tools, selection or undo history.

Camera-follow centres plans on the perspective camera's position or the orbit
camera's world target. Camera zoom remains independent, and panning a plan does
not move the camera. **Centre Plan Views on Camera** provides the same centring
once without changing preferences or plan scale. It waits for a usable preview
of the current map. Asynchronous preview replacement does not recenter plans.

Frame Selection and Zoom to Fit choose the smallest fitting scale among visible
plan panes when zoom is linked. Explicit selection framing keeps plans on the
selection even with camera-follow enabled; following resumes on the next camera
navigation. Saved views preserve exact poses and link choices while leaving
settings defaults intact. Navigation is editor metadata: package contents,
material/model sources, map serialization and compiler inputs are unchanged.
Changing one link while a bookmark is active persists that choice alone; the
bookmark's other temporary overrides do not replace their saved defaults.

```sh
vibestudio --cli editor view-links --json
vibestudio --cli editor view-links --centers on --zoom on --follow-camera off
```

The command reads preferences when no options are supplied. Each option accepts
`on` or `off`, including `--zoom=on`; omitted options retain their previous values.
JSON returns `links` with boolean `centers`, `zoom` and `followCamera`. Unknown or
duplicate options, invalid values, missing values and extra arguments return
usage code 2 before writing. Unwritable or newer settings return 1 for changes;
successful reads/writes return 0. Standard settings and locale overrides apply.
CLI changes configure subsequent GUI use and do not control a running window.

`level-navigation-smoke` covers axis/scale propagation, invalid-state atomicity,
settings persistence, version-1 compatibility, strict version-2 parsing and the
actual CLI. `level-view-links-ui-smoke` uses semantic Qt calls to exercise the
real shell, package materials and model preview, tool/selection/history isolation,
camera follow, fitting, bookmarks and Quake-to-Doom switching. Widget renders
cover 100%/200% text, high contrast, expanded translations and RTL. Native input,
screen readers, macOS/Linux and large-map latency still require acceptance work.

The 2026-10-04 focused Windows run passed eight suites: linked views, existing
view layouts, navigation/CLI, bookmarks, settings, Models viewport, Doom preview
and material painting. The checked source and binary fingerprints stayed stable
through that run. Reviewed widget renders, build records, test output and
validator logs are retained under `.agents/tmp/level-view-links/`.

## Patch Caps

**Edit → Cap Patch…**, the command palette and the map context menu cap a
Quake III patch's first/last row or column. Choose one boundary or its opposite
as well. Closed loops become disks; open arches close along the straight chord
between their endpoints. The source patch remains intact, and new caps inherit
its entity owner. All caps in one operation form one undo step.

Each cap is an exact radial quadratic surface with three radial controls:
center, midpoint and the unchanged source boundary. The boundary's original
quadratic curve is retained, including refined grids up to 31 controls. Facing
follows the source edge; **Invert cap facing** reverses it explicitly. Closed
loops use the mean boundary-control position as their center. **Custom center**
supports one boundary at a time; an open arch's center must remain its chord
midpoint. Planarity, monotone polar directions and total angular sweep reject
folds, self-overlap, straight/collapsed boundaries and centers outside the
boundary's visible region. Each quadratic span may cover at most half a turn.
Subdivide a wider span or reshape an unsupported boundary before capping it.

**Planar** UVs use map units per tile (default 128), centered at UV (0.5, 0.5),
with U pointing toward the original first boundary control and V in its plane.
**Match boundary** retains the boundary UV function and interpolates toward its
mean at the center; seams and distortion may converge there. The material can
inherit from the source or use a different package material. Fixed subdivisions
retain the source boundary's count and use one radial subdivision. Header
extensions survive. Preview tessellation is capped independently of saved data.

Preparation runs on an immutable cancellable worker. Source-plus-caps, caps-only
and original views use the shared Models renderer, shader editor images and
package/staging material resolver. Caps are hatched, details expose centers,
grids and asset paths, and the status panel wraps and scrolls. Invalid or pending
drafts cannot apply. A map, selection, material package or palette change makes
the draft stale. New caps remain editable through the normal Patch Editor,
stitching, selection, save, dependency and compiler workflows.

Caps are separate patch surfaces; they do not create a solid brush. Collision
and adaptive tessellation depend on the chosen material and target engine.
Non-planar covers and propagation through neighboring patches remain separate
authoring work. Save before compiling or packaging the result.

```sh
vibestudio --cli map cap-patch curves.map --patch 0 --boundary first-row --boundary last-row --texture base/endcap --uv planar --units-per-tile 128 --output capped.map --dry-run --json
```

`--patch` uses a zero-based ID. Repeat `--boundary` for up to four distinct
boundaries. Optional `--center X,Y,Z` enables a custom center for one boundary;
`--invert`, `--uv planar|boundary` and `--texture` mirror the dialog. `--output`
is required, existing files require `--overwrite`, and `--dry-run` writes
nothing. JSON includes a `cap` report. Usage errors return 2, invalid geometry
or save failures 4, source-read failures 1 and success 0.

`patch-cap-smoke` checks all boundary orientations and facing/UV choices against
an independent surface evaluator, tilted planes, refined and maximum-size grids,
invalid geometry, owner preservation, atomic insertion/undo and CLI persistence.
`patch-cap-ui-smoke` exercises the dialog and shell through direct Qt calls and
widget renders, including package staging, comparison views, stale drafts,
100%/200% high-contrast RTL expansion, and selected-text fit within combo fields.
Native input, screen readers and macOS/Linux acceptance remain unverified.

The optional `src/tests/patch_cap_compiler_workflow.py` harness passed 17 expected
outcomes with installed q3map2 `2.5.17n-git-e62c6f4b`: closed cylinder caps and
open-arch caps survived BSP/VIS/LIGHT with all 72 cap controls, 24 boundary controls,
correct facing and planar UVs retained. Dependency subset export and full PK3
validation verified six payloads with no warnings. Only generated fixtures were
used; no game was launched. Exact commands, outputs and measurements are under
`.agents/tmp/patch-caps/compiler-all/`.

## Patch Stitching

**Edit → Stitch Patches…**, the command palette and the map context menu join
two existing Quake III patch boundaries. Choose a first/last row or column on
each patch. Selected patches populate the initial pair; the selectors can also
choose other patches in the map. The patches must be distinct and share an
entity owner. Materials, facing, header extensions and comments remain intact.

Unequal edge grids are refined to a common number of quadratic spans using
their least common multiple. Refinement preserves the entire original surface
and UV function before the seam moves; it never approximates a curve by adding
sampled points. Both axes remain bounded to 31 control points. A pair whose
common grid exceeds that limit is refused without editing either patch.

**Boundary direction** chooses Forward, Reversed or Automatic. Automatic compares
both control-point pairings and refuses an ambiguous tie. **Maximum gap** bounds
the distance between every paired control before joining (default 8 map units).
Join at the first boundary, second boundary or their average. Adjacent interior
handles move with their boundary to retain each original cross-edge derivative.
**Match geometric tangents** instead sets equal, opposite interior offsets across
the seam, reshaping both adjacent spans; a collapsed offset is refused. The join
then has matching first derivatives in the neighboring quadratic spans.
An order that reverses face winding across the join produces a warning; use
the Patch Editor's facing inversion where appropriate. The seam preview is
double-sided so both surfaces remain inspectable.

Texture coordinates are preserved by default. Choose the first, second or average
UV boundary to make UVs continuous as well. Matching translates each adjacent UV
handle by its boundary's UV change. Materials are not replaced, and this does not
smooth UV derivatives. Existing joins with third patches or a periodic boundary
on either selected patch are not propagated; review their corners after joining.

The coalesced worker shows original or stitched surfaces with hatched boundary
strips. Package/folder textures, staged replacements and shader editor images use
the same resolver and Models renderer as the level camera. Seam and material
details expose grid sizes, direction, maximum gap/movement and asset paths.
Geometry authoring works without images. Preview tessellation is bounded
independently of saved settings. Different fixed/adaptive subdivision settings
are retained and produce a warning: matching control curves alone does not prove
identical compiled tessellation. Review the compiler result.

The selectable, read-only status view wraps long translated words and paths,
and scrolls vertically when necessary to keep diagnostics accessible at larger
text scales.

Apply replaces both patches in one undo step. Pending or invalid drafts cannot
apply; a changed map, selection, package or palette invalidates the dialog.
Save the map before dependency review, compilation or package export.

```sh
vibestudio --cli map stitch-patches curves.map --first 0:last-column --second 1:first-column --max-gap 8 --match-tangents --uv first --output stitched.map --dry-run --json
```

IDs are zero-based; boundary tokens are `first-row`, `last-row`, `first-column`
and `last-column`. Optional `--direction auto|forward|reversed`,
`--target first|second|average`, `--uv preserve|first|second|average` and
`--match-tangents` match the GUI. `--output` is required; existing output needs
`--overwrite`. JSON includes the `stitch` report. Validation failures return 4,
usage errors 2, source-read failures 1 and success 0. Save failures follow the
shared map command contract and return 4. A dry run writes nothing.

`patch-stitch-smoke` checks exact refinement against an independent evaluator,
all boundary pairings/directions, gap/limit rejection, UV and tangent choices,
atomic undo, source preservation and CLI persistence. `patch-stitch-ui-smoke`
uses direct Qt calls and widget renders for staged materials, before/after views,
100%/200% high-contrast RTL expansion and the actual shell apply/undo/save path.
Native input, screen readers and cross-platform acceptance remain unverified.

The optional `src/tests/patch_stitch_compiler_workflow.py` harness exercises
the real compiler and package path with generated assets. On Windows with
q3map2 `2.5.17n-git-e62c6f4b`, its 18 expected outcomes passed: a too-wide gap
was refused, a 3/5-control seam was joined with UV/tangent matching, BSP/VIS/LIGHT
completed, and dependency subset export plus full PK3 validation verified all
four payloads. All 30 compiled patch controls retained their positions and UVs,
allowing q3map2's uniform whole-tile UV shift per surface; the five shared controls
matched on both sides. This is compiler/persistence evidence, not an in-game test.
Exact commands and measurements are in `.agents/tmp/patch-stitch/compiler/`.

## Brush Merging

**Edit → Merge Brushes…**, the command palette and **Transform → Merge
Brushes…** join selected brushes into one convex solid. Selected brush entities
expand to their brushes. Every selected object must be supported, and all source
brushes must share an entity and face dialect. Point entities, patches, mixed
owners and mixed dialects are refused. The owner and unrelated objects survive.

The operation checks an **exact union**. Touching, overlapping, duplicate and
contained brushes are supported when their union is convex. Gaps, concavities,
edge/point-only contact and hidden cavities are refused. It keeps original
exterior planes and proves coverage by subtracting the source volumes from the
candidate; adding overlapping volumes would not provide that proof. Numerical
classification uses a 1e-7 map-unit tolerance and the shared solver's unsnapped
coordinate mode. Pending transforms are checked at their serialized precision.
Rewritten brush plane coordinates retain up to twelve fractional digits so
shared planes remain coplanar after arbitrary rotation and save/reload.
Limits are 64 source brushes,
2,048 source faces, 4,096 source vertices, 4,096 coverage fragments and eight
million classification/split visits. The resulting solid uses the shared
128-face/256-vertex topology limit. Difficult selections fail without an edit.

The preview runs on an immutable, cancellable worker. Switch between **Merged
brush** and **Source brushes**, and use **Frame brushes** as needed. Package or
folder materials, staged image replacements and shader editor images use the
same resolver as the level camera and Models. Missing images do not block a
merge. **Merge and material details** exposes the report and resolved paths.

Coplanar exterior faces may disagree on material, UV mapping or flags. Each
conflict requires a **Keep surface from** choice; the chosen source supplies all
three across the entire merged face. Equivalent projections are compared at
the merged polygon's vertices. The initial conflict preview uses the first
source and cannot be applied. Internal faces disappear. Source comments,
including comments on removed faces, are retained once in the new brush block.
Complete faces and brush/entity braces must occupy separate source lines.

Classic, Valve 220, `brushDef` and `brushDef3` faces retain their selected source
definitions. One undo step restores source brushes, selection and source text;
redo restores the merge. A changed map, selection, package snapshot or palette
invalidates the GUI draft. Save before compiling or packaging the changed map.

```sh
vibestudio --cli map merge-brushes room.map --object brush:0 --object brush:1 --output room-merged.map --dry-run --json
vibestudio --cli map merge-brushes room.map --object brush:0 --object brush:1 --face-source 2=1:2 --output room-merged.map
```

`--object` also accepts `entity:id`. Repeated `--face-source
outputFace=brushId:sourceFace` resolves individual surfaces. **All CLI IDs are
zero-based**, and output faces are ordered by source brush ID then source face.
Use the report's `merge.faces[].sources` candidates; the example choice is only
valid when reported for that selection. Unresolved conflicts return a validation
failure with a reviewable JSON report and write nothing. `--dry-run` writes
nothing; `--output` is required and `--overwrite` protects existing destinations.

`level-merge-smoke` covers solid coverage, mapping conflicts, four dialects,
source preservation, undo, saving, stale plans and CLI parity. Forty deterministic
random box unions are also checked against an independent occupied-cell oracle.
`level-merge-ui-smoke` uses direct Qt calls and widget renders to exercise
source choices, staged material previews and actual shell commits. These checks
do not establish native input, screen-reader or cross-platform acceptance.

A synthetic room with two merged brushes passed q3map2 BSP, VIS and LIGHT on
Windows on 2026-10-04 (`2.5.17n-git-e62c6f4b`). The test first reviewed four
material/UV conflicts, then chose individual donors. BSP inspection confirmed
one collision brush with all six exterior planes, six material surfaces and
the expected UVs at 24 vertices, allowing the compiler's uniform whole-tile
offset per surface. Dependency subset export and full PK3 validation passed
with all four payloads verified and no warnings. Exact commands, maps, BSP,
package and measurements are retained under `.agents/tmp/level-merge/compiler/`;
the build and test summary is `.agents/tmp/level-merge/validation-summary.txt`.
No game was launched; in-game and other compiler acceptance remain open.

## Brush Primitives

**Edit → Add Brush…**, the command palette and the existing context-menu
entry now open a primitive builder. Choose **Box**, **Wedge**, **Cylinder**,
**Cone** or **Sphere**, then set its centre, XYZ size, axis and material.
Radial shapes expose 3–64 sides; spheres also expose 2–16 latitude bands,
including the two pole fans. Sides × bands must not exceed 128 sphere faces.
All primitives are closed convex solids, bounded to 128 faces and 256 vertices.
Flat, non-finite, out-of-world or numerically unstable results are refused.
Every dimension is at least one map unit and every vertex is within ±32768.
Radial shapes can use fractional coordinates. The existing qbsp advisory remains
available for Quake-family qbsp targets; it no longer flags Quake III maps that
use q3map2. Quake/Quake II compiler acceptance requires separate verification.

Vertices fit the requested world-aligned bounds, including odd-sided polygons.
A Z-axis wedge rises along +X and extrudes along Y. X/Y axis choices cyclically
permute those local axes; use the normal rotation or mirror tools for another
orientation. Unequal sphere dimensions create an ellipsoid. Detail does not
imply a smooth curved patch: these are solid brush faces. **Frame brush** fits
the preview after a size or position change.

Preview generation runs on an immutable map snapshot in a coalescing worker.
The preview uses the Models renderer and the shared package/staging material
resolver, including shader editor images. Material Details exposes paths and
warnings; unavailable images use flat shading and do not prevent geometry
authoring. Apply rejects a changed map or package. Cancel leaves the map alone.

Add inserts one brush into worldspawn, selects it and creates one undo step.
The map's first current brush supplies its classic, Valve 220, `brushDef` or
`brushDef3` dialect, including pending edits and pasted unsaved brushes. Empty
maps use classic mapping. New faces use zero offsets, unit classic/Valve scales
or the existing primitive default of one repeat per 128 map units. Surface
Alignment, component editing, entity assignment, dependency review and map
saving then use their normal services. Source comments and unrelated geometry
remain intact. Saves validate the reconstructed solid; precision failures are
reported before insertion.

```sh
vibestudio --cli map add-brush arena.map --shape cylinder --axis z --sides 12 --mins -64,-64,0 --maxs 64,64,256 --texture base/metal --output arena-column.map
vibestudio --cli map add-brush arena.map --shape sphere --sides 8 --bands 4 --mins 0,0,0 --maxs 128,128,128 --texture base/stone --output arena-sphere.map --json
```

`--shape` defaults to `box`, `--axis` to `z`, `--sides` to 8 and `--bands` to 4.
The existing required bounds, material and output path are unchanged. `--bands`
applies to spheres; `--sides` applies to radial shapes. `--dry-run` writes no
file, and replacing an output requires `--overwrite`. JSON adds `shape`,
`faceCount` and `faceDialect` to the ordinary map-save report.

`level-primitive-smoke` covers shape topology and analytic volumes, all three
axes and four face dialects, low/odd/maximum detail, pending dialect conversion,
atomic rejection, exact undo/redo, persistence, Surface Alignment handoff and
CLI output protection. `level-primitive-ui-smoke` exercises the actual builder
and shell, package images, stale/queued previews, offline authoring, undo/save,
and direct widget renders at 100% and 200% high-contrast RTL with expanded text.
Native input, screen readers and cross-platform acceptance remain open.

A synthetic room containing the four new shapes passed the installed q3map2's
BSP/VIS/LIGHT stages, dependency resolution, package subset creation and full
PK3 payload validation. All 56 authored face planes matched compiled collision
planes within 0.00002 map units, and all 57 source vertices remained inside the
compiled solids within that tolerance. Compiler bevels were allowed in addition
to authored faces. This verifies the compiler/package path; in-game acceptance
remains outstanding. Local logs, exact arguments, generated maps/BSP/PK3 and
measurements are retained in `.agents/tmp/level-primitives/compiler/`.

## Surface Alignment

### Quick adjustments

The **Surfaces** tab beside the level Inspector keeps texture adjustments
available while the map stays visible. Select **Selection** to affect all
faces of the selected brushes and brush entities, or choose **Face**
after focusing a face property in the Inspector. The target summary identifies
the affected face or count; changing scope cancels pending work.

U/V shift, rotation, independent U/V grow/shrink, **Fit 1 × 1** and **Centre**
also appear in **Edit → Surface Adjustments**, command search and keyboard
customization. Their keyboard shortcuts operate only in level viewports, leaving
normal field editing intact. The Q3Radiant profile binds Shift+arrows to U/V
shift, Shift+Page Up/Down to negative/positive rotation and Shift+5 to fitting.
Other profiles can assign these commands in Settings. Buttons and commands share
the same target and adjustable steps: initially 8 texels, 15 degrees and 10%
texture size. These step values last for the current studio session. Growth
multiplies texture size by `1 + percent / 100`; shrinking divides by that factor.
Rotation and scaling preserve the UV at each face's winding centroid.

A 20 ms collection window groups rapid input into ordered batches of at most 64
adjustments. One worker prepares a batch while up to 64 more adjustments queue.
Each completed batch becomes one undo step; an invalid intermediate mapping
rejects the complete batch. Opposite shifts that cancel exactly add no history.
Each adjustment uses the existing six-decimal writer precision, just like
separate dialog/CLI edits. A source matrix with finer precision can therefore
change through rounding even when two requested shifts mathematically cancel.
Geometry is solved once per affected brush per batch, and target lookup scans
the document once. Asset lookup, validation, scene-lock checks and undo
construction run off the UI thread. Required material dimensions come from the
current immutable package/staging snapshot; no size is guessed. Fit failure
explains missing dimensions, and the full dialog below also offers an explicit
size override. Dimensions resolved within a continuous queue are reused.

Pending work has a visible status, progress indicator and **Cancel**
button. Completed batches remain available in undo history. Map edits, undo,
saving, document replacement, selection/face/scope changes and package changes
invalidate pending candidates before adoption. Selection, source identity,
save point, scene state, package revision and palette are checked again at
completion. No map or package is saved automatically; save before building.
Patches and Doom surfaces retain their existing dedicated workflows.

Quick actions and the CLI's `map align-textures` use the same surface service
and mapping rules. The following dialog remains available for arbitrary values,
individual face lists, negative scaling, repeat counts and detailed previews.

### Surface clipboard

**Copy Surface** in the Surfaces tab captures the inspected brush face's material,
mapping and contents/surface/value flags. Middle-click sampling captures the
same definition in Q3Radiant, GtkRadiant and both NetRadiant profiles. Copy can
read a locked brush and changes neither selection nor history. The clipboard
lasts across maps for the current session. It keeps the copied material even if
the painting picker later changes; it has no system-clipboard integration.

**Paste Surface** applies to the tab's **Selection** or inspected **Face**.
Both commands appear in Edit > Surface Clipboard, command search and key
customization. Q3Radiant/GtkRadiant also use Ctrl+middle on the hit brush and
Ctrl+Shift+middle on the hit face. These exact chords retain tool and selection
and always use matching-format parameters. NetRadiant Shift+middle uses the
same face parameter paste. NetRadiant Custom Ctrl+middle performs a single
seamless brush-face wrap; it uses the Surfaces conversion consent when needed.
Custom Shift+middle pastes Radiant values onto the hit and selected objects.
Selected brushes (including entity-owned brushes) receive mapping and material;
selected patches receive material only. Only the explicit brush hit receives
copied flags. Alt+Shift+middle keeps target materials and flags while pasting
values; Alt+Ctrl+middle wraps mapping only on one face. Selection stays intact.
Ctrl+Shift+middle performs native Radiant projection on the hit and selection;
Alt+Ctrl+Shift+middle projects mapping while retaining materials and flags.
All profiles can customize
`camera.surfacePasteFaceButton`, `camera.surfacePasteFaceModifiers`,
`camera.surfacePasteBrushButton`, `camera.surfacePasteBrushModifiers`,
`camera.surfaceWrapFaceButton` and `camera.surfaceWrapFaceModifiers`, plus
the `camera.surfaceValues`, `camera.surfaceValuesOnly` and `camera.surfaceWrapOnly`
Button/Modifiers pairs.
`camera.surfaceProject` and `camera.surfaceProjectOnly` configure the two
projected-paste Button/Modifiers pairs.
Saved navigation and explicit sample/paint bindings take precedence over newly
inherited paste defaults; older paste bindings also take precedence over the
new wrap and selected-value defaults. Explicit conflicts must be resolved before Apply.
Existing explicit value/wrap choices also take priority over new projection defaults.

Hold a Values, Project or Wrap gesture to transfer across several surfaces.
The camera shows the staged materials and UVs while the map, dependencies and
undo history retain the last committed state. Only the initial hit includes
selected objects; later hits follow the current modifiers and target one face
or patch. Wrap advances its private source after each successful face, including
the destination package's image dimensions. Repeated hits on the same surface
and mode are skipped until the target or mode changes. Release applies the net
change in one undo step and publishes the final wrapped clipboard source.
Escape or **Cancel Stroke** discards the entire transaction, including queued
work after release. Focus loss, camera/profile changes and stale map, selection
or package context cancel the stroke. Invalid or locked targets discard all
pending changes. Values/Project accept patch hits; Wrap still requires brushes.

Material regrouping cannot change a held gesture's target: picking uses the
mouse-down geometry and texture coverage, while a separate worker rebuilds
the live preview. Preview requests coalesce and reuse geometry; image decoding,
staging indexes, surface validation and undo construction stay off the GUI thread.
The queue holds at most 256 pending hits and a stroke accepts at most 4,096 hits,
16,384 changed surfaces and 256 changed patches. Exceeding a bound discards the
stroke with a visible error. The camera remains reserved until publication or
cancellation; the existing Surfaces controls provide keyboard alternatives.

- **Matching-format parameters** copies native mapping values. Classic maps,
  Valve 220 and primitive matrices are distinct families; `brushDef` and
  `brushDef3` share the matrix family. Full-definition paste needs no dimensions.
- **Radiant values** uses the same native parameters but retains each Valve face's
  existing axes, matching Custom's Values behavior on differently oriented faces.
  Matrix values use source and destination dimensions to retain texel density,
  including full-material paste between packages with different image sizes.
  Both parameter modes support patch material targets without changing patch UVs.
- **Radiant projection** follows NetRadiant Custom Project: classic faces copy
  native parameters, Valve faces copy parameters and axes, and primitive matrices
  project the source world mapping with source/destination texel density. Brush
  families must match; this mode never converts the map. Matrix shifts normalize
  by whole repeats. Selected or explicitly hit patches receive UVs evaluated at
  every control point, retaining geometry, subdivisions, owner and comments.
  Required patch image dimensions resolve through the same package snapshots.
  Perpendicular brush faces can deliberately receive edge-on mappings, producing
  stripes or a flat colour; completion and CLI JSON report `edgeOnFaces`. Values
  mode is available for face-relative alignment. Other projection modes retain
  their collapsed-mapping refusal. A collapsed face cannot be copied as a new
  portable source; the original session source remains available.
- **World projection** keeps the source's UV function at world positions.
  Crossing between texels and repeats requires the copied material's actual
  dimensions, resolved from the copy-time package/staging snapshot. Missing sizes
  cause a clear refusal. Perpendicular projection can collapse and is refused.
- **Seamless wrap** rotates the mapping around the intersection of the copied
  and target planes. UVs agree along that line and preserve surface scale,
  handedness and shear. Parallel, opposite or nearly parallel planes (squared
  normal cross product at most `1e-10`) use world projection because there is
  no stable hinge. The planes need not belong to touching brushes. Cross-format
  wraps use actual image dimensions under the same rules as projection.
  A successful single-face wrap makes the resulting face the next clipboard
  source, enabling repeated corner wrapping. Multi-face batches independently
  wrap each target from the original clipboard; they do not infer a traversal.
  Failure, cancellation or stale context advances neither document nor clipboard.
  Undo restores the map; the session clipboard remains at its last successful copy.
- **Allow map-wide Valve 220 conversion** explicitly permits converting all
  classic faces when projected or wrapped mapping requires shear. q3map2 chooses syntax
  from the first face, so conversion keeps the entire map consistent. Unpasted
  materials, flags and UVs remain unchanged. Unselected locks still apply; any
  locked or invalid face rejects the whole batch. Verify compiler support for
  Valve 220. Conversion shares the 16,384-face/128-faces-per-brush limits and
  refuses maps containing primitive matrices.
- **Keep materials and flags** transfers mapping only in any mode. Primitive
  mappings require source and destination image dimensions to preserve texel
  density rather than repeat frequency. The source package context is captured
  with Copy, including its map format and engine's asset rules; the target uses
  the current package. Same-named images can have
  different sizes across packages. Missing original dimensions cause a clear
  refusal; target images never silently replace the captured source. Patches
  are unchanged by mapping-only value paste. Mapping-only Radiant projection
  updates their UVs and retains their material.

Preparation, image lookup, source-layout validation and undo construction run
on the existing surface worker. The Surfaces tab opens with progress and Cancel.
Copy retains implicitly shared package state without constructing a package
index on the GUI thread. Required source and destination staging indexes are
built on the worker with cancellation; dimension-free edits skip them.
Paste owns one batch; a second paste or quick adjustment waits until it finishes
or is cancelled. Locks and invalid targets reject the whole batch. Context
changes invalidate pending results. A successful paste is one undo step;
unchanged settings add none. Geometry, selection, unrelated source text and line
endings remain intact. Ordinary map save, recovery, dependency review, compilation
and packaging consume the result; material dependency summaries update immediately,
including on undo. Save before building. Patch control editing and Doom surfaces
retain their dedicated editors. Placed model materials remain authored in Models.

The CLI exposes a portable, bounded version 1 JSON definition:

```sh
vibestudio --cli map copy-surface source.map --target face:0:1 --output wall.surface.json --json
vibestudio --cli map paste-surface target.map --clipboard wall.surface.json --target brush:2 --target face:3:1 --output pasted.map --dry-run --json
vibestudio --cli map paste-surface primitive.map --clipboard wall.surface.json --target face:0:1 --mode project --texture-size 128,64 --output projected.map
vibestudio --cli map paste-surface corner.map --clipboard wall.surface.json --target face:0:3 --mode seamless --output wrapped.map
vibestudio --cli map paste-surface corner.map --clipboard wall.surface.json --stroke --target face:0:3 --target face:0:5 --mode seamless --output wrapped-stroke.map
vibestudio --cli map paste-surface target.map --clipboard wall.surface.json --target face:0:1 --object brush:2 --object patch:0 --mode radiant-values --output selected.map
vibestudio --cli map paste-surface primitive.map --clipboard wall.surface.json --target face:0:1 --mode radiant-values --mapping-only --texture-size 128,64 --material-size studio/target=64,256 --output density.map
vibestudio --cli map paste-surface room.map --clipboard wall.surface.json --target patch:0 --object brush:2 --mode radiant-project --mapping-only --texture-size 128,64 --material-size studio/patch=64,256 --material-size studio/wall=128,128 --output projected.map
```

Copy takes one face; paste accepts repeatable face/brush/patch targets, at most
16,384 expanded surfaces, 128 faces per brush and 256 patches. Patch targets use
`patch:id` with parameter modes or `radiant-project`. Repeatable
`--object brush:id|patch:id|entity:id` adds the selected set to those modes;
the explicit hit remains required.
Only explicit face targets receive copied flags. Face numbers are one-based. Both require
`--output`, including dry runs; existing output requires `--overwrite`. Paste
uses the normal atomic map writer, source guards and backups. Copy writes JSON
atomically and refuses to overwrite the input map. `--mode` is `parameters`
(default), `radiant-values`, `radiant-project`, `project` or `seamless`. `--mapping-only` retains
materials and flags. `--texture-size W,H` supplies original source dimensions;
repeatable `--material-size material=W,H` supplies normalized material dimensions,
including retained target materials (1–65,536 per dimension). Duplicate normalized
names are rejected. Project/seamless accept `--allow-valve220` for the map-wide
conversion policy above. JSON reports `changedPatches`, `mappingOnly`, `edgeOnFaces`,
`selectedObjects` and `convertedFaces` separately; `changedFaces` includes
representation changes. The CLI requires explicit dimensions when converting
units. Files larger than 16 KiB, unknown schemas/fields, invalid material names,
non-finite numbers, collapsed mappings and out-of-range flags are rejected.
`--stroke` replays up to 4,096 individual face/patch targets in argument order,
including repeated visits. It uses the same transaction as the held camera
gesture, with selection inclusion on the first hit only. Seamless steps advance
the source; subsequent size-dependent steps use `--material-size` for the new
source, never the original `--texture-size` override. Brush-wide selectors are
rejected in stroke mode. JSON adds `strokeHits`, `sourceAdvanced` and `finalSource`
(a portable surface definition). Input clipboard files remain unchanged.
Without `--stroke`, batch targets continue to share the original source.
CLI traversal uses one chosen mode and mapping-only policy for all hits. Per-hit
modifier changes are available through the shared core API and camera gesture;
a mixed-mode CLI stroke-file format remains an integration gap.

`level-surface-clipboard-smoke` covers four dialects, existing/absent flags,
all 16 projection pairs on displaced oblique targets, map-wide conversion,
preserved unpasted UVs/flags, source preservation, exact undo/redo, cancellation,
locks, stale plans and real CLI calls. `level-surface-clipboard-ui-smoke` covers
the actual shell, package dimensions, worker responsiveness/cancellation, both
classic Radiant paste profiles, both distinct NetRadiant profiles, clipboard
chaining, cross-map handoff and 100%/200% high-contrast RTL layouts
with expanded text. These semantic Qt checks use widget rendering without native
input injection. Native input, screen readers, production-map latency and
macOS/Linux acceptance remain open, as do patch-source copying/wrapping, NetRadiant
paste aliases, work-zone depth and light-color sampling. Native Custom Project
copies classic/Valve values but projects primitive matrices; the studio's World
projection deliberately preserves world UVs across all supported mapping families.

`level-surface-stroke-smoke` verifies ordered cross-material seams, private
previews, exact undo/redo, no-op redo preservation, mixed modifier/patch targets,
locks, cancellation, stale plans and actual CLI replay. The UI suite exercises
live material regrouping, frozen picking, source-package advancement, cancellation
after release, selection drift and accessible controls at 100%/200% high-contrast
RTL with expanded translations. The optional compiler workflow's `--stroke
--mapping-only` variant independently checks two consecutive hinges with unequal
image sizes through BSP/VIS/LIGHT, dependency subset export and PK3 validation.

`level-surface-transfer-smoke` covers mixed brush/patch selection, entity-owned
targets, hit-only flags, retained Valve axes and patch UVs, locks, stale selection
and source bindings, exact history and dependency references, plus actual CLI
dry runs/writes. Its 16-format-pair mapping-only oracle checks serialized texel
coordinates with unequal source/target image sizes. Shell checks cover native
Shift/Alt+Shift/Alt+Ctrl chords, mixed selection, patch hits, separate copy-time
and target package dimensions, copied shader lookup across map formats, immutable
staged image snapshots, cancellation and accessible scaled controls.

`level-surface-project-smoke` checks all four brush dialects and both patch
definitions with independent saved/reopened UV samples, native brush fields,
unequal image dimensions, edge-on results, material/flag policy, comments,
subdivisions, locks, stale UVs, cancellation, exact undo/redo and real CLI calls.
The shell tests exercise both Ctrl+Shift and Alt+Ctrl+Shift on hit brushes and
patches with a mixed selection, plus remapping and legacy preference migration.
The compiler workflow's `--mapping-only --radiant-project` variant adds a curved
patch and verifies compiled UVs for both the patch and projected brush faces.
Its Windows run passed 27 BSP/VIS/LIGHT and package steps, 51 independently
checked compiled UV samples and three validated PK3s. Evidence is retained in
`.agents/tmp/level-surface-project/compiler/verified.json`; no game was launched.

`level-surface-wrap-smoke` exercises an independent displaced/oblique hinge
oracle with mirrored, sheared covectors, 384 serialized shared edges across all
16 source/target format pairs, surface metrics, parallel fallback, missing image
dimensions, conversion consent, cancellation and locks. CLI and shell tests use
the same worker, package snapshot and atomic map writer.

The optional `src/tests/level_surface_wrap_compiler_workflow.py` exercises generated
classic, Valve and primitive rooms through CLI paste, q3map2 BSP/VIS/LIGHT,
dependency review, asset subset export and PK3 publication. On Windows the 28-step
run verified 24 compiled UV samples against independent corner-unfolding equations
(allowing q3map2's whole-repeat normalization), three validated packages, and
42-face map-wide conversion for the classic input. Source maps and clipboard files
remained unchanged. Evidence is in `.agents/tmp/level-surface-wrap/compiler/verified.json`.
Its `--mapping-only` variant uses a 128×64 source and retained 64×256 target
images, verifying texel density through the same 28 compiler/package steps.
That proof passed with 24 compiled UV samples and three validated PK3s;
evidence is in `.agents/tmp/level-surface-transfer/compiler-mapping-only/verified.json`.
This is compiler/package acceptance using synthetic assets; no game was launched.

A generated Quake III room pasted through the CLI converted all 36 classic
faces consistently and compiled with q3map2 to a valid IBSP46 containing six
draw surfaces and 24 vertices. This checks compiler acceptance; no game was
launched. A Windows clang-cl debug (`/Od`) synthetic CLI measurement updated
384/1,536/6,144 faces in 0.19/0.43/1.62 seconds, including process startup,
loading, validation and atomic saving. Source files stayed unchanged. These
repeated-brush fixtures do not establish production-map or interactive latency.
Logs, arguments, generated inputs and results are retained locally under
`.agents/tmp/level-surface-clipboard/compiler/` and `performance.json` in that
task directory.

### Detailed preview

**Edit → Surface Alignment…**, the command palette and the map context menu
open a face list and textured preview for the selected brushes, including
brushes owned by selected entities. Select individual rows or a batch of faces,
or pick faces in the preview. A face focused in the Inspector starts selected
when its brush is the sole object selection.
The dialog uses the same package/staging snapshot, shader/image resolver,
original image dimensions and renderer as the level camera and Models.
Material Details exposes image paths and resolution warnings. Missing images
remain flat shaded; the preview does not simulate game shader effects.

- **Shift** adds U/V texel offsets. Brush primitives convert these offsets to
  repeats using image dimensions.
- **Scale** multiplies texture size around each face's winding centroid. A
  negative factor mirrors that axis; zero is rejected.
- **Rotate** holds the centroid's UV fixed. Classic maps use their stored
  rotation convention, Valve 220 turns explicit axes around the face normal,
  and primitives rotate in texel space to account for non-square images.
- **Fit** maps the current UV bounds to zero through the requested positive
  U/V repeats, retaining the current orientation and mirroring.
- **Align** independently places U/V bounds at the first tile's minimum,
  centre or maximum. **Keep** leaves an axis unchanged.

Fit/Align in classic or Valve maps, and Shift/Rotate in primitive maps, require
known image dimensions. Open the relevant package/folder or enable **Use explicit
texture size**. The override applies to the listed brushes and their preview;
it does not resize an image or change package contents. Primitive Fit/Align
already operate in repeats and do not require image dimensions. Package image
sizes can come from shader editor images; the existing shader-preview limitations
apply. No default size is silently substituted for a required dimension.

Preparation and preview run on immutable worker snapshots. Changing controls
supersedes pending work; Cancel closes the draft. Apply checks that the map,
selection and package have not changed. All changed faces form one map undo
step; no-op edits preserve undo/redo and the save point. Save the map normally
before compiling or packaging it. Source dialect, face planes, material names,
flags, comments and line endings are retained. Operations are bounded to 16,384
requested faces, 128 faces per brush and a 50,000-triangle preview. Invalid,
collapsed or unrepresentable results fail atomically at the map writer's
six-decimal precision. Patches retain their separate control-point UV editor;
Doom surfaces are outside this operation.

The Inspector now exposes all six brush-primitive matrix fields and a **Full
matrix** row for atomic edits. `map edit`
accepts `faceN.matrix00` through `faceN.matrix12`, or `faceN.matrix` with six
row-major values to change a matrix atomically. Matrix offsets are repeats,
not texels. A collapsed matrix is refused. `map inspect --json` includes
`brushPrimitive` and the six-element `matrix` array for each applicable face.

```sh
vibestudio --cli map align-textures arena.map --engine idTech3 --object brush:0 --fit 1,1 --package ./assets --output arena-fitted.map
vibestudio --cli map align-textures arena.map --face 0:1 --align center,minimum --texture-size 128,64 --output arena-aligned.map
vibestudio --cli map edit primitive.map --select brush:0 --set "face1.matrix=0,0.015625,0,-0.015625,0,0" --output primitive-turned.map
```

`--object` and `--face brushId:faceNumber` are repeatable; face numbers are
one-based, matching the Inspector and `map edit`. Choose exactly one of
`--shift U,V`, `--scale U,V`, `--degrees N`, `--fit U,V`, or `--align U,V`.
Alignment modes are `keep`, `minimum`, `center`, `maximum`. `--texture-size W,H`
explicitly overrides dimensions; `--package` and `--palette` use the shared
material resolver.
`--engine` supplies the target when a classic map's syntax is ambiguous.
`--output` is required; `--overwrite`, `--dry-run` and
`--json` follow normal map-save behavior. JSON reports changed face/brush counts
and package material diagnostics. A failed edit or dry run writes no output.

`level-surface-smoke` checks all four brush dialects, independently calculated
UV bounds/centres, non-square matrix rotation, signed scaling, matrix fields,
byte-exact undo, save/reload, cancellation, stale plans and CLI behavior.
`level-surface-ui-smoke` uses the actual dialog/shell, package images, batch undo,
stale draft rejection and direct widget renders at 100% and 200% high-contrast
RTL with expanded labels. Native input, screen readers and cross-platform
acceptance remain part of the broader editor audit.

A synthetic rotated Valve 220 brush was fitted to 2 × 3 repeats and compiled
with the installed q3map2 through BSP/VIS/LIGHT. All 24 emitted vertices matched
the saved projection within 0.000001 repeats after each surface's common integer
tile bias; repeat spans stayed within the compiler's eighth-unit position-snap
bound. Dependency resolution, asset subset export, BSP insertion and full PK3
payload validation also passed. This is compiler/package evidence, not in-game
acceptance. Logs, command arguments and measurements are retained under
`.agents/tmp/level-surfaces/compiler/` locally.

## Material Camera and Package Assets

The level camera shares the Models renderer and reads an immutable snapshot of
the open package or asset folder, including staged additions, replacements and
deletions. **Textures** switches between material images and flat shading;
**Reload** retries resolution, **Cancel** stops pending work, and **Details**
reports image paths, original dimensions, shader scripts and limitations. Open
the project's asset folder or package explicitly; project package-folder lists
and installed game packages are not automatically combined for this camera.

Classic and Valve 220 brush coordinates use the original image's width and
height, even when the preview is downsampled. Brush-primitive matrices and patch
coordinates already store texture repeats. The same projection service used by
numeric texture-locked rotation supplies camera UVs. Placed MDL, MD2 and MD3
models use frame zero, their stored UVs, and embedded or referenced skins.
Generated staged props use this same path. Unsupported model formats remain
reported in Details.

Quake III materials prefer `qer_editorimage`; otherwise the first static image
reference from a stage is used, skipping built-in lightmaps. An animated stage
shows its first referenced image. Shader lighting, blending, animation,
deformation, clamp modes and texture-coordinate effects are not simulated.
Duplicate shader declarations or image identities are refused. An unreadable,
malformed or budget-limited shader catalog withholds material previews, because
the resolver cannot establish which script owns a name. Doom flats, composite
wall textures, namespace resolution and pegging use the separate bounded Doom
resolver described above. Generated fallback palettes are identified in Details.

Image/model decoding and geometry reconstruction run on a coalescing worker.
Unchanged asset requests reuse images and models; document edits still rebuild
geometry and UVs. Stale jobs cannot publish into a later map. Picking waits for
current triangle ownership; camera position survives edits and Save As. Hide
and Isolate affect both views. Staged changes, undo/redo, palette changes and
package reopen invalidate the relevant preview. The Levels texture tiles reuse
the resolved preview images and show their dimensions.

Default limits are 256 material records, 32 model files, 16 MiB per asset read,
4 MiB per model, 64 MiB total reads, 64 MiB retained image pixels, and 512 pixels
on a preview's longest edge. Model geometry is limited to 150,000 triangles and
450,000 vertices in aggregate; the final scene also stops at 150,000 triangles.
Shader scanning stops at 2,048 scripts, 4 MiB per script, 16 MiB of scripts or
65,536 definitions. Image decoding is limited to 16 megapixels. Limits are
reported; a partial preview is not treated as complete. Cancellation is checked
between bounded reads and geometry objects.

The same resolver is available without the GUI:

```sh
vibestudio --cli map materials ./maps/arena.map --package ./assets --engine idTech3 --json
```

`--package` accepts a supported archive or folder; `--palette` overrides indexed
image colours. Text and JSON include original and preview dimensions, source
paths, statuses, counts and budget warnings. Unavailable materials/models or an
incomplete scan return validation failure. Built-in compiler materials are
accounted for without an image. This previews appearance; use `map dependencies`
for the complete packaging dependency graph.

`level-materials-smoke` covers synthetic images, shader priorities, all brush UV
dialects, patches, model skins, snapshot isolation, staged replacement, PK3
parity, changed sources, budgets, cancellation and CLI output.
`level-materials-ui-smoke` exercises the worker and actual shell, including
superseded results, cached assets, edits/undo, camera preservation, and direct
widget renders. These fixtures do not establish large-project performance,
native input/screen-reader acceptance, or macOS/Linux behavior.

## Transform Texture Controls

**Edit → Texture Lock** keeps brush UVs attached during numeric/viewport moves,
quick quarter turns and flips. It defaults to on. **Texture Scale Lock** applies
the same rule to numeric resizing and viewport resize handles, stretching the
texture with the geometry; it defaults to off. **Allow Valve 220 Conversion**
defaults to off and permits map-wide conversion only when an exact locked
projection requires it. These three settings persist between sessions. Numeric
rotation starts with the lock/conversion settings and permits per-draft changes.

All four brush dialects use one affine projection service. It applies the inverse
transpose of the geometry transform to the UV axes, then solves the original
dialect where possible. Materials, flags, entity ownership and model references
remain intact. A required classic-to-Valve conversion also preserves unselected
brush appearance and belongs to the same undo step as the geometry change.
Nonfinite or collapsed transforms, stale objects, malformed face lines and
unrepresentable mappings fail before any document/history changes. Selected
brush entities and their individually selected children move only once.

With either lock off, written brush parameters remain unchanged. This is world
anchoring for translation and axial resizing, but a changed plane-dependent
basis can alter the mapping on an oblique face. Patch UVs always travel with their
control points. These settings do not change Doom wall/flat offsets or component
reshaping. Ownership-aware **Snap Selection to Grid**, duplication and paste
offsets now share the rigid texture-lock policy and cancellable preparation;
see [Placement and grid alignment](#placement-and-grid-alignment). Other direct
transforms remain synchronous and need large-selection responsiveness work.

`map move`, `map rotate` (both `--turns` and `--degrees`), `map flip` and
`map resize` accept `--texture-lock on|off` and `--allow-valve220`. The CLI defaults
match the editor defaults: on for moves/turns/flips, off for resize. JSON names
the effective `textureLockPolicy` and conversion permission. An explicit policy
overload is available to core callers; older move overloads retain their original
delta-command/source-parameter behavior for compatibility.

```sh
vibestudio --cli map move arena.map --object brush:6 --delta 16,0,0 --texture-lock on --output arena-moved.map --json
vibestudio --cli map resize arena.map --object brush:6 --size 256,64,128 --texture-lock on --allow-valve220 --output arena-stretched.map --json
```

The reusable `src/tests/level_transform_compiler_workflow.py` generates its own
textures and sealed maps, chains move/turn/flip/resize, compiles BSP/VIS/LIGHT,
checks emitted vertex UVs against an independent inverse transform, and builds
dependency subsets and validated PK3s. The Windows run with q3map2
`2.5.17n-git-e62c6f4b` passed 33 steps across classic, Valve 220 and brushDef input:
72 compiled UV samples, six faces per variant, and eight verified payloads per
package. brushDef3 is fixture-tested in the core; it is outside that Quake III
compiler run. No game was launched. Evidence lives under
`.agents/tmp/level-transform-lock/`.

## Numeric Rotation and Texture Lock

**Edit → Rotate Selection…**, also available in the viewport's Transform menu
and command palette, previews an arbitrary angle around X, Y or Z. Choose the
selection centre, world origin or custom coordinates as the pivot. Positive
angles follow the right-hand rule. Preview work runs on an immutable snapshot
in a worker; changing a control supersedes the old preview. Apply stays disabled
until the current request has passed validation. A map reload, edit or selection
change invalidates the draft at commit. Cancel leaves the document unchanged.

Texture lock preserves the texture coordinates of every point on a brush face.
Valve 220 axes and brushDef/brushDef3 matrices retain their dialect. Classic
shift/rotation/scale fields are solved exactly when possible. A rotation that
requires shear fails unless **Allow Valve 220 conversion** is enabled. Because
q3map2 selects one texture dialect for the whole map, this option converts *all*
classic faces when needed, including unselected brushes, while preserving their
appearance. All affected faces are reported in the preview and included in the
same undo step. Conversion refuses maps containing brush-primitive matrices.
Check that the project's compiler accepts Valve 220 before using that option.
Turning texture lock off preserves the written face parameters. Patches always
retain their control-point UV coordinates.

Brush bounds are reconstructed from the rotated planes; patch bounds come from
the rotated control points. Entity `angles` compose pitch, yaw and roll using
the same Z/Y/X convention as model placement. A scalar `angle` cannot represent
every 3D orientation: unsupported cases fail rather than silently changing the
entity's game semantics. Materials, model paths, ownership, flags and surrounding
source comments survive. Face definitions must occupy complete individual lines.
The geometry preview uses the shared model viewport; it does not load material
images or packaged model meshes yet.

Binary Doom and Hexen rotate in the XY plane. Selected vertices, linedefs,
sector boundaries and things share a pivot; shared vertices move once. Positions
round to WAD integers, and a collapsed linedef or out-of-range coordinate blocks
the operation. Geometry changes invalidate node data and require a node rebuild.
Binary Doom and Hexen also support X/Y reflection about the selection bounds.
Every incident linedef must have both endpoints in the transform. An attached
partial selection is refused with guidance to **Select Connected Geometry**
(Edit, command palette or the plan view's Transform menu). Expansion follows
shared vertex IDs, preserves independently selected things and leaves disconnected
components unselected. Selecting a sector seeds all of its boundary loops,
including holes and islands. Expansion itself does not edit the map or history.

A reflection reverses the affected linedef endpoints and retains front/back
sidedefs, sector ownership, flags, actions, tags, Hexen arguments, textures and
offsets. Things share the pivot and reflect their headings. The whole edit is
one undo step; Undo restores the original WAD, including its node records. Saving
changed geometry clears obsolete runtime node payloads and removes the selected
map's separate GL cache group. Reopening therefore still reports that a node
build is required. GUI and CLI use the
same cancellable preparation service, scene-lock checks and native serialization;
the GUI also rejects results if selection, map or package state changed. Existing
preview, recovery, dependency and WAD save services consume the resulting document.

Binary Doom does not encode negative wall texture scales, so reflection retains
native offsets/pegging; it does not promise Quake-style locked UVs. Normal map
health checks still apply to overlaps or boundaries moved relative to unselected
components. Automatic detach/stitch for attached partial reflections, UDMF
topology tools and broader 3D manipulation remain open. UDMF property authoring
is available through the text-map document layer described below.

```sh
vibestudio --cli map flip ./maps/start.wad --map MAP01 --object linedef:0 --object thing:0 --connected --axis x --output ./maps/start-mirrored.wad --dry-run --json
```

`--connected` is opt-in and requires binary Doom/Hexen geometry. Without it,
already complete geometry selections and things can still be mirrored. The
CLI save report's `staleLumps` array identifies required node rebuilds without
parsing translated warnings. Its Doom texture policy is `native-offsets`. The
`level-doom-mirror-smoke` and `level-doom-mirror-ui-smoke` suites cover native
records, exact history, cancellation, locks, WAD preservation and shell adoption.
The optional `src/tests/level_doom_mirror_compiler_workflow.py` proof uses the
registered ZDBSP wrapper and independently checks compiled seg-side ownership.

### Doom node readiness

Map Health and `map inspect --json` share the `nodeBuild` report for the current
WAD: `missing`, `invalid`, `stale`, `present`, or `unsupported`, with format,
record counts, warnings and `needsBuild`. The source report is cached during
background loading; geometry edits override it. Runtime products are cleared
on save, while the owned source snapshot keeps exact Undo available. Other maps,
resources and duplicate resource entries retain their bytes and order. Ambiguous
separate GL labels refuse a geometry save instead of choosing an owner.

The validator checks classic SEGS/SSECTORS/NODES and XNOD/ZNOD,
XGLN/ZGLN, XGL2/ZGL2 and XGL3/ZGL3, including GL records in SSECTORS. It checks
record bounds, geometry references, subsector coverage, BSP tree references,
nonempty BLOCKMAP lists and REJECT size. A single subsector legitimately has
zero NODES records. Decoded node streams are capped at 128 MiB, WAD snapshots
at 512 MiB, with cancellation during reading, parsing and decompression.
An empty BLOCKMAP warns that the source port must generate collision data.
UDMF ZNODES uses the same extended/compressed structural validator. DeePBSP
and separate GL cache formats are reported as unvalidated; a
separate GL cache beside valid native nodes also produces a warning.

ZDBSP and ZokumBSP outputs use this same service before they are registered as
successful. Explicit map selectors limit inspection to those maps. A successful
build produces an output whose node report is `present`; opening that output
clears the source-WAD rebuild warning. The original authored WAD retains its own
status. Structural validity cannot prove externally retained nodes were built
from the current geometry, or establish gameplay and source-port compatibility.

Doom launch planning validates the selected map in a supplied WAD and refuses
missing or invalid runtime nodes. Unsupported formats remain warning-bearing
plans. Plans record the validated WAD SHA-256 and recheck it before launch.
The `-file` argument uses that absolute path even when the request was relative.
MAP-number and episode/map warp shorthand are resolved to native map labels
for inspection while preserving the requested launch arguments.
The GUI preview shows pending validation; actual preparation and the final hash
check run in a cancellable worker. Cancellation waits for acknowledgement and
cannot leave a worker that starts the game after the dialog returns.

`level-doom-nodes-smoke` covers malformed records, save/reopen/recovery, exact
undo, GL ownership, compiler selection and a harmless launch recorder.
`level-doom-nodes-ui-smoke` covers worker responsiveness, cancellation, accessible
controls, reduced motion, 100%/200% themes and expanded RTL text.
`src/tests/level_doom_nodes_compiler_workflow.py` exercises real ZDBSP formats,
package validation and CLI launch plans without starting a game. Native
screen-reader, keyboard and cross-platform acceptance remain manual checks.

```sh
vibestudio --cli map rotate ./maps/arena.map --object brush:6 --axis z --degrees 31.75 --pivot 128,0,64 --texture-lock on --allow-valve220 --output ./maps/arena-rotated.map --json
vibestudio --cli map rotate ./maps/start.wad --map MAP01 --object sector:0 --axis z --degrees 15 --output ./maps/start-rotated.wad --dry-run --json
```

`--degrees` and `--turns` are mutually exclusive. `--texture-lock` accepts `on`
or `off` and defaults to `on` for all rotation commands. `--allow-valve220` is explicit
conversion consent; `--pivot x,y,z` overrides the selection centre. Angles must
be finite, within ±360,000°, and not a whole number of full turns. Pivot and
Quake geometry coordinates are limited to ±32,768; WAD coordinates use their
signed 16-bit range. Save As requires `--output`, replacement requires
`--overwrite`, and `--dry-run` performs validation without writing.

Bare `--turns` and Rotate 90° Left/Right now use the same projection locking as
numeric rotation, including classic faces and primitive matrices. Use
`--texture-lock off` or the editor's Texture Lock toggle to retain source
parameters. The JSON result names the policy.

The Windows rotation core/CLI and actual-shell tests cover all four brush
dialects, map-wide conversion, source comments, atomic failures, model angles,
patch ownership, Doom/Hexen shared vertices, node state and undo/save/reload.
Widget renders cover normal scale and 200% high contrast with RTL and expanded
labels; numeric controls fit without horizontal clipping. An original synthetic
q3map2 BSP/VIS/LIGHT-to-PK3 fixture verified UVs on 24 rotated and 24 unselected
BSP vertices within `3.55e-7` repeats. Evidence and command manifests are under
`.agents/tmp/level-rotation/`. These checks do not establish native keyboard,
screen-reader, other-compiler or cross-platform acceptance.

## Map Documents and Recovery

**Levels → New Map** creates an empty document or starter room for Quake,
Quake II, Quake III, binary Doom, or binary Hexen. Brush rooms contain six
walls/floor/ceiling brushes, a player start, and a light; Doom rooms contain one
sector and a player start. Texture names reference your own game/project assets.
The editor includes no commercial textures; review Dependencies before building.
New Quake II documents carry a harmless target comment so Levels Compile and
CLI `map compile-plan` retain `-q2bsp` after reopening or recovery. Existing
unmarked maps keep their existing compiler/profile behavior.

**Save** (`Ctrl+S` while Levels has focus) chooses a path for an untitled map and
then saves that document in place. **Save As** chooses another destination and
makes it the active document, preserving undo/redo. Saves run on a worker with
visible progress and cancellation. Before replacing a file, the shared service
checks its SHA-256 against the loaded baseline, backs up the exact prior bytes
under `<destination folder>/.vibestudio/map-backups`, rechecks the destination,
and commits with `QSaveFile`. Backups are retained until removed by the user.
If the original changed or disappeared, use Save As to keep your edits, or
reload after reviewing the outside change. This is conflict detection, not an
exclusive filesystem lock; another process must not write concurrently during
the final commit. Failed opens and saves retain the working document.
Saving refuses a destination with unsaved Code edits or staged/active package
work. Clean Code views and an open WAD package refresh after the save, before
the document watcher adopts the new baseline.

WAD documents own the ordered archive snapshot they loaded, including other
maps, resource lumps, and duplicate names. Saving another path uses that
snapshot even if the original WAD changes or disappears. Geometry edits still
require the appropriate external node builder; recovery does not build nodes.

**File → Recover Maps** lists local checkpoints, restores one as unsaved work,
and can delete an explicitly selected checkpoint. Automatic checkpoints are on
by default, written every minute for changed documents on a background worker.
They never replace the actual map. Checkpoints include the serialized document,
source fingerprint, map identity, revision and Doom node-staleness flag, with
bounded metadata and a verified content hash. They start a fresh undo history
when restored. Saving, explicitly discarding, or closing a document normally
retires its checkpoint, including a write already in flight. Separate document
UUIDs prevent one studio instance from overwriting another's checkpoint.
A restored checkpoint is retained until explicitly deleted in Recover Maps;
saving the restored document retires only that document's own new checkpoint.
This protects checkpoints still owned by another running studio instance.

The recovery directory is `map-recovery` beneath Qt's local application-data
location. With `--settings-file`, it is beside the chosen settings file instead,
so portable/test profiles remain isolated. The Recover Maps dialog controls the
`levels/recoveryEnabled` preference. Checkpoints contain local map data and
paths and are never sent to an AI connector. A crash can lose changes made since
the last completed checkpoint; recovery is not a substitute for saving.

The CLI uses these same services:

```sh
vibestudio --cli map new --game quake3 --preset room --output ./maps/arena.map --dry-run --json
vibestudio --cli map new --game doom --map MAP01 --texture WALL --floor-texture FLOOR --ceiling-texture CEILING --output ./maps/start.wad
vibestudio --cli map recoveries ./profile/map-recovery --json
vibestudio --cli map recover ./profile/map-recovery/<uuid>.vsrecovery --output ./maps/recovered.map --dry-run --json
```

`map new` accepts `--game quake|quake2|quake3|doom|hexen` (default `quake3`),
`--preset room|empty` (default `room`), `--map` for a Doom marker, and optional
`--texture`, `--floor-texture`, and `--ceiling-texture`. `map new` and
`map recover` require `--output`; replacement needs `--overwrite`. Dry runs
fully serialize and validate without creating directories, outputs or backups.
`map recoveries` checks bounded metadata; the payload integrity check happens
on restore. JSON save reports include `backupPath` and `sha256`.

The document service limits loaded/serialized maps to 512 MiB. Loading remains
synchronous, and large-map performance still needs dedicated acceptance work.
Document lifecycle and patch authoring have focused tests; the full editor
target below remains open.

## Brush Component Authoring

Select a brush and choose **Edit → Edit Brush Components** (also available in
the context menu and command palette). Vertices, Edges and Faces modes expose
the solved solid's components, rather than the three arbitrary plane points
stored on each map face. The XY/XZ/YZ view supports picking, Ctrl-click,
dragging, arrow nudging, panning, zoom and framing. A standard Qt table exposes
every component ID and its position; edge and face coordinates are their vertex
centers. The Surface tab shares the model renderer and supports face selection.

Move Selected and Snap Selected apply to the union of selected component
vertices. A convex hull rebuild splits bent faces and merges coplanar ones;
faces inherit material, flags and texture parameters from the source face with
the most shared vertices (normal alignment breaks ties). Texture parameters
remain in their stored frame; deforming a face does not provide texture lock.
The draft reports vertex/face counts and collapsed vertices. Interior or merged
vertices require **Allow Vertex Collapse**; flattened, open, numerically unstable
or out-of-range results are rejected without changing the draft. Editing is
bounded to 128 faces, 256 vertices and coordinates within ±32768 units.

Local Undo/Redo retains selection and geometry. Apply commits one map undo
operation; Cancel discards the draft. A map reload or another document edit
blocks Apply and keeps the draft visible, so stale geometry cannot overwrite
the current map. Classic, Valve 220, brushDef and brushDef3
keep their own material syntax, flags and comments. The edited brush is
re-emitted at the end of its owning entity with the same in-memory ID; saving
and reloading can therefore renumber brushes. Component IDs describe the current
topology and must be queried again after changes. Shared brace/face lines and
face definitions split across lines may need normalization in Code first.
The saved result is parsed and checked against the requested planes before
the document changes. Copying, duplication, material edits, recovery and CSG
continue through the existing map services.

```sh
vibestudio --cli map brush-components arena.map --brush 7 --json
vibestudio --cli map move-components arena.map --brush 7 --kind vertex --component 2 --component 3 --delta 0,0,16 --output raised.map
vibestudio --cli map move-components raised.map --brush 7 --kind face --component 0 --grid 8 --output snapped.map --dry-run --json
```

`brush-components` reports zero-based vertices, edges and face vertex lists.
`move-components` requires `--kind vertex|edge|face`, repeated `--component`,
`--delta x,y,z` and/or `--grid`, and `--output`. Movement precedes snapping of
the resulting coordinates. `--allow-collapse` explicitly accepts disappearing
vertices. JSON reports include before/after counts and `collapsedVertices`.
The write command uses the normal dry-run, overwrite and document protections.

`level-brush-smoke` covers all four face dialects, every cube edge/face,
fractional vertex edits, collapse rejection, source comments/material mappings,
exact undo, recovery, clipboard/paste, CSG and CLI persistence. The direct-API
`brush-editor-ui-smoke` checks selection, coordinates, local/map undo, failed
commits, save/reload and rendering at 100% and 200%, including high contrast,
right-to-left layout and expanded labels. Physical gestures and screen-reader
behavior remain manual acceptance gaps.

An original synthetic room reshaped through vertex, edge and face CLI edits
passed q3map2 BSP, VIS and LIGHT on Windows on 2026-10-04
(`2.5.17n-git-e62c6f4b`). The resulting BSP was leak-free and retained its
material references. Dependency export and PK3 validation verified the BSP,
shader script and image: three payloads, no unchecked entries or warnings.
The command manifest and generated artifacts are under
`.agents/tmp/level-components/compiler/`. No game was launched.

## Curved Surface Authoring

**Edit → Add Patch** creates a Quake III plane, open cylinder or open cone.
The same actions appear in the map context menu and command palette. Select a
patch and choose **Edit Patch Control Points** to reshape it. Plane grids have
odd dimensions from 3 to 31; cylindrical presets use nine columns with a closed
geometric seam. Their ends remain open. Patches are not available in Quake,
Quake II or Doom documents.

The control grid offers XY, XZ and YZ projections, point picking, Ctrl-click
selection, dragging, arrow-key nudging, middle-button panning, zoom and framing.
Selected handles are square; unselected handles are round. The adjacent standard
Qt table exposes zero-based row/column identities and editable X, Y, Z, U and V
coordinates. Table selection and canvas selection stay synchronized. Move Selected
applies a delta; Snap Selected rounds positions to the chosen grid. A zero grid
disables snapping. UV coordinates are in texture repeats.

The editor has local Undo/Redo, Invert Facing, Reset UV, and Split Rows/Columns.
Splitting uses quadratic Bézier subdivision and preserves the surface and UV
mapping. Apply commits one undoable map edit; Cancel drops the local draft.
The Surface / UV Checker tab previews the tessellated result with a procedural
checker, not the selected game's material. Shared level preview meshes now carry
interpolated patch UVs. Existing `patchDef3` subdivision counts are retained and
used on each preview axis; the creation presets write `patchDef2`. Compiler
support depends on the map dialect: the bundled q3map2 source accepts
`patchDef2`, so preserving a loaded `patchDef3` does not establish Quake III
compiler compatibility.

Editing normalizes the patch's own text block, retaining its header extension
fields and comments. Unrelated map text stays intact. Patches whose opening or
closing brace shares a line with other text cannot be edited in this workflow;
put those braces on separate lines in Code first. Save, recovery, copy/paste,
duplication, deletion, dependency inspection and package material references use
the same map document. Saving remains necessary before a compiler can see edits.

```sh
vibestudio --cli map add-patch arena.map --shape cylinder --origin 0,0,64 --size 128,128,128 --texture textures/studio/metal --output arena-curves.map
vibestudio --cli map edit-patch arena-curves.map --patch 0 --point 1,2 --point 1,3 --delta 0,0,16 --output arena-raised.map
vibestudio --cli map edit-patch arena-raised.map --patch 0 --point 1,2 --uv 0.25,0.5 --subdivide columns --output arena-refined.map --dry-run --json
```

`add-patch` defaults to a 3×3 XY plane, size 128 on each axis, centered at zero.
Use `--columns`, `--rows` and `--plane xy|xz|yz` for planes; curved presets always
use nine columns. `--texture` is required. A leading
`textures/` is removed once from material choices for `patchDef2`, because
q3map2 supplies that prefix; existing map tokens retain their spelling.
The Health texture check follows the planned package, including staged edits and
Undo/Redo. It runs in the background with visible progress, Cancel and Retry;
changes to either document supersede older requests. Missing references and
incomplete shader/source checks are explicit, and an unavailable package plan
cannot fall back to the original archive. The CLI accepts saved package drafts
through `map textures --package`, with `complete` and `cancelled` JSON fields.

Texture audits and dependency export resolve these tokens against full package
material paths, including shader scripts and their referenced images.
`patchDef3` retains full material paths. `edit-patch` accepts repeated
`--point row,column`, then applies movement/snapping, UV assignment, material
replacement, subdivision and facing inversion in that order. `--grid` snaps the
resulting positions; `--uv` assigns the same pair to each named point.
`--subdivide rows|columns` and `--invert` affect the entire patch. Both commands
require `--output`, support `--dry-run`/`--json`, and require `--overwrite` for an
existing destination.

`level-patch-smoke` covers geometric/UV equivalence under subdivision, exact undo,
multiline source preservation, copy/delete, recovery, dependency resolution and
CLI parity. `patch-editor-ui-smoke` exercises programmatic widget actions and the
actual shell. Physical gestures and screen-reader behavior still need manual
acceptance; tests do not inject input.

An original synthetic room containing a cylinder, cone and subdivided curved
plane passed q3map2 BSP, VIS and LIGHT on Windows on 2026-10-04
(`2.5.17n-git-e62c6f4b`). Inspection verified three patch surfaces and the expected
shader names in the BSP. Dependency export resolved the shared shader and image,
and package validation verified all three payloads in the resulting level PK3.
The local command manifest and artifacts are under
`.agents/tmp/level-patches/compiler/`. No game was launched. Non-planar covers,
multi-seam propagation and broader compiler/game acceptance remain. Boundary
stitching and planar caps are now available as described above. The main level camera
now resolves material images; the Patch Editor retains its UV checker.

## Reusable Prefabs

**Edit > Export Selection as Prefab** captures selected Quake-family brushes,
patches and entities in a `.vprefab` authoring asset. Selecting any primitive in
a brush entity includes that entity's properties and all its sibling geometry;
the preview reports this expansion. Worldspawn project settings are excluded.
Give the assembly a name and description and use its bottom-centre anchor or
enter an explicit XYZ anchor. Capture leaves the source map and selection intact.

**Stage Selection as Prefab** adds owned bytes to the open package's pending
changes. Choose a package-relative `.vprefab` path; replacing an entry is explicit.
Package undo and Save As remain separate from map undo and saving. Prefabs are
authoring assets for folders and PAK/ZIP/PK3 libraries; WAD's flat lump namespace
does not support these paths. **Export Selection as Prefab** writes a loose file
on a worker through the shared package publication service, retaining a verified
independent backup on explicit overwrite and exposing recovery details on failure.

**Insert Prefab**, a project/file open, or opening a package `.vprefab` entry
prepares an isolated placement preview in the current map. Set the anchor's XYZ
position and rotation about X, then Y, then Z. Texture lock starts from the
editor's persisted rigid-transform preference. It retains each brush's texture
dialect and refuses an unrepresentable projection; there is no implicit Valve
220 conversion. Patches retain UVs, and model-style entity angles follow rotation.
The accepted candidate inserts as one undo step and selects the new objects.
Undo restores the earlier selection; redo reselects the complete assembly.
Changes to the map, selection, package or palette invalidate an open preview.

Each placement prefixes internal `targetname` definitions and matching `target`,
`killtarget`, `pathtarget`, `combattarget` and `deathtarget` values with a unique
`prefabN_` namespace. Names already referenced by the destination also reserve a
name, preventing an unresolved link from accidentally capturing new geometry.
An explicit prefix must be unique and match `[A-Za-z_][A-Za-z0-9_]{0,31}`; resulting
names are limited to 255 UTF-8 bytes. External links keep their names and produce
a warning. Custom properties containing `target` are reported without remapping;
game-specific scripts and other link conventions need review.

The asynchronous preview shares the Models renderer and the Levels material,
model and dependency services, including staged package changes. **Details**
shows renamed targets, external links, resolved paths and missing dependencies.
The view shows only the assembly, with static frame-zero models and material
images; point-only assemblies may have no renderable mesh. Full shaders, sound
playback and game logic are not simulated. Missing assets permit authoring but
must be resolved before a complete dependency export. A prefab references its
textures, models and sounds by their existing paths; it does not bundle those
assets. Save the map and publish required assets into the compiler's search path
before building. Insertion produces ordinary independent objects, not live linked
instances that update when the prefab changes.

```sh
vibestudio --cli map export-prefab arena.map --engine idTech3 --object brush:6 --object entity:2 --name Door --anchor 0,0,0 --output door.vprefab --json
vibestudio --cli map inspect-prefab door.vprefab --package assets.pk3 --json
vibestudio --cli map insert-prefab arena.map --engine idTech3 --prefab door.vprefab --position 128,0,0 --rotation 0,0,90 --output arena-door.map --json
vibestudio --cli map insert-prefab arena.map --engine idTech3 --prefab library.pk3 --entry prefabs/door.vprefab --position 128,0,0 --output arena-door.map --dry-run --json
vibestudio --cli map inspect-prefab library.pk3 --entry prefabs/door.vprefab --json
```

Export also accepts `--description` and an optional `--package` for dependency
review. Without `--anchor`, it uses the captured bounds' bottom centre. Insertion
requires `--position`; `--rotation` defaults to zero, `--texture-lock on|off` to
on, and `--target-prefix` to automatic allocation. `--package` overrides the
dependency reader; otherwise a prefab loaded via `--entry` uses that package.
Classic Quake III maps may need explicit `--engine idTech3` because their face
syntax alone does not distinguish Quake from Quake III. Input paths can instead
use `--input`; unknown/duplicate options, missing values and extra positionals
fail. Export/insertion require `--output`, support write-free `--dry-run`, and
require `--overwrite` for existing destinations. JSON includes `prefab` metadata,
counts, dialect, links, warnings, placement and optional dependency details, plus
the normal save result. Exit codes are 0 success, 2 usage, 3 missing input, and
4 invalid prefab/placement/publication; map load failures retain shared CLI codes.

The version-1 JSON envelope contains `format: "VibeStudioPrefab"`, `version: 1`,
string `name`, `description`, `engineFamily`, `map`, and a three-number `anchor`.
Files are capped at 8 MiB; map text at four million UTF-16 characters. Names are
1–128 characters, descriptions at most 4096, coordinates within ±32768, and
rotations within ±360000 degrees. Bounded grammar preflight precedes the shared
map parser. A definition needs one worldspawn, at most 4096 entities, 4096 brushes,
256 patches, 128 faces per brush and 16384 faces total. Duplicate entity keys,
compiled inline model references, invalid geometry, mixed face dialects and
cross-engine insertion are refused. VibeStudio's existing family IDs group
Quake/Quake II as `idTech2` and Quake III as `idTech3`; this does not establish
game-code compatibility between games. Doom prefabs, cross-dialect conversion,
linked-instance updates, prefab library browsing and large-assembly performance
remain future work.

`level-prefab-smoke` covers capture ownership, version/schema validation,
publication/backups, target isolation, four texture dialects, cancellation,
failure atomicity and exact undo/redo. `level-prefab-ui-smoke` uses direct Qt
calls and widget rendering for capture, staged assets, placement, actual shell
routes, stale guards, 100%/200% text, high contrast, RTL and translation expansion.
The optional `src/tests/level_prefab_workflow.py` generates original model, sound,
texture and map data. Its 27-step Windows run passed q3map2 BSP/VIS/LIGHT
(`2.5.17n-git-e62c6f4b`), retained two patch surfaces and the separate target
names, baked generated MD3 geometry, and verified six final PK3 payloads with no
warnings or unchecked entries. Evidence is under `.agents/tmp/level-prefabs/`.
No game was launched; native interaction, macOS/Linux and production-scale
acceptance remain open.

## Placement and Grid Alignment

**Duplicate with Offset…** and **Paste with Offset…** are available from Edit,
the command palette and the map context menu. They preview explicit XYZ offsets
using an immutable snapshot of the current package draft. Brushes, patches and
static models share the camera's material resolver and renderer; Details exposes
asset and dependency diagnostics. Preview preparation runs on a worker, with a
visible busy state, cancellation on close, and rejection of obsolete results.
Progress reports geometry preparation, preview filtering, package asset
resolution, dependency review and mesh construction. Reduced-motion settings
replace indeterminate animation with a static indicator.
Changing the map, selection, active scene destination, save state or package
invalidates publication. Applying the prepared candidate is one undo step.

The existing quick Duplicate command still moves one grid step in the active
plan projection. Quick Snap, Duplicate and Paste also prepare on a worker. A
progress window appears after 150 ms for longer edits, with textual phases,
object counts where available, and Cancel/Escape. Cancellation closes immediately
and leaves the current document untouched; the worker discards its snapshot at
the next checkpoint. Parsing, object/face traversal, brush solving, insertion
and final history preparation check cancellation. Results are checked against
the current map, selection, scene and package even before that window appears.

Both quick and explicit placement use the editor's **Texture
Lock** preference. CLI `map duplicate`, `map paste` and `map snap` default to
locked brush textures; `--texture-lock off` retains the source parameters.
Classic, Valve 220, brushDef and brushDef3 projections use the shared affine UV
service. Patch UVs always travel with their control points. The legacy C++
overloads retain their previous unlocked policy; new callers should provide it
explicitly. Translation does not require conversion between texture dialects.

**Snap Selection to Grid** computes every offset from the original document:

- Point entities use their origins. Selected brush entities move as assemblies,
  using their origin or, when absent, their combined lower corner. An absent
  origin remains absent. Selecting an owner and its children moves each native
  record once.
- Independent brushes and patches use their lower corners. Brush bounds must
  be solved; malformed/open brushes are refused. This is object translation,
  not vertex deformation or automatic shape repair.
- Doom/Hexen things and vertices snap in XY on a positive integer grid. Lines
  and sectors expand to unique boundary vertices. Shared vertices move once;
  a resulting collapsed linedef is refused. Geometry changes invalidate nodes.
  A thing's height, angle and other native fields remain unchanged by snapping.

Finite offsets, world bounds, texture mappings, source bindings and scene locks
are validated before publication. Failure preserves the map, selection, scene,
revision and undo/redo history. Undo/Redo uses exact native snapshots. Copy
insertion restores the previous selection on undo and selects the copies on redo.
Duplicate retains source scene membership; paste joins the active creation node.
Locked originals can be copied into an unlocked destination. A duplicate that
would inherit protected membership, or paste into a locked node, is refused.
Bulk placement, source-line ownership checks and snapshot replay build object
indexes once per operation, including maps with ID gaps after deletions. Geometry
and texture validation still run for every changed brush.
Binding validation avoids formatting discarded face text and reuses the
original source template across chained unsaved transforms and duplications.

Paste accepts entity blocks, bare brushes/patches, or worldspawn contents. Imported
brushes must match the destination texture dialect; mixed brush dialects are
refused. Patches require a Quake III map. Clipboard text is bounded to 4,194,304
UTF-16 characters with no NUL, 8,192 blocks, seven nested blocks, 64 parenthesis
levels, 512 root parenthesis groups per block and 250,000 total parenthesis
openings before geometry parsing. CLI input additionally has an 8 MiB byte limit
and requires valid UTF-8 (a BOM is accepted).

```sh
vibestudio --cli map snap room.map --object entity:3 --object brush:12 --grid 16 --output aligned.map --json
vibestudio --cli map duplicate aligned.map --object entity:3 --delta 128,0,0 --output copied.map --json
vibestudio --cli map paste copied.map --from assembly.map --delta -128,0,0 --output placed.map --dry-run --json
```

`map paste` requires `--from` and `--output`; `--delta` defaults to zero.
Unknown, repeated, missing-value or extra positional arguments fail before
publication. Output replacement requires `--overwrite`. JSON adds `pasted`,
`copies` or `snapped` native selectors, the offset/grid and texture-lock policy.
Use `--engine idTech3` when an otherwise ambiguous classic text map needs that
target. Duplication and paste retain model, sound, target and material references;
they do not rename target connections or move asset files. Use reusable prefabs
when each assembly needs a new internal target namespace. Doom/Hexen duplication
supports things, rounded to their stored whole coordinates; Doom map-text paste
and geometry duplication remain unsupported.

`level-placement-smoke`, `level-placement-cli-smoke` and
`level-placement-ui-smoke` cover dialect/UV persistence, owner overlap, sparse
IDs after deletion, conflicting source lines, shared Doom geometry, locks,
atomic refusals, bounds, history, CLI output protection,
package draft previews and semantic Qt shell routes. `level-placement-jobs-smoke`
adds cancellation at each transaction phase, mid-parser cancellation, concurrent
and nested controls, chained unsaved Valve conversion/copies and exact history.
`level-placement-jobs-ui-smoke` checks GUI heartbeat, immediate cancellation,
quick action adoption/undo and stale map rejection. Widget renders exercise
100%/200% text, high contrast, RTL and expanded translations. The optional
`level_placement_compiler_workflow.py` generated three maps and passed 28 CLI and
q3map2 stages: 216 compiled UV samples across 54 placed faces, generated MD3
geometry and sound dependencies, and three validated PK3s. Evidence is under
`.agents/tmp/level-placement/`; worker/cancellation follow-up evidence is under
`.agents/tmp/level-edit-jobs/`. Native input/screen-reader, cross-platform,
production-size placement latency and engine acceptance remain open.

The Windows debug service benchmark selects every brush in synthetic 100, 1,000
and 10,000-box documents, with texture lock enabled (three repetitions). Removing
discarded face-text rewriting reduced median snap times to 23 ms, 229 ms and
9.13 s; duplicate times were 23 ms, 221 ms and 7.14 s. Previous 10,000-box medians
after bulk indexing were 22.5 s and 21.1 s. The new 10,000-box samples varied from
4.84–9.55 s for snap and 7.12–8.98 s for duplicate. These are local service timings,
excluding selection, rendering and shell refresh; synthetic boxes and a debug
build do not establish release performance. The benchmark accepts optional
`brush-count repetitions` arguments and records phase timings as JSON.

The worker UI test cancelled a 10,000-box preparation at 100%/200% text. Cancel
returned in 1 ms at both scales; the largest GUI heartbeat gaps were 38/31 ms.
Core tests also interrupt parsing mid-pass and discard candidates cancelled
after history preparation. Publication and workbench refresh still occur on the
GUI thread, allocation/native library calls are not interruptible, and source
selection, real-map throughput and complete shell adoption need further work.

## Acceptance Areas

| Area | Required outcome | Current evidence and gaps, 2026-10-06 |
| --- | --- | --- |
| Document lifecycle | Create maps, save repeatedly, save copies, survive failed opens/saves, detect outside edits, recover unsaved work, preserve undo and unrelated archive content. | New/Save, cancellable worker opens/saves, source-change and stale-result guards, backups, fingerprints, owned WAD snapshots and background recovery have core/CLI and actual-shell tests. Same-file reload geometry/navigation and cancelled-open recovery preservation are covered. Real project profiling and native cross-platform verification remain. |
| Brush authoring | Draw primitives; edit vertices, edges and faces; transform with texture lock; clip, hollow, carve, merge, and manage brush entities without silent geometry loss. | Primitives, transforms, clip, hollow, carve, exact convex merge and component authoring share undo/CLI/persistence. Move/quick turn/flip/resize now share exact affine texture lock, persisted GUI controls and CLI policy. Required Valve conversion remains explicit and atomic. Three generated q3map2-to-PK3 variants verified compiled UVs. Component editing and ownership-aware snap/duplicate/paste offsets now have GUI/CLI, UV and compiler/package coverage. Large-map performance and broader component/interaction acceptance still need work. |
| Curved surfaces | Create, reshape, tessellate, texture and persist Quake III patches with selectable control points and undo. | Plane/cylinder/cone creation, control-grid and XYZ/UV editing, subdivision, facing, checker preview, map undo and CLI persistence are implemented. Stitching joins boundaries with exact refinement, optional UV/tangent matching, one atomic undo and package/staging previews. The level camera resolves package materials. A synthetic BSP/VIS/LIGHT and dependency/PK3 workflow passed with the installed q3map2. Planar caps for closed loops/open arches now share atomic owner-aware insertion, package preview and CLI authoring. Non-planar covers, multi-seam propagation and broader compiler/game acceptance remain. |
| Doom geometry | Author sectors, lines, vertices and things; edit heights, flags, actions and tags; handle shared topology; retain map dialect and unrelated WAD content. | Binary Doom/Hexen editing includes shared-pivot rotation, connected X/Y mirroring, rounding/collapse checks and node invalidation. The camera reconstructs concave/holed/disconnected sectors and wall parts with exact material targets; invalid boundaries are reported. Draw/split/join/merge tools exist. UDMF has lossless properties and native move/rotate/mirror/snap/resize, common-field previews, scene locks, recovery and real ZDBSP/package validation. UDMF topology tools, attached partial mirror detach/stitch, broader topology tools and 3D manipulation remain open. |
| Materials | Browse project/package textures, select faces, paint, align/fit/rotate/scale UVs, edit brush-primitive matrices, and preview the result accurately. | The Surfaces tab adds bounded asynchronous quick adjustments and Q3Radiant texture keys; Surface Alignment adds batch Shift/Scale/Rotate/Fit/Align with original dimensions, one undo step and CLI parity. The camera resolves package images, shader editor images, static model skins, Doom flats and composite walls. Paint/sample uses exact face/patch/wall/floor/ceiling identities; four Radiant profiles add instant material sampling, Q3Radiant/GtkRadiant add material painting plus face/brush paste, NetRadiant adds face parameter paste, and NetRadiant Custom adds single-face seamless wrapping, hit-and-selection values/projection, patch UV projection and mapping-only variants. A material/mapping/flag clipboard, world projection, chained or batch seamless wrapping and portable CLI definitions share the surface worker and undo. Doom offsets/pegging, namespace/staged resolution and dependency input review are implemented. Engine effects, complete surface rendering, multi-package/project search and native gesture acceptance remain incomplete. |
| Entities and scene organization | Definition-driven creation and inspectors, connection graphs, models, groups/layers, visibility, reusable prefabs, and reliable search. | Definitions, links, query selection, generated props and static package MDL/MD2/MD3 preview exist. Versioned prefabs capture complete owners, preview package assets, namespace internal links and insert through atomic undo with CLI parity. The Scene tab provides persistent layers/nested groups, assignment, selection and visibility with structural-edit inheritance and CLI parity. Inherited editing locks protect native records through shared authoring services. Linked instances, model attachments/animation and broader placement workflows remain incomplete. |
| Navigation and interaction | Synchronized orthographic/perspective views, configurable layouts, dependable picking/manipulation, grid/snap, isolation, camera bookmarks and complete profile behavior. | Camera/top/front/side panes share map edits, selection, visibility, package-backed previews and compiler trails; layouts and splitter sizes persist independently of profile controls. Optional linked plan centres/zoom and camera-follow share GUI/CLI defaults. Named views retain exact poses and links with per-map storage, a manager and portable files. Nineteen profiles route controls and document adaptations, including QuArK four-view controls and modern held-button flight. Eighty-four gesture and navigation-key preferences share staged GUI editing, validated per-profile settings, portable files and CLI coverage, preserving camera poses, layout and undo. Temporary maximize/restore and equal sizing preserve shared authoring state. Broader manipulation and native acceptance remain incomplete. |
| Validation and production loop | Actionable map health, leak/portal inspection, source-linked compiler diagnostics, reproducible build/package/test with current assets and saved map data. | Quake/Quake II/Quake III prepared workspaces capture unsaved maps and package drafts, verify successful output inventories, and publish engine-specific BSP/lighting and assets through PAK/PK3 writers. Quake includes captured WAD2 textures; Quake II validates WAL/animation dependencies. Complete Quake-family installation deployment shares destination review, per-operation write permission, backup and optional windowed launch across GUI/CLI. Classic targets remember numbered PAK slots and enforce their engine's loading sequence; complete review hashes guard CLI automation. Doom layouts, custom outputs, runtime search precedence, real engine testing and diagnostic remapping after structural edits remain open. |
| Scale and responsiveness | Large-map loading, selection, editing, rendering, saving and asset indexing remain responsive, cancellable where practical, and measured. | Bounded brush-geometry reuse covers plan edits, worker opens and the camera worker. Scene indexes and cached selection bounds accelerate repeated plan readouts/framing while preserving sparse IDs, hidden ownership and edit/reload invalidation. Quake plan wires, patch borders and selected outlines now prepare on coalescing workers with retired-result rejection and separate GUI paint/completed-image measurements. Cold grid/member images now prepare on an independent coalescing worker for large scenes/selections, including Doom, with current primary/handles/picking and combined accessible pending state. Bounded immediate caches and primary-label layout remain shared. Synthetic 100–10,000-brush editing/rendering benchmarks and load phase, GUI heartbeat, adoption and cancellation measurements exist. Quick and explicit placement prepare on cancellable workers with guarded adoption; redundant face formatting is removed. Parser/transform throughput, full shell adoption, overlapping wireframe painting, complex projects and other synchronous paths still require improvement. Synthetic boxes do not prove production performance. |
| Accessibility and localization | Scalable, high-contrast, keyboard-accessible controls, screen-reader semantics, RTL/translation expansion and consistent status/undo feedback. | Shared theming and extraction exist. Real keyboard/screen-reader acceptance remains unverified; input automation requires specific user authorization. |
| Portability and robustness | Native Windows/macOS/Linux checks, malformed-input coverage, deterministic round trips, source protection and realistic end-to-end editor exercises. | Windows has passing results for 68 selected checks, including document lifecycle and patch core/UI coverage; focused reruns resolved initial capture-setup and model/ZIP test failures. The input-injecting shell interaction suite was excluded. Native macOS/Linux execution and broader authoring coverage are still missing. |

## Verification Rules

Keep the original target above active while individual improvements land. For
each area, test the actual workflow and its interaction with project assets,
packages, undo, saving, diagnostics and compilers. Record unsupported formats or
operations explicitly rather than silently losing content. Do not label this
plan complete until every acceptance area has direct, appropriately scoped
evidence. No game launch or mouse/keyboard injection is authorized by this plan.

## Lossless UDMF property authoring

Existing UDMF WAD maps load through the shared level document. A bounded parser
retains the original TEXTMAP bytes, comments, property spellings, duplicate keys,
unknown fields and extension blocks. The native projection provides vertices,
linedefs, sidedefs, sectors and thing mirrors, including fractional coordinates,
heights and offsets. The plan and textured camera use that same projection and
the current package asset snapshot. Rendering covers common Doom fields; slopes,
3D floors, portal effects and other namespace extensions are not reconstructed.

**Edit → UDMF Properties…** selects `global` or a `type:index` block. Select a
property row, stage a scalar value or removal, then Apply. Numbers, booleans,
keywords and quoted strings retain UDMF syntax. The table identifies pending
changes in text. Validation runs on a cancellable worker; publication refuses a
changed map, selection, scene destination or package context. A failed/cancelled
batch leaves source bytes, preview and history unchanged. One batch produces one
undo step. Undo restores the exact source and original node state. Existing
topology and binary property commands still refuse unsupported UDMF edits.
Standard move, rotate, mirror, snap and resize commands use the lossless writer
described below.

`map inspect-udmf` reports globals, block selectors, literal values and lines.
`map edit-udmf` shares the transaction, supports repeated `--set` / `--remove`,
`--dry-run`, explicit save-as and `--overwrite`:

```sh
vibestudio --cli map inspect-udmf ./maps/arena.wad --map-name MAP01 --json
vibestudio --cli map edit-udmf ./maps/arena.wad --map-name MAP01 --object vertex:0 --set x=16.75 --output ./maps/edited.wad --json
```

Missing required fields, non-finite/out-of-range native numbers, invalid new
references, ambiguous duplicate property edits, malformed scalars and conflicting
batch entries are refused. Native coordinates are bounded to ±10,000,000; integer
fields use signed 32-bit bounds. A particular game or compiler can impose tighter
limits. TEXTMAP is capped at 64 MiB, 250,000 blocks, one million properties and
1 MiB per scalar. Manual property batches are capped at 4,096 edits. Native
transforms use the document's one-million-property budget. Unknown numeric literals
retain their exact source even when a JSON double cannot represent them exactly.

Map records extend through the required ENDMAP, including arbitrary sidecars.
Saving replaces only TEXTMAP spans and the selected map's editor metadata/node
products. Other maps, assets and duplicate resource records keep their order and
bytes. Scene groups, visibility and locks work with the native projection;
VS_SCENE stays inside ENDMAP and binds to TEXTMAP. Unknown/global property edits
are conservatively refused when scene content is locked. Recovery carries the
same serialized text and persistent node-rebuild state.

Every property transaction conservatively invalidates nodes because extension
fields may affect a builder. Saving clears obsolete runtime products. UDMF
ZNODES uses the shared extended/compressed node validator; compiler output and
launch planning consume the same result. Empty BLOCKMAP warns that a source port
must generate collision data. A builder may rewrite TEXTMAP and discard unknown
block types: the editor warns about extension blocks and preserves the authoring
source separately. No complete source-port rendering or runtime compatibility is
claimed from structural node checks.

`level-udmf-smoke` covers parser failures, literal preservation, transactional
rollback, cancellation, scene locks, exact undo, recovery, multiple-map/duplicate
entry persistence and CLI/package round trips. `level-udmf-ui-smoke` exercises the
actual shell action, worker apply/cancel, validation feedback, textured preview,
undo and save, with 100% dark and 200% high-contrast RTL/expanded text renders.
`src/tests/level_udmf_compiler_workflow.py` proves 28 steps across Doom/ZDoom
namespaces with real ZDBSP extended and compressed nodes, package validation and
launch plans. It uses generated content and does not launch a game. Native UDMF
block creation/deletion, topology tools, advanced effect rendering,
large production-map performance and native assistive-technology acceptance
remain open; this is progress toward the full editor goal, not its completion.

### UDMF native transforms

Move, numeric/quarter-turn rotation, X/Y mirror, grid snap and resize now operate
on the common UDMF projection and replace only changed coordinate, heading and
linedef endpoint values in TEXTMAP. Fractional coordinates retain double precision;
unchanged number spellings, comments, custom fields, sides and sector properties
remain byte-for-byte intact. Lines and sectors expand to unique boundary vertices,
so overlapping selections transform a shared vertex once. Thing movement includes
fractional height. Geometry moves/resizes in XY; sector elevation stays in UDMF
Properties. Grid snap affects XY, including fractional grids through the shared
service/CLI; the GUI's grid selector continues to offer its existing integer steps.
Thing headings use whole degrees. Namespace-specific effects remain unchanged
and are not geometrically transformed or reconstructed by this common-field editor.

Mirrors reverse affected linedef endpoints and preserve sidedef ownership, offsets,
flags and actions. Attached partial geometry is refused; **Select Connected
Geometry** or CLI `map flip --connected` expands the vertex graph explicitly.
Scene locks protect affected native records, including indirectly shared boundaries.
Moving/turning ordinary things retains node validity. Geometry transforms mark nodes
stale; exact undo restores the original TEXTMAP and node products. Manual property
batches retain their conservative invalidation policy.
In ZDoom/Hexen/Vavoom namespaces, known polyobject controls (types 3000–3002 and
9300–9303) retain their angle as an identifier. Moving their XY position invalidates
nodes; changing only height does not. Full polyobject/effect preview remains
outside the common-field renderer.

Numeric and viewport move/resize, quick turns, and numeric rotation Apply share
the cancellable placement worker with mirror and snap. The rotation preview uses
the same service and shared Models viewport. Publication checks map revision,
selection, scene destination, source identity and package context. Cancellation
inside encoding, validation or history preparation discards the candidate.
An unchanged transform reports that nothing changed and adds no undo entry.

```sh
vibestudio --cli map move ./maps/arena.wad --map MAP01 --object sector:0 --delta .375,-.125,0 --output ./maps/moved.wad
vibestudio --cli map rotate ./maps/moved.wad --map MAP01 --object sector:0 --axis z --degrees 22.5 --pivot 0,0,0 --output ./maps/rotated.wad
vibestudio --cli map flip ./maps/rotated.wad --map MAP01 --object linedef:0 --connected --axis x --output ./maps/mirrored.wad
vibestudio --cli map snap ./maps/mirrored.wad --map MAP01 --object sector:0 --grid .125 --output ./maps/snapped.wad
```

`level-udmf-transform-smoke` checks independent affine/area oracles, exact source
undo, archive preservation, reference/collapse/range refusal, inherited locks,
ordinary-thing node validity and cancellation through final publication. A generated
3,004-vertex transform exercises 6,008 changes in one transaction; this is not a
production-map performance acceptance result. `level-udmf-transform-ui-smoke`
uses the actual shell controls at 100% and 200% high-contrast RTL with expanded
translations, without native input. The compiler workflow in
`src/tests/level_udmf_transform_compiler_workflow.py` chains every operation,
checks directed boundaries after ZDBSP's legitimate vertex reordering, validates
the resulting WAD as a package and prepares a hash-bound launch plan. No game
launch or full engine-rendering acceptance is implied.

## Per-instance MD3 Compiler Skins

Quake III `misc_model` `_skin`/`skin` and `_remap*` properties now drive the level
camera and dependency/export closure together. Copies of one model may retain
different surfaces and materials. Camera Details exposes exact skin inputs,
hashes and entity ownership; staged skins participate. Use the entity inspector
or `map edit` for normal undoable authoring. The pinned compiler ignores MD3
nonzero frame selection, so review diagnoses those requests and directs authors
to bake a static pose in Assemblies. Read [Placed Model Appearances](LEVEL_MODEL_APPEARANCE.md)
for filename conventions, remap priority, strict validation and scope limits.
