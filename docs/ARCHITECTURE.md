# VibeStudio Architecture

`core/audio_meter` owns fixed-capacity sample-clock meter processors and the
shared offline range-analysis service. Optional renderer hooks accumulate
combined pre/post strip taps and master taps without altering audio samples.
Peak decay is 24 dB/s, exponential RMS is 300 ms, and held maxima, over-range
counts and integrated energy/correlation persist until reset. Normalized energy
accumulation avoids overflow when internal bus amplitudes exceed float32 range.
Logical-frame windows include latency warmup and reject frames outside the
request; offline completion drains disconnected strips with longer insert
latency than the master. The transport retains history across loops and clears
it on start/seek/stop. Playback publishes fixed snapshots through a bounded
coalescing mailbox outside rendering, before audition volume/clipping.
`app/audio_meter_dialog` exposes native accessible rows; offline analysis uses
the session's cancellable worker and `asset audio-session meters` uses the same
service. Session v7 persistence, undo, recovery and delivery samples are unchanged.

`core/audio_media` owns source usage/file-availability inventory, bounded
decoded candidates with file identities, and transactional rename/relink/
replace/remove/prune. Relinking requires bit-identical decoded samples and
changes provenance only. Replacement assigns a new immutable source ID and
retargets every referencing region, retaining all other arrangement state;
explicit SRC uses the shared resampler. Final session validation enforces
channel/rate/source-range and aggregate limits. Embedded samples remain valid
independently of external file availability. The native v7 schema is unchanged.

`app/audio_media_dialog` performs cancellable inventory/review and prepares
before/after waveform caches off the GUI thread. Changed controls invalidate
the review. The session worker revalidates the input digest/canonical path,
prepares the new source cache and adopts one undo/recovery state. Save/export
guards include source paths from retained current/undo/redo states. The strict
`audio-session media` CLI protects original provenance even after removal or
relinking, and can verify a prior dry-run digest. Shared rendering carries new
samples into waveform/game/package/level delivery. Bulk relink discovery,
automatic embedded-source waveform round trips and external streaming remain
integration gaps. No new dependency or borrowed implementation is involved.

`core/audio_range` supplies transactional half-open clear/ripple-delete/insert/
repeat operations over an explicit track scope. Clip slices retain source and
fade windows; group links cannot broaden scope, and repeats get fresh links.
`core/audio_automation` retains original curve endpoints, span and offset through
cuts, with right-point precedence at splice discontinuities. Selected track,
bus/effect and optionally master lanes follow the same interval mapping; empty
lanes remain static. Native v7 stores these domains and reads v1–6 ordinary
points. GUI Range controls, timeline brackets, CLI, undo/recovery and every
rendered delivery share this state. Tempo/meter maps remain unchanged. Stateful
processor histories are recomputed normally, not copied across edits.

`core/box_resize` keeps double-precision selection bounds and axis/ray drag
math separate from Qt. `app/model_viewport_resize` adds opt-in face handles to
the shared camera; immutable raster work applies the proposed bounds to selected
triangles and translates point-owned visuals through their source origins.
The existing bounded worker coalesces intermediate pictures without changing
the map during a gesture. `app/level_camera_resize_actions` captures source
revision, load identity and selection, checks scene locks and commits through
the existing placement/resize transaction. Plan panes, undo, texture policy,
serialization, CLI and downstream builds retain the same services. Doom geometry
requires a topology-aware preview and is not offered by these camera handles;
Doom thing spacing uses XY. See [camera resizing](LEVEL_EDITOR.md#camera-selection-resizing).

`core/audio_arrangement` expands explicit clip selections through named group
links and validates transactional edits across tracks. Descriptor-only moves,
copies, splits, gain/fades/mute and group changes share GUI/CLI services and
immutable source snapshots. Split regions retain a window into their original
fade domain, evaluated identically by direct/routed rendering, playback, stems
and waveform handoff. Native v6, undo and recovery preserve links and windows;
readers migrate v1–5 with empty defaults. Automation remains sample-anchored.
`app/audio_arrangement_dialog` supplies staged native controls alongside tree
multiselection and linked timeline movement. No new dependency or device path.

`core/asset_formats` owns suffix/capability descriptors for media and native
studio documents. Asset analysis and project file classification use the same
lookup, texture import dialogs derive their filters from it, and `asset formats`
and `asset route` expose the catalog. Content parsers and package namespace
metadata remain authoritative. `core/extra_image` and the bounded PakFu-derived
DDS decoder feed the existing image/texture services; DDS/FTX export reuses
texture validation and staging. ZIP-family aliases share the ZIP reader.

`core/workspace_document` owns bounded, versioned `.vibeworkspace` JSON and
revision-checked atomic persistence. `app/workspace_actions` captures/restores
shared project, package, map, code and asset selection context. `cli/workspace`
uses the same codec and reports missing references. Native authoring, package
staging, prepared builds and recovery retain their existing state ownership;
workspaces contain references, not competing document copies or commands.
See [Workspaces](WORKSPACES.md) and [Asset Formats](ASSET_FORMATS.md).


Model collision uses bounded static or per-frame oriented boxes in `ModelMesh`,
source schema 5/6 for static volumes and 7 for tracks, and normal `ModelDocument` mutation, fingerprint, history and recovery
services. `model_collision` shares rotation and convex-brush construction with
the level editor. Its map handoff uses cancellable placement and one undo command;
GUI publication checks the captured level context. A completed camera snapshot
also owns the collision overlay. Native formats and assembly baking report
omitted collision. Tracks have exactly one pose per mesh frame; scalar fields
mirror frame zero in memory. Copying, duplication, deletion and in-between frame
generation retain that invariant. Shared tag quaternion interpolation samples
box orientations. Map handoff requires a stored frame for animated sources and
creates static brushes. See [Model Collision](MODEL_COLLISION.md).

`core/level_build_workspace` joins level serialization, explicit dependency
inspection, the archive-reader/staging abstraction, portable extraction planning
and compiler pipelines. A private sibling directory receives streamed independent
copies and `build-inputs.json`; publication is one directory rename. A bounded
reader validates the inventory and verification hashes inputs before and after
compiler execution. `core/level_quake_assets` resolves bounded WAD2/miptex
bytes from the package and assembles a deterministic compiler WAD. Quake/Quake II
use explicit ericw search/log paths, with target-aware WAL dependency validation
and runtime lighting classification. Quake III base/home flags are injected by the shared service
for every stage; caller path overrides are refused. `app/level_build_workspace_dialog`
prepares on a worker with cooperative cancellation and phase/count progress.
The shell guards document/package freshness before pinning Build to the snapshot;
`cli/level_build_workspace` calls the same core. `core/level_build_artifacts`
owns successful output records, bounded input/output verification, workspace
build/publication locks, and previous-output retention outside compiler search
paths. Its PAK/PK3 publisher selects captured assets and runtime outputs through
`PackageStagingModel`, then reuses deterministic writing, per-chunk identity
checks and atomic backup publication. `app/level_build_package_dialog` provides
asynchronous paged review and a receipt-hash freshness guard; the modular
`cli/level_build_artifacts` exposes inspection and publication. Normal package
opening remains available for general inspection. This adds no dependency or
alternative compiler runner. See [prepared builds](LEVEL_EDITOR.md#prepared-builds-with-current-assets)
for isolation, supported outputs and remaining engine/diagnostic boundaries.
`core/level_build_deployment` combines the verified output inventory, installation
profile, safe path checks, expected destination hash, shared PK3 publisher and
game launch planner. `PackagePublicationOptions::expectedDestinationSha256`
checks reviewed content/absence under the destination save lock. The same
package dialog has a deployment mode; `cli/level_build_deployment` exposes
read-only plans and explicit deploy/launch operations. Installation permissions
are not mutated. Worker results retain publication state if later launch fails.

`core/level_placement` prepares snap, duplicate and paste on private document
snapshots, returning a candidate only after validation and history preparation
finish. It reuses `level_map`'s native transform command without applying that
intermediate command. Duplicate/paste build one insertion command from validated
records; snapping supplies unique native offsets to the same transform service.
Scoped thread-local controls carry cancellation through parsing, object/face
loops, geometry solving, insertion and finalization. Independent workers and
nested preparations restore their own control; cancellation never escapes the
public transaction boundary or publishes partial history. Source-line validation
checks the original binding without formatting and discarding a rewritten face.
`level_texture_mapping` classifies brush dialects for both paste and prefabs.
`app/level_placement_dialog` prepares immutable candidates, resolves package-draft
assets and builds previews off the GUI thread, with phase/count progress.
`app/level_placement_task_dialog` also runs quick Snap, Duplicate and Paste on a
worker; it delays showing progress for 150 ms to avoid flashing on fast edits.
Cancel closes immediately while the worker discards its private snapshot.
The shell checks document,
selection, active scene, save and package state before adopting the candidate.
CLI uses the same placement preparation service before its existing guarded
save/dry-run path; no editor-only geometry or UV path exists.

`core/level_scene` owns layers, nested groups, membership validation, inherited
visibility and a bounded source-bound metadata codec. The map document and its
undo commands carry scene snapshots, object origins and Doom ID remaps. Text
serialization records emission order; Doom writes map-local `VS_SCENE` records.
`app/level_scene_panel` and `cli/level_scene` use these services. Plan, camera and
SVG display paths honor visibility while compiler, dependency and package inputs
retain all content. Package WAD grouping includes the scene sidecar. See
[Level scene organization](LEVEL_SCENE.md) for boundaries and remaining gates.

`core/level_scene_locks` applies inherited protection at map-authoring service
boundaries. A scoped copy-on-write candidate is validated before publication;
protected records include entity ownership and Doom geometric dependencies.
Topology commands supply side/vertex/line/sector identity remaps independently of
undo retention. A failed edit cannot change history, selection or the live map.
Scene changes have a separate hierarchy/membership guard; Undo/Redo replay history.
Model, audio, texture and prefab handoffs use these same authoring services and
retain their package/map transaction boundaries. New map mutators must enter this
guard; the common undo recorder asserts that invariant in debug builds.

`core/doom_preview_geometry` reconstructs sector interiors and wall parts without
nodes, and supplies exact floor/ceiling/sidedef targets to the shared map mesh.
`core/doom_preview_materials` composes classic Doom textures through the normal
bounded package reader. Material records retain exact namespace and directory
occurrences, including staged replacements, for Levels' Details and dependency
review. Both services run on the existing preview worker and feed the Models
renderer. Geometry diagnostics are visible in Details and `map materials
--geometry`; dependency subsets remain disabled for Doom until texture-table
and namespace closure is supported.

`core/level_material_paint` defines exact face/patch/wall/flat targets and a
source-validated batch plan. Preparation and commit use the map module's existing
texture rewriting and undo; the renderer never mutates documents. Preview meshes
carry material provenance beside selection owners. The shared Models viewport
has an opt-in paint/sample lifecycle; `app/level_material_paint_actions` collects
one command per stroke, connects package material context and supplies native
target controls. `cli/level_material_paint` shares these operations and atomic
map saving. See [Material Painting](LEVEL_EDITOR.md#material-painting).

`LevelSurfaceStroke` in `core/level_surface_clipboard` stages ordered transfers
against a private document, retaining original source bindings and one final
net-change plan. Preview steps discard their temporary histories; final commit
uses the normal surface validator and history/save-point rules; a no-op retains
redo and the save point. Values/Project include selection only on the first hit, while
seamless wrapping advances the private clipboard and its package-size role.
`app/level_surface_stroke_actions` queues bounded hits through `LevelSurfaceWorker`
and coalesces separate `LevelPreviewWorker` snapshots. Frozen mouse-down picking
survives material regrouping. Cancellation and source-context checks gate both
document and clipboard adoption. The shared material status uses a fixed-height
readout so translated progress cannot resize the camera during a gesture.
`map paste-surface --stroke` replays the same core transaction; normal save,
recovery, dependency, compiler and package workflows consume only its committed
result. Patch-source wrapping and production-map preview throughput remain gaps.

`core/level_navigation` defines validated plan/camera state and a versioned,
bounded saved-view codec. GUI and `cli/level_bookmarks` use its canonical-path
identity, per-WAD-map isolation, revision checks, lock and atomic writer.
`MapViewport` and the shared `ModelViewport` expose navigation capture/restore;
orbit state stores a world target rather than screen pixels. The Levels shell
keeps navigation metadata outside map edits, undo, compiler inputs and package
staging. Its manager stages list changes, rejects a changed document/list, and
retains failed drafts; pending camera restores apply after the current preview
worker publishes its mesh. See [Saved Level Views](LEVEL_EDITOR.md#saved-level-views).
The same core projects a shared world centre into plan axes and links scale.
`levels/viewLinks` stores independent centre/zoom/camera-follow defaults shared
with `editor view-links`. Guarded shell propagation preserves focus and tool
state, skips asynchronous mesh framing, and restores bookmark poses without
feedback. Version-2 bookmarks include link choices; version-1 reads default
them off. No navigation state becomes map, model or package content.

`app/level_view_actions.cpp` arranges Levels' camera and three orthographic
panes. One active plan supplies editing context; every pane shares the map,
selection, hidden set and compiler leak trail. `MapViewport::synchronizeSceneFrom`
reuses implicitly shared solved geometry and link arrays while retaining local
projection/pan/zoom. A scene generation separates geometry changes from cheap
selection synchronization. The Models camera continues to use the common
package/staging preview worker. `levels/viewLayout` is a validated setting shared
by GUI and `editor layout`; splitter states are scoped per layout and do not
become game-map or package data. See [Four-View Workspace](LEVEL_EDITOR.md#four-view-workspace).
Editor-profile binding checks cache the normalized built-in command identifiers;
localized shortcut and palette descriptions remain uncached so changing language
does not leave stale labels. This avoids rebuilding every translated command for
each profile binding during startup.

`core/map_geometry_cache` retains solved brush polygons behind exact plane,
material-name and precision keys. A plan edit recomputes changed brushes; sibling
panes share value snapshots so switching the editing pane retains this benefit.
Keys are assembled into one pre-sized, bounded byte array; they still compare
all values rather than relying on container identity. Read-only viewport passes
use const container access to preserve shared document storage. Scene visibility
returns immediately for an empty hidden set and expands hidden entity ownership
using numeric IDs, preserving exact selector matching and inherited visibility.

`levelMapInspectionSummary` computes normalized material names and statistics
for one document snapshot. The workbench passes that value to the overview,
Details and inspector, and shares one `levelMapTextureUsage` result with paint
controls and texture tiles. Standalone/selection-only refresh paths compute
their own current values; no persistent summary cache or new invalidation key
is introduced. The common Details drawer avoids replacing an unchanged text
document, retaining its cursor selection and scroll position across context
updates. The Levels shell preserves the section the user chose.
Inspector and camera highlight/resize traversals use const container access,
preserving the implicitly shared document arrays held by the Objects model and
preview snapshots until an actual edit requires a new value.

Hidden/deleted brushes are pruned, and closing or replacing the plan document
clears its cache. Each cache bounds estimated payload to 64 MiB and 32,768
entries; invalid or diagnostic-bearing geometry is recomputed. Cache limits
never drop geometry. These are payload estimates, not process memory limits.
The camera preview worker owns a separate snapshot, solves on demand within the
existing cancellation/triangle budget, and publishes its cache only with a
current successful result. Live UVs, package dimensions, paint provenance and
placed Models geometry are assembled from the current request. Documents,
undo, serialization, compilers and one-shot CLI rendering continue to use the
same solver without depending on cached state. See
[Geometry Reuse and Scale Measurements](LEVEL_EDITOR.md#geometry-reuse-and-scale-measurements).

`app/map_plan_wires` projects those polygons into bounded, immutable wire batches.
Exact edge deduplication is scoped to consecutive pen styles, preserving world,
entity and diagnostic order. `MapViewport` shares matching projection snapshots,
retires them on scene/projection changes, and keys a bounded brush image by
navigation, viewport size, physical scale and palette. It composites batches
through `paintModelWireframe`, the same CPU edge coverage used by model previews;
grid, labels and interaction overlays are drawn separately. Budget failures use
the complete ordinary draw path. Picking and authoring always use the original
objects. The cache does not enter project, package, CLI or compiler state.
Selected brush and patch outlines have a separate bounded wire/image layer.
Direct references resolve through the scene index; selected owners expand in
one scene pass, deduplicating explicitly selected children. Both the cached and
fallback path visit the same visible geometry, while authoring bounds still use
the full source. Invalid brushes contribute only their perimeter to the selection
layer, preserving the underlying warning cross. Patch borders share the same
projection/tessellation helper for ordinary and selected drawing. Scene,
selection and projection changes retire selected drawing data; same-projection
sibling panes can share it without merging source objects.

`app/map_plan_renderer` draws immutable brush/patch and selection snapshots into
bounded QImages, with complete ordinary painting when wire preparation exceeds
its cache budget. `app/map_plan_render_worker` has one active Qt thread and one
replaceable pending request per plan pane. Navigation cancels obsolete raster
work while preserving useful projection preparation; source/projection and
selection revisions cancel their respective geometry. Only complete images are
published, and the GUI verifies scene, projection, selection and view identity.
Old navigation images follow the current world transform until replacement;
source/visibility changes clear them and selection changes clear old highlights.
Qt implicitly shared containers keep snapshots independent of authoring. Closing
a pane cancels and joins its worker before widget state is destroyed. Large-scene
preparation and patch-border drawing run on that worker; small scenes, oversized
physical image targets and allocation-failure fallback remain synchronous.
The translated updating status is shared by the HUD and accessible description.
Picking continues to query current full geometry, independent of rendered images.

`app/map_grid` retains a native Qt grid image keyed by exact navigation scalars,
pane size, physical scale, grid units and palette. Its 8,388,608-pixel / 32 MiB
ceiling has a complete line-drawing fallback. `app/map_viewport_projection`
shares pure object-position semantics between live interaction and immutable
overlay snapshots. `app/map_viewport_overlay_worker` independently prepares
grid and member images for larger Quake scenes or selections above 64 objects,
including Doom. Each pane has one active request, one replaceable pending
request and at most one queued start callback. Cancellation is polled during
grid batches, member lookup and native ellipse drawing. Only complete images
with the requested scene/selection/projection/view identity are adopted.
Navigation reprojects compatible old images; selection and visibility changes
immediately retire old member highlights. Grid units, palette and display scale
cannot reuse incompatible pixels. The HUD and accessible description aggregate
both workers' pending states; primary markers, handles and picking remain live.
Each grid/member image has the same 32 MiB limit; previous and replacement
images can coexist. Unsupported physical targets/transforms and failed image
allocations retain the complete ordinary path. Closing cancels and joins both
workers. No rendering data enters authoring, package, compiler or CLI state.

`app/viewport_image` shares physical target sizing and fractional device-origin
calculation across plan geometry, selection, grid and member images. Child panes
at nonintegral physical offsets render with that exact phase and compensate it
when presenting the image. Leading/trailing coverage pixels count against the
unchanged image budget. A changed phase refreshes the view cache; whole-physical-
pixel pane moves reuse it. Axis-aligned positive uniform scaling is supported;
rotation, shear and nonuniform transforms use the complete ordinary painter path.
This keeps cached strokes aligned with current primary markers and edit handles
inside fractional-scale split layouts without altering world coordinates.
The shared Levels/Models CPU wire renderer anchors selection dashes at the
leftmost (then topmost) endpoint independently of its scan axis, so rounding near
a diagonal cannot flip the dash pattern when an image origin changes.

For immediate/fallback rendering, `app/viewport_ring_cache` reuses Qt-rasterized
member rings at exact physical subpixel phases. Each pane admits
at most 512 entries and 8 MiB of charged payload; a temporary atlas of at most
8 MiB shares painter setup for misses. Unsupported transforms and allocation or
budget failures draw ordinary rings. Neither cache changes object positions,
the distinct-visible-marker limit, selection identities or picking. Closing a
document clears both caches; hiding the grid releases its image. These are
per-cache limits, not total process memory limits.
The primary label uses `viewport_hud` layout to remain inside the pane, near its
marker and clear of the status tags, with a contrasting theme-matched background.
Direction-aware elision retains the full
identity in the existing accessible description. Optional, disabled-by-default
paint statistics separate grid, geometry, markers, handles and HUD costs for the
selection benchmark; they do not change user settings or CLI authoring behavior.

`core/level_patch_cap` constructs planar radial caps that retain the exact
source boundary curve, with an analytic polar-order check for fold prevention.
`level_map::addLevelMapPatches` validates and round-trips all new definitions
before inserting them into an explicit owner in one undo command. Single-patch
creation delegates to it. The cancellable cap dialog and `map cap-patch` share
the same service; package/staging material snapshots and Models rendering keep
preview, dependency review and subsequent package/compiler operations connected.

`core/level_patch_stitch` performs exact common-grid refinement, boundary pairing,
position/UV joining and optional cross-edge tangent matching on immutable patch
values. `level_map::replaceLevelMapPatches` validates every replacement before
one grouped undo command; the single-patch editor delegates to the same service.
The seam dialog coalesces cancellable workers, resolves package/staging material
snapshots and renders through Models. The GUI and `map stitch-patches` use the
same document operation. Saved dialect, entity ownership and unrelated source
text remain document-owned. See [Patch Stitching](LEVEL_EDITOR.md#patch-stitching).

Package editing documents use `core/package_draft`: a `.vibepackage` directory
contains atomically committed metadata and independently copied SHA-256 payload
objects. Source provenance remains separate from payload backing.
`PackageStagingModel` records bounded insertion/removal deltas, grouped edits,
redo branches, stable operation IDs, and a saved revision. Draft persistence
includes every reachable history payload and validates replay before adoption.
Serial admission prevents operation IDs and committed revisions exceeding the
largest valid persisted value (`2^64 - 2`). Appends and unstage check before
mutation; nested groups commit one admitted revision. Empty groups remain usable
for read-only/no-edit CLI flows. Undo/Redo reuse existing serials, and cancelled
groups do not recycle allocated operation IDs. The loader shares the same bound.
`core/package_zip` contains bounded ZIP/PK3 end-record, extra-field, filename,
local-header and descriptor parsing. It reads through the immutable content
device, retains no payload, and shares the archive indexing budget. Global
structural failures refuse opening; local failures retain unavailable entries
and their exact source ordinals. UTF-8, Unicode Path and CP437 names feed the
same normalized paths used by asset lookup, staging and CLI operations.
`PackageArchive::loadSnapshot` adapts a value reader for existing consumers
without changing entry indexes or repeated WAD occurrence identity. GUI draft
workers and CLI draft operations share these services. The shell keeps an open
draft's history across archive exports and watches its metadata rather than the
original source. `packagePlannedArchive` supplies one inspection adapter for the
GUI and CLI, preserving readable effective entries, unreadable base rows and
conflict diagnostics, and synthesizing browser folders. Export consumers retain
blocking validation. The shell caches this value by document revision and reload
generation; list rows retain their reader index for exact occurrence previews.
Workspace search, Quick Open and asset property filters query the same planned
metadata used by the package and asset browsers.
`PackageStageOperation::sourceOrdinal` selects an original occurrence, with the
recorded current path checked during replay. The live path index retains every
slot, so rename/delete do not hide remaining duplicates. Replacements retain
source identity and WAD type hints while clearing original payload metadata.
Draft version 2 introduced selectors; versions 1–4 remain accepted. Version 3
adds an explicit unavailable-reason alternative to a base object hash. Known
unreadable source metadata enters the plan with this reason immediately; it stays
visible once, can be explicitly replaced/deleted, and blocks export while current.
For a source initially marked readable, a payload failure can first become an
unavailable historical record only when that original is absent from a successfully
prepared current plan. Existing object corruption, imported content, cancellation,
storage errors and source-identity changes remain strict failures. The source
occurrence and deltas remain intact; plan replay adds a nonblocking historical
warning and blocks export if Undo exposes the missing bytes. Read adapters mark
that row unreadable. Recovery metadata/status exposes the count before restore.
All new drafts write version 4 with a required protected-input array. Diagnostic
text participates in document and plan budgets; no source path is reopened for
an unavailable record.
`PackageStagingModel::createEmpty` creates a dirty, source-free document. Shared
`core/package_directory` operations preflight a complete subtree before applying
one history entry; a metadata fingerprint detects changed descendants on replay.
Explicit directories participate in the live path index so file edits cannot
occupy their paths. Draft base records retain generated operation origins for
stable subtree identity after rebasing. ZIP/PK3 retain empty directories; writers
refuse formats that would silently drop them. `PackageDraft::save` has a no-write
mode that hashes payloads and checks the same metadata and destination constraints
without publishing objects, acquiring a file lock or marking the document saved.
`PackageFileIdentity` can retain a shared `PackageContentStorage` owner. Accepted
file imports and replacements stream/verify an independent temporary copy; models,
undo deltas and worker readers share its lifetime. The owner holds no QObject or
open payload handle. It retains a plain `QLockFile` session lease; its last release
queues guarded single-file deletion and reservation release on Qt's worker pool.
No hard links or original-file writes occur. Draft adoption
substitutes durable objects while earlier readers retain their working copies.
`PackageFileImportMode::VerifyOnly` is reserved for short-lived CLI inspection and
dry-run plans, avoiding hidden temporary writes. Existing archive/folder readers
keep their original source-change validation.
`core/package_import_store` manages temporary UUID sessions with checksummed
constant-size reservation counters. Store locking serializes admission across
processes; reserve-before-create and delete-before-decrement keep interrupted
writes conservatively accounted. Document/history/readers and queued cleanup
retain the session lease. Settings enter through `PackageReadControl::importOptions`,
so core readers and no-write CLI plans remain independent of preferences. Bounded
read-only review and fingerprint-checked explicit discard are shared by
`app/package_import_dialog` and the working-import CLI commands.
`core/package_import_locks` bounds inspection to 64 KiB per recognized
store/session lock or stale-removal guard. Explicit release binds path,
native identity, timestamps and content to a SHA-256 review and excludes
active owners through Windows sharing or macOS/Linux `flock`. Dry runs
check the same exclusion without deleting or creating files; unsupported
exclusion fails. A dedicated cleanup pool retains session ownership through
payload and final-session deletion. GUI/CLI main-scope guards join outstanding
global worker work and drain package cleanup after document teardown, before
destroying Qt. The first cleanup failure marks its session for review; queued
files retain reservations without repeating the lock timeout. Abnormal
termination still requires reviewed recovery. Package limit readers use
`StudioSettings::AccessMode::ReadOnly`, which preserves missing/legacy stores
without automatic schema creation or migration. Normal editable settings
continue to initialize and migrate as before.
Discard proves
the session is idle before deleting recognized files, without recursion; unknown
paths, links and changed storage fail. Normal last-reference cleanup reclaims
empty sessions; failed cleanup leaves reviewable storage. Limits cover logical
payloads, with separate metadata/scan bounds. Worst-case performance and
external-writer/power-loss/network boundaries remain open.
`core/package_draft_access` owns one native directory lease per saved/loaded
snapshot, shared by its manifest and payload identities. These value-only owners
hold no QObject or open payload file. Windows readers allow all sharing while a
maintenance handle denies read sharing; this preserves atomic child-file renames.
Unix readers use shared `flock` and maintenance requires exclusive `flock`.
Native identity checks reject replaced roots. Reads create no lock file; crashes
release the handle. Unsupported exclusion refuses destructive maintenance.
Windows exclusion prevents root enumeration, so maintenance takes the writer lock,
scans, acquires exclusion and rechecks native identity/directory statistics before
checking each candidate. Advisory locks do not exclude arbitrary external writers.
`core/package_draft_storage` verifies complete history/payloads and reviews unused
objects with bounded storage fingerprints. GUI and CLI compaction use the same
closed-reader requirement and report partial cleanup. Recovery compaction also
uses these reader leases. An active older checkpoint reader defers optional
compaction; new checkpoints still commit within quota, counting retained unused
bytes. Retirement continues to refuse active readers. Content opening and final
verification check the leased root identity, rejecting replaced directories even
when payload metadata is identical. `PackageDraft::save` preflights without writes, projects
deduplicated payloads, counts existing garbage and peak metadata space, then
rechecks under its writer lock. `PackageReadControl::draftLimits` supplies GUI/CLI
preferences; default core limits are 32 GiB/200,000 files. Unchanged metadata needs
no additional slot. No version 1/2 schema change is required. Extra verification
I/O and native/network acceptance remain release gates.

`core/package_recovery` embeds an optional recovery envelope in the same atomic
draft manifest. `PackageRecoverySession` holds a document-lifetime lease; writes,
restore and compaction share a per-draft write lock. Inventory bounds metadata;
restore fully verifies history/objects and copies to an independent new draft.
`app/package_recovery_writer` owns one worker and one coalescing snapshot, and
serializes retirement after cancellation. The chooser, shell lifecycle and CLI
use these services; checkpointing never changes the live saved revision.
Post-commit reclamation removes only unreachable owned objects, with a bounded
nonrecursive preflight refusing links and unrelated files. `core/package_storage`
provides bounded file statistics and a storage review fingerprint for incomplete
copies. A store-level lock serializes quota scans, writes and cleanup. Recovery
bounds logical data bytes and copy count; the draft object writer checks transient
object/manifest headroom and can hash-only reuse an existing object. Limit failure
preserves the last manifest. No automatic eviction occurs. Ordinary drafts use
the separate limits and reader-safe maintenance described above. Occurrence-based
archive subsets use the shared selection/group service described below; see
[Package Manager](PACKAGE_MANAGER.md).

`core/level_texture_mapping.*` evaluates classic, Valve 220 and brush-primitive
UV projections and solves exact affine texture lock for translation, rotation,
reflection and nonuniform scaling. `level_map` stages
geometry, mapping, entity orientation and Doom vertex snapshots into one atomic
undo command. The GUI and map move/rotate/flip/resize CLI share that service.
StudioSettings stores separate rigid-transform and resize lock preferences,
plus explicit permission for required Valve conversion. Numeric
rotation previews immutable snapshots on a worker, discards superseded results,
and uses the model viewport for geometry. Classic-to-Valve conversion, when
explicitly enabled and required, covers the entire map to satisfy compiler
dialect rules; unselected geometry and asset paths remain unchanged. Material
image resolution in this preview is still an integration gap.

`core/level_patch.*` owns validated patch presets, control-point movement,
Bézier subdivision and canonical patch definitions. `level_map` commits patch
creation/replacement through its existing undo and document lifecycle. The
Qt Widgets patch editor keeps a local draft and applies one document command;
the CLI calls the same services. `map_geometry` interpolates geometry and UVs
for the shared map/model preview. Material names flow through the normal
dependency and package services; the editor's checker preview is synthetic.
`map_assets` supplies the shared mapping from Quake III map material tokens to
package paths, so texture audits and dependency export resolve the same names.
`app/level_texture_audit_panel.*` runs the Health check on immutable map and
planned-package snapshots, with one active worker and one replaceable pending
request. Cancellation reaches reference/index walks, shader token scans and
streamed reads. Generation guards discard stale completion, including a Cancel
received after computation but before queued delivery; destruction joins the
owned worker before releasing its snapshots. `package_context_actions.cpp`
connects package revisions to Health and the workspace package section. It
updates only the corresponding health rows and preserves the selected workspace
section. Refused planned views expose their reason, never original metadata.
The shared core distinguishes source-index admission from complete requested
texture auditing, and CLI saved-draft reads use the normal package loader.

`core/level_document.*` owns map creation, atomic saves, backups, source
fingerprints and versioned recovery records. `LevelMapDocument` owns its loaded
WAD archive bytes, preserving unrelated maps and duplicate resources without
rereading a changed source during Save As. GUI and CLI share this service; the
GUI saves and checkpoints immutable snapshots on workers. Document identity is
updated only after a successful commit, keeping the watcher, compiler input,
recent files and undo state aligned. See [Level Editor](LEVEL_EDITOR.md).

`app/level_load_dialog.*` runs the shared map loader on a joined `QThread` with
phase progress and cooperative cancellation. Core callbacks poll reads, parsing,
brush solving, preflight and hashing; they never access widgets. A separate
bounded `MapBrushGeometryCache` moves into the plan's initial scene. The shell
adopts only a successful, current result, suspends watcher notifications while
opening, and retains document/history/recovery on failure. Same-source reloads
invalidate the viewport key while retaining navigation. WAD name routing reads
directory metadata with bounded lookahead; the loaded document supplies marker
choices. Expanded payload size is checked before lump copies. See
[Background Map Opening](LEVEL_EDITOR.md#background-map-opening) for limits.

Raster authoring uses `core/texture_document.*` for bounded layers, compositing,
clipping selections, stroke transactions, structural/pixel undo, operation
recipes, palette remapping, and atomic PNG output. `core/texture_paint.*` owns
deterministic square/round stamp spans and line/rectangle/ellipse masks. Blend
over reads immutable gesture-start pixels, avoiding opacity accumulation where
stamps overlap. The document owns seed-relative RGBA tolerance fill and exact
cyclic offsets; GUI and recipes share selection, wrapping and cancellation rules.
Stroke previews cache the composite and invalidate bounded image regions, with
wrapped seams split on each axis. Region rendering uses the same layer compositor
as full rendering and is compared pixel-for-pixel in tests. Long document jobs
and large undo/redo transitions prepare the full composite on the worker before
publishing it to the UI. Shared image decoding budgets each surface and the sum
of mip/frame pixels before allocation; editor imports lower the per-image limits.
`app/texture_preview_worker.*` owns background browser reads, palette resolution,
native decoding and thumbnail scaling from immutable package snapshots. Each
worker keeps one active and one replaceable pending request; request serials and
source revisions prevent old selections or staged palettes from publishing.
Full decoded results feed Edit Selected directly, while thumbnails release
their original surfaces after scaling. `app/texture_thumbnail_cache.*` bounds
source pixmaps by bytes and entry count; view references are reset on eviction
and source changes. Viewport demand replaces eager package-wide decoding;
dimension/format queries retain offscreen metadata without thumbnail pixels.
Explicit editor palette refresh shares the value-only resolver through its
cancellable document worker, rejecting changed source revisions on completion.
Native decoding checks between rows/blocks; SP2 reads share an aggregate byte
budget. Opaque Qt codecs and one bounded archive read finish before cancellation.
`core/texture_transform.*` implements exact indexed/RGBA quarter turns, nearest
resampling, canvas placement, and selection replacement with bounded progress.
The document validates placement and aggregate layer limits before applying
transforms. Selection-only transform history retains the content revision;
canvas sizing includes every layer while selected transforms require an editable
active layer. GUI anchor controls and recipe IDs share the same coordinate rules.
`core/texture_output.*` captures export destination fingerprints before encoding,
checks them before publication, atomically replaces approved existing files, and
publishes new files without overwriting a competing writer. All texture profiles use this same
bounded service from the GUI and CLI; dry runs never create output or temporary files.
Its PNG sink bounds encoded bytes and checks cancellation between codec writes.
`app/texture_png_export_dialog.*` uses the service for browser snapshots, with
cancellable preparation and a separate non-cancellable publication worker. It
supports the decoder's image limit without creating or replacing an editor document.
`core/texture_export.*` owns versioned profile settings, explicit alpha handling,
palette/index rules, native metadata, mip generation and nine encoders. Its
results include actual output previews, provenance, warnings and changed-pixel
counts. `core/texture_handoff.*` validates compiler map tokens against the
profile and package destination, then prepares package staging and map edits
on copies before committing either. Updating an already applied texture keeps
map history unchanged. WAD2 miptextures, Quake II WAL and Quake III PNG/TGA have
explicit placement rules; file/package publication remains separate.
`app/texture_export_panel.*` presents profile-specific controls and mip
previews; pixel/palette/settings changes invalidate them. The dialog separates
cancellable encoding from short guarded publication, so committed writes cannot
  be reported as cancelled. CLI `texture profiles`, `texture validate` and `texture
export` call the same services. The indexed PNG writer preserves color type 3
even for grayscale tables; it uses Qt's existing compression and the core CRC.
`core/texture_project.*`
owns the bounded, checksummed `.vtexture` format, palette/export metadata,
verified previous-version backups, and conflict-aware atomic project saves.
Its process-local prepared-save record captures the destination before
cancellable encoding. Publication verifies that same target before backups
and atomic replacement; the synchronous CLI wrapper uses the identical phases.
Project PNG layers share the bounded output sink, and file reads/checksums
check cancellation in 64 KiB chunks.
Per-layer indexed tables also survive Qt's grayscale PNG optimization, with
validation before document replacement and compatibility for earlier payloads.
`app/texture_canvas.*` owns canvas input and rendering;
`app/texture_editor_dialog.*` runs expensive operations on worker snapshots.
The shell hands the selected encoded profile to `PackageStagingModel` and PNG/TGA texture paths
to undoable map operations. Textures reads `PackageStagingArchive` for staged
images and thumbnails. Native WAD operations record explicit lump types, namespace
requirements and insertion anchors. Marker creation and texture insertion form
one undo group; draft persistence retains these fields, and missing or changed
markers block publication. The archive reader and staged view share namespace
classification. `texture stage` saves the same plan as a portable package draft.
Editor handoffs compare package revisions and map selection identities before
applying worker results. Palette resolution uses the staged archive; cache
invalidation covers palette changes, and explicit editor refresh keeps authored
pixel colors intact. CLI `texture create`, `texture edit`, and `texture inspect`
use the same core. `core/texture_recovery.*` owns bounded `.vtrecovery` envelopes,
inspection and restoration; `app/texture_recovery.*` serializes background writes
with one coalescing pending snapshot and safe retirement of in-flight drafts.
CLI `texture recoveries` and `texture recover` share the core. Restored drafts
require a new project destination and never adopt recorded source paths.
The dialog resumes deferred Save/New/Open/Close actions, including shell close,
only after a successful project save. Native game output and remaining integration gaps are documented in
[Texture Editor](TEXTURE_EDITOR.md).

VibeStudio is organized as a studio shell with shared project state underneath
specialized work surfaces. The goal is one environment where editing, package
management, game detection, asset preview, compilation, and diagnostics all
understand the same project graph.

Every change must consider how the studio modules work together, as required by
`AGENTS.md`: shared context and asset paths, core services, validation, undo and
staging, diagnostics, and CLI coverage connect authoring to shipping. Integration
gaps must be stated rather than hidden behind isolated work surfaces.

Implementation choices should follow the stack decision record in
[`docs/STACK.md`](STACK.md). Architecture documents describe boundaries and data
flow; the stack document describes preferred libraries, rendering progression,
CLI infrastructure, AI integration, accessibility, localization, persistence,
and packaging strategy. Efficiency goals are documented in
[`docs/EFFICIENCY.md`](EFFICIENCY.md), and accessibility/localization goals are
documented in
[`docs/ACCESSIBILITY_LOCALIZATION.md`](ACCESSIBILITY_LOCALIZATION.md).

## Layers

### Studio Shell
- Qt Widgets application frame.
- Mode rail for workspace, levels, models, textures, audio, packages, code, scripts, materials, and build output.
- Shared inspector, status, search, diagnostics, and task surfaces.
- Dockable panes for asset context, compiler logs, map/object properties, and dependency information.
- Layout preset service for editor-profile workspaces and user-customized panes.
- Accessibility, language, theme, scale, density, motion, and TTS preferences.
- First-run setup/resume service for tailoring the application ecosystem.
- Global activity center for queued/running/completed tasks, progress, cancellation, and detailed logs.
- Durable recent activity history for terminal package, compiler, setup, and
  shell tasks shown in the workspace timeline after restart.
- Notification and status system for inline feedback, toasts, task cards, warnings, and recoverable errors.
- External-change watch that polls the open map, open package, and code-editor
  file from a shell-owned one-second timer, then offers a reload, a reopen, or
  an ignore for each substantive change.

### Project Core
- Project manifest and settings with `.vibestudio/project.json` metadata,
  current-project persistence, and dashboard health checks.
- Game installation profiles with manual settings persistence, stable IDs,
  engine-family defaults, and read-only validation.
- Asset database and dependency graph.
- Automation graph for reusable workflows, presets, command manifests, and batch operations.
- Package mount graph for folders, WADs, PAKs, PK3s, and nested containers.
- Task runner for compilers, converters, validators, and external tools.
- Compiler registry descriptors and executable discovery for imported external
  tools, user-configured executable paths, project-local overrides, version
  probes, and capability flags.
- Compiler wrapper profiles and command planning for ericw-tools `qbsp`, `vis`,
  and `light`, Doom-family node-builder stages, and q3map2 probe/BSP stages.
- Compiler runner services for `QProcess` execution, cancellation, stdout/stderr
  capture, diagnostic parsing, output registration, and re-running manifests.
- Schema-versioned compiler command manifests with structured task-log entries,
  environment subsets, inputs/outputs, hashes, duration, exit code, and raw
  process output.
- Level-map document services for Doom WAD map lumps, Quake-family `.map`
  text, Quake III `.map` text, entity/brush/thing inspection, texture/material
  references, validation health, safe entity and movement edits, undo/redo, and
  non-destructive save-as.
- Entity definition catalogues for Radiant `.def`/`.qc`, Valve `.fgd`, and
  Quake III `.ent` sources, with base-class inheritance folding, conventional
  project search paths, and map entity validation for classnames, key value
  types, required keys, spawnflag bits, and target/targetname references.
- Document watch service that fingerprints the files the studio holds open by
  role, coalesces the burst a single save produces, and reports whether a path
  was modified, touched, removed, replaced, or created. It is plain QtCore with
  no `Q_OBJECT`, so the app layer drives its `poll()` instead of connecting to
  signals.
- Advanced Studio services for idTech3 shader script models, shader stage
  edits, sprite workflow planning, source tree indexing, extension manifests,
  and deterministic AI creation proposals.
- The materials module (`core/material_*`): one definition model for Doom,
  Quake, Quake II, Quake III and Doom 3 surfaces, source-preserving script
  edits, expression and wave evaluation, engine image lookup, a CPU renderer
  per engine, a library scan and node graphs whose edits become text edits.
  The Materials page (`app/material_*`, the `shell.mode.shaders` mode) and the
  `material` CLI family (`cli/materials.*`) share it; see
  [Materials](MATERIALS.md).
- Shared command services used by both GUI actions and CLI commands.
- Operation state model for loading, scanning, indexing, compiling, extracting, saving, cancelling, and failure recovery.

### Format And Package Layer

`core/package_browser` prepares immutable positional child/occurrence metadata
and filtered row indexes from an admitted reader snapshot, without payload reads.
It shares property-query semantics and cancellable stable sorting with the
existing core services. Each index admits at most 250,000 entries and 250,000
non-root folders; working keys and retained folder paths share a 128 MiB text
limit. Failure or cancellation publishes no partial result. `app/package_entry_view`
uses a native Qt list model with uniform batched layout and on-demand display,
icon and accessible roles. One worker and one replaceable pending request
coalesce folder/query changes. Revision/generation checks reject stale results;
model resets clear selection-dependent previews and commands. Cancel/Retry,
complete-result selection and exact-index drag preparation share the same model.
The index is reused until the planned revision changes. It also contains sorted
folder parent/child/row relationships, exact path lookup and at most eight
composition buckets and archive summary counts with overflow-checked byte totals.
The existing worker prepares these once; `app/package_folder_view` supplies a native Qt tree model
with uniform row heights and on-demand roles. Folder selection expands ancestors
only. Column sizing measures visited captions and depth, with pixel scrolling for
deep selections; it never measures every folder row. Current tree identities
survive query changes and are cleared on revision changes. Folder editing uses the tree path plus document revision independently
of entry-list readiness. Composition/detail summaries reuse cached buckets, and
folder navigation does not rebuild staging. `app/package_staging_view` shares
prepared operation/conflict vectors through a native list model. Its batched
layout requests constant-size hints for change rows without formatting text;
visible display, tooltip and accessible roles format on demand. Only the bounded
overview/composition rows use wrapped variable heights. Exact operation IDs,
rename targets and source ordinals drive Unstage/reveal. A resizable, read-only
details pane exposes complete current-row text. Unchanged snapshots preserve
selection; reset/close clears stale selection and details. Remaining load/import
projection, aggregate process-memory and whole-shell timing acceptance remain
separate work. No dependency or CLI command is added.

`app/package_entry_view` reports current-occurrence changes separately from
multi-selection. The shell updates previews on current changes and model
invalidation. Selection and listing signals call `refreshPackageCommandEnablement`
to update package commands and toolbar controls immediately from one final-state
policy. The full shell refresh also calls that policy; opening, save, edit and
extraction completion restore the same controls. Package selection does not
refresh other surfaces or reset their recent-file selections.
Shared detail drawers retain existing chooser rows when section identities and
order remain stable, updating accessible labels and state metadata in place.
Preview completion retains the selected detail section.

`app/package_preview_worker` owns one active immutable reader and one replaceable
pending exact entry. A generation token prevents superseded/cancelled results
from reaching the inspector, while its revision/index guard rejects stale shell
state. Read progress is coalesced by a Qt timer. Destruction cancels and joins
before releasing the reader's content ownership. `core/package_preview` samples
through `streamEntryAt`, bounds retained bytes and propagates read cancellation.
Full samples require final integrity success; intentional prefix termination is
reported as truncated. Analysis checks cancellation before/after bounded codec
work. GUI and CLI share this service. Audio's already-verified memory adapter
implements streaming so metadata analysis preserves its exact occurrence.
Package texture previews retain their palette-aware texture worker; both paths
share the inspector's Cancel/Retry control. No dependency is added.

`core/package_copy` plans exact row/folder selections under file, entry and byte
limits, creates one owned temporary batch and invokes shared streamed extraction.
It exposes a storage lease and root paths only after the complete batch verifies;
failure/cancellation destroys the private batch on its operation thread.
Overlapping folder selections are deduplicated and empty folders are retained.
`preparePackageCopyEntries` retains a private batch and pending window/store
reservation tokens. `prepared()` distinguishes that state from `succeeded()`.
Publication commits those tokens; discard removes the batch before releasing
them, retaining full charges when deletion fails. Shared result aliases observe
the same preparation state and cannot publish a discarded batch. Dropping the
last unpublished result also discards it. Preparation and finalization belong
on operation threads. The synchronous `copyPackageEntries` wrapper performs both
steps for existing core/CLI callers.
The shell keeps successful leases for its session, rejects changed-context
handoffs, and never overwrites an earlier copy. Drag MIME and disk-based authoring
handoffs use this same worker path; Code keeps the resulting copy read-only.
Level Save routes a session copy to Save As with a durable directory suggestion.
Both Code Save As and the shell map-save operation use
`core/package_copy::packageCopyStorageContainsPath` to refuse lexical or resolved
paths inside their window's temporary-copy root before starting a writer. The
shared package path service handles missing descendants and Windows junctions. A successful external
save adopts the new map identity without changing the package; staging remains
explicit. This guard concerns the owning window, not another process's storage
or an external filesystem writer racing the path check.
`core/package_copy_budget` atomically reserves initial bytes, files, entries and
one batch against configurable per-window limits before payload reads. Its
mutex-protected shared state includes pending workers. Verified failure cleanup
releases the reservation; failed cleanup conservatively retains it and reports
the path. Successful reservations last for the window, matching the shell's
retained leases across package changes. The GUI exposes live usage and settings;
`cli/package_copy_limits` shares the settings with strict no-write inspection
and proposal behavior. It does not read another process's live usage.
Consumer edits and filesystem overhead are outside this logical accounting.
`core/package_copy_store` now gives each window a UUID `.copies` session under
`vibestudio-package-copies` in the resolved OS temporary root. It reuses
`PackageDraftAccess` native directory leases for live ownership, creation
serialization and reviewed maintenance. Batch storage deleters retain the shared
session even when callers retain only the `QTemporaryDir` lease. Last release
queues explicit bounded cleanup while retaining the native owner; main drains
the dedicated pool after shell/global workers and before Qt teardown. Crashes
release native ownership without stale lock files. Unrecognized legacy copy
folders are not adopted.

The managed store additionally serializes durable reservations across processes
using a native `PackageDraftAccess` handle on `.coordination`. This separate
directory allows Windows to enumerate session storage while maintenance denies
read sharing on the coordinator. Creation, policy changes, reservation commits/
releases and cleanup use the same coordinator. Acquisition is cancellation-aware
and bounded to two seconds. Version-2 session records seal initial byte/file/
entry/batch counters and pending/failed state with SHA-256; metadata commits use
`QSaveFile` without direct-write fallback. Pending reservations are committed
before payload creation and survive abrupt process exit. Successful/failed-cleanup
charges last until session removal. A failed accounting update retains the prior
charge conservatively. Implicit parent folders count toward admission.

`limits.json` stores the physical store's shared policy independently of QSettings.
Defaults are 8 GiB, 32,000 files, 160,000 entries and 256 batches; the hard payload
entry ceiling is 248,832 to leave review space for batch/session metadata.
`cli/package_copy_store_limits` and the worker-backed Shared Storage Limits editor
share bounded inspection, no-write proposals and checksum-bound policy commits.
Lower limits preserve existing copies. Legacy/missing/invalid reservation records
block new admission but do not prevent review of recognized safe contents.
Invalid policy files are retained; automatic policy repair remains open.

Shared bounded inventory reports actual logical payload sizes and metadata
review fingerprints. `app/package_copy_sessions_dialog` and
`cli/package_copy_sessions` reuse inspection and reviewed discard. The GUI uses
owned cancellable workers; CLI inspection/default dry run creates no files or
preferences. Both discard modes take native maintenance ownership and recheck
the review. Unsafe layouts are retained; missing/invalid records remain labelled
but a recognized safe layout can still be reviewed. Cleanup removes each checked
file and empty directory explicitly, leaving the record until payload removal.
Interrupted cleanup requires a fresh token. Review bounds are 128 sessions,
250,000 aggregate entries, depth 128, 32 MiB of path text per session and 64 KiB
per record. Tokens bind names, sizes/times and record bytes, not payload contents.
Native exclusion coordinates participating sessions, not arbitrary external
writers. Inventory carries actual usage and shared reservation snapshots with
separate completeness flags. Later consumer growth, native drag and broader
shutdown cleanup acceptance remain open. No dependency or archive parser is added.

`app/package_operation_dialog` runs opening, file staging, extraction, temporary copies, source/staged
comparisons, validation and package writes
on owned workers. Its callbacks publish value results on the UI thread. Cancel
and close requests keep the dialog alive until completion; destruction and
application shutdown join the worker before its owner is destroyed.
Its native progress bar shares the studio's palette-aware Fusion rendering;
filled and empty regions use separate text roles. Font-based sizing follows live
text-scale changes, with the same approach used by the Level map-loading dialog.
`app/package_progress` formats shared binary byte quantities and exact localized
accessible counts. Integer rounding preserves distinct partial/total amounts at
uint64 limits; the bounded progress value reaches its maximum only after the
reported total. Operation dialogs keep metadata in record units, expose unknown
byte totals explicitly, and clear details when the phase finishes. Validation
uses a synchronized file/byte snapshot. Core and CLI reports retain exact counts.
Opening and file staging use modal event loops around owned workers so existing
asset/session handoffs remain sequential without disk reads on the UI thread.
Failed opens preserve the current archive and plan; cancelled staging batches
return the original plan. Opening, staging and palette completion callbacks
recheck the latched cancellation state before adoption. Temporary copies retain
their pending reservations through this decision, then run disposal or reservation
publication on a second owned worker. Cancel is disabled after that decision;
Close waits for finalization. Committed saves/drafts and completed extraction
retain their authoritative output reports. Comparison and validation retain
completed diagnostic results. The shell prevents concurrent package-context changes.
`core/package_extraction` accepts the shared archive-reader interface, keeps
physical entry indexes through selection/sorting, and preflights output names
and containment before creating anything. Explicit `entrySelections` map exact
file indexes to optional output paths. Colliding output names are rejected;
`app/package_extraction_paths` lets GUI users review distinct relative paths
before the worker performs the full preflight. Archive and staged readers implement bounded,
cancellable streaming; checksum failure discards the temporary file. New files
use atomic-only `QTemporaryFile::rename`; replacements use `QSaveFile` without
direct-write fallback and recheck the previous output identity. Partial batch
reports distinguish committed bytes from bytes read. Shell extraction owns its
worker through the modal operation dialog; CLI calls the same service directly.
Saving holds the shell's package context stable and suspends document polling
until the committed output is reopened and staging is rebased. Dependency,
texture, model, audio, and project context are refreshed after adoption. Failed
or cancelled writes leave the live plan intact. Comparison shares
`core/package_compare` with the CLI and reports incomplete checks explicitly.
Reader warnings and unchecked content prevent an identical verdict; metadata-only
mode explicitly limits the claim to names and sizes. No dependencies changed.

`core/model_design` owns bounded primitive designs, schema-versioned JSON,
deterministic static MD3/OBJ export, atomic output writes, and the model-to-map
handoff. Schema 2 adds X/Y/Z part rotations and bounded UV transforms; schema 1
loads with identity defaults. Preview, CLI, exports, and staging all consume the
same transformed geometry and normals. `app/model_design_dialog` owns the
editable document, saved-state comparison, and selection-aware undo history;
its shell callbacks route material navigation, package staging, and map edits
through existing services. Generated bytes are held in `PackageStageOperation`
and `PackageStagedEntry`, with content hashes in manifests.

`PackageStagingArchive` exposes a value snapshot of a conflict-free plan through
`PackageArchiveReader`. Dependency inspection and subset writing consume this
same snapshot, preserving generated bytes and imported-file provenance.
On-disk content remains lazy. `map_preview_mesh` accepts decoded model snapshots
without doing file I/O and preserves entity ownership for selection. See
[Model Design](MODEL_DESIGN.md) for the end-to-end workflow and current limits.

`core/model_assembly` and `core/model_assembly_document` own a separate schema-1
`.assembly.json` graph. Bounded file/package snapshots retain native poses and
tags; independent frame samplers compose nested rigid attachments and positive
uniform scales. Preview and static export share the same sampler and input
fingerprints. Candidate document edits retain selection-aware history; relative
references and guarded source saves share the model file service. The Qt
`ModelAssemblyDialog` and `model assembly` CLI use those services. Missing inputs
leave a repairable GUI recipe with no stale preview. `runModelTask` supplies the
existing cancellable progress surface for both mesh and assembly workers.
Static baking enters the ordinary mesh editor for staging, dependency review,
level placement and compilation. `core/model_assembly_recovery` stores bounded,
checksummed recipe/context records; its session lease, inventory limits, reviewed
discard and source-free restoration are shared by GUI and CLI. The coalescing
assembly recovery writer keeps one active and one pending snapshot and retires
copies after active writes. It shares the mesh recovery preference, with a
separate format and folder so linked sources never flatten into meshes.
`core/model_assembly_animation` uniformly samples immutable assembly inputs into
a bounded vertex-frame sequence. Preflight checks frame-vertex storage; every
composed sample must validate and retain topology/winding, UVs and materials.
The GUI review/worker and CLI use the same atomic bake and guarded export.
A named clip retains the sampling FPS in mesh schema 6. Static and sampled
bakes enter ordinary mesh authoring, package/dependency review and level handoff.
Native game timing configuration and animated-bake engine acceptance remain open; see
[Model Assemblies](MODEL_ASSEMBLY.md).

`core/model_document` owns editable native-mesh snapshots, strict source parsing,
candidate validation, selection-aware bounded history, and source fingerprint
checks. Topology edits update every animation frame and preserve indexed UV
seams. `core/model_topology` enumerates canonical indexed edges and performs
bounded distance welding with fixed anchors, all-pose checks, and default
UV/normal seam protection. Conforming midpoint splits remap edge selections;
history and recovery retain endpoint pairs. `core/model_tags` edits named
attachment identities across frames and rigid per-pose origins/bases. Tag
selection is exclusive of mesh components and remains part of history/recovery.
`core/model_fingerprint` streams ordered geometry through a bounded hash buffer
and uses the source serializer for metadata identity. Internal revision hashes
remain distinct from on-disk source hashes. Documents prepare immutable edge
order/incidence indexes on the normal worker; selection validation, component
tables and history share those indexes. Their estimated storage participates in
the history limit. Table refreshes preserve compact selection ranges and avoid
model resets when only the pose or selection changed.
The Animation form, table selection, viewport overlays and move/rotate previews,
and `model tags` / `model edit` CLI routes share those candidate operations;
MD3 staging retains every tag pose. No source schema or runtime dependency changes.
`core/model_animation` owns indexed clip metadata edits, full-pose copying and
bounded in-between generation. All surfaces and attachment poses participate;
position/normal interpolation and Qt quaternion interpolation operate on a
disposable candidate before all-pose document validation. Existing endpoints
remain exact. Clips grow only across both interpolation endpoints; later ranges
shift. `app/model_editor_animation` and `model animations` / `model edit` share
the source representation, worker, history and recovery. Viewport clip indices
distinguish imported duplicate names. Preview FPS can override the session, while selecting a clip adopts its saved
rate. Add Clip / Apply Clip FPS and CLI timing edits retain optional fractional
FPS in schema 6, with optional MDL/collision metadata. Untimed sources keep their
previous schema and all older versions remain readable. Native exporters report
omitted timing; native MDL schedules stay independent. Dependencies are unchanged.
`core/model_pose` supplies bounded looping time samples and the shared position,
normal and rigid-tag interpolation used by generated poses and smooth preview.
`app/model_viewport_animation` samples a monotonic elapsed-time clock, supports
clip-relative seeking, and keeps transient fractions outside the mesh document.
Projection, picking and tag overlays use the sampled pose. Pause/step/frame
selection return to stored poses, and late fractional raster work is retired on
pause or explicit seeks. The editor's Smooth preview checkbox is session-only;
reduced motion prevents automatic playback in either preview mode. A delayed
timer samples the elapsed position rather than advancing once per callback.
Each raster request retains compact projected attachment markers and its logical
viewport size. Completed images and those markers are presented together while
later poses or camera updates render; resize scales both consistently. Retiring
content clears the displayed image and markers. Smooth playback hides vertex
editing dots and transform handles until pause, while selected edges remain in
the rendered snapshot. No extra full-size overlay image is allocated.
Hidden attachment overlays skip projection and tag interpolation diagnostics;
surface compatibility diagnostics remain active. Changing tag visibility
invalidates the marker snapshot along with its raster.
Explicit gesture requests pause smooth playback before resolving handles on the
stored pose; ordinary pointer navigation does not target hidden handles. Gizmo
hit testing uses current camera/document geometry and does not wait for raster
depth, which it never reads.
The mesh inspector derives its minimum width from each complete page, including
nested styled groups. Editor-control and studio-handoff smoke tests are separate:
the latter preserves shell reentry, primitive baking and deferred-close coverage
while applying the saved theme before widget construction, matching app startup.
`core/model_topology_health` inspects indexed incidence, duplicate faces, unused
vertices, disconnected fans, winding conflicts and boundaries. Bounded corner
and face groups avoid pairwise scans around crowded edges. Whole-surface repairs
remove duplicates or unused samples, split disconnected fans/nonmanifold edges, or propagate
consistent winding while retaining authored attributes. Maps preserve component
selection and seam marks across poses. `app/model_editor_health` runs inspection
on the existing document worker, invalidates stale reports, and exposes finding
selection plus explicit repairs. `model topology` adds the same health report
to its edge list; `model edit` shares the five atomic repair operations.
Nonmanifold splitting groups corners at affected endpoints through existing
two-face edges only, preserving those connections without pose-dependent face
pairing. It preflights added vertices across all poses, copies attributes exactly,
remaps marks and selection through face corners, and verifies incidence again
before publication. Boundaries and unrelated disconnected fans stay explicit.
`core/model_import_repair` keeps damaged geometry outside `ModelDocument` while
preparing explicit face removals, selective normal reconstruction and orphaned
seam removal. A private bounded source decoder is shared with strict parsing;
normal loading still validates before adopting any value. Repair retains all
usable vertices/attributes and validates the complete result before publication.
The GUI review uses a lazy table and prepared-copy viewport; the CLI shares the
same plan and grouped report. Saving checks the reviewed source identity and
uses ordinary atomic document save to a new destination only. The saved copy
then follows existing authoring, recovery, export, staging and placement paths.

`core/model_intersections` scans geometric self and cross-surface contacts in
stored poses through a per-pose spatial hierarchy. It bounds face poses,
candidate pairs, node visits and results, and publishes a complete deterministic
report only after success. `core/model_triangle_contact` shares the existing
scale-relative contact predicate with boundary authoring; private double-precision
geometry helpers avoid divergent implementations. `app/model_editor_intersections`
runs the read-only scan on the document worker and retains revision-bound session
results in a lazy list model. Face navigation synchronizes pose and selection
without a document edit. The strict `cli/model_intersections` adapter exposes
the same report with all/one-pose and incident-surface scopes. No source-schema,
dependency, native-export or recovery format changes are introduced.
`core/model_boundary_fill` and `core/model_boundary_bridge` share internal
boundary discovery, workload accounting and proposed-face intersection checks
through `model_boundary_helpers`. Filling projects and triangulates selected
loops; bridging uses a bounded original dynamic program for two seam orientations
at a reference-pose anchor pair, with explicit twist and unequal loop counts.
Both validate new faces in every stored pose, preserve existing vertex attributes
and select the new faces through the normal document transaction. Geometry
controls and `model edit` share cancellation, history, recovery and export paths.
No extra renderer, source schema, dependency or setup preference is introduced.
`core/model_export` writes animated MD3 and selected-frame OBJ through the
same validated representation. `app/model_editor_dialog` and the model import,
edit, and build CLI share these services and `stageModelMesh` for package/map
handoff. `core/model_recovery` stores versioned headers and checksummed editable
payloads through atomic writes. `app/model_recovery_writer` coalesces immutable
snapshots and permanently retires discarded IDs; the chooser scans and restores
on workers. Source paths are provenance only. The CLI uses the same catalog and
validation service. See [Editable Meshes](MODEL_MESH.md); the outstanding editing
responsiveness, production interaction, and format gaps are explicit release gates.

`core/model_surfaces` partitions and combines surfaces without recomputing authored
attributes. Every pose, seam edge and ordered material binding passes through the
same candidate validation and selection-aware history. The Widgets review dialog
and dedicated `model surfaces` CLI require explicit material adoption for
mismatched bindings. Resulting surface indices refresh material/UV previews and
flow through ordinary recovery, native export and staging. External name-based
references remain a documented integration boundary; see
[Surface Authoring](MODEL_SURFACES.md).

`ModelSelection::surfaces` stores a whole-surface set, mutually exclusive with
components, tags and collision. Its active surface is a member of that set.
`core/model_surface_selection` validates this invariant and resolves one common
reference-pose pivot for the document worker and viewport. The viewport retains
implicitly shared geometry to cache the pivot, and carries the set into its
asynchronous projection/normal work. History accounts for the set; recovery adds
an optional validated field compatible with older payloads. Normal mesh source
and native exports do not store selection. GUI and `model edit --surfaces` use
the same atomic transform. Join remaps the selection to the surviving target.

`core/model_material_slots` validates bounded ordered binding edits and creates
geometry-free material preview snapshots. `SetMaterialSlots` commits a reviewed
list through normal document history, source/recovery and export services.
`app/model_material_slots_dialog` buffers review edits; the dedicated CLI uses
the same list operations. Per-surface preview indices are session state keyed
by surface name, clamped on edits and reset on source changes. The existing
material worker keys its requests by selected paths, package revision and
palette, so newer slot choices retire stale results. `model materials` uses
the same snapshot service for explicit overrides. See
[Material Slots](MODEL_MATERIAL_SLOTS.md); no renderer or dependency changed.

`core/model_appearance` prepares a geometry-free material snapshot for ordinary
browser and CLI inspection. It validates separate external-slot, embedded-MDL
and exact package-skin modes, decodes only the chosen indexed member, and reuses
the existing Quake III binding parser and verified package reads. Preparation
publishes atomically and leaves original mesh data untouched. Receipts describe
the effective bindings and selected input identity. `app/model_browser_appearance`
owns session controls; `app/model_preview_worker` owns decoding and resolution,
retiring stale generations and notifying the shell when cancellation has settled.
Appearance requests keep the current viewport geometry, frame and camera, while
context changes reset them. `cli/model_appearance` uses the same preparation and
bounded resolver, including portable drafts and package palettes. Browser edit
and frame-export handoffs retain original bindings. Placed MD3 compiler skins
and remaps use the separate shared `core/level_model_appearance` contract described
in [Placed Model Appearances](LEVEL_MODEL_APPEARANCE.md).

`app/model_uv_render` prepares value-only UV images on the existing cancellable
worker. `app/model_uv_fill` converts selected faces to winding contours, removing
only opposite indexed borders and exactly collinear fill segments. Qt paints
the selected union once; wire and picking topology are unchanged. Ordinary
wires, seams and selection use `app/model_rasterizer` in that order. Its opaque
tile cache proves pixels unchanged before bypassing coverage work and is local
to one color pass. A callback polls cancellation within long strokes. UV images
retain a 4,194,304-pixel limit for both ordinary and extreme aspect ratios.
Mesh data, document transactions, material resolution and CLI operations keep
their existing services and contracts.

`core/model_uv_transform` applies independent bounding-box pivots to complete
selected UV islands through the document worker and CLI. Indexed topology supplies
stable chart order; unselected faces retain their original indices, and selected
charts split shared corners only after preflighting global vertex/pose capacity.
All positions and normals copy exactly across poses. Per-face seam remapping
retains both sides of split boundaries. Transform and projection validate a whole
candidate before selection-aware history publication. Sources, recovery, native
exports and package handoffs consume the resulting ordinary mesh representation;
the operation adds no schema or dependency.

`core/model_uv_atlas` supplies automatic charting and packing through pinned
xatlas, compiled as a private standard C++ static library. A reviewed build
adaptation preserves indexed boundaries and avoids independent-axis texel
rounding. The document operation splits corner fans along seams, maps output
corners back to every pose, checks storage/UV overlap limits, and publishes only
a fully validated candidate. A thread-local allocator bounds library memory and
reclaims incomplete graphs after allocation failures; upstream worker pools are
disabled because the studio already supplies a cancellable document worker.
GUI and CLI use the same options and source/history/recovery representation.
The generated xatlas header/source also accept independent atlas axis limits;
original pinned bytes remain unchanged. Repacking converts input UVs to the
chosen pixel aspect before library normalization. Unwrapping keeps geometric
chart proportions in pixels. Rectangular packing bounds uniform density by
individual chart extents, reduces it until one atlas fits and performs four
bounded refinements. Output normalizes each axis separately. Atlas edits
preserve material paths and unselected UVs. `core/model_uv_mapping` shares exact
all-pose corner copies and seam remapping with the separate obstacle service.

`core/model_uv_obstacles` implements Pack Around Unselected without adding a
dependency. Complete indexed islands retain orientation and relative scale;
unselected active faces and other surfaces sharing normalized nonempty material
slot paths become fixed texel masks. All alternate slots participate. Fixed
regions outside the 0–1 tile are refused. A conservative row-strip rasterizer,
bitset collision tests and pixel padding support rectangular atlas dimensions.
Deterministic placement scans from the lower indexed row; optional uniform
fitting uses bounded reduction/refinement, while preserve-scale tries exactly
one scale. Raster allocation and search budgets, normal topology/overlap limits
and cooperative cancellation bound the operation. Result and receipt publish
only after final validation and corner remapping. GUI and CLI call this service
through ordinary document edits, history, recovery and exports. Shader/image
aliases, persistent per-corner pins, multi-tile packing and texture rebaking
remain explicit integration gaps.

`app/model_material_worker` resolves a small material-only mesh snapshot through
`resolveModelPreviewAssets` in `core/level_materials`. It retains no geometry poses
or undo history. Immutable `PackageStagingArchive` readers preserve replacements,
deletions, and palette bytes while the shell changes its live plan. A binding key
includes source revision, palette, model provenance, and surface materials; one
active job and one coalesced pending job suppress outdated completion. Geometry
edits reuse resolved images. The editor's 3D and UV views and `model materials`
use the same bounded shader/image lookup and diagnostics.

`core/model_file_io` provides bounded reads, reviewed destination identities,
cancellable staging, writer locks, and atomic publication for mesh and primitive
design output. New files publish without replacement; existing files retain
content/path checks immediately before `QSaveFile` commits. `core/model_work`
carries cancellation and phase progress without owning UI state. The editable
document adopts its saved identity only after publication succeeds.
`app/model_document_work` runs value-only document candidates on a worker while
servicing the UI event loop. The editor guards reentrancy, disables document
controls, and defers close until completion. A delayed window-modal progress
surface exposes the phase and cancellation. Candidate/history adoption occurs
only on success; a committed write remains successful after late cancellation.
Instrumented geometry and serialization loops poll in batches of 256 visits.
Metadata/selection preparation, container algorithms, native decoders and Qt
codecs finish their bounded calls before the next cancellation checkpoint.

`core/level_dependencies` connects map objects to explicit texture/shader-image,
model, and sound references using `PackageArchiveReader`. It produces a bounded,
deterministically ordered report with candidates, source attribution, exportable
file paths, cancellation, and coverage limits. `core/package_selection` resolves
exact occurrence indexes, unambiguous paths, folder prefixes and the shared
package query language. `core/package_subset` implements
`PackageStagingModel::loadBaseArchiveSubsetAt`: it rebases immutable planned
content without consuming caller history, expands required Doom map/GL/namespace
and local texture-table groups, and retains WAD source order. Invalid, ambiguous
or cancelled selections leave the previous plan unchanged. Unreadable/skipped WAD
records cannot silently disappear. The path-only wrapper rejects duplicate names.
The review records snapshot indexes, source ordinals and inclusion reasons.

The CLI, package browser and `app/level_dependency_dialog` consume these services.
`app/package_subset_dialog` prepares and writes a captured reader on workers,
coalesces progress and joins before destruction. It reviews selected versus
required group members and protects all source inputs from overwrite. The GUI
preserves the live editor plan and records export results in Activity. CLI subsets
consume drafts through the same planned adapter without reopening by name after
selection; strict options forbid post-selection edits and in-place exports.
Map-object navigation rejects stale document revisions. Export cancellation is
checked between file reads and before commit; compression of the current entry
finishes first. `core/package_wad_groups` now owns the shared semantic scan used
by subset expansion and group edits. Compact range inventories identify maps,
GL companions, namespaces and local texture tables. Content/plan fingerprints
reject stale reviews. Edits append existing ordinal/path operations to a private
candidate, validate once and publish one history group. No draft opcode or
schema migration is needed. The Qt group dialog inspects and verifies on workers,
shows paged changes and checks the owning document before adoption. CLI group
options use the same service and require the inventory fingerprint.

Every WAD document supplies a positional plan, including new documents and
source-free draft/subset snapshots. New Doom plans use the shared group scan to
assemble binary/GL runs before inspection, attaching loose map records only to
one unambiguous owner. Namespace contents and UDMF sidecars keep their order.
Assembly before a batch of non-addition edits binds map membership before a
marker can lose its conventional label; draft/undo replay follows the same path.
The writer saves that exact reviewed order without another inference pass.
Native texture anchors, named maps and GL runs therefore keep the same meaning
in inspection, preview, group edits and saved output. Existing WAD source order
is unchanged. The older single-map writer fallback remains limited to conversion
from non-WAD sources. Metadata/script reference rewriting
and full texture/patch/game-code dependency closure remain integration requirements.
- PakFu-derived archive interfaces, virtual path safety, read-only readers,
  extraction reports, staging manifests, deterministic writers, and format
  parsers.
- Shared package entry metadata, read-only listing readers, and mount-layer
  session state for folder, PAK, WAD, ZIP, and PK3 workflows.
- Shared package preview service for text/script samples, image/texture
  metadata, model metadata, audio metadata/waveform summaries, and binary
  hex/metadata summaries used by both GUI and CLI surfaces.
- Shared asset tooling service for image conversion queues, WAV export helpers,
  native idTech model metadata boundaries, and project text find/replace.
- Shared idTech image decoding for Doom picture/flat/palette/colormap lumps,
  Quake `.lmp` and WAD2/WAD3 miptex, Quake II `.wal`, `.m8`, and `.m32`, Quake
  and Half-Life `.spr`, PCX, and Targa, with generated stand-in palettes rather
  than shipped game palettes. Quake II `.sp2` is read as a frame table whose
  images are resolved from the open package.
- Shared idTech model geometry decoding for Quake MDL (IDPO 6), Quake II MD2
  (IDP2 8), and Quake III MD3 (IDP3 15), with header-only reads for MDC, MDR,
  and IQM, package-resolved skins, frame-name-inferred animations, and
  single-frame Wavefront OBJ export. `core/model_frame_export` shares browser/CLI
  destination review, source/package protection and guarded publication through
  `core/model_file_io`. The browser captures immutable mesh/package values and
  uses the existing cancellable document worker; closing waits for cancellation.
- Shared level-map parsing and save-as service for direct WAD map lump access
  and text-map round-tripping, keeping map edits independent from package entry
  sorting or archive browser presentation. Text-map save-back is line-based:
  an edited value is replaced inside its quotes and nothing else on the line
  moves, a removed key takes only its own pair, a deleted entity, brush, or
  patch drops the source lines it owns (`LevelMapDocument::deletedLineRanges`),
  and entities added in the editor are appended after the last one. A copied
  brush or patch keeps its original's text as a template
  (`LevelMapBrush::sourceLines`) and is written with its own points, inside the
  entity that holds it. Adds, copies, and deletes are undo commands that
  snapshot the objects and their positions. `levelMapSelectionText()` and
  `pasteLevelMapText()` move objects through the clipboard as .map text,
  pasted primitives keeping the pasted text as their template. A replaced
  texture is written over the name token alone (`replaceFaceTexture()`), so
  points, texture matrices, and flags on the face line stay as they were.
- Shared Advanced Studio format helpers for shader text round-tripping, mounted
  texture-reference validation, Doom/Quake sprite package paths, and
  extension-generated staged-file declarations.
- Shared safe extraction service for selected/all entries, dry-run output-path
  reporting, no-overwrite defaults, progress callbacks, and cancellation.
- Fixture-backed support matrix.
- Safe write-back model with staging, diffing, conflict handling, deterministic
  save-as writers, and reproducible manifests.
- Package comparison that pairs entries by case-folded virtual path and
  occurrence index, reporting added, removed, changed, identical, and case-only
  results, with entry content decided by size or verified SHA-256 payloads.
- Doom WAD write-back that keeps each map's lumps grouped under their own
  marker and reads lump bytes by source directory ordinal, so a WAD holding
  several maps with repeated lump names round-trips.
- Verified in-place package replacement: the new archive is written to a
  uniquely reserved sibling and re-hashed. `core/package_publication` copies
  and verifies the original, journals both versions, then atomically replaces
  the destination without first removing it. Qt `QTemporaryFile` publishes new
  output without overwrite; `QSaveFile` replaces existing output with direct
  writes disabled. `QLockFile` serializes cooperating saves and recovery.
  SHA-256 baselines detect changed destinations/backups. GUI/CLI recovery verifies
  the journal and files before finishing backup publication or cleanup.
  `core/package_publication_inventory` discovers bounded metadata in explicit
  output folders without hashing payloads. `app/package_publication_dialog`
  verifies/finishes selected saves on cancellable workers, carries a journal
  review checksum, and opens current output through the normal document guard.
  Save workflows flush a bounded folder history before writing. The recovery
  chooser adds project/recent folders; other studio publishers can be reviewed
  by choosing their output folder. It never installs an uncommitted replacement.
- DEFLATE encoder with `store`, `fast`, `default`, and `best` levels that
  chooses stored, fixed-Huffman, or dynamic-Huffman blocks per chunk by
  measured bit cost.
- Deterministic fuzz corpus generation so the DEFLATE, image, BSP, package,
  map, and model readers run against corrupted input as ordinary Meson tests.
- Palette, material, shader, model, and map metadata services.

### Tool Surfaces
- Level editor for Doom-family and Quake-family workflows.
- Interaction profile registry (`core/editor_profiles`) with 19 editor profiles.
  `core/level_editor_controls` supplies plan gestures, camera navigation/keys,
  grid defaults and layouts, including QuArK's four-view adaptation. Sixty-four
  validated gesture/navigation preferences share GUI, settings, portable files
  and CLI services. `MapViewport`, `ModelViewport` and `StudioCommandRegistry`
  apply the effective controls while layout overrides remain independent.
  `core/level_camera_keys` supplies bounded position-steering rates and
  `app/model_viewport_drive` owns the cancellable Qt timer and fixed camera
  steps. Profile data chooses those behaviors; it never changes document state.
  [Editor Profiles](EDITOR_PROFILES.md) documents each adaptation and the
  remaining native-behavior acceptance gaps.
- AI text transport (`core/ai_transport`): provider-neutral chat requests built and answers read as plain functions for three wire formats (OpenAI-compatible Chat Completions, Anthropic Messages, Gemini generateContent), secret and path redaction, the connection resolver that applies AI-free mode, cloud opt-in, model, endpoint, and credential rules, and `AiChatClient`, which sends one request at a time over Qt Network with a total timeout and cancellation. The shell's Assistant dock and the CLI's `ai ask` and `ai test-connection` share it. It also asks for structured JSON output in each provider's form and checks answers against the schema (`aiJsonSchemaProblems`). See `docs/AI_AUTOMATION.md`.
- AI image transport (`core/ai_image_transport`): the same plain-function design for pictures over OpenAI's Images API (generations, and edits as multipart forms), Gemini's generateContent with image output, and the Stable Diffusion web UI API (txt2img/img2img with tiling), with its own connection resolver (`resolveAiImageConnection`) under the same AI-free, cloud opt-in and credential rules, and `AiImageClient`, which repeats one-picture-per-call providers and fetches linked pictures without the key.
- Level generation (`core/level_generation`, `core/level_generation_build`): prompt specs, coordinate-free semantic plans (rules planner, or a text model's schema-checked JSON) with deterministic repair, integer-grid layout with stairs and A*-routed loops, sealed 2.5D cell geometry written as Quake, Quake II and Quake III `.map` text or a Doom PWAD, reachability checks, read-back through the level parser, and a layout preview. The Levels page's Level Generator and `map generate`/`map plan` share it.
- Texture generation (`core/texture_generation`): per-game texture profiles and prompts, seam measurement and blending, wrap-around resampling, palette conversion through `core/texture_export`, derived companion maps for source ports, and writers for WAD2, WAL, TGA plus shader, Doom PWAD lumps and PNG with a provenance record. The Textures page's Texture Generator and `texture generate`/`texture derive` share it.
- AI map edits (`core/level_ai_edit`): the map summary a text model sees (selectors, classes, origins, keys, brush bounds, textures, selection, with paths shortened), a strict JSON Schema of seven actions (add entity, set or remove a key, add a box brush, retexture, move, delete), validation of each action against the map, and application through the level editor's own undoable operations with deletes last. Edit with AI on the Levels page and `map ai-edit` share it; a saved proposal applies with no model.
- AI sound transport (`core/ai_audio_transport`): ElevenLabs' sound-effects request (and a custom endpoint taking it) as plain build and parse functions, its connection resolver (`resolveAiSoundConnection`) under the same AI-free, cloud opt-in and credential rules, and `AiSoundClient`.
- Sound generation (`core/sound_generation`): per-game sound profiles, names and model prompts, a deterministic synthesizer of fifteen kinds of effect, processing (mono, trim, fades, correlation-aware seamless loops, normalization to -1 dBFS in the delivered format) through `core/audio_delivery`, and writers for Quake-family WAVs and Doom DMX lumps in a PWAD with a provenance record. The Audio page's Sound Generator and `asset audio-generate` share it; Quake II and III sounds are placed with `core/audio_level`.
- Map viewport direct manipulation: rubber-band and modifier-based selection
  shared with the objects list, plus drags and arrow-key nudges that snap to
  whole grid steps by default, are previewed locally by the viewport, and are
  committed by the shell as one compound undo command.
- Entity inspector that resolves the selected entity against the loaded
  definition catalogue, explains each key, marks spawnflag state as text rather
  than colour, and folds entity findings into the map health view.
- Texture, sprite, model, audio, and cinematic editors.
- Software-rendered model viewport: orthographic/perspective projection with
  bounded depth buffering in `app/model_rasterizer.*`, matching picking,
  perspective-correct textures and per-pixel transparency compositing. QPainter
  presents the cached image and overlays without an OpenGL dependency, with
  wireframe/flat/textured modes,
  inferred animation playback, and OBJ export of the displayed frame.
  The same cancellable worker projects immutable scene snapshots, prepares a
  bounded screen-space picking index, and paints wireframe images. Pointer
  queries reuse that index with exact depth/alpha sampling; camera/pose revision
  checks reject stale geometry. Selection/style changes reuse projection data,
  and one active worker coalesces newer requests. This also applies to the
  Levels preview. Wireframe uses the original antialiased CPU line path in
  `app/model_rasterizer.*`; it deduplicates shared surface edges and composites
  selected dashes after ordinary wires. Clipped boundaries and displaced face
  edges retain their separate geometry. Bounded scans avoid work proportional
  to offscreen coordinates, and cancellation never publishes partial pixels.
  Vertex authoring adds a worker-prepared projection/occlusion snapshot with one
  cell reference per finite vertex. `app/model_vertex_overlay.*` performs exact
  indexed picks and bounded CPU marker stamps. Selection-only overlays reuse the
  mesh image; cached marker images follow its camera/pose snapshot. The overlay
  uses at most 32 MiB per image at the shared pixel ceiling; current and in-flight
  images can coexist. Component-header display has independent selection state
  so header painting cannot scan all selected rows. Complete edge selections and
  their connected vertices share immutable sets prepared with the document's
  topology; history estimates include both sets. Columns use bounded initial
  sizing and retain user widths across pose changes. Unchanged inspector refreshes
  preserve the completed camera/material image. Gizmo queries cache validated
  selection bounds against retained pose/selection inputs. Oblique vertex
  silhouettes use incident-face coverage within the marker footprint.
  `core/model_transform_axes.*` resolves World, Selection and Custom bases for
  numeric edits, viewport gestures and CLI. Selection uses a deterministic usable
  face or a tag/box orientation in one reference pose; the resolved orthonormal
  basis stays fixed across affected poses. Position and inverse-transpose normal
  transforms share that basis. Gizmo rays and handles use its coordinates, while
  pivot coordinates remain in model space. Collision size uses intrinsic box axes
  to avoid shear. Axis settings are operation/session values; document schema,
  recovery, package and native-export paths consume the resulting geometry.
  `core/model_trackball.*` maps normalized screen coordinates onto a visible
  hemisphere and its equator, then resolves the shortest rotation into the
  captured transform basis. It snaps the total angle before Euler decomposition;
  ordinary document commits must not resnap those components. The immutable
  drag state makes updates independent of intermediate event sampling. The
  Rotate gizmo exposes a labelled centre and dashed circle without changing
  constrained ring picking, camera navigation or opt-in editing. Existing
  candidate validation, frame scope, history and package handoff consume the
  resulting mesh/tag/collision transform. No new schema or runtime library.
  `model-viewport-latency-smoke` records maximum editable-grid
  event-loop gaps independently of time to the completed image.
  `model-editor-latency-smoke` measures the real editor's preparation, selection,
  mode switches, exact vertex queries and pose updates at the same geometry limit.
- Code/script IDE, with an active source index, language-hook descriptors,
  diagnostics, symbol search, build task hints, and launch-profile summaries.
- idTech3 shader graph with text round-tripping, stage previews, mounted
  package-reference validation, and safe stage directive edits.
- Sprite creator workflows for Doom lump names, Quake sprite sequencing,
  palette previews, frame rotations, and package staging paths.
- Extension surface for manifest inspection, trust/sandbox metadata, reviewed
  command plans, dry-run execution, and staged generated files.
- Package comparison surface that diffs the open package against another
  archive on disk and reports the per-entry result in the package detail
  drawer.
- Compiler pipeline editor.
- Detail-on-demand inspectors for raw metadata, dependency graphs, manifests, logs, and format-specific internals.

### UX Feedback Layer
- Loading displays and progress surfaces for every noticeable background operation.
- Skeleton/placeholder views while project, package, preview, or compiler data is arriving.
- Shared status chip, keyboard shortcut, and command-palette semantics for
  project, package, compiler, installation, AI, validation, QA, and support
  workflows.
- Visual summaries for project health, package composition, map health,
  compiler pipeline readiness, and Advanced Studio shader/sprite/code/extension
  status, with broader run graphs planned.
- Expandable detail drawers that expose raw logs, manifests, metadata, and diagnostic traces.
  Their width-dependent header layout moves wrapping actions below the title
  when needed. Long source subtitles use the shared middle-eliding label, with
  full tooltip/accessibility text. `WrappingActionButton` keeps native Qt button
  behavior; existing package actions use the same implementation. The shared
  `PropertyGrid` measures styled/translated headings, retains the chosen column
  width in font-relative units, and permits horizontal scrolling in narrow
  metadata panes. Launch, model and audio summaries use this common grid.
- Consistent success/failure/retry/cancel affordances.

### Accessibility And Localization Layer
- High-visibility themes, scalable text/UI, density presets, reduced motion, and OS-backed TTS.
- Accessible metadata for shell widgets, custom editors, package trees, compiler logs, graph views, and setup screens.
- Keyboard navigation, command palette coverage, focus order, and no keyboard traps.
- Locale target metadata, locale formatting, pseudo-localization, pluralization
  samples, expansion stress samples, representative layout-budget checks,
  Arabic/Urdu right-to-left smoke validation, and stale/untranslated
  translation status reporting.
- Initial 20-language localization target set, seed Qt TS catalog scaffold,
  dry-run Qt Linguist extraction validation, and planned runtime translator
  loading and translated release bundles.

### Initial Setup Layer
- First-run flow for language, accessibility, theme, density, role, editor profile, game installs, projects, toolchains, AI connectors, CLI, and automation.
- Auto-detection with manual add, skip, later, and review paths.
- Exportable/importable setup profile and editable preferences after setup.
- Smoke-check summary for installs, compilers, AI connectors, TTS, and workspace readiness.

### CLI Layer
- Full command-line surface over projects, packages, previews, compilers, validation, automation, and release workflows.
- Human-readable output by default, structured JSON output for automation.
- Stable exit codes, command manifests, and scriptable operations.
- Active lightweight router and command registry for `project`, `package`,
  `install`, `asset`, `map`, `entity`, `model`, `shader`, `sprite`, `code`,
  `extension`, `compiler`, `ai`, `credits`, and `cli` subcommands, with flat
  legacy options kept compatible.
- `entity definitions`, `entity validate`, `model inspect`, `model export`, and
  `package compare` run the same core services the shell uses. `package
  compare` returns the validation-failed exit code when the two packages
  differ, and `entity validate` returns it for entity errors, or for warnings
  as well under `--strict`, so a release script can gate on either.
- Global `--json`, `--quiet`, `--verbose`, `--dry-run`, `--watch`, and
  `--task-state` behavior for automation-friendly workflows where supported.

### AI Automation Layer
- Optional provider-neutral AI connector abstraction with active connector,
  model, credential-status, and workflow-manifest registries.
- Connector capability registry for reasoning, coding, vision, image, audio, voice, 3D generation, embeddings, tool calling, streaming, cost/usage, and local/offline execution.
- OpenAI implemented as the first general-purpose connector scaffold; design-stub connector targets remain for Claude, Gemini, ElevenLabs, Meshy, local/offline models, and custom HTTP/MCP-style integrations.
- Safe AI-callable tools for project summaries, package metadata search,
  compiler profile listing, compiler command proposals, staged text edits,
  shader scaffolds, entity snippets, package validation plans, batch conversion
  recipes, and staged asset-generation requests.
- Prompt-to-plan, prompt-to-asset, prompt-to-command, and prompt-to-action workflows that call explicit VibeStudio tools.
- Supervised agentic loops for gather context, plan, review, stage, validate, and summarize.
- Reviewable proposals before file/package/compiler changes are applied.
- Local project context redaction and consent controls.
- Active global AI-free mode and project-level disablement.

### External Toolchain Layer
- Wrapper model around external compiler submodules.
- Structured command manifests.
- Captured stdout/stderr and parsed diagnostics.
- Per-game build profiles.
- Reproducible output locations and package integration.

## Initial Code Shape
The current scaffold contains:
- `src/core`: manifest and future non-UI studio logic.
- `src/core/game_installation.*`: PakFu-inspired game installation profile
  model, known idTech game keys, engine-family defaults, read-only validation,
  and confirmable Steam/GOG candidate detection.
- `src/core/compiler_registry.*`: compiler tool descriptors and discovery
  results for imported ericw-tools, q3map2, ZDBSP, and ZokumBSP executables.
- `src/core/compiler_profiles.*`: wrapper profile descriptors and command-plan
  generation for ericw-tools, Doom-family node builders, and q3map2, plus
  schema-versioned compiler command manifests.
- `src/core/compiler_runner.*`: compiler execution, re-run, stdout/stderr
  capture, diagnostic parsing, cancellation callbacks, manifest saving, and
  output registration data.
- `src/core/project_manifest.*`: project manifest schema, project-local
  settings/compiler overrides, registered compiler outputs, JSON load/save, and
  health summary checks for the workspace dashboard and CLI.
- `src/core/studio_settings.*`: Qt settings facade for shell state, recent
  projects, recent activity history, accessibility/localization preferences,
  setup progress, editor profile selection, game installations, compiler
  executable overrides, and AI automation preferences.
- `src/core/package_archive.*`: PakFu-derived package/archive interface
  descriptors, read-only folder/PAK/WAD/ZIP/PK3 readers, package entry
  metadata, mount-layer session state, safe normalized virtual paths, and
  selected/all extraction reporting. `PackageIndexLimits` bounds each on-disk
  load: physical/skipped records plus implied folders, encoded directory bytes,
  decoded paths/diagnostics, depth and aggregate chunk fingerprints. Rejection or
  cancellation clears the unpublished index. ZIP uses separate verified devices
  for central records and local headers, preserving one chunk cache per stream;
  it never retains the complete central directory. Index progress reaches the
  existing worker dialog. `PackageArchiveSession` shares the limits across
  at most 64 layers, charging hidden/skipped records, relocation paths and implied
  mount-prefix directories. Candidate admission and cancellable merged-index
  construction precede publication; failures preserve the previous stack. The
  multi-folder texture audit uses this session and rejects a partial catalog on
  index failure. `sourceIndexComplete` distinguishes unavailable roots, including
  maps without external texture references. `loadSnapshot` now admits its
  projected metadata before adoption, preserves the old reader on failure,
  freezes owned text and flattens/freeze-copies known archive backing. It refuses
  self/cyclic or excessively nested adapters. Known backing indexes are admitted
  independently; reported usage retains the larger backing/projection charges.
- Private `core/package_index` shares disk-index record/text/depth admission and
  synthetic-directory identities between opening and writing. Writer preflight
  models exact encoded records and decoded names before source reads/publication;
  ZIP adds its measured ZIP64 extras before retaining central records. Its byte
  sink bounds all output, including headers/tails, by whole fingerprint chunks
  in actual and no-write passes. Duplicate diagnostics share the reader's text
  charge. ZIP wire folder slashes are removed from canonical index identities.
  The save worker exposes cancellable `CheckIndex` record progress. API callers
  may lower `PackageWriteRequest::indexLimits`, never raise the shared ceilings.
  This disk policy is separate from the richer snapshot/history accounting.
- `core/package_snapshot` shares projection admission between general adapted
  readers and `PackageStagingArchive`: 250,000 physical/diagnostic/implied-folder
  records, 64 MiB of source/entry/diagnostic text and 128 path components. It
  preserves physical indexes and admits missing parent metadata before creating
  synthetic rows. Entry and folder preparation report progress and cancellation.
  `PackageArchiveReader::errorString` carries refusal through workers, GUI/CLI,
  validation, comparison, extraction and copy handoffs; a refused view retains
  the document and Undo/Redo. Draft base projections are checked before commit.
  Reader-owned payload backing retains its own policy. Already validated path
  prefixes are sliced directly when building implied folders.
- `PackageStagingModel::preparePlan` publishes derived entries and conflicts
  only after complete replay. Writer, snapshot, manifest, source-verification,
  draft-base and WAD-group preparation forward their existing read controls.
  Private plan work latches cancellation across nested helpers, reports metadata
  progress, and sorts bounded runs with cancellable merges. Folder identity
  emission streams the original sorted JSON-string representation, preserving
  saved-draft fingerprints. Public folder edits accept the same control and
  preserve history on cancellation. Canonical parent indexes avoid repeated
  normalization. `PackageStagingPlanLimits` bounds live entries/conflicts to
  250,000, path/parent index keys to 500,000, and logical text to 128 MiB per
  row or key representation. Admission precedes container growth; deleted slot
  compaction preserves WAD order. Folder identity pre-admits conservative JSON
  text; folder edits check candidate rows/keys before history adoption. Cached
  resource failures expose one blocking diagnostic, while cancellation remains
  retryable. Draft save/Undo preserve recovery; base adoption checks the plan.
  Manifest `summary.planLimits` exposes the caller policy. Individual append and
  unstage operations validate private candidates against the actual InspectPlan
  projection before history adoption. Outermost `endOperationGroup` is fallible:
  it checks the whole pending group once, then commits one history step or rolls
  back all pending changes. Production GUI/CLI/WAD callers handle refusal and
  cancellation; file-import view preparation stays on its worker. Undo/Redo and
  legacy draft loading preserve recovery through unavailable derived views.
  `summary.viewLimits` exposes the model's browser policy, retained across base,
  draft and subset adoption and intersected with stricter reader policies.
  Remaining synchronous GUI preparation, WAD/helper allocation audits and
  independent snapshot/process totals still need work.
- Archive/session indexes publish cached `PackageArchiveSummary` values with
  their winning entries. Shared checked accumulation saturates an overflowing
  `quint64` total and sets an explicit flag; directories contribute no bytes.
  Staging publishes summary counts and before/after composition together with
  each successful plan. Bucket/key text and retained blocker messages use the
  existing per-representation plan budget, and summary preparation has its own
  cancellable metadata phase. Failed preparation caches unavailable statistics;
  edits/history changes invalidate them. Oversized output adds a bounded blocking
  conflict while preserving inspection and deletion/Undo recovery. Opening,
  saved-output reopening and draft save/restore workers warm staging summaries
  before adoption. GUI/CLI expose overflow explicitly; JSON adds exact decimal
  strings and uses null for oversized numeric totals. Repeated getters share
  prepared vectors. The staging list also shares operation/conflict vectors and
  formats visible rows on demand. Aggregate independent-cache memory and full
  performance acceptance remain open.
- `core/package_staging_content` admits a document's retained generated bytes
  (256 MiB) and payload chunk hashes (128 MiB), including the base, active edits,
  undo/redo and open groups. Shared allocations count once. Ordinary appends
  update a conservative cached bound; exact diagnostics and exhausted allowance
  recount retained content. Reclaiming redo/evicted history uses a private
  candidate, so rejection preserves history and revision. Ownership mutations
  invalidate accounting. New generated inputs own their bytes. Draft objects
  share the hash budget by content identity and are admitted before capture;
  the manifest's existing size cap independently bounds its own hashes. These
  are per-model payload bounds, not process-wide or transient-memory quotas.
- `core/package_staging_metadata` admits 1,000,000 retained logical records and
  128 MiB of index/text metadata per document. It charges the base index, entries,
  source/protected paths, WAD records, conflicts, identities, unique operations,
  history deltas/labels and open groups. Unique operation slots stay reserved
  through undo/redo; ordinary appends use a conservative cached bound and exact
  diagnostics/reclamation recount the retained state. Group creation and unstage
  are fallible; GUI/CLI callers report refusal, and selected GUI unstage edits
  adopt one private candidate. Operation strings and group labels own raw-backed
  caller input. Draft save predicts destination metadata before payload work;
  load validates all metadata/history and registry reachability before hydrating
  objects. Actual captured models are checked again before adoption/commit.
  `summary.retainedMetadata` reports the logical counters; these do not cover
  internal plan allocations or aggregate independent snapshots/process memory.
  Plan representations use `PackageStagingPlanLimits`; reader projections are
  admitted separately by `core/package_snapshot`.
- `PackageStagingModel::loadBaseArchive` admits a private candidate under a
  latched read control through base metadata, WAD helpers, bounded-run ordering,
  planned-reader rebasing and final view preparation. Retained entry accounting
  shares `entryMetadata` with usage reports and precedes container growth.
  WAD wire counts obey the disk-index ceiling and remaining retained-record
  allowance before reserve; names are admitted individually. Concrete readers
  use their physical ordinal directly, avoiding redundant pairing indexes.
  Borrowed filesystem adapters capture a native reader during adoption under the
  remaining metadata/hash limits. Bounded keys bind each occurrence once; missing
  or ambiguous matches remain unavailable. Owned generic snapshots retain their
  provider instead of reopening a display path. Virtual WAD providers supply a
  subtype and consistent physical ordinal/type metadata, including literal type
  zero. Drafts materialize decoded bytes and use zero for unspecified wire offsets.
  No staging read reopens deferred filesystem backing. Snapshot verification
  checks stream length and latches cancellation, skipping known unavailable rows.
  Adapter-owned `entries()`/payload allocations and immutable backing remain the
  adapter's responsibility. `visitProtectedInputPaths` enumerates immutable source
  roots; the shared predicate fails closed on incomplete declarations. Staging
  captures bounded roots before payload access. `package_staging_protection`
  freezes source/base/draft identities and undo/redo paths before rebasing or
  materializing a draft. `package_protection` retains normalized absolute roots
  and resolved aliases with count/text/path caps and latched cancellation.
  Extraction and archive export enumerate once per operation and share the
  resulting hash index across destination checks. Matching walks complete native
  path ancestors, then freshly resolved output ancestors. The shared
  `packageResolvedAbsolutePath` resolver checks cancellation between missing
  prefixes, Windows junction segments and suffixes; no check can interrupt a
  blocking filesystem API itself. Output link/containment and publication guards
  remain in force. Declared source metadata must stay immutable during the
  operation; broader concurrent filesystem mutation is not solved by indexing.
  Draft version 4 requires these roots; recovery inspection rejects malformed
  protection metadata. Older drafts migrate only the source provenance they
  already contain. The current draft root is implicit to keep repeated saves
  stable; Save As retains the old backing directory. Foreign absolute paths
  stay literal rather than binding to the current working directory. Archive
  saves protect the lock path, and new drafts cannot enclose protected inputs.
  Source warnings, protected paths and rebased rows remain charged. GUI open, save/reopen and subset APIs forward their controls;
  cancellation never replaces the caller's document or redo branch.
- `src/core/package_staging.*`: staged package add/import, replace, rename,
  delete, conflict reporting, before/after composition, manifest export,
  save-as guards and deterministic PAK/ZIP/PK3/PWAD/IWAD/WAD2/WAD3 writers.
  `streamEntry` shares verified positional content with planned readers,
  writers, staged comparison and manifest hashes. Generated operation metadata
  uses the same cancellable reader and per-call digest cache. ZIP measures
  size/CRC before a second verified
  pass; the incremental deflater keeps bounded payload buffers. Save byte/phase
  callbacks drive the GUI worker without changing CLI output schemas. Optional
  manifest content is prepared before atomic source replacement; publishing its
  file after commit can report a warning. Plan/directory/JSON metadata remains
  proportional to entries.
- `src/core/package_compare.*`: entry-by-entry package comparison, case-folded
  occurrence pairing, added/removed/changed/identical/case-only results,
  size/SHA-256 content decisions, positional streaming reads and text and JSON
  reports. A shared hash consumer checks declared lengths and cancellation in
  bounded chunks for every reader and staged plan, exposing source/path/byte
  progress. Interrupted rows are excluded from partial counters and digests.
- `src/core/package_content.*`: immutable full-file and 64 KiB chunk SHA-256
  identities, cancellable capture/verification and a read-only `QIODevice` that
  verifies each chunk before returning bytes. Accepted file imports also retain
  an independent temporary copy through shared `PackageContentStorage` ownership;
  history and background readers keep it alive. Independent reads own separate
  handles and bounded buffers. The default temporary alias resolves to a real
  directory; asynchronous single-file cleanup rejects links and changed metadata.
  Archive/folder sources and read-only CLI plans retain original source identities;
  recalculating a plan cannot replace them with current metadata. Saves verify
  complete sources before writing and publication, including folder membership.
- `src/core/package_import_store.*`: managed working-import sessions, checksummed
  byte/file reservations, cross-process store locking, lifetime-held session leases,
  bounded inventory and reviewed crash-orphan discard. GUI/CLI use the same
  service; inspection and dry runs create no locks. Normal cleanup stays queued
  until the last document/history/reader reference releases its content.
- `src/core/package_validation.*`: streaming integrity validation with bounded
  payload buffers, per-entry CRC/size checks and SHA-256 evidence, cancellation,
  optional payload budgets and shared CLI/GUI reports. Complete source identity
  checks cover non-payload bytes and empty packages; JSON reports payload and
  source verification bytes separately. Skipped loader records block save;
  ambiguous name-based edits of repeated records are blocked without dropping
  occurrences. Occurrence selectors preserve identity through editing and draft replay; map/GL rename and complete group deletion now share a reviewed atomic service; dependency-aware rewrites remain open.
- `src/core/package_preview.*`: shared read-only preview model for package
  entry text/script samples, image dimensions/format/palette metadata, native
  model metadata, audio metadata/waveform summaries, and binary samples.
- `src/core/asset_tools.*`: image conversion, texture metadata, MDL/MD2/MD3
  metadata, audio metadata/WAV export, local text highlighting/diagnostics, and
  compatibility includes for project text search.
- `src/core/project_text_search.*`: bounded saved-file and live-snapshot scans, literal/whole-word
  matching, file globs, cancellation, byte-preserving replacement proposals,
  source-hash preflight, and atomic per-file application. The existing asset
  CLI commands and `src/app/project_search_panel.*` share the service. The panel
  owns value-only worker snapshots; `src/app/code_search_actions.cpp` supplies
  project context, tab identity/revision/hash validation, navigation, activity,
  undoable unsaved buffer edits, and post-write reloads. Core defaults reject
  plans containing buffers; only an editor host can defer them for GUI-thread
  application after disk commits. The shared text codec preserves exact ranges
  and untouched separators when preparing disk bytes.
  See [Project Search](PROJECT_SEARCH.md) for bounds and integration gaps.
- `src/core/audio_clip.*`: bounded floating-point PCM documents, frame-range
  processing, empty/silent creation, range replacement, additive mixing, envelopes,
  transactional package handoff, and default PCM16 delivery.
  `audio_export` owns atomic WAV saves, integer 8/16/24/32-bit and exact float32
  encoding, deterministic optional TPDF dither, extensible headers, and writer locks.
  `audio_delivery` plans fixed game sound formats, converts a separate float copy,
  encodes DMX with checked padding/minimum length, and shares atomic output and
  transactional WAD/WAV staging between the GUI and `asset audio-export`.
  `audio_level` validates stock Quake II/III speaker plans, engine path limits and
  bounded legacy PCM delivery. `stageLevelSound` prepares value copies and commits
  package staging and one map entity together in memory; undo and save stay owned
  by each document. `AudioPlacementDialog` reviews the shared plan, and editor
  handoff checks package reload/revision and map load/revision tokens before and
  after conversion. `map place-sound` validates an existing package sound through
  the same service before writing a separate map. Dependency review honors exact
  speaker lookup roots. Other game/mod sound entities use normal entity tools.
  Preparation is cancellable; file commit is a separate short worker operation.
  `audio_analysis` computes read-only selection/channel sample statistics with
  compensated double-precision sums and bounded cancellation intervals.
  `AudioAnalysisDialog` and `asset audio-analyze` share the report; silent dBFS
  and absent frame positions are JSON null. Pinned libebur128 adds integrated
  loudness; the existing r8brain converter measures true peak in streamed double
  precision. Explicit zero boundaries include both interpolation tails, while
  original loudness samples preserve absolute gating and exact range duration.
  Bounded worker chunks share a lock across all libebur128 calls because
  initialization writes global constants. True-peak output is not retained as
  an enlarged sample document.
  `AudioChannelMapDialog` and CLI `--channel-map` supply explicit surround roles;
  mono/stereo have defined defaults. Unavailable/disabled/below-gate results
  carry status and text, with absent numeric values represented as JSON null.
  Analysis creates no undo revision and roles are not persisted in the document.
  The Audio browser uses `packagePlannedArchive` to adapt an immutable
  `PackageStagingArchive` inspection view through `PackageArchive::loadSnapshot`,
  so unrelated save conflicts do not hide readable sounds. Listing, preview, audition, browser export,
  and editor reopening consume the same pending bytes. Plan changes invalidate
  cached playback even when the virtual path is unchanged.
  `decodeAudioClip` reuses the native readers in `asset_tools.cpp` with stricter
  complete-container checks. `audio_decode` adds bounded dr_libs MP3/FLAC and
  Xiph Vorbis decoding with container/frame validation, allocation budgets,
  cancellation, native-rate float output and explicit omitted-metadata warnings.
  Xiph's unchanged C sources use a forced-include allocator adapter and guarded
  per-import regions; no C++ resource owners cross a decoder allocation-failure
  jump. Ogg channels are reordered into WAVE/FLAC order. Browser WAV export uses
  the same decoder/writer on a worker; its immutable archive snapshot and atomic
  commit boundary preserve source files and report actual completion.
  `src/app/audio_playback.*` owns a generation-checked audition lifecycle with
  finite loading/buffering timeout, retained playhead, live/paused seek, and
  retryable failure. Its Qt adapter owns each player and immutable media buffer,
  chooses the default output per session, monitors device removal, and retires
  players outside backend callbacks. Tests inject events without opening devices.
  Selection encoding remains on the editor worker; Stop and browser handoff can
  cancel preparation. The browser uses this same controller with native media,
  backend duration updates and millisecond seeks. `audio_browser_worker.*` owns
  separate preview and audition queues, each with one active snapshot and one
  replaceable pending request. Package revisions and exact occurrence indexes
  reject stale results; streaming reads verify source integrity before playback.
  Both paths coordinate mutually exclusive audition, including queued work.
  Browser previews read at most 64 MiB; larger sounds sample a 64 KiB header and
  suppress partial waveforms. Audition input/output is capped at 128 MiB. WAV
  precision is preserved, DMX is widened to PCM16, and compressed bytes retain
  their native codec. Header/analysis phases check cancellation between phases.
  `src/app/audio_editor_dialog.*` owns selection,
  bounded undo/redo, optional edited playback, and value-only background workers.
  Edit/no-op comparisons run in cancellable sample blocks on the worker and use
  exact float bits. Unchanged edits retain the existing state/cache. Worker
  exceptions discard prepared results and report a recoverable diagnostic while
  preserving the document and history. The 32-state/256-MiB history budget counts
  each retained state conservatively; current, clipboard and in-flight buffers
  have their own bounded lifetimes and are outside that history total.
  `audio_project.*` adds lossless, versioned, checksummed float32 documents,
  atomic hash-guarded saves, and bounded local recovery files. The dialog's
  `AudioRecoveryWriter` serializes background checkpoints and retires stale
  in-flight copies; export/staging never mark an edited project saved.
  `audio_recovery_store` adds bounded inventory verification, 32-copy/512-MiB
  retention limits without eviction, live editor leases, and digest-checked
  explicit discard. The asynchronous recovery manager and `asset audio-recoveries`
  share this service; restored drafts protect both provenance and checkpoint paths.
  `AudioRecoveryDiscovery` uses the same bounded scanner without opening payloads
  at session startup. `audio_recovery_actions` connects its cancellable worker to
  the existing notice bar, queues behind crash notices, and supplies File/command
  recovery plus synchronized Getting Started preferences. The selected settings
  profile owns the default recovery folder; an explicit recovery-root override
  takes precedence. Notification and checkpoint preferences are independent.
  `asset audio-edit`, `asset audio-new`, and `asset audio-project` call the same core
  services. GUI editors share an application-local float clipboard; format
  mismatches fail without modifying the destination or system clipboard.
  `audio_markers` owns bounded typed cues/forward loop, RIFF metadata, and
  structural-edit/SRC transforms. `AudioClip` carries markers through history,
  clipboard, native version 2 saves, and recovery. `AudioMarkersDialog` stages
  changes until acceptance; marker-only revisions share sample/cache storage.
  Waveform marker state is separate from the immutable sample cache. CLI
  `asset audio-markers` uses the same validation and separate-output services.
  Delivery reports standard, Quake/II legacy, or omitted marker behavior.
  `audio_waveform` builds an immutable multilevel extrema cache on workers;
  `AudioWaveformView` queries exact visible ranges, draws sample points when
  zoomed in, and exposes frame-based selection, navigation, and an overview.
  Its cache occupies about 1/16 of sample storage and counts toward undo memory.
  `audio_resample` wraps pinned r8brain-free-src for worker-side linear-phase
  conversion, retaining channel timing/headroom and mapping selection by time.
  Integer frame math, input/output processing bounds, and cancellation are shared
  by the CLI and GUI. The third-party headers are isolated from public APIs.
  See [Audio Editor](AUDIO_EDITOR.md).
- `src/core/audio_take.*` owns append-only, new-only `.vstake` capture journals,
  chained block checksums, verified-prefix inspection and bounded channel/range
  materialization. `app/audio_capture` separates the input owner from storage
  using a preallocated sixteen-block queue; overflow or input/storage failure
  stops capture and retains earlier verified blocks. The QAudioSource adapter
  opens only on explicit Record after permission; Stop cancels pending access.
  `audio_take_dialog` exposes arming, frame/peak/queue status, prefix acceptance
  and compensated placement, then enters normal session import/undo/recovery.
  `cli/audio_take` shares verification and native/float-WAV export. Take paths are
  user documents outside checkpoint eviction. Session media stays in memory;
  synchronized overdubbing and monitoring use the duplex path below. Automatic
  take discovery remains open.

- `src/core/audio_duplex.*` prepares a device-independent pass with explicit
  armed channel maps, timestamp-to-frame placement, punch cropping and dry
  multi-arm capture. Live renderer mode exposes physical graph latency and
  feeds monitoring into the existing track/bus/master graph. A bounded SPSC
  frame ring in `audio_duplex_queue` publishes complete arm sets to the recording
  disk worker without callback allocation, locks, I/O or notifications.
  Synthetic completed/interrupted journals use the ordinary take reader and
  source importer. `app/audio_duplex_device` binds a pinned private PortAudio host
  through an injectable API seam: explicit enumeration-scoped selections,
  interleaved float input/stereo output, variable callbacks, atomic Stop, drain
  status and synchronous callback shutdown. WASAPI patches preserve packet QPC,
  dropout/silence semantics and bounded output drain; CoreAudio xrun consumption
  is atomic. `app/audio_recording` adds permission/playback-acknowledgement gates,
  preparation of every journal before input, bounded telemetry, a progress watchdog
  and asynchronous finalization. `core/audio_recording` owns new-only `.vsrecord`
  plans/receipts and independent per-arm verification, also exposed by
  `asset audio-recording inspect`. `core/audio_recording_import` validates reviewed
  hashes, ranges/channels and targets, clears old clip windows before importing,
  then produces one grouped session snapshot. `app/audio_recording_dialog`
  provides scrolling Record/Review forms. Session adoption checks the captured
  revision and creates one undo step after background waveform preparation.
  Browser/waveform/session playback acknowledge output release before capture.
  CLI import uses the shared service and guarded writer. Physical acceptance,
  open-ended loops and dedicated comp lanes remain open. Dry-input/output meters use fixed
  telemetry. `audio_loop` separates authored loop positions from the advancing
  DSP clock; bounded recording loops keep inserts, monitoring and route delays
  continuous. Per-arm journals concatenate passes and version-2 plans preserve
  the count. Review and version-2 CLI import select pass-local ranges through
  the same importer. A pure comp planner validates repeated-pass sections and
  after-cut crossfade handles; version-3 review and the Qt queue share it.
  Selected ranges batch-decode per journal after folder inspection and become
  ordinary clip/fade snapshots with cut provenance and one-step undo. Native
  alternate take lanes remain open. A temporary audition snapshot shares the
  importer and compensated playback worker; isolated mode removes backing clips
  while preserving the mixer. Playback handoffs, cancellation and close/import
  shutdown are acknowledged before devices or parent adoption proceed. CLI
  preview renders the same span through guarded float32 mixdown.
  `core/audio_recording_review` reads bounded v1/v2/v3 review JSON, resolves
  recording paths relative to its file, verifies selections and publishes
  portable review files through `audio_publication`. The native queue restores
  every selection; async Open/Save and CLI save-review share the service and
  output revision guards. Review files are separate from native session clips; see
  [Duplex recording integration](AUDIO_DUPLEX.md).

- `src/core/audio_session.*` owns immutable mono/stereo source snapshots,
  64-bit region descriptors, gain/pan automation and deterministic stereo block
  mixing. Prepared renderers validate once, preserve float headroom and can seek
  far timeline positions without allocating intervening silence. `renderInto`
  takes caller-owned float output/double scratch; `renderBlock` wraps the same
  implementation for offline callers. `core/audio_transport` supplies the exact
  frame clock, ranges, loop/seek/pause and master metering with bounded buffers.
  It has one owner thread and performs no file/device work. `app/audio_session_playback`
  marshals commands and generation-checked status through a dedicated Qt worker;
  the device adapter opens QAudioSink only on Play, retains short/partial writes,
  resets queued lookahead on seek and waits for finite output to drain. Device
  processed-time estimates are distinct from the exact render clock. The existing
  QMediaPlayer transport remains for waveform/browser preview. Disk streaming,
  synchronized monitoring/overdubbing and hard-real-time device acceptance remain open.
  `audio_session_io` stores checksummed `.vssession` documents with embedded
  `.vsaudio` media and streams WAV through the existing precision encoder.
  Atomic output, digest checks, source protection and cancellation are shared by
  `cli/audio_session` and `app/audio_session_dialog`. The dialog has bounded
  descriptor/media history; waveform caches retain only sources referenced by
  the current state or history. Worker results are adopted only on success.
  `AudioSessionTimeline` paints cached extrema and emits selection/move requests;
  the standard Qt track tree and inspectors supply named keyboard alternatives.
  `audio_session_actions` connects waveform snapshots to sessions and sends a
  bounded mix back to the existing editor for analysis, final delivery, package
  staging and level placement. Documents retain separate save/undo boundaries.
  Session checkpoints use `audio_recovery_store`'s separate bounded envelope and
  the existing `AudioRecoveryWriter`, parameterized by document kind. A one-second
  non-restarting timer coalesces GUI edits; the worker retains one pending snapshot.
  Retirement cannot resurrect an in-flight draft, and retaining a reviewed idle
  copy releases its lease without deleting it. Both editors share recovery budgets,
  settings, startup discovery, verification and exact-kind discard. Restoration
  creates an unsaved session through the same checksum decoder used by the CLI.
  `audio_routing` validates a bounded DAG of stereo strips, buses and pre/post
  sends, including disabled edges. Render scratch separates pending and audible
  solo paths until a selected strip is crossed, preventing parallel-route leaks.
  Polarity, swap, gain/balance and bus automation share playback and export.
  Version-3 native sessions persist routing and effects; version 1 migrates to
  direct master, and versions 1/2 receive empty effect chains.
  `audio_tempo` prepares bounded tempo/meter segments outside rendering, using
  compensated fractional-frame accumulation and logarithmic position lookup.
  Musical ticks use 960 per quarter note; BPM counts quarter notes while notated
  beats follow the meter denominator. Meter entries anchor to bar numbers, tempo
  entries to ticks. Native v5 timing maps migrate v1–v4 constant defaults.
  `AudioTempoDialog`, the arrangement ruler/navigation/snap controls and CLI
  map/position commands share these services. Normal edits stop playback and
  retain sample-anchored sources, regions, fades and automation through undo and
  recovery. Ramps, metronome, MIDI and musical media anchoring remain open.
  `AudioRoutingDialog` stages one normal session edit with validation, undo and
  recovery, and the CLI translates routing/send operations to the same edit.
  `audio_effects` owns parameter schemas, strict chain validation and prepared
  stateful processors. Track/bus inserts run after fader/pan; master inserts
  follow master gain. Nonlinear shared-bus solo uses separate audible/combined
  histories and their output residual, preserving selected downstream paths.
  The renderer retains contiguous history and resets on discontinuity/failure;
  transport explicitly resets at stop/seek or a loop-policy change. Loop repeats
  advance an independent physical clock while `AudioTimelineLoop` maps source
  and latency-corrected automation positions. Live and compensated modes share
  boundary splitting; compensated audition primes wrapped future context once.
  Meters retain unwrapped signal time and histories. Saved tails extend
  default ranges, while explicit ranges start fresh without preroll.
  `AudioEffectsDialog` and CLI effects operations use the same normal edit,
  undo, recovery and publication services. Waveform delivery receives the rendered
  result; source media remains unchanged. Effect state is bounded to 128 MiB.
  `audio_reverb` supplies bounded stereo comb/allpass state with constant-time
  history reset; modulation shares the renderer's sample clock and resets.
  `audio_automation` shares linear/step/smooth evaluation and strict point JSON
  across gain, pan and effect lanes. Effects bind bounded session lanes to
  physical parameter slots during preparation, reserve maximum delay/reverb
  storage, and update coefficients/taps on the absolute sample clock. Native v4
  persists the lanes; v1–v3 migrate to linear gain/pan and empty effect lanes.
  `AudioAutomationEditor` shares one model between its graphical preview and
  native table/fields. Effects and CLI edits preserve/prune stable targets;
  history budgeting includes points, and preset replacement clears old targets.
  `audio_effect_preset` owns nine recipes and strict 64 KiB `.vsfx` JSON. Applying
  a recipe renews IDs and validates the complete session; file values are never
  silently clamped to a different sample rate. `audio_publication` shares the
  existing atomic session/export guards with preset output, including hard-link
  identity. The effects dialog performs preset I/O on a cancellable worker.
  `audio_stems` plans stable filenames, preflights every destination and guards
  existing revisions before a version-1 delivery manifest starts. Render targets
  prune unrelated/downstream graph nodes while preserving ancestor effects,
  automation and nonlinear solo domains. All taps share the original frame range.
  WAVs commit independently, with explicit partial results and bounded manifest
  finalization after cancellation. `AudioWavEncoder` retains deterministic TPDF
  state across successful blocks; cancelled blocks do not advance its sequence.
  GUI and CLI use the same cancellable service and publication guards.
  `audio_latency` computes longest-path insert arrival times and pre/post/main
  route compensation; each solo domain owns preallocated stereo delay lines.
  `audio_lookahead` supplies an original linked sample-peak limiter with a bounded
  monotone future-peak queue. Fixed lookahead is structural, while eligible controls
  bind to the authored output clock. Effects wait for their input latency before
  advancing internal phase/history. The renderer primes fresh ranges in bounded
  private blocks and returns only aligned authored frames, with no earlier preroll.
  Const session reads preserve shared Qt containers during rendering. Validation
  admits combined effect/compensation state below 128 MiB; diagnostic timing is
  separate from native serialization and is carried into mixdown/stem reports.
  Preset asset-browser routing and project libraries remain integration gaps.
  Bulk media relinking, MIDI, plugins, advanced recording and surround routing
  are tracked as integration gaps in the [DAW plan](plans/audio-daw.md).
- `src/core/level_prefab.*` captures complete entity ownership in a bounded,
  versioned `.vprefab` envelope, namespaces internal links through the shared
  target-property vocabulary, transforms isolated geometry with the texture
  mapping service, and pastes one ordinary map undo command. Loose files use
  `PackagePublication`; generated package entries use `PackageStagingModel`.
  `app/level_prefab_dialog` prepares immutable candidates and package/material/
  model/dependency previews on a coalescing worker. The shell guards document,
  selection and asset revisions before accepting them. GUI and CLI share these
  services; no additional dependencies or external code were introduced.
- `src/core/level_primitive.*` generates bounded primitive point sets through
  the shared original convex hull and topology validator in `level_brush`.
  `level_map` owns dialect-aware insertion, source bindings, selection and undo.
  `app/level_primitive_dialog` prepares isolated drafts and material images on
  a coalescing worker, sharing the package resolver and Models renderer.
  `map add-brush` calls the same service. See [Brush Primitives](LEVEL_EDITOR.md#brush-primitives).
- `src/core/level_merge.*` validates exact convex brush unions with bounded,
  cancellable polygon subtraction and explicit per-face material/UV/flag choices.
  `level_map` prepares source-preserving plans and commits one replacement undo
  command with selection restoration. `app/level_merge_dialog` shares immutable
  package/staging assets and the Models renderer; `map merge-brushes` consumes
  the same plan and conflict report. See [Brush Merging](LEVEL_EDITOR.md#brush-merging).
- `src/core/level_surface.*`: immutable batch surface-alignment plans using the
  shared projection/brush solver. `level_map` validates source bindings and
  commits each plan through existing undo and source-preserving serialization.
  `app/level_surface_dialog` resolves immutable package/staging material snapshots
  on a worker and renders with Models; `map align-textures` uses the same service.
  Missing required dimensions fail unless the caller supplies an explicit size.
- `src/core/level_materials.*`: bounded material/image and static model resolution
  over immutable package/staging readers, shared by the level camera and
  `map materials`. It reuses shader/image/model parsing and map path rules;
  original texture sizes feed `level_texture_mapping` in `map_preview_mesh`.
  `app/level_preview_worker` coalesces requests, caches unchanged assets and
  prevents retired jobs from publishing. `app/level_preview_actions` connects
  package changes, camera state, status, cancellation and texture tiles.
- `src/core/level_map.*`: Doom WAD map lump reader/writer, Quake-family and
  Quake III `.map` parser, map statistics, texture/material references,
  validation/preflight health, selection/property views, undo/redo edit stack,
  safe movement/entity edits, save-as, and compiler-request handoff.
  Material counts and edits share a surface visitor; summaries aggregate
  distinct spellings without allocating an edit record per surface. Selected
  Doom linedefs resolve sidedef membership in one pass. `levelMapTextureNames`
  supplies sorted, trimmed, case-insensitive names to statistics, textual
  reports and the brush inspector without inserting a UI placeholder.
  A statistics-lines overload formats an already computed document summary.
  The shell passes that stack-owned result through one workbench refresh; it
  retains no persistent statistics cache that could outlive an edit.
- `src/core/entity_definitions.*`: Radiant `.def`/`.qc`, Valve `.fgd`, and
  Quake III `.ent` parsers, `@include` resolution, inheritance folding, project
  definition search paths, map entity validation, and inspector/JSON reports.
- `src/core/model_obj.*`: original bounded polygonal OBJ parser, seam expansion,
  smoothing and concave triangulation, plus verified immutable-package streaming.
  `model_mesh` dispatch, editable import and the CLI share it. Unsupported data
  fails with line diagnostics. `app/model_preview_worker` uses one active and
  one replaceable request for OBJ and native package model geometry, header
  metadata and per-surface material resolution. Cancellation and generation
  checks prevent stale publication. `core/model_archive` shares bounded,
  exact-entry streaming with the CLI and native palette lookup; failed final
  verification never publishes bytes. Native decoding polls through skin,
  triangle, vertex, tag and bounds loops and discards partial results.
  The editor uses its existing document worker. MTL shading conversion is open;
  no new dependency is linked.
- `src/core/model_mesh.*`: format detection, the format catalogue
  (`modelFormatCapabilities`), companion-file sources, package skin resolution,
  frame-name-inferred animations, summary lines, and single-frame Wavefront OBJ
  export. Each native decoder is its own `src/core/model_format_*.cpp` behind
  `model_formats_p.h` (MDL, Hexen II MDL, MD2, FM, MD3, MDC, MDS, MDM/MDX, MDR,
  Ghoul 2, IQM, MD5, LWO, ASE, Half-Life MDL, KVX); `core/model_skeleton` holds
  joints, skinning, clips, baking into frames and bind-pose re-binding, and
  `core/model_md5`, `core/model_iqm` and `core/model_ase` write those formats.
  See [Native Model Formats](MODEL_FORMATS.md). `core/model_document` preserves
  all poses and authoring metadata in schema-3 sources (schema 4 for native MDL,
  5 for static collision, 6 for saved clip FPS, 7 for animated collision,
  8 for skeletons)
  and shares undo/recovery/CLI
  operations. MD2 GL/indexed consistency warnings originate in the decoder so
  package-browser and file imports use the same authoring refusal. `core/model_export`
  writes MD2 and MD3 with target limits and quantization validation. `core/model_mdl*`
  preserves exact indexed members, groups/timing and header settings, supplies
  candidate edits and schema-4 storage, and writes original-renderer-compatible
  MDL with seam packing and quantization diagnostics. Its image imports reuse
  the bounded idTech decoder without RGB reconstruction. `cli/model_mdl` and
  the Quake MDL inspector share the document services. `core/model_mdl_playback`
  samples software-Quake cumulative timing or original-GLQuake pose/skin schedules
  for both CLI and viewport. Selected skin pixels decode once on the worker into
  at most 64 MiB of shared opaque images; transport ticks do no pixel conversion.
  Model and UV raster requests retain immutable snapshots and coalesce updates.
  Native time/phase is session state and is retired after document changes. Mesh
  handoff prepares bytes on the document worker before publishing the package plan.
  `core/model_skin_source.*` streams exact immutable package occurrences into
  indexed MDL skin inputs, with bounded reads, strict palette lookup and source
  receipts. GUI/CLI use the same service; the metadata-only skin picker consumes
  the shell's staged package snapshot, and one successful import is one document
  undo step. Saved `.vibepackage` plans provide the equivalent CLI source.
- `src/core/document_watch.*`: role-tagged path registration, SHA-1 content
  fingerprints that fall back to size and modification time above a size limit,
  coalesced change events, and `poll()`/`pollAt()` entry points.
  The class has no `Q_OBJECT` and no signals, so `src/core` stays a QtCore
  static library with no moc step; the app layer owns the timer that calls
  `poll()`.
- `src/core/parser_fuzz.*`: seeded xorshift64* corpus generation, seven byte
  mutations, truncation boundaries, and reproducible case ids for the parser
  fuzz tests.
- `src/core/advanced_studio.*`: idTech3 shader script parser/editor model,
  mounted package-reference validation, Doom/Quake sprite workflow planning,
  source workspace indexing, extension manifest/trust/sandbox command plans,
  and staged AI creation proposal helpers.
- `src/core/localization.*`: shared 20-language target registry, locale
  normalization, pseudo-localization, right-to-left smoke metadata, `QLocale`
  formatting samples, translation expansion stress data, and Qt TS catalog
  status reports.
- `src/core/studio_semantics.*`: shared non-color-only status chip semantics,
  default keyboard shortcuts (marking which ones fire only on their own
  surface, so two surfaces may share a sequence), conflict checks, and
  command-palette metadata for
  shell and CLI surfaces.
- `src/core/studio_query.*`: the filter query language the studio's lists and
  the CLI share (`key=value`, `key:text`, `key!=value`, `key<n`, `key>n`, size
  suffixes, and plain words): the Levels Objects filter and `map find` over map
  objects, the Packages filter and `package list --where` over entries, the
  Textures, Models, and Sounds filters over assets, the Shaders filter over
  shaders, and the Code page's Files tree over project files.
- `src/cli`: diagnostics and automation entry points, including the active
  subcommand router, localization reports, diagnostic bundle export, JSON
  output envelopes, and stable exit-code contract.
- `src/app`: Qt Widgets studio shell, including the software-rendered map and
  model viewports. The build's moc step covers app headers only. The app
  sources build as the `vibestudio_app` static library; the `vibestudio`
  executable is `src/main.cpp` and `src/cli/cli.cpp` linked against it, which
  lets a GUI test construct and drive the real `ApplicationShell`.
- `src/app/level_object_model.*` and `level_object_list.*`: immutable map-row
  snapshots, on-demand labels, stable object-ID lookup and the native Levels
  list. A single worker coalesces queries, uses cancellable core property
  indexing and rejects obsolete source/query/visibility results. Selection and
  frame/context actions still route through the shell's shared map services.
  Uniform two-line rows use Qt's [list-view model API](https://doc.qt.io/qt-6/qlistview.html),
  with complete source text retained for accessibility and tooltips.
- `src/app/studio_theme.*`: design tokens for every theme, density, and text
  scale; the application palette and generated stylesheet; Fusion plus a proxy
  style for check and radio indicators. `studio-theme-smoke` holds each theme
  to WCAG AA contrast.
- `src/app/studio_icons.*`: the painted, theme-aware icon set behind every
  menu, tool bar, rail, and page action.
- `src/app/studio_layout.*`: shared work-surface parts: the mode rail, page
  header, empty state, card, dock title bar, eliding label, panel tabs, and
  icon scaling.
- `src/app/studio_docks.*`: where the panels dock whichever way the interface
  reads. `QMainWindow` keeps its dock areas by side, so the shell opens panels
  on `trailingDockArea()` and keeps its saved window state in left-to-right
  terms, mirrored for a right-to-left window by swapping the left and right
  dock area records in the bytes `QMainWindow::saveState()` writes.
- `src/app/code_editor.*`: the Code page's plain-text editor with a
  line-number gutter and current-line band drawn from the theme.
- `src/core/code_files.*` provides a bounded, cancellable filename and metadata
  catalog shared by the Code Files panel, Go to File, and the `code files` /
  `project files` CLI. The optional asset scope classifies supported media and
  studio documents using the existing surface format registry. Filename language
  classification is shared with syntax highlighting; source-directory exclusions
  are shared with the symbol index. `app/code_files_worker.*` coalesces background
  scans and suppresses retired results. `app/code_files_panel.*` applies rows in
  event-loop batches, caches query properties, and owns progress/cancellation and
  browsing-state preservation. Saves, project replacement and watched source
  changes invalidate the metadata catalog; ordinary mode changes reuse it.
- `src/app/quick_open_catalog.*` validates recent paths and gathers the shared
  project catalog and an immutable package metadata snapshot on a cancellable
  worker. Request generations suppress retired results; no archive payload is
  read. `QuickOpenDialog` ranks large lists in bounded event-loop batches and
  shares that ranking with the symbol picker. Project/package context changes
  retire the picker; the shell still owns opening, edit guards and surface routing.
- `src/app/code_index_worker.*` runs the shared bounded source scanner in one
  cancellable thread, coalescing pending requests and discarding obsolete results.
  `src/app/code_index_actions.cpp` owns progress, Activity, live Code snapshots,
  result filtering and guarded deferred navigation. `indexCodeWorkspace` in
  `advanced_studio` also serves the CLI, using the text-document codec and explicit
  partial-result metadata. Indexing never writes source files.
- `src/core/language_server.*`: an asynchronous local stdio LSP client shared by
  the Code workspace and `code language-server`. It owns bounded JSON-RPC framing,
  process lifecycle, UTF-16 negotiation, versioned document synchronization,
  diagnostics, definition/completion/reference/hover/signature requests, timeouts and cancellation. It refuses server
  edits and executes no server-requested commands. `code_language_panel.*` owns
  inert tool preferences, explicit connection, progress and logs.
  `language_diagnostics.cpp` validates push/pull ranges and bounds, schedules at
  most four document pulls, retains unchanged-result caches and retries server
  cancellation twice. Synchronization, dependency refresh and successful-save
  notifications share the same document identities. Problems and CLI consume
  explicit pending, incomplete and failed diagnostic states.
  `code_language_actions.cpp` supplies current project buffers and guards source
  locations against stale tabs, revisions and caret positions. Project changes
  stop the connection. See [Local Language Services](LANGUAGE_SERVICES.md).
- `src/core/language_completion.*` validates bounded completion lists/defaults,
  UTF-16 ranges, source hashes and non-overlapping edits for the GUI and CLI.
  `src/app/code_completion.cpp` presents semantic and local suggestions through
  the editor's Qt completer, with debounce, cancellation and revision/caret guards.
  The shell applies a chosen item's same-document edits as one undo block; normal
  unsaved-document recovery, source indexing and search invalidation then apply.
  Completion resolve preserves materialized list defaults and opaque data, permits
  only advertised metadata/additional edits to change, and reparses the complete
  item against the same source. The popup resolves highlighted entries, cancels
  superseded requests and holds early acceptance until the full edit set validates.
  The CLI shares explicit indexed resolution without writing files.
  `completion_snippet.*` parses and expands bounded field syntax and document
  variables into UTF-16 ranges, including related-edit offsets. `code_snippet.cpp`
  owns local field navigation, nested retirement, linked edits and normal Undo;
  `code_snippet_bar.cpp` exposes native controls through the shared Code workspace.
  All non-session document edits retire tracking. Ordinary single-edit completion
  inside a field retains linking; another snippet or related-edit completion
  starts a new context. No snippet parser reads the environment, clipboard or files.
- `src/core/language_references.*` prepares bounded reference ranges and previews
  from immutable open buffers or project files through the shared text codec.
  The CLI and Code Search Results panel share path validation, source hashes,
  deduplication, limits and partial-result reporting. GUI preparation is asynchronous;
  cancellation spans the protocol request and background preview worker. Existing
  tab/revision/hash checks guard navigation. Semantic results never form a replace
  plan, and neither references nor completion bypass normal save, recovery,
  compilation or package staging.
- `src/core/language_workspace_edit.*` validates bounded WorkspaceEdit text
  changes, correlates synchronized buffer versions and prepares exact
  source-hashed project replacement plans. `code_rename.cpp` orchestrates provider
  preparation through `language_rename.*`, the native name dialog and Search Results review. The existing
  asynchronous replacement writer and GUI document-undo host apply the edits;
  the CLI binds writes to a deterministic whole-plan SHA-256. Resource operations
  and server-initiated edits remain unsupported.
- `src/core/language_code_actions.*` parses bounded action lists and resolved
  literals while preserving identity and opaque provider data. Current versioned
  diagnostics supply bounded action context. `code_actions.cpp` coordinates the
  native `code_actions_dialog.*` picker, cancellation and context guards, then
  routes text edits through `language_workspace_edit` and Search Results. The CLI
  shares list/resolve/preview behavior and whole-plan hash guards. Command-bearing
  actions remain unavailable; no arbitrary server command is executed.
- `src/core/language_formatting.*` validates all-or-nothing document/range edit
  arrays and previews output against normalized source hashes. `code_formatting.cpp`
  connects asynchronous requests to cancellable Activity/progress and one-block
  document undo. Changes retire on document/version/context mismatch; recovery,
  indexing, normal Save, compiler and package boundaries remain shared. CLI
  formatting previews by default and uses `saveTextFileEdits` for explicit,
  saved-hash-guarded atomic writes with exact untouched line separators.
- `src/core/language_hover.*` parses bounded hover content and optional UTF-16
  ranges against a hashed source snapshot for GUI and CLI. `code_quick_info.*`
  supplies a persistent, selectable Qt documentation pane and short pointer
  hints. Tab/revision/version/context guards retire stale replies. Markdown uses
  a resource-free document, raw HTML is disabled, images are removed and links
  cannot activate. Rendering reuses studio typography. Explicit requests use the
  normal command/shortcut registry, loading/cancellation controls and no edits.
- `src/core/language_signature.*` validates bounded overloads, UTF-16 parameter
  labels, active indices and documentation against a source snapshot. The shared
  client negotiates trigger/retrigger context; `code_signature_actions.cpp`
  debounces edits/caret changes and cancels obsolete replies. `code_signature_panel.*`
  presents a compact inline signature, native overload selection, textual and
  non-color emphasis for the active argument, and optional documentation. The
  selection survives retriggers; document/version/hash/caret guards prevent stale
  display. `language_documentation.*` shares resource-isolated Markdown/plain-text
  rendering with Quick Info. GUI hints and CLI inspection never modify sources
  or bypass normal Undo, recovery, save, compilation or package staging.
- `src/core/text_document.*`: bounded strict UTF-8/BOM-marked UTF-16 decoding,
  per-line separator preservation, immutable source snapshots, and hash/path
  checks before atomic saves. Code tabs and `code text-info` / `code text-save`
  share this service; editor serialization uses raw document text to retain
  non-breaking spaces and Unicode soft breaks. See [Code Editor](CODE_EDITOR.md)
  for matching rules, mixed-ending limits, and cross-workspace integration.
- `src/app/code_document_actions.cpp` owns untitled Code documents, reviewed
  Save As and restoration, retaining QTextDocument identity and undo history.
  `src/core/text_recovery.*` stores bounded, checksummed text checkpoints;
  `src/app/code_recovery.*` queues immutable snapshots off the UI thread and
  retires writes after save/discard. Recovery source paths are provenance only.
  New/copy/recovery CLI commands use the same core destination checks.
- `src/main.cpp` also accepts `--open <path>` (routed like a drop onto the
  window) and `--ui-snapshot <dir>` with an optional `--ui-snapshot-size WxH`,
  which renders every work surface to PNG and exits, followed by the Activity
  panel, the Build page's Toolchain tab, the navigation rail opened over the
  Workspace, and the command palette floating over it.
- `src/app/studio_runtime.*`: session logging, crash-report capture and
  parsing, session markers, and previous-session detection. `src/main.cpp`
  installs session logging and, for an interactive run, crash capture (per the
  `diagnostics/crashReports` preference) before the shell exists; the shell's
  `beginSession()` then offers a crashed session back through its notice bar.
  The self-test and snapshot runs arm neither, and a run with `--settings-file`
  keeps its crash folder beside that file. `studioProcessIsRunning()` is shared
  with the session record, which names the process that owns it.
- `src/tests`: smoke tests for core services, package flows, compiler flows,
  asset tooling, level maps, entity definitions, model meshes, package
  comparison, document watch, parser fuzz corpora, corrupt fixtures, studio
  runtime, Advanced Studio services, UI primitives, and the studio theme and
  icon set. `shell-interaction-smoke` (Qt Test, offscreen) links the app
  library, builds its fixtures in code, and drives the window with key presses,
  row activations, and dialog answers; it runs itself as a stand-in compiler to
  exercise a real build.
- `scripts/english_plurals.py`: generates and checks the English plural forms
  in `i18n/vibestudio_en.ts`, so `%n item(s)` reads as "1 item" and "5 items".
- `scripts/package_portable.py`, `scripts/generate_offline_guide.py`,
  `scripts/validate_packaging.py`, and `scripts/validate_release_assets.py`:
  portable package staging, generated offline docs, license/checksum bundling,
  fixture smoke tests, timing checks, and release asset gates.
- `external/compilers`: imported compiler submodules.

## Design Principles
- `core/level_brush` reconstructs solved vertex/edge/face topology and validates
  convex component edits. `app/brush_editor_dialog` supplies a local draft,
  component view and the shared model surface renderer. The map document
  commits the parsed result as one undo command, retaining the owning entity,
  source material syntax and package dependency paths. GUI and CLI use these
  same services; no additional dependency or renderer is introduced.
- Keep project state central and shared across tools.
- Keep `src/core` free of generated sources: core services stay plain QtCore
  types that the app layer drives, so the core library builds without a moc
  step and its state machines can be tested without an event loop.
- Keep editor surfaces specialized, but not isolated.
- Keep layout/control profiles adaptable without fragmenting the underlying editor model.
- Think from the user's current uncertainty: show what VibeStudio is doing, why it is waiting, and what the next safe action is.
- Keep modern UX clean at the surface while preserving detailed inspection paths for expert users.
- Treat efficiency as a product feature: reduce setup time, repeated work, context switching, uncertainty, and waiting.
- Treat accessibility as a product feature: the studio must support high visibility, scaling, keyboard use, screen readers, TTS, and non-color-only state.
- Treat localization as architecture: every user-visible string, layout, status, and setup step should be designed for translation and locale-aware formatting.
- Treat initial setup as a tailoring workbench, not a tour.
- Use graphical representations to communicate real structure and status, not as decoration.
- Treat every parsed byte as hostile: bounds-checked reads, fixed limits on
  counts and sizes, named fixtures for known corruption, and a deterministic
  fuzz corpus in the test suite.
- Treat packages as editable project containers, not just files to extract.
- Make compiler runs reproducible and inspectable.
- Make prompt-based and agentic AI actions auditable, reversible where practical, connector-neutral, and available through the same command services as manual workflows.
- Keep AI optional while designing the architecture so AI-assisted users can move much faster.
- Keep CLI parity in mind for every feature that can reasonably run headless.
- Keep borrowed code credited at the point of use and in repository-level credits.

### UDMF document and module handoffs

`level_udmf` owns immutable TEXTMAP bytes and scalar/block spans plus a native
Doom projection. `level_map` owns atomic edits, scene guards, exact text undo,
WAD serialization and recovery. The GUI worker and CLI call the same property
transaction. Standard UDMF transforms encode validated native deltas through the
same span writer, with a document-sized edit budget and exact raw undo. Scene
guard identity stays attached to the active document through publication;
cancelled history preparation rolls back the candidate. Move/rotate/resize now
join mirror/snap in the cancellable placement worker, including rotation previews.
The plan/camera consume native geometry and package materials;
`level_doom_nodes` validates UDMF ZNODES for health, compiler output and launch.
Unknown source fields are preserved while advanced namespace rendering remains
explicitly outside the common-field preview. No separate package writer or
parallel editor history is introduced. See [UDMF authoring](LEVEL_EDITOR.md#lossless-udmf-property-authoring).

## Quake III Native Animation

`core/model_q3_animation` implements bounded native Quake III configuration parsing, export and steady clip sampling. Optional assembly schema 2 bindings select lower/upper parts and native clips; the ordinary assembly sampler, bakes, history and recovery consume that data. GUI and `model assembly` CLI use the same validation and guarded writer. No renderer or dependency change is required. See [the native animation contract](MODEL_ASSEMBLY.md#quake-iii-native-animation).

Assembly schema 3 adds optional per-part skin references, including alongside
native animation. `core/model_assembly_skin` reads loose files or exact package
occurrences through the shared skin reader, prepares complete material mappings,
and changes only the resolved mesh snapshot. Model and skin byte identities stay
separate; preview, bakes and material diagnostics consume the effective materials.
Reference rebasing, recovery, cancellation and source-protected writes stay in
the normal assembly services. [Linked skins](MODEL_ASSEMBLY.md#linked-skins)
document reload behavior and remaining package/level integration limits.

## Native Player Package Publication

`core/model_player_bundle` prepares an owned byte snapshot from resolved native
assembly roles and shared `inspectModelMaterialDependencies` validation. It
bakes local transforms and inherited scale into separate MD3 parts, generates
native skins/configuration, converts the icon through `texture_export`, captures
material dependencies and rechecks closure from the captured bytes. A private
`PackageStagingModel` publishes with input guards and deterministic atomic writes.
GUI workers and `model assembly` CLI share these services, including portable
package drafts. No new renderer, library or build dependency is introduced.
See [publication contracts](MODEL_ASSEMBLY.md#native-player-packages).

## Placed MD3 Appearance Resolution

`core/level_model_appearance` derives a deterministic request/cache key from a
Quake III `misc_model` entity and prepares an immutable static model. It models
the NRC compiler's MD3 slot-zero material normalization, implicit importer skin,
explicit compiler skin and ordered suffix remaps. It records exact skin rows,
hashes and per-surface outcomes; invalid requests cannot publish partial geometry.
`level_materials`, `map_preview_mesh`, the level worker and `level_dependencies`
share this contract. Worker keys include instance ownership as well as appearance
parameters so dependency/detail selectors cannot remain stale across map edits.
The resolver diagnoses the pinned importer's ignored nonzero frame requests;
an Assembly pose bake supplies a separate static derivative. Existing map entity
authoring remains responsible for history and scene locks. No renderer, library
or build-system change is introduced. See [the contract](LEVEL_MODEL_APPEARANCE.md).

### Modeller Profiles

`core/model_editor_controls` defines the modeller's controls profiles as data:
navigation for 3D and orthographic views, selection gestures, transform style,
layout and a command key table, with override diffs, conflict checks and the
`vibestudio.modeller-controls` file format. `core/model_sidebar` arranges and
names the Mesh Editor's sidebar pages per profile family. `StudioSettings`
stores the profile, overrides and sidebar state (`studio_settings_modeller.cpp`).
The Mesh Editor applies them in `app/model_editor_layout.cpp`,
`app/model_editor_profiles.cpp` and `app/model_editor_modal.cpp`;
`cli/model_controls` serves `model profiles`, `model controls` and
`model formats`. See [Modeller Profiles](MODELLER_PROFILES.md).
