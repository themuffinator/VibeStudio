# VibeStudio Offline User Guide (0.1.0-rc1)

This generated guide is bundled with portable release artifacts so a user can
configure, inspect, validate, and troubleshoot the MVP without network access.
It summarizes the release-candidate workflows; the full source documentation
remains in the `docs/` directory.

## What This MVP Is

VibeStudio is currently an integrated foundation for idTech1, idTech2, and
idTech3 projects. It provides a Qt6 Widgets shell, first-run setup scaffold,
project manifests, read-only package browsing and extraction, compiler command
planning/runs, staged package save-as, sample projects, safe AI workflow stubs,
localization smoke reports, diagnostic bundle export, credits visibility, and a
CLI for repeatable automation.

Level editing, static prop design, imported mesh editing, frame animation,
asset previews, shader editing, and package staging are active, bounded workflows.
Professional production acceptance and broader authoring workflows remain
roadmap work. Write/export operations report exact output paths and keep raw
details, logs, and manifests inspectable.

## First Launch Checklist

1. Start VibeStudio.
2. Complete, skip, or resume first-run setup.
3. Choose language, theme, text scale, density, reduced motion, and optional
   OS-backed TTS preferences.
4. Select an editor profile or keep the VibeStudio default.
5. Add or detect a game installation, or defer installation setup.
6. Open or initialize a project folder.
7. Confirm the Activity Center and Inspector show current state and warnings.

All setup choices are stored in normal settings and can be edited later from
Preferences or the CLI. AI-free mode is enabled by default and core workflows do
not require cloud services.

## Finding Your Way Around

One row of chrome sits above every page. The menus lead it, then **Back** and
**Forward** (Alt+Left and Alt+Right, or the mouse's side buttons), then the open
commands. **Search commands** sits in the middle of the window: click it or
press Ctrl+Shift+P, type part of a command's name, and press Enter. **Run Build
Pipeline** and **Launch Game** close the row. Ctrl+P opens Go to File.

The navigation rail on the left switches work surfaces (Ctrl+1 to Ctrl+0). It
rests as icons and shows its labels while the pointer rests on it or the
keyboard is in it; the pin at its foot keeps them open. The current page's icon
takes the accent colour, with a short bar beside it.

Each page's header names the page and what it shows, such as the open map with
its entity and brush counts, and holds the page's actions. The Workspace page's
tiles show that same line for every surface, so it doubles as a map of what is
open. The status bar reports the project, package, game installation,
compilers, and AI mode; each item opens the place that can act on it.

## Familiar Level Editor Controls

Choose **Levels > Controls** or **Settings > Language and Editing > Editor
profile**. The 19 profiles cover VibeStudio, TrenchBroom, GtkRadiant 1.6.0, Q3Radiant,
NetRadiant, NetRadiant Custom, QuArK, Hammer/Worldcraft, J.A.C.K., Sledge, DarkRadiant, Doom Builder
2/X, Ultimate Doom Builder, SLADE, Eureka, Unreal, Unity, Godot and Blender.
They share map documents, undo, assets and compiler services. Choosing controls
does not add another engine's formats or change the map's build target.

**Show Every Gesture and Key** opens a searchable reference with the actual
bindings and any workflow differences. **Layout** offers one plan, one camera,
camera beside plan, or four views; **Follow Editor Profile** restores the chosen
profile's arrangement. User keyboard bindings take priority over profile keys.
Focus loss, hiding a camera or changing profiles ends camera navigation.
Sledge uses Space for temporary plan pan or camera look, Q/E for elevation and
arrows to look; Shift+arrows translate/pan. Z toggles look. Standalone NetRadiant
uses Delete/Insert for zoom, Backspace/Z for deletion and an 8-unit default grid.
Their adaptation notes distinguish remaining upstream behavior differences.

**Controls > Customize Gestures…** edits per-profile plan/camera gestures and
fly/drive/mouse-look keys. Camera Keys offers Profile default, Custom key and
None. Duplicate directions and reserved cancellation/focus keys are refused;
possible camera/command shortcut overlaps have expandable details. Apply
validates and saves the draft; Restore Defaults and Import stage changes, and
closing discards unapplied edits. Export uses a portable VibeStudio JSON file.
`editor gestures [profile] --json` lists choices; repeated `--set field=choice`
changes a batch, `--set field=default` removes one override, and `--dry-run`
previews it. `--reset`, `--input FILE` and `--output FILE` are separate operations.
Existing exports require `--overwrite`. Command shortcuts remain in Keyboard.
For example, `--set camera.flyKeys.forward=I` changes a movement key;
`--set camera.lookToggleKey=Ctrl+Space` changes its toggle. Direction keys are
unmodified single keys, and `none` disables a key. CLI JSON includes field types
and `shortcutWarnings`. `camera.lookHoldKey` and `plan.panHoldKey` use unmodified
hold keys; release or focus loss ends the temporary mode. Fly/drive sets also
include `pitchUp` and `pitchDown`. Existing pointer-only gesture files remain valid.

**Layout > Maximize Active View** expands the focused camera or plan within the
viewport area. **Restore View Layout** returns the original panes and sizes;
**Equalize View Sizes** gives them equal space. Hammer/Worldcraft, J.A.C.K. and Sledge
use Shift+Z, both NetRadiant profiles use F12, and VibeStudio Default uses Ctrl+Space.
Classic Hammer uses Ctrl+A to equalize. These shortcuts require viewport focus.
Expansion keeps editing, assets and inspectors available and leaves saved
layout preferences and bookmarks intact. Changing the layout/profile or map,
restoring a saved view or choosing another pane ends temporary expansion.
Plan and camera status labels adapt to narrow panes and enlarged text, keeping
the view identity first. Full details remain available in the inspector.

CLI `editor profiles --json`, `editor controls <profile> --json`, and
`editor select <profile> --json` share the catalog. Aliases such as `hammer++`,
`worldcraft`, `udb` and `slade3` save the canonical profile ID. Read
`docs/EDITOR_PROFILES.md` for coverage, reference sources and remaining gaps.

## Creating, Saving and Recovering Maps

Levels offers New Map with an empty document or starter room for Quake,
Quake II, Quake III, Doom and Hexen. Texture names refer to your own assets;
check Dependencies before compiling. Ctrl+N and Ctrl+S act on Levels while
that surface has focus. Save chooses a location for untitled maps, then
updates that file. Save As changes the active destination. Both preserve undo.

Saves run on a worker, check for outside edits, and retain replaced content
in `.vibestudio/map-backups` beside the destination. Outside changes or a
removed source require Save As or a reviewed reload. WAD copies retain the
loaded archive snapshot, including other maps and duplicate resources.

File > Recover Maps offers local checkpoints of modified maps, saved every
minute by default. Restoring opens unsaved work with a fresh undo history and
never replaces the original automatically. Its checkbox controls automatic
recovery. Records live in `map-recovery` beneath local application data, or
beside an explicit `--settings-file`. Checkpoints and backups stay local.

CLI `map new`, `map recoveries` and `map recover` use the same document services.
Writing commands require `--output`; `--dry-run` validates without writing,
and replacement requires `--overwrite`. Read `docs/LEVEL_EDITOR.md` for the
complete options and the remaining professional-editor acceptance work.

## Brush Components

Select a brush and choose Edit > Edit Brush Components. Vertices, Edges and
Faces share an orthographic view and a numeric table; the Surface tab previews
the convex result and picks faces. Move or snap selected components, review
the new face/vertex counts, and use local Undo/Redo before Apply commits one map
undo operation. Allow Vertex Collapse explicitly accepts merged or interior
vertices; flat or invalid solids are rejected. Stored material mapping is
retained, but deformation does not provide texture lock. Save before compiling.

CLI `map brush-components` lists current IDs. `map move-components` takes
`--brush`, `--kind vertex|edge|face`, repeated `--component`, `--delta` and/or
`--grid`, and `--output`. It supports dry runs, JSON and explicit overwrite.
Re-query IDs after topology changes or saving/reloading. See
`docs/LEVEL_EDITOR.md` for supported dialects and geometry limits.

## Material Gestures

Q3Radiant and GtkRadiant: middle click samples the material name; Shift+middle
paints one hit surface. Both NetRadiant profiles: middle click samples. These
instant actions keep the current tool and selection. Sampling updates the picker
used by Paint and new brushes. Painting preserves alignment and adds one Undo
step; unchanged material adds none. Locked surfaces refuse paint; placed model
materials remain in Models. Finish navigation or a pending edit before using a
gesture, and wait for the camera preview to finish updating.

Customize sample/paint button and modifier pairs in Controls > Customize
Gestures > 3D Camera. Conflicting pairs cannot be applied. Existing custom
navigation disables a newly inherited material shortcut when necessary.
The same settings use `camera.materialSampleButton`,
`camera.materialSampleModifiers`, `camera.materialPaintButton` and
`camera.materialPaintModifiers` in CLI `editor gestures`. Explicit Paint/Sample
and keyboard Targets remain available. Sampling a brush also captures its mapping
and flags. Q3Radiant/GtkRadiant Ctrl+middle pastes onto the hit brush and
Ctrl+Shift+middle onto the hit face. Customize these through the
`camera.surfacePasteFaceButton`/`Modifiers` and
`camera.surfacePasteBrushButton`/`Modifiers` fields. Existing explicit sample/paint
choices take precedence over new inherited paste defaults. Patch/Doom sampling
clears the brush clipboard. NetRadiant Shift+middle pastes parameters onto the
hit face. NetRadiant Custom Ctrl+middle wraps one brush face seamlessly; configure
`camera.surfaceWrapFaceButton`/`Modifiers` to change that chord. Patch-source
wrapping and depth/light sampling remain open.
Custom Shift+middle pastes native values onto the hit and selected brushes/patches.
Only the explicit brush hit receives flags; patches retain UVs and Valve faces
retain axes. Alt+Shift pastes mapping values only; Alt+Ctrl wraps mapping only.
Ctrl+Shift projects onto the hit and selected brushes/patches; Alt+Ctrl+Shift
projects mapping only. Both are remappable, preserving older explicit bindings.
Hold Values, Project or Wrap to cross surfaces with a live preview. Selection is
included only on the initial hit; later hits use the current modifiers. Wrapping
follows the last face and its image dimensions. Release applies one undo step;
Escape or Cancel Stroke discards the whole transaction, including queued work
after release. Context changes and invalid hits cancel the complete stroke.

## Surface Clipboard

Levels > Surfaces > Copy Surface captures the inspected brush face. Paste Surface
applies its material, mapping and flags to the Selection or inspected Face.
The clipboard lasts across maps in the current session and keeps the copied
material even when the painting picker changes. Matching-format parameters reuse
native values; World projection preserves UVs at world positions, resolving
actual package image dimensions when converting texels/repeats. Allow map-wide
Valve 220 conversion is explicit when classic mappings need shear. Every classic
face converts in the same undo step to keep compiler syntax consistent. Unpasted
materials and UVs stay fixed, and their locks apply; verify compiler support.
Seamless wrap rotates the mapping around the intersection of the copied and
target planes, keeping UVs aligned along the edge. Parallel planes use world
projection. One successful face becomes the clipboard source for the next wrap;
multiple targets each use the original source. Failed or cancelled wraps advance
neither map nor clipboard. Undo restores the map and retains the last clipboard.
NetRadiant Custom's Ctrl+middle wrap uses the Surfaces conversion consent.
Radiant values retains each Valve face's axes and uses source/target image sizes
for primitive texel density. Parameter modes change patch
materials only. Radiant projection copies classic/Valve parameters, projects
primitive matrices and patch UVs, and reports permitted edge-on brush mappings.
Geometry, patch subdivisions and comments remain unchanged.
Keep materials and flags transfers mapping only; primitive UVs
use actual source and target dimensions to retain texel density. Copy captures
the source package context, so switching packages cannot substitute a differently
sized image with the same name. Missing source dimensions cause a clear refusal.

Preparation runs on the surface worker with visible progress and Cancel. Locks,
invalid targets and stale context reject the complete batch. One successful paste
adds one undo; identical settings add none. Save before compiling or packaging.
CLI `map copy-surface source.map --target face:0:1 --output wall.surface.json`
exports a portable definition. `map paste-surface target.map --clipboard
wall.surface.json --target brush:2 --output pasted.map` uses the same service.
Targets repeat; face numbers start at one. Both support dry-run/overwrite/JSON.
Projection uses `--mode project --texture-size W,H`; wrapping uses `--mode seamless`.
Both accept explicit dimensions for unit conversion and `--allow-valve220` if
needed. See `docs/LEVEL_EDITOR.md` for limits and remaining native differences.
`--mode radiant-values` retains Valve axes. Parameter modes accept `--target
patch:id` and repeated `--object brush:id|patch:id|entity:id`. `--mapping-only`
retains materials/flags; repeat `--material-size material=W,H` for target image
sizes and use `--texture-size W,H` for the original source size.
`--mode radiant-project` accepts the same brush/patch selection and reports
`edgeOnFaces`; it does not accept map-wide format conversion.
`--stroke` replays up to 4,096 individual face/patch targets in argument order,
including repeated visits and first-hit selection. Wrapping advances the source;
later source dimensions use destination `--material-size` entries. JSON adds
`strokeHits`, `sourceAdvanced` and the portable `finalSource` definition. The
source map and input clipboard remain protected; an invalid hit publishes no file.

## Surface Adjustments

Levels > Surfaces keeps brush texture controls beside the map. Choose Selection
for the selection or Face for the inspected face and check the target summary. Shift U/V, rotate, grow
or shrink U/V, fit one repeat, or centre the mapping. Step values apply to both
buttons and command shortcuts; they last for the current studio session.
Q3Radiant uses Shift+arrows, Shift+Page Up/Down and Shift+5 for shift, rotate and
fit. Rotation and size changes hold each face centre fixed. Grow/shrink uses a
multiplicative percentage, not native Radiant's additive scale increments.

Rapid adjustments queue on a worker in batches of up to 64. Each completed
batch is one undo step. Cancel drops pending work; completed batches stay in
history. Changing the map, selection, target, save point or package invalidates
pending work. Open the correct asset package/folder for required texture sizes;
no size is guessed. Edit > Surface Alignment offers a detailed preview, arbitrary
values, explicit texture sizes and individual face lists. Save before building.
CLI `map align-textures` uses the same mapping rules. Patches and Doom surfaces
use their separate editing tools.

## Numeric Rotation

Edit > Rotate Selection previews an arbitrary angle with a selection-centre,
world-origin or custom pivot. Texture lock preserves classic, Valve 220 and
brush-primitive mappings. If a classic mapping needs explicit axes, Allow Valve
220 Conversion authorizes converting all classic faces in the map, including
unselected brushes, without changing their appearance. The target compiler must
support Valve 220. Apply commits one undo step; Cancel leaves the map unchanged.
The geometry preview runs on a worker and does not load material images.

CLI `map rotate --degrees 31.75 --axis z --pivot 128,0,64 --texture-lock on
--allow-valve220 --object brush:6 --output <map>` uses the same service and supports
dry-run, JSON and explicit overwrite. Bare `--turns` and the quarter-turn GUI
shortcuts retain legacy texture behavior. Binary Doom/Hexen geometry rotates in
XY with whole-unit rounding and requires a node rebuild afterward. See
`docs/LEVEL_EDITOR.md` for format restrictions and accepted ranges.

## Quake III Patches

Levels > Edit > Add Patch creates a plane, open cylinder or open cone. Select a
patch and choose Edit Patch Control Points to reshape it. The control grid and
point table share selection; edit XYZ and UV values, move or snap selected points,
split rows/columns without changing the surface, invert facing, and review the
UV checker preview. Local Undo/Redo edits the draft; Apply adds one map undo step
and Cancel drops the draft. Save before compiling. Materials remain ordinary map
dependencies that Packages can inspect and resolve.

CLI `map add-patch` and `map edit-patch` use the same authoring and save services.
Both support `--output`, `--dry-run`, `--overwrite` and `--json`. See
`docs/LEVEL_EDITOR.md` for point indices, options and remaining requirements.

## Package Workflows

Opening an archive or folder admits up to 250,000 records including skipped
entries and implied folders, 128 path components, 64 MiB of logical index metadata
and 64 MiB of source fingerprints. Folder files share the fingerprint budget.
GUI and CLI use these fixed limits; over-limit inputs return an error without a
partial listing. Open a smaller archive or narrower folder. The opening dialog
reports source verification, indexing and index preparation with Cancel. ZIP
central records stream individually. Combined sessions and multi-folder map
texture lookup share these budgets across at most 64 layers/roots, including
overridden records. Failed mounts preserve the previous session; incomplete
folder audits report `sourceIndexComplete: false` and CLI exit 4. Staged documents
also admit retained generated bytes (256 MiB) and payload hashes (128 MiB) across
base, edits and undo/redo. Rejection preserves the document. Draft objects share
hash storage by content identity; `package manifest` reports exact counters under
`summary.retainedContent`. Retained index/text metadata admits 128 MiB and
1,000,000 logical records across base, edits and history; the manifest reports
`summary.retainedMetadata`. Undo/redo slots stay reserved at capacity. Drafts
check metadata before payload reads. Reopening an exported package releases edit
history; a draft preserves it. Editable packages keep their original content
provider, and filesystem adapter sources are captured when adopted. Missing or
ambiguous backing stays unavailable until explicitly repaired; files created later
cannot silently supply it. Preview, export and draft checkpointing share the same
retained source. Reader snapshots and staged browser projections
also admit 250,000 physical/diagnostic/implied-folder records, 64 MiB of metadata
text and 128 path components. **Package view unavailable** explains refusal and
retains Undo/Redo. Header, composition and asset browsers share the diagnosis;
the CLI reports an error without a partial listing. Use
`package draft-undo` to restore a saved draft's view. Internal plan preparation
separately admits 250,000 live entries/conflicts, 500,000 path/parent keys and
128 MiB of text per representation. Folder edits check expanded metadata before
changing history; cached plan refusals retain Undo/Redo and draft recovery.
`summary.planLimits` reports the fixed policy. Individual edits and outermost
edit groups check the complete plan and browser projection before history commit.
Grouped imports check once on their worker; final refusal or cancellation returns
no accepted files and preserves the original document and redo. Legacy draft
history remains recoverable with Undo/Redo. `summary.viewLimits` exposes the
browser policy. Helper allocation audits, remaining synchronous preparation and
aggregate process memory still need acceptance; see `docs/PACKAGE_MANAGER.md`.
Persisted history counters also
refuse edits before overflow, keeping Undo/Redo and draft save/export available.
Export and reopen an ordinary package to start fresh history; drafts preserve it.

Package entry indexing and filtering run in the background. The list shows
records checked; Cancel stops the request and Retry prepares the current query
again. Changing folder or query replaces obsolete work and clears stale previews
and entry actions. Every match remains in the native Qt model, with presentation
prepared on demand; Enter in the filter selects the complete result once ready.
Duplicate source-entry labels remain distinct and unreadable members retain text
diagnostics. Tree/staging population and remaining projection handoffs still need
scale acceptance. No new setting is required.

Package writes and no-write dry runs must also fit the opening index policy.
Physical and implied-folder counts, encoded directories, decoded names and
duplicate diagnostics are admitted before source reads; ZIP64 growth is checked
when sizes are known. The fingerprint budget permits at most 128 GiB of archive
bytes, including headers/directories, subject to lower format limits. The save
dialog reports **Checking package output limits…** with cancellable record
progress. Refusal preserves output, backup and document history; reduce the
package or undo recent edits and retry. CLI archive-write refusal returns exit 4.
ZIP folder identities are canonical, while ZIP bytes retain directory slashes.
See `docs/PACKAGE_MANAGER.md` for separate snapshot and history budgets.

**New Package** creates an untitled PAK, ZIP, PK3 or WAD document without an
input archive. Empty new documents also offer Save/Discard/Cancel. **New Folder**,
folder Rename and Delete act on whole subtrees in one undo step; rename needs
an unused destination. Folder context menus and F2/Delete in the folder tree
share these controls. ZIP/PK3 retain empty folders; PAK export blocks rather
than discarding them. WAD keeps its flat lump namespace.

**Save Package Draft** (`Ctrl+S` on Packages) stores the base content, staged
edits, and undo/redo history in a `.vibepackage` directory. **Open Package Draft**
resumes it without needing the original inputs. Move the whole directory,
including its metadata and payload objects. `Ctrl+Z` undoes one edit group and
`Ctrl+Shift+Z` redoes it; imports and selected deletions/unstaging are grouped.
Saved drafts retain history across archive exports. Closing modified work offers
Save, Discard, and Cancel. Local recovery checkpoints run every 30 seconds by
default. **File > Recover Packages** reviews copies, changes the 5–600 second
interval or disables future checkpoints, and restores complete content/history to
a new independent draft. The package browser reports checkpoint status. Inventory
checks metadata; restoring verifies payloads and rejects changed selections.
Reviewed discard cannot remove a copy owned by an active editor. The CLI shares
`package recoveries`, `package draft-recover` and `package recovery-discard`;
restore requires a selected manifest checksum, and discard previews unless
`--write` is supplied. Storage defaults to 8,192 MiB and 32 copies; the chooser
shows usage and adjusts limits without evicting older copies. Incomplete copies
cannot restore, but can be discarded after review in the chooser or with CLI
`--expected-storage-sha256`. Existing recovery copies are retained when disabled.

The CLI shares this document model through `package draft-save`, `draft-info`,
`draft-undo`, and `draft-redo`. `package info`, `list`, `preview`, `extract`,
`validate`, `stage` and `save-as` accept draft inputs. `package create <output>`
creates an archive or draft directly. Stage options include `--mkdir`, paired
`--rename-folder` / `--folder-to`, and `--delete-folder`. **WAD Groups** reviews
map/GL renames and complete group deletions in new and opened Doom WADs. `package groups`
returns group IDs and the fingerprint required by `--rename-group` / `--group-to`
or `--delete-group` with `--groups-fingerprint`. Group edits share Undo and drafts;
new WADs assemble binary/GL runs before review and retain that planned order through
saving and subset export, preserving namespace regions and UDMF sidecars.
Metadata/script dependency rewrites remain incomplete. Creation and
`draft-save --dry-run` verify inputs without writing files.

File imports and replacements retain verified independent working copies before
acceptance. Changing or deleting the original file does not change an imported
asset or its undo history. Copies use Qt's configured temporary directory and are
released after the last document/history/reader reference. Drafts and checkpoints
provide persistence across restarts; temporary working copies are not recoverable
documents by themselves. Read-only CLI inspection and dry runs create no working
copies and keep checking their original sources.

**File > Recover Packages > Saved Draft Storage…** reviews a saved draft, adjusts
its save limits (32 GiB/200,000 files by default) and reclaims reviewed unused
objects. Close the draft and wait for its readers before cleanup; undo/redo content
is retained. Save accounting includes unused objects and peak metadata space.
`package draft-storage <draft> --json` reports usage and review checksums;
`package draft-compact <draft> --expected-storage-sha256 <hash>` previews cleanup.
Add `--write` to exclude participating readers and reclaim unused files. A dry run
creates no locks and reports `readerExclusion: false`. Corrupt/incomplete drafts
and unknown files prevent reclamation. See the package guide for filesystem limits.

**File > Recover Packages > Working Import Storage…** reviews current/reserved
payload usage and abandoned sessions. Limits default to 8,192 MiB and 50,000 files
and also apply to actual CLI imports. Lowering limits preserves existing content;
new admissions stop when their reservation would exceed a limit. Live documents,
undo/redo and background readers keep their session protected. Explicit discard
checks the reviewed files and proves the session is no longer live. Unknown paths,
links or changes require a new review; no automatic eviction occurs.

`package working-imports --json` lists sessions and storage checksums without
writing. `package working-discard <id> --expected-storage-sha256 <hash>` defaults
to dry-run review; add `--write` for removal. Both accept `--directory <store>`.
Dry runs create no locks and report `leaseChecked: false`. **Review Lock Files…**
reviews an interrupted store/session lock, including an empty lock left during
creation. Release verifies the same path, native file identity and content,
then proves no cooperating process holds it. Payloads remain for separate
session review. `package working-unlock <relative-lock-path>
--expected-lock-sha256 <hash>` previews the same check; `--write` releases the
reviewed lock. Native exclusion also applies to dry runs. Inspection reads
limits without creating or migrating settings. Normal shutdown drains queued
import cleanup; a failed cleanup retains its session for explicit review.
Working files are
separate from recovery checkpoints and cannot restore an editing document.

The package list, folders, preview and extraction show staged additions, renames
and deletions immediately. Undo/redo refreshes this view and asset browsers.
The package inspector prepares metadata previews in the background. **Cancel
Preview** stops the selected request; **Retry Preview** rereads it. New selections
clear old text and supersede pending work. Full samples verify their complete
payload; a truncated sample leaves its tail unverified. Use **Validate** for a
complete integrity check.
Repeated names carry a source-entry label; Preview, Replace, Rename and Delete
address that exact file occurrence. Staging and history retain its identity.
If extraction output names collide, **Extraction Paths** proposes separate
relative paths and lets you edit them; invalid names or collisions disable
Extract. CLI `package preview --entry-index N` and repeated extraction
`--entry-index N --as <relative-path>` use current `list --json` indexes.
Staging uses persistent source ordinals: `--replace-ordinal N --replace-file`,
`--rename-ordinal N --to`, and `--delete-ordinal N`. CLI numbers start at 0;
GUI source-entry labels start at 1. New version 4 drafts preserve these selectors
and source protections through Save As, reopening and recovery. Versions 1–3 remain
readable and upgrade on their next save. An output or save-lock collision with
protected source content requires another destination. Export, extraction and
temporary copy progress show records checked while matching source protections;
Cancel is available during this phase, before content is written. **Export Selected** and CLI `package subset` preserve
exact occurrences and staged bytes, including required WAD groups. Blocked edits
must be resolved before planned extraction or subset export.

Use **Packages > Review Changes** to compare the current staged result with its
source before saving. Filter the result list, select a row for paths and hashes,
or export JSON. Unchecked files stay visible alongside differences. **Compare**
compares the planned package with another archive. Both comparisons and **Save As** run in
the background with progress and Cancel. Closing a running operation requests
cancellation; it remains open until the current file finishes. A save already
committing may finish successfully.

For a regular archive, successful Save As opens the output as the active package
and clears the saved operations; an open draft keeps its plan and history.
Failure or cancellation keeps the staged changes for retry. In-place
replacement still requires confirmation and keeps a verified backup. Interrupted
saves retain a journal and recovery copies beside the output. Inspect them with
`vibestudio --cli package recover <journal> --json`; `--finish` completes backup
publication only when the new package is already installed and verified. See
[Package Manager](PACKAGE_MANAGER.md) for recovery details and CLI examples.

**Packages > Validate** checks every source payload, including repeated WAD
lumps, with progress and cancellation during reads. Export the report as JSON,
or use `vibestudio --cli package validate ./release.pk3 --json`. A pass requires
all files to verify with no warnings; byte limits leave files explicitly unchecked.

Dragging package rows and opening temporary script/map copies run verified
streaming reads on a worker with progress and Cancel. Failed or cancelled batches
are discarded before any handoff. Empty folders and exact selected occurrences
are preserved; each successful batch stays owned for the studio session. Code
copies are read-only. Use Extract Selected for conflicting names or batches over
2,000 files, 10,000 entries or 512 MiB. File > Temporary Package Copies reviews
the window's initial reservations and sets aggregate limits (2,048 MiB, 8,000
files, 40,000 entries and 64 batches by default). Lower limits preserve existing
copies; failed cleanup remains charged. `package copy-limits` inspects or
proposes policy changes, and `--write` saves them. Initial reservations exclude
later consumer edits and filesystem overhead. Review Retained Copies shows
actual logical usage across managed sessions and explicitly discards selected
unused copies, including those retained after a crash. Live sessions are protected.
`package copy-sessions` supplies review IDs/checksums; `package copy-discard`
previews by default and requires `--write` for deletion. Preserve any consumer
edits first. Shared Storage Limits and `package copy-store-limits` also cap
initial reservations across processes using that physical store (8,192 MiB,
32,000 files, 160,000 entries and 256 batches by default). Pending and crash
reservations remain charged until verified release. Shared policy inspection
and proposals write nothing; only `--write` persists the policy. Changes preserve
existing copies and refuse stale policy reviews. Older or invalid session records
require review before preparing more copies. Older unregistered folders are not
removed. Later consumer growth, native drag and broader shutdown acceptance
remain open.

Supported read-only package inputs are folders, Quake PAK, Doom WAD, ZIP, and
PK3 archives.

Common CLI commands:

```sh
vibestudio --cli package info ./id1/pak0.pak --json
vibestudio --cli package list ./baseq3/pak0.pk3 --json
vibestudio --cli package validate ./maps.wad
vibestudio --cli package extract ./pak0.pak --output ./out --dry-run
vibestudio --cli package stage ./mod-folder --add-file ./autoexec.cfg --as scripts/autoexec.cfg --json
vibestudio --cli package save-as ./mod-folder ./build/mod.pk3 --format pk3 --add-file ./autoexec.cfg --as scripts/autoexec.cfg --manifest ./build/mod.manifest.json
vibestudio --cli asset inspect ./baseq3/pak0.pk3 textures/base_wall/wall.bmp
vibestudio --cli asset convert ./baseq3/pak0.pk3 --entry textures/base_wall/wall.bmp --output ./converted --resize 128x128 --dry-run
vibestudio --cli asset find ./my-mod --find developer
```

In the GUI, package open and extraction operations create Activity Center tasks
with loading/progress/result states, warnings, cancellation where available,
and exact output paths. Package staging shows add, replace, rename, delete,
conflict, blocker, and before/after composition summaries before save-as writes
a new PAK, ZIP/PK3, or tested PWAD output. Asset commands share the same
package services for metadata previews, image conversion queues, WAV export,
and project text find/replace reporting.

## Code Document Saves

Code tabs detect UTF-8 and BOM-marked UTF-16 and preserve encoding, BOM, existing
line separators, Unicode spaces, and final-newline state on Save. The editor
readout exposes the format. Unsupported encodings and files above 4 MiB are
read-only. Save checks the source hash before writing atomically; if the disk
file changed, Cancel keeps both copies and Overwrite requires explicit review.
An unsaved shared map in Levels blocks Code Save.

`code text-info <file> --json` reports format and SHA-256. `code text-save <file>
--input <edited-file> --dry-run --json` previews the same save service. Writing
requires `--write --expected-sha256 <source-hash>` from text-info. The target's
format is retained. See `docs/CODE_EDITOR.md` for mixed-ending matching rules
and parser/search limits.

## Local Language Services

Code's Language Server output tab connects an explicitly selected local stdio
tool, such as clangd, for live diagnostics, Quick Info, Parameter Hints, semantic completion, Go to Definition
and Find All References.
Choose its absolute executable, language and extensions; enter one literal
argument per line under Arguments and log, then Connect. The tool runs with
your account's permissions and receives matching named project documents,
including unsaved buffers. Choose a trusted executable. Settings are remembered
without starting it; project changes disconnect, and Disconnect stops it.

Version checks retire stale source markers and jumps. F12 uses supported
connected documents, otherwise the built-in source index. Ctrl+Space and server
trigger characters request completion; selecting an item applies its replacement
and related document edits in one unsaved Undo step. Escape cancels. Local
completion remains available without a server. Ctrl+I opens Quick Info with
selectable type information and documentation, progress and Cancel; pointer dwell
shows a short hint without moving the caret. Replies must match the current
source snapshot. Markdown cannot fetch images, run HTML or activate links.
Ctrl+Shift+Space opens Parameter Hints above the source, with overload selection
and a numbered active argument emphasized in the signature. Documentation expands
on demand. Advertised trigger characters open hints while typing; edits and caret
changes refresh the current call and preserve the selected overload. Escape or
Close dismisses hints, and tab changes or disconnect retire late responses.
Alt+Shift+F formats the document; Ctrl+Alt+F formats the selected source with a
capable connected server. Progress and Activity expose Cancel. Valid edits form
one unsaved Undo step; source changes retire late replies. Save before compiling
or staging. The CLI previews with --format-document or --format-range and range
endpoints; --write requires --expected-sha256 from that saved-source preview.
F2 previews Rename Symbol through the connected provider. Enter a new name,
review exact before/after edits in Search Results, then Apply Preview. Open tabs
receive one unsaved Undo step each; unopened files use guarded atomic saves.
Source changes or disconnect invalidate the preview. Invalid edits or file
operations block the whole plan. Cancellation lists any disk writes already
completed. CLI --rename <new-name> --line N --column N previews the plan;
--write requires --expected-plan-sha256 from the reviewed result.
Ctrl+. opens Code Actions for a caret or selection. Choose a quick fix or
refactoring, including lazily resolved actions, then review its edits in Search
Results. Preferred and unavailable states include text labels and reasons.
Apply shares rename's unsaved Undo and guarded disk writes; source changes,
disconnect or Cancel retire pending work. Commands and file operations cannot
apply. CLI --code-actions --line N --column N lists actions; --action-index N
previews one. Optional --end-line/--end-column specify a selection, and --write
requires the preview's --expected-plan-sha256.
Completion providers can resolve highlighted suggestions for documentation and
related imports. Progress and unavailable reasons appear in the list. Accepting
early waits for the full edit set; Escape, typing, caret or tab changes cancel.
All resolved edits remain one unsaved Undo step. CLI --completion with
--resolve-completion N inspects one indexed suggestion without writing sources.
Snippet suggestions expand into linked editable fields. Tab/Shift+Tab move among
them, and the inline bar offers field status, previous/next controls, choices and
Finish. Escape ends navigation at the current caret. Insertion with imports is
one unsaved Undo step; each later field edit updates its mirrors in another.
Ordinary completion inside one field retains linking. Undo/Redo, external edits
and document changes retire the session. Regex transforms and stacked snippet
sessions remain unsupported. CLI completion JSON exposes expanded edits and
absolute UTF-16 tab-stop ranges for the same validated proposal.
Diagnostics can be published by a server or requested by the editor. Pull
providers expose Refresh diagnostics, version-checked Problems rows and explicit
failure/retry states. Successful Save/Save As notify interested servers after
synchronizing the saved text; failed writes send no notification. CLI diagnostic
checks also accept pull-only providers and never write source files.
Shift+F12 requests semantic
references and includes the declaration; Search Results shows the provider,
progress, Cancel and source previews. Activating a result selects its exact
range after checking its snapshot. Source changes and disconnect retire pending
requests. Disconnected or unsupported documents use whole-word text search.
Reference results never form a replacement preview; Search Text starts a new
textual query. Save before compiling or
staging. No server installation or AI connector is required for core editing.
`code language-server <file> --server <absolute-executable> --json` exposes the
same client for saved files. Add `--completion --line N --column N` to inspect
validated completion proposals without applying them, or `--references --line N
--column N` for semantic locations. `--exclude-declaration` limits references to
uses. `--hover --line N --column N` returns symbol documentation and checked
source ranges. `--signature-help --line N --column N` inspects call overloads,
active arguments and documentation without editing. Only one query type can be used at a time.
See `docs/LANGUAGE_SERVICES.md` for flags, bounds,
version provenance, configuration and capability gaps.

## Project Search And Replacement

In Code, choose Find in Project or Ctrl+Shift+F. Search Results offers case and
whole-word matching, include/exclude file patterns, progress, and cancellation.
Activate a result to open its file and line. Find All References uses the same
textual search when a semantic server is unavailable. Named open project Code documents supply their current text,
including inactive and unsaved tabs; other files are read from disk.

Enable Replace with and search to review before/after text; an empty replacement
deletes matches. Apply Preview confirms the exact prepared changes and checks
source hashes, live revisions and unsaved Levels maps. Open documents receive
undoable unsaved edits; unopened files are saved atomically with their original
UTF-8/UTF-16 encoding and line endings. A cancelled or failed batch reports files
already saved. Search again after applying. Incomplete results cannot be applied.
Untitled documents, archive entries and package staging remain outside search.

```sh
vibestudio --cli asset find ./my-mod --find player --whole-word --include "*.qc;*.qh" --json
vibestudio --cli asset replace ./my-mod --find obsolete_flag --delete-matches --include "*.cfg" --dry-run --json
```

Use --write only after reviewing; each CLI invocation prepares its own preview.
See [Project Search](PROJECT_SEARCH.md) for scope, limits, and partial-write
reporting.

## Audio Editing

On Audio, choose Edit Sound for a package entry or Open Audio for a local WAV or
digital Doom DMX sound. Drag a waveform selection or enter exact start/end frames
(end exclusive). New creates an empty sound or initial silence at a chosen sample
rate/channel count. Copy/cut/paste share exact float audio between editor windows
with matching formats. Paste replaces the selection; mix adds at its start.
Insert Silence shifts the tail. Trim, delete, silence, fades, reverse, gain,
normalize, mono/stereo conversion, polarity, and DC correction share named
undo/redo. Empty documents support native saves and recovery; WAV output requires
samples. Qt Multimedia, when installed, auditions the edited
selection as float32 with pause, looping, and volume; editing also works without
playback. Playback frame seeks during playback or pause without changing the
selection. Live seeks use backend millisecond positions; selecting a start frame
before Play preserves that exact boundary. Stop cancels pending preparation.
Missing/disconnected devices and a 30-second loading/buffering timeout report a
retryable error; choose an output in system sound settings and try Play again.
The Audio browser uses the same transport with asynchronous preview and audition
preparation. Its Position slider seeks compressed sounds even without a waveform.
Stop cancels pending audition; new selections discard stale results. WAV playback
preserves source precision; DMX is widened to PCM16. Preview reads up to 64 MiB,
with a 64 KiB header sample for larger sounds; audition is limited to 128 MiB.
Repeated WAD sound names retain their selected entry through playback, editing
and browser WAV export.
Zoom In/Out, Fit Sound/Selection, and Ctrl+wheel reveal exact sample points.
The scrollbar and full-sound overview pan the visible range. Left/Right moves
one frame; Shift extends the selection without millisecond rounding.
Effects > Resample changes the whole sound to a preset or custom rate while
preserving pitch and duration. The dialog previews its frame count; conversion
keeps float headroom, maps selection by time, and supports cancellation and undo.

Save Project (Ctrl+S) preserves exact float32 samples, selection, and source
provenance in a `.vsaudio` document. Save As creates a separate project. Native
saves reject external changes and preserve unsaved edits after a failure.
Keep local recovery copies enables background checkpoints. Recoveries verifies
local copies, restores a reviewed copy as an unsaved draft, and offers explicit
discard. Live editor leases and reviewed hashes protect active or changed copies.
Storage is limited to 32 copies / 512 MiB; full storage stops checkpoints without
evicting existing work. `asset audio-recoveries --json` exposes the same inventory.
File > Recover Audio and the command palette open the same manager without
opening a source first. Startup offers retained files after a background
metadata-only scan; an earlier crash notice stays visible until dismissed.
Settings > Getting Started > Audio Recovery controls checkpoints and startup
offers separately, shows the recovery folder, and provides Review Copies.
Disabling either preference keeps existing files. An explicit --settings-file
uses audio-recovery beside that profile unless VIBESTUDIO_AUDIO_RECOVERY_ROOT
overrides it. Restoring always requires review and opens an unsaved draft.
Export and staging do not mark the editable project saved.

Export Audio offers PCM8/16/24/32 and exact float32, plus optional triangular dither
for final integer delivery. Float output retains headroom and disables dither.
Export Audio and Stage Sound also offer Doom and Quake-family sound presets.
They mix/resample a delivery copy: Doom uses padded mono PCM8 DMX at 11025 Hz,
Quake uses mono PCM8 WAV at 11025 Hz, and Quake II/III use mono PCM16 at 22050 Hz.
Stage WAV in folders/PAK/ZIP/PK3, or Doom DMX in an IWAD/PWAD using a DS sound
name without an extension. Review and save the plan in Packages; replacing an
entry is explicit. Pending sounds appear immediately in Audio; preview, playback,
browser export, and editor reopening read the same staged bytes.
Markers edits named cues and one forward loop; Select Loop prepares its exact
range for normal transport audition. Marker changes support undo, native saves,
recovery, structural edits, and resampling. WAV preserves supported markers;
Quake/II presets write legacy loop cues, and omit cue-only metadata to prevent
unintended loops. Doom DMX omits markers. Original Quake III ignores embedded
loop instructions. Export summaries explain the retained metadata.
`asset audio-markers` shares inspection and bounded JSON authoring with separate
outputs and dry runs. MP3, native FLAC and single-stream Ogg Vorbis import through
bundled decoders even without Qt Multimedia. Compressed tags/artwork/marker
conventions are omitted with an import warning that persists in native projects.
Open Edit Sound for the decoded waveform; browser Export WAV performs cancellable
complete-stream validation and separate PCM16 output.
See `docs/AUDIO_EDITOR.md` for format and memory limits.

Use **Range…** in a multitrack session to clear clips, ripple-delete a gap,
insert silence at the range start or repeat the section after its end. Check
exact tracks or All tracks; clip groups do not add unchecked tracks. Time edits
follow track and effect automation by default, with a separate master choice
for all-track scope. Clear leaves automation in place. The timeline brackets
show Range start/end (end exclusive), also used by playback and delivery.
Cuts preserve original fades and automation curves through undo, save and
recovery. Tempo/meter markers remain in place. Native saves use version 7 and
read versions 1–6. The broader DAW capability gates remain in progress.

Choose **Audio > Multitrack…** to arrange independent mono/stereo clips. **To
Session** imports the waveform editor's sample snapshot; mismatched rates need
explicit conversion. Track/Clip inspectors expose gain, pan, mute/solo, exact
positions, trims and fades; Automation edits frame-based gain/pan points.
Save arrangements as `.vssession`. Sessions have undo and guarded saves, and
share the waveform Audio Recovery preference, storage budget and startup offer.
Review Audio Recoveries verifies both document kinds. Session restoration opens
an unsaved draft and preserves the reviewed copy and original session. Checkpoints
include the arrangement, automation and embedded media; they do not retain undo
history or provide crash-safe recording.
Select Range start/end frames for streamed Play Range or Edit Mixdown. Output and
Output buffer choose a compatible device and requested buffering for this window.
Pause/resume keeps queued audio; cursor seeks discard old lookahead. Status reports
device-estimated frames, actual buffer size, dropouts and master peaks. Edits or
output/range changes stop playback. No input device opens. The latter
opens a separate float sound in the waveform editor for analysis, final dither,
game export, staging and level placement. Export WAV streams longer ranges as
float32 or integer PCM with optional TPDF dither, bounded by RIFF's size limit. Source media
stays in memory. Record Tracks supports synchronized backing and punch recording;
physical low-latency acceptance, MIDI, plugins and source disk streaming remain open. Gain/pan and numeric effect parameters
support Linear, Step and Smooth curves, a graphical preview and native point
controls. Read automation can be switched off while retaining points. Session
version 7 saves curves and inherited interpolation domains; older versions migrate linear envelopes. Presets contain
static values; replacing a chain clears its old lanes. Live write/touch/latch
recording remains planned. `asset audio-session new|inspect|import|edit|arrange|range|automation|effects|effect-automation|presets|tempo-map|position|mixdown|stems|recover|transport` shares
the implementation and supports JSON, dry runs and explicit output paths.
Recovery requires `--expected-sha256` and a separate `.vssession` output.

Tempo / Meter edits stepped tempo and bar-boundary time signatures. Set Tempo
uses bar.beat.tick positions; Set Meter uses a bar number. OK applies one
undoable map edit, while Cancel preserves the session. Audio and automation keep
their sample positions. Choose Bars and beats in Ruler and use Position/Go to
navigate; Snap offers bar/beat subdivisions and triplets. BPM counts quarter
notes, with 960 ticks per quarter; notated beats use the meter denominator.
The read-only CLI position command converts frames/ticks/positions and reports
optional grid snapping. tempo-map edits the same validated maps. Tempo ramps,
metronome, musical clip anchors and MIDI synchronization remain planned.

Add Bus creates a stereo subgroup/return. Routing / Sends selects a strip's
output, channel swap/polarity and up to eight pre/post-fader sends. New GUI sends
begin disabled at -12 dB. Apply validates the graph, stops playback and creates
one undoable edit; missing buses and feedback cycles are rejected. Solo retains
paths through selected strips without leaking parallel bypass routes. Saves use
version-5 sessions; versions 1/2/3/4 remain readable. CLI edit operations add-bus,
routing, send and remove-send use the same services. Per-strip live meters,
seamless live controls, surround and sidechains remain planned.
Effects… and Master Effects… stage up to eight ordered inserts per chain:
gain, filters/EQ, compressor, gate, sample peak and lookahead limiters, stereo delay, soft
saturation, stereo reverb, chorus, flanger, tremolo and phaser. Apply stops playback and makes one undoable edit; bypass and
parameters survive save/recovery. Track/bus inserts follow fader and pan;
pre-fader sends stay dry. Master inserts follow master gain. The global Session
tail setting (0–60 seconds, initially 2) extends the default range for enabled
filters/delays. Explicit ranges retain their end and start with fresh history;
stop/seek and loop-policy changes reset history. Pause/resume and loop repeats
retain it. Session loops wrap sources and automation while delays, reverb and
modulation continue; lookahead primes wrapped future context once. There is no implicit
preroll, true-peak limiting or oversampled saturation. Automation inside Effects
edits bounded linear/step/smooth curves for eligible numeric parameters. Read/off
retains the lane; removing an effect or replacing its chain prunes the old lanes.
Session version 5 retains these curves; presets save static values only.
The lookahead limiter has fixed 0–20 ms lookahead plus automatable ceiling,
attack and release. Lookahead cannot be automated: Apply rebuilds compensation
and stops playback. Automatic alignment covers tracks, buses, pre/post sends,
master and stem taps. Fresh ranges prime from their requested start using later
session context, with no leading padding or extra exported frames. The inspector
reports insert latency; CLI and delivery reports include processing latency,
which is separate from measured hardware latency. Expand Presets to load one of nine
factory recipes or open/save a portable .vsfx file. Loading replaces only the
draft chain, creates fresh IDs and keeps the longer session/preset tail; Apply
commits it. File values must fit the destination rate. Preset I/O runs on a worker
with visible progress, cancellation and guarded atomic output. Preset libraries
and generic asset-browser opening remain gaps.
CLI presets lists factory IDs, parameter bounds and automation eligibility. Effects operations add,
set, move, remove, clear, tail, preset and save-preset use the same
validation and guarded output services; see Audio Editor for parameter keys.
Transport diagnostics take `--frames N`, optional frame bounds, `--block-frames`
and `--loop true|false`, reporting sample-clock/digest results without opening an
audio device or writing a file. Loop sample digests include continuous effect
history, matching session playback. Exports render the finite range once.

Export Stems selects track/bus pre- or post-fader WAVs and an optional master,
all with a common frame range and optional seeded integer dither. Review the
filename plan and destination folder before Export. Normal source protection,
explicit replacement and revision guards apply. Completed files remain after
cancellation or failure; Delivery Report and the JSON manifest identify each
written, failed or pending file. Overlapping track/bus stems can double-count
content, and nonlinear master processing changes their sum. GUI and CLI stems
use the same renderer, compensation, publication and partial-result reporting.

Use `asset audio-recoveries --discard UUID --kind session --expected-sha256 HASH`
to review a session discard; only adding `--write` commits it.

**Single Take…** selects an input, channel order, a new `.vstake` path and
optional measured compensation. Arm, then press Record; only Record opens input
and requests permission. Existing audition stops. Stop saves already delivered
frames and offers review; queue/device/storage failure retains earlier verified
blocks. Open Take or Open Audio reopens interrupted files. Accept an incomplete
prefix explicitly, then select exact frames and one or two stored channels for
session import. Imported samples use normal undo/save/recovery and remain subject
to source/session memory limits. Take files are retained user documents, separate
from automatic checkpoints. Use **Record Tracks…** for monitoring, synchronized
overdubbing and punch/finite-loop capture. Native microphone/permission and power-loss acceptance are
not established. On macOS, recording requires a correctly declared application
bundle and Qt 6.5+ microphone permission support.
`asset audio-take inspect TAKE --json` reviews the prefix. `export TAKE` requires
`--expected-prefix-sha256 HASH --start-frame N --end-frame N --channels 1,2
--output FILE`; add `--allow-incomplete` for recovery, `--dry-run` to validate,
or `--format wav` for float32 WAV. The default is a native `.vsaudio` project.
CLI take operations never open input devices.

`asset audio-recording inspect FOLDER.vsrecord --json` inspects a grouped
recording plan, final receipt and each independently verified take prefix.
Check `planValid`, `receiptValid` and `receiptMatches`: a readable plan returns
exit 0 even if the final receipt is missing or interrupted. Different arms may
have different verified lengths after a storage failure. Individual ranges can
use the reviewed `audio-take export` workflow above. **Multitrack → Record Tracks…**
records up to eight armed mono/stereo tracks with explicit input/output devices,
backing/punch frames and optional per-track monitoring. Opening or arming does
not start input. Record waits for permission, playback shutdown and file setup.
**Loop passes** repeats the punch range 1–10,000 times with one preroll and
continuous monitoring/effect tails. Stop retains a partial final pass.
**Review → Recorded pass** selects an available pass for each arm. Trim frames
are local to that pass and recorded alignment uses the original punch position.
Changing pass resets its trim; source journals remain available for later review.
Stored audio is dry. **Meters** shows mapped dry inputs and stereo output before
safety clipping, with sample peak/RMS, maximum/headroom and over-range counts.
**Reset Meter History** changes readings only. Final levels remain in the current
dialog but are not stored in the pass folder; use take export and waveform or
session analysis for reopened audio. **Review → Open Recording…** reopens retained passes against
the current session. Choose target tracks, ranges, stored channels, placement,
replacement and grouping; interrupted passes require explicit prefix acceptance.
Import is one undoable change after hashes are checked again. For comping, enable
**Assemble comp sections**, choose each pass/range and **Add Current Section**.
Select queued rows to update or remove them. **Crossfade frames** uses zero for
hard cuts or at least two for complementary linear fades after adjacent cuts.
The outgoing pass needs extra audio after the cut; channel counts must match
and the incoming section must be long enough. Invalid overlaps/handles are
rejected. Import Comp adds editable clips and fades in one undo; journals stay
unchanged. **Save Review** / **Save Review As…** preserve active selections in
portable review JSON. **Open Saved Review…** verifies sources and restores the
complete ordered queue, including several passes from one arm. **Use queued take
selections** distinguishes those choices from checked whole takes. Saved/unsaved
status is visible; Close/Open/Record offer Save/Discard/Cancel for changed choices.
External file changes require reopening or Save As. Relative
recording paths move with the review. Keep the matching session and journals;
recipes are separate from native take lanes and reimport follows the selected
replacement ranges. CLI `asset audio-recording save-review SESSION --review
PLAN.json --output REVIEW.json` uses the same verification and atomic writer,
with dry-run, overwrite and session-hash guards.
**Audition Current Section** hears the focused take fields before
import; **Audition Review** hears the comp queue or checked takes. The Audition
tab provides output/buffer selection, listening volume, Pause/Resume, exact frame
seeking and repeat. **Include backing clips** is on by default; turn it off to
isolate the reviewed clips through the same mixer and effects. Mute/solo still
apply. Playback covers the imported clip span without extra preroll or tails.
Edits stop stale playback; Record, Import and Close wait for output shutdown.
Listening adds no session edit or undo entry.

`asset audio-recording import SESSION --review PLAN.json --output OUTPUT.vssession`
uses the same validation without devices. Copy the `importPlan` object from
**Show File and Import Details** to the review JSON file. `--dry-run` validates
without writing; `--expected-session-sha256` pins a reviewed source revision.
Version-2 review JSON adds one-based `loopPass` to each selection and supports
distinct arm/pass pairs within the session limits. Existing version-1 review
files select the first pass. Raw `audio-take export` ranges still address the
continuous journal. Comp reviews use version 3 with required `comp: true`,
`crossfadeFrames` and `loopPass` on every selection; repeated arm/pass pairs are
allowed within the same source/sample limits, including fade handles. Existing
outputs and in-place updates require `--overwrite`.
`asset audio-recording preview SESSION --review PLAN.json --output PREVIEW.wav`
exports the same span as float32 WAV without a device. Use `--isolated` to omit
backing and the same dry-run, reviewed session hash and overwrite guards. GUI
listening volume and device clipping do not affect the export.
Open-ended loops, dedicated comp lanes,
and physical platform acceptance remain open. Standalone
capture remains under **Single Take…**. See the repository's `docs/AUDIO_DUPLEX.md`
for the complete review schema and platform limits.

With a Quake II/III map and a folder/PAK/ZIP/PK3 open, **Stage & Place in Level**
reviews the game, position, loop/trigger mode and target name before converting
to mono 22050 Hz PCM16 and staging a sound plus an undoable speaker. Quake II
uses a `noise` path relative to `sound/`; Quake III uses the full package path.
Unmarked legacy maps need an explicit Quake II choice. Stale map/package state
blocks the handoff. Save the map and package separately; each keeps its own undo.
Use Level Dependencies before compilation and packaging. Other game/mod speakers
use their normal entity tools. CLI `map place-sound` validates an existing package
WAV and writes a separate map using the same placement rules.
`map dependencies --package <draft.vibepackage>` reviews unpublished staged
contents too, so sound placement and dependency review use the same assets.
Review is read-only; staged deletion reports a missing reference, and an invalid
draft fails to load instead of exposing its storage files.

Analyze measures the current selection, or the whole sound at an empty cursor,
without changing the document. Its read-only channel table includes sample peak,
RMS, DC offset, full-scale counts, absolute event frames, and over-range runs.
RMS includes DC. True peak and BS.1770 integrated loudness are available at
8–384 kHz. Surround audio first needs reviewed speaker roles in source order;
mono/stereo have defined defaults. Uncheck Measure loudness to keep
sample/true-peak statistics only. Selections shorter than 400 ms and below-gate
signals have explicit results. The CLI shares the report, `--channel-map` and
`--no-loudness`; silent dBFS/dBTP and absent measurements use JSON null.
These measurements precede delivery mixing and conversion; analyze the exported
file when checking final game output. Roles are not saved as document metadata.

```sh
vibestudio --cli asset audio-edit ./sound.wav --operation normalize --db -1 --output ./normalized.wav --dry-run --json
vibestudio --cli asset audio-edit ./mod.pk3 --entry sound/wind.wav --operation trim --start-frame 0 --end-frame 22050 --output ./trimmed.wav
vibestudio --cli asset audio-project ./sound.wav --output ./sound.vsaudio --dry-run --json
vibestudio --cli asset audio-project ./sound.vsaudio --json
vibestudio --cli asset audio-export ./sound.vsaudio --preset doom --output ./DSSOUND.dmx --dry-run --json
vibestudio --cli asset audio-analyze ./sound.vsaudio --json
vibestudio --cli asset audio-new --sample-rate 44100 --channels 1 --output ./new.vsaudio
vibestudio --cli asset audio-edit ./new.vsaudio --operation paste --paste-input ./sound.wav --output ./assembled.vsaudio
```

## Project And Compiler Workflows

### Texture Editor

Choose **Textures > Texture Editor** to create a transparent image, or **Edit
Selected** to edit the displayed mip level/frame. Paint, erase, fill, and pick
colors in **Paint**. Brush offers square/round stamps and explicit replace/blend
alpha; Fill has seed-relative RGBA tolerance. Line/Rectangle/Ellipse preview
until committed; two Space commands set endpoints for cursor authoring.
Crop, resample, rotate, and cyclically offset pixels in **Transform**. **Set Canvas
Size** pads or crops all layers at the chosen anchor without resampling. In
**Selection**, resize or rotate selected active-layer pixels at a corner, edge,
or center anchor. Results must fit inside the canvas; Undo restores pixels and
selection bounds. Nearest transforms preserve exact indexed values and RGBA.
**Palette**
shows provenance, swatches, and optional indexed remapping. Tile Preview checks
seams in a 3 × 3 repeat; **Wrap painting** enables editing across tile boundaries.
Wheel zoom holds the pointed pixel. Undo and Redo preserve per-stroke history and save state.

Use **Layers** for visibility, locks, opacity, blending, import and merge.
Selections clip edits to active-layer pixels. **Save Project** preserves layers
and palette/export metadata in `.vtexture`; **Export** writes the visible composite
without marking the project clean. Opened projects detect external save conflicts.
Project saves offer Cancel during preparation and layer compression. The final
backup/publication phase checks the same destination and finishes before the
document is marked saved. Cancelled preparation leaves your edits open.
**Recovery** keeps background local checkpoints, offers a configurable interval,
and restores retained layers and palette settings as an unsaved draft. Its first
project save requires a new file. Save/Discard/Cancel continue New, Open and Close
only after a successful save. Previous project versions remain in verified backups.
The **Export** inspector offers RGBA/indexed PNG, TGA, PCX, Quake miptexture/WAD2,
Quake II WAL and Doom flat/patch. **Preview and Validate** shows encoded colors,
palette provenance, alpha changes, engine warnings and each generated mip.
Choose explicit alpha handling, native names/flags/offsets and classic or
source-port limits. Indexed output requires an actual palette or explicit
acceptance of generated stand-ins. File imports retain supported native metadata;
new mip chains come from the edited surface. Export to a project asset folder,
or set the matching filename in **Package** and use **Stage Export**. WAD2
accepts Quake miptextures with a matching bare name; Doom WADs accept flats and
patches with namespace markers and grouped package undo. Staged pixels and
palettes feed browser previews. Refresh Palette Source updates the editor's
chosen palette; existing pixels keep their colors until remapped or exported.
**Stage and Apply** textures Quake III selections using PNG/TGA, or Quake-family
selections using WAL files or matching WAD2 miptexture lumps. File texture paths
use the lower-case `textures/` prefix and profile extension. Updating pixels
under an existing map reference preserves map history. Configure the saved Quake
WAD in the map/compiler search path, and save the package and map separately.
`texture stage` writes a reviewable `.vibepackage`
draft through the same services. WAD3 encoding and Doom wall-definition
composition remain unavailable; PNG requires a compatible target engine.

```sh
vibestudio --cli texture create --size 64x64 --color transparent --output texture.png --json
vibestudio --cli texture edit texture.png --operations edits.json --output edited.png --dry-run --json
vibestudio --cli texture create --size 64x64 --output texture.vtexture --json
vibestudio --cli texture inspect texture.vtexture --json
vibestudio --cli texture recoveries ./texture-recovery --json
vibestudio --cli texture recover ./draft.vtrecovery --output ./recovered.vtexture --dry-run --json
```

The recipe is an ordered JSON array of stroke, line, rectangle, ellipse, fill,
offset, crop, resize, flip, rotate,
palette, layer and selection operations. Existing output requires `--overwrite`. See
`docs/TEXTURE_EDITOR.md` for the schema and limits. The canvas supports arrows
and Space for pixel editing, Shift-arrows for crop selection, and F to fit.

### Level Dependencies And Asset Subsets

Open a map and its package or asset folder, then choose **Levels > Dependencies**.
The cancellable scan reads the current package staging plan and lists textures,
shader images, models, model materials, and sounds with missing and ambiguous
states. Search by asset path or map object; **Select in
Map** locates the recorded users. **Copy JSON** provides the full report.
**Export Assets** reviews resolved files before writing a separate asset package.
**Packages > Export Selected** exports exact selected occurrences from an
independent snapshot of the staged edit plan, preserving live undo history.
The review distinguishes selected files and required group members, with a page
control for lists larger than 500 entries. Every reviewed page is exported.

```sh
vibestudio --cli map dependencies ./maps/arena.map --engine idTech3 --package ./assets --json
vibestudio --cli package subset ./assets ./arena-assets.pk3 --map-input ./maps/arena.map --engine idTech3 --dry-run --json
vibestudio --cli package subset ./assets ./textures.pk3 --prefix textures/arena --where "ext=tga"
```

Choose a destination outside the input folder. Map-driven exports require a
complete scan with no dependency problems; the bundle does not include the map
or BSP automatically. Decoded MDL/MD2/MD3 references are inspected; external
`.skin` overrides, secondary shader references, dynamically selected game assets,
and Doom composite texture/patch dependencies need additional review. Doom WAD
subsets preserve complete binary/UDMF maps, matching GL groups, namespace markers
and local texture name tables in source order. Incomplete or ambiguous groups
block export; WAD2/WAD3 select exact texture records. This preserves grouping,
while full game dependency closure and group-wide rename/delete remain open.
CLI `package subset --entry-index N` accepts repeatable zero-based indexes from
`package list --json`, including saved drafts. A dry run shows explicit and
required members under `subset` without writing files or preferences. Stage edits
in a draft first; subset commands refuse post-selection edits and in-place saves.
Cancel Export stops during plan preparation, source reads and compression,
preserving an existing destination before commit. Folder preparation and WAD
ordering also check cancellation. The staged document and Undo/Redo remain
available for retry.

### Design, Stage, And Place A Prop

Choose **Models > Design Prop** to assemble boxes, cylinders, and planes. Edit
part dimensions, position, and X/Y/Z rotation in **Part**. **Surface** contains
material paths and UV scale, offset, and rotation; **UV checker** previews tiling
without project textures. Select parts in the preview or parts list. Undo restores
selection and recognizes the saved design. **Handoff** contains package/level options.
Save the editable schema-2 `.model.json` source; schema-1 files still load. Export
a static MD3 or OBJ when needed; both include geometry and UV transforms.
**Stage in Package** adds generated bytes to the open package plan. With a Quake
III map open, **Stage and Place** also creates an undoable `misc_model` entity.
Generated props appear in the level's 3D preview and dependency review. Map Undo
and package staging are separate: undoing placement keeps the generated asset.

```sh
vibestudio --cli model build ./samples/models/pillar.model.json --output ./build/pillar.md3 --dry-run --json
vibestudio --cli model build ./samples/models/pillar.model.json --output ./build/pillar.md3
vibestudio --cli package save-as ./assets ./build/props.pk3 --add-file ./build/pillar.md3 --as models/props/pillar.md3
vibestudio --cli map place-model ./maps/arena.map --engine idTech3 --package ./build/props.pk3 --entry models/props/pillar.md3 --origin 0,0,32 --output ./build/arena.map --dry-run --json
```

The sample supplies geometry only; provide its `textures/props/stone` material.
There are at most 32 parts, one frame, and no animation, tags, or collision mesh.
MD3 vertices must fit its -512 to 511.984375 local coordinate range. OBJ preserves
material names but does not bundle an MTL or textures. Save map and package
outputs separately, then expose model/material files in the compiler's game
directory before running q3map2. Compiler setup does not mount the unsaved plan.
See `docs/MODEL_DESIGN.md` for the authoring schema and integration boundaries.

### Edit A Mesh

Use **Models > Mesh Editor** for OBJ/MDL/MD2/MD3 geometry or **Design Prop > Edit as Mesh**
to bake primitives. Select faces, vertices, or indexed edges in the component
table or preview. Vertices mode picks exact visible points; X-ray enables hidden
points. Geometry offers numeric transforms, extrusion, subdivision, edge splits,
distance welding, duplication, deletion, and normal recalculation. Welding
preserves UV/normal seams by default. Surface edits UVs/materials; Animation
duplicates, deletes, or renames frames. Topology changes span every frame;
position transforms can target the current frame.

Choose Move, Rotate, or Scale beside Gizmo. Drag labelled axes, rotation rings,
or the centre box for view-plane movement or uniform scaling. Geometry's Pivot
chooses the model origin, selection bounds centre, or custom coordinates. The
displayed pose supplies a fixed pivot across all affected frames. Release
validates one undo step; Escape cancels. Snap uses separate move, angle, and
scale steps and also applies to numeric transforms. Scale snapping rounds the
distance from 1; invalid or zero-scale gestures cancel on release.
Rotate also has a round Free centre and a dashed trackball circle. Drag the
centre or unoccupied circle interior for free rotation; axis rings retain
priority elsewhere. Outside the circle, the drag follows the rim. The starting
view, pivot and axes stay fixed, and returning to the start is neutral. Free
rotation snaps its total angle while preserving its axis. Its XYZ preview can
be reproduced through Geometry or the existing numeric CLI with angle snapping
off, using the same axes, pivot and frame scope. Mesh components, whole surfaces,
tags and static/animated collision share this one-step undo workflow.
Geometry's Transform axes chooses World, Selection, or Custom. Selection follows
the first usable selected or touching face, the active whole surface, or a selected
tag/box orientation. The displayed pose fixes the axes across affected frames.
Custom reveals XYZ orientation angles; the pivot stays in model coordinates.
Extrude and Duplicate Faces use Offset in the same axes. Changing axes cancels
the active preview. Isolated or degenerate selections can use World or Custom.
The view selector provides exact top, front, and side planes, orbit, and
perspective. Save the `.mesh.json` source separately from game exports.

Open polygonal `.obj` files directly or use `model import prop.obj --output prop.mesh.json`.
The importer retains UV/normal seams and smoothing groups, triangulates concave
planar faces and keeps the original axes and units. Direct `usemtl` paths resolve
through package materials. Wavefront material libraries (`mtllib`), vertex colours,
free-form geometry, lines, points and loose vertices require conversion in the
source modeller; unsupported records fail with a line diagnostic. Package OBJ
and native model previews load on a cancellable worker and include staged changes.
Cancel Preview stops loading; select the model again to retry. Native frames,
skins, tags and raw headers remain available, with separate surface materials.
Returning to Models or invoking a model command preserves the current preview's
warnings and cancellation until its source, selection or palette changes.
Filled and wireframe previews prepare in the background. Navigation stays
available; picking resumes when the displayed camera and pose catch up.
Wireframe retains every edge, with thicker dashed selection cues that scale
with display density and high-visibility themes.
Vertex markers and exact indexed picking prepare on the same worker. Selection
and frame changes retain the component table and its selected rows; unchanged
inspector refreshes reuse the completed preview. Full cell values remain in
tooltips and accessibility data. No new renderer or setup option is required.
Mesh Editor requires decoded geometry; header-only formats cannot open a placeholder
mesh or export a frame. See
[OBJ interchange](../docs/MODEL_MESH.md#obj-polygon-interchange) for bounds and details.

The browser's **Export OBJ** and `model export` write one geometry frame with
guarded publication. Choose a destination outside the input package folder or
portable draft; use package staging to change those contents. Browser export
shows cancellable progress and preserves existing output on cancellation before
commit. The CLI supports `--dry-run` and explicit `--overwrite`, protects loose
source files, and reports omitted data in JSON `notes` or stderr beside raw OBJ
stdout. Use the mesh editor's OBJ export to retain per-surface material assignments.

The UV tab shares component selection. Pick Islands selects complete charts;
Select Islands expands the current components. Mark or clear selected edge seams
in Surface, then detach or move selected faces to split their shared UV corners.
Seam marks remain in editable sources and recovery; MD3/OBJ retain resolved UVs
and split indices. Middle-drag pans, the wheel zooms, F frames selection, and Home
frames all. Drag the centre square to move UVs; Escape cancels. Numeric UV pivots
choose origin, selection centre, custom coordinates or Individual Islands.
Individual Islands rotates/scales each complete chart around its own centre;
use Pick Islands or Select Islands first. Projection computes each centre in
the displayed pose. Shared corners split with all pose geometry and normals
preserved; partial islands fail without edits. Snap UV Offset rounds
numeric and dragged offsets to the chosen tile step. Version-3 sources store
seams and MD2 skin sizes and accept version-1/2 input. `model uv` inspects charts through the CLI.

```sh
vibestudio --cli model import ./samples/models/angled-panel.model.json --output ./build/panel.mesh.json
vibestudio --cli model edit ./build/panel.mesh.json --operation transform --faces all --offset 0,0,8 --output ./build/raised.mesh.json
vibestudio --cli model build ./build/raised.mesh.json --output ./build/panel.md3 --dry-run --json
```

In **Health**, **Inspect Surface** reports duplicates, unused vertices,
disconnected fans, winding conflicts, branching edges and boundaries. Select a
category and **Select Findings** to highlight its components. The five repair
actions remove duplicate faces or unused vertices, split disconnected fans or nonmanifold edges, or
orient faces consistently. Repairs affect the entire active surface in every
pose, preserve UVs and authored normals, and commit one undo step. Boundaries are
informational. Nonmanifold splitting keeps existing two-face connections and
copies affected endpoints without dropping faces or moving any pose; new open
boundaries can result. Other holes still need an explicit filling decision. Face orientation
retains each component's lowest-index face as its seed, without guessing outside.
`model topology --json` adds exact `health` indices; `model edit --operation
remove-duplicate-faces|remove-unused-vertices|split-disconnected-fans|split-nonmanifold-edges|orient-faces`
uses the same repairs with `--surface N`, dry-run and normal save protection.

**Repair Import…** prepares a copy of a damaged mesh JSON, MDL, MD2 or MD3 before
normal source admission. Review the exact removed-face, rebuilt-normal and seam
indices beside the prepared-copy preview. Faces unusable in any pose are removed
in every pose. Other vertices, UVs, usable normals, skins, tags and clip timing
remain intact; any +Z normal fallback is listed explicitly. Save and Open Copy
requires a new `.mesh.json`, protects existing files and checks the reviewed
input for changes. Incomplete decodes, missing arrays, invalid metadata and
malformed OBJ polygons remain blocked. Cancel preserves the current editor.
`model repair-import <source> --json` reviews without writing; add `--output
<new.mesh.json>` to save, or `--dry-run` with an output to validate it.

**Health > Geometry intersections** scans crossings and coplanar area overlaps
within and between all surfaces. Choose All poses or Current pose, then Inspect
Intersections. Each face-pair finding identifies its pose and both surface/face
indices. Show First Face or Show Second Face pauses playback and selects that
exact face for ordinary authoring. Edits invalidate the report; cancellation
retains the last complete result. Shared boundaries and isolated point contacts
are allowed. Motion between stored poses and automatic repair are outside the
scan. `model intersections <source> --frame all --json` shares the service;
optional `--surface N` retains contacts with other surfaces. Workload limits
fail explicitly without publishing a partial report or changing any source.

**Surfaces** selection mode retains a whole-surface set across viewport clicks,
table selection, edits, undo and recovery. Control-click toggles a surface; the
table supports ranges and Select All. Table focus chooses an active selected
member; the surface combo adds and activates a member. Move, Rotate, Scale and
numeric Geometry transforms include unused vertices and use one common pivot
from the displayed pose. All-frame edits retain that fixed pivot; current-frame
edits affect one pose. Numeric mirroring requires all frames. Component geometry
and UV edits require Faces, Vertices or Edges mode. Materials inspect the active
surface; tags and collision remain separate. `model edit --operation transform
--surfaces all|0,2` uses the same service with normal pivot, snap, frame and output
options. The selector cannot be mixed with `--surface` or component selectors.

**Surface > Manage Surfaces** reviews rename, separate, move, duplicate, delete
and join operations. Separate and Move use selected faces; Join has a checked
surface list initialized from Surfaces mode and an explicit target. All operations retain every pose, UV, normal,
seam and ordered material list. Move/Join require matching materials unless
**Use target material bindings** explicitly replaces incoming slots. One undo
step and normal recovery cover each operation. New/moved faces stay selected for
finishing. If all faces are separated, the existing surface is renamed in place;
moving all faces removes the source surface. External `.skin` references may
need updating after name changes. `model surfaces <source.mesh.json>` lists the
surfaces; `--operation rename|separate|move|duplicate|delete|join` exposes the same
edits with explicit selectors, `.mesh.json` output, dry-run and overwrite checks.
See [Surface Authoring](MODEL_SURFACES.md) for complete syntax and capacity limits.

**Surface > Manage Material Slots** reviews ordered external texture/shader paths.
Add, Replace, Remove, Move Up/Down and Clear All change the buffer; Apply commits
one undoable all-pose edit. Duplicate paths are preserved. **Preview slot** chooses
the image shown for each surface without changing source or export order;
**Assign Material** changes that selected binding and **Show Material** opens its
path. Embedded MDL skins retain Animation's native skin controls. `model slots`
offers the same list operations; `model materials --surface N --material-slot N`
resolves an alternate through the same package snapshot. See
[Material Slots](MODEL_MATERIAL_SLOTS.md) for slot limits, CLI examples and exports.

In Levels, each Quake III MD3 `misc_model` can use its own `_skin` suffix and
`_remap*` material mappings. The entity inspector and `map edit` retain normal
history and save-as; Camera Details and `map materials --json` show exact skin
inputs, hashes, omitted surfaces and map users. Staged package skins participate
in preview and dependency export. Compiler skins match material names, while
native player skins match surface names. The pinned q3map2 importer ignores
nonzero MD3 frame selection: bake the desired pose to a separate static MD3 in
Assemblies, then place that asset at frame zero. See
[Placed Model Appearances](LEVEL_MODEL_APPEARANCE.md) for the verified contract.

**Geometry > Fill Boundary Loops** closes chosen holes. Select at least one
boundary edge per hole in Edges mode; the action expands each to its complete
loop. The displayed pose chooses the triangles and every animation pose is
checked before one undo step commits. New faces stay selected for UV projection,
detachment or normal work; existing vertex attributes remain intact. Branching,
crossing or collapsing caps and new intersections on the active surface fail
without changes. Concave and suitably projectable nonplanar loops are supported;
strongly folded loops may require manual editing. This is not a general
self-intersection audit. CLI: `model edit --operation fill-boundary-loops
--edges a:b,c:d --source-frame 0 --output filled.mesh.json`, with optional
`--surface N`, dry-run and normal save protection. Topology always covers all
poses; `--source-frame` only chooses the reference (default 0). See the
[editable mesh reference](MODEL_MESH.md) for geometry and workload limits.

**Geometry > Bridge Boundary Loops** connects exactly two open boundaries on one
surface. Select an edge from each in Edges mode; unequal vertex counts work too.
Use **Bridge twist** to shift the second anchor from the closest pair in the
displayed pose. Every stored pose is validated before one undo step commits.
The new faces stay selected for Detach/Unwrap, UV projection and normal work;
existing vertex attributes are retained. Join separate surfaces first if needed.
Invalid geometry or Cancel leaves the model intact. CLI: `model edit <source>
--operation bridge-boundary-loops --edges a:b,c:d --bridge-twist 0 --source-frame 0
--output bridged.mesh.json`. Both numeric options default to 0; twist accepts
-1023 to 1023 and wraps by the second loop's size. See the
[editable mesh reference](MODEL_MESH.md) for candidate-search and geometry limits.

In Surface, **Unwrap and Pack Faces** creates charts from selected faces using
the displayed pose, marked seams and existing indexed boundaries. **Pack Selected
UVs** preserves island shape, orientation and relative scale. Atlas Width defaults
to 512; Same as width keeps the height equal. Turn it off and set Atlas Height for
a rectangular image, with both axes in 32–4096 pixels. Padding defaults to 4 and
must stay below one eighth of the smaller axis; use Select Islands for whole charts.
Match the material image's dimensions to retain pixel proportions. CLI accepts
`--uv-atlas-size 512` or `--uv-atlas-size 512x128`. These controls do not resize
images or change MD2 export skin dimensions.
Both run offline with progress/Cancel and one undo step, preserving geometry in
every pose. Unselected UVs stay fixed and can overlap the new atlas. Existing
texture images are not repainted. CLI equivalents are `model edit --operation
uv-unwrap|uv-pack --faces all --uv-atlas-size 512 --uv-padding 4`.

Use **Pack Around Unselected** to keep new islands out of existing painted regions.
Select complete islands first. Unselected faces and other surfaces sharing any
material slot stay fixed; their UVs must fit the 0–1 tile. **Fixed-region packing**
offers **Fit uniformly** or **Keep current UV scale**. Both keep orientation and
relative density; the latter refuses if there is not enough space. Atlas
dimensions and padding apply. The bounded search may leave unused space and
does not repaint images. Progress, Cancel, undo and ordinary source/export
handoffs are shared. CLI uses `--operation uv-pack-around --uv-pack-scale
fit|preserve`, with `--uv-islands` to expand the selection when needed.

MD2 export preserves all poses and ordered skin slots, checks byte-precision
collapse/reversal, and reports position/normal/UV loss. Apply matching PCX skin
dimensions in Handoff, then choose Export MD2 or a `.md2` package path. It targets
original Quake II limits; MD2 level placement still requires game-specific work.
Skin settings are saved, undoable and recoverable. The CLI uses `model edit
--operation md2-skin-size --skin-size 320,200` and `model build --format md2`.
Use **Animation > Animation clips** to add, rename, range or delete saved clips.
The first and last indices are inclusive; clips may overlap or contain one pose.
Choose a clip or All frames for preview. Selecting a clip adopts its saved FPS.
Preview FPS overrides the session; **Saved clip FPS / Apply Clip FPS** stores
0.001–1,000 FPS in the editable source (zero means unspecified). This uses one
undo step and retains fractional rates through save/recovery. Reduced motion
disables playback. **Smooth preview** blends poses and attachments,
including the selected clip's end-to-start loop. Disable it for discrete frames;
pause to return to the stored pose for editing. It does not modify the source.
**Copy Full Pose** copies every surface and attachment
from the chosen source into the displayed frame, retaining its name. **Insert
In-between Frames** generates poses between the displayed frame and its next
frame, with a count and name prefix. Existing poses remain exact; incompatible
tag handedness, cancelling normals or collapsed generated geometry rejects the
whole edit. Only clips containing both endpoints grow; later clips shift.
These operations use normal validation, cancellation, undo and recovery.
`model animations` lists clip indices; `model edit --operation
add-clip|rename-clip|set-clip-range|set-clip-fps|delete-clip|insert-inbetweens|copy-frame-pose`
provides the same authoring through the [documented options](MODEL_MESH.md#cli).
Native exports report clip metadata they cannot store; configure game timing
separately and retain the editable source.

Use **Animation > Attachment tags** to add, duplicate, rename or delete an
attachment in every frame. Select one name in **Tags** component mode, then
move/rotate its origin and local axes with the gizmo or numeric Geometry
controls. **Set Origin**, **Reset Orientation** and **Copy Tag Pose** use the
all/current-frame scope; scale is unavailable. **Use Selection Centre** fills a
new tag's origin from selected geometry. Names must be unique printable ASCII
and fit 63 characters. Undo and local recovery retain the selected tag and all
poses. `model tags` inspects them; `model edit --operation add-tag` and the other
[tag operations](MODEL_MESH.md#cli) use the same document service.

Use **Models > Assemble** to link models through these tags. **Add Part** chooses
a file or current-package model, parent tag, local transform and independent
frame range/FPS/phase. **Apply** commits the form; undo/redo includes the
selected part. Save the links as `.assembly.json`; file references follow the
source directory and package references need the intended package context.
**Details** lists input fingerprints, sampled frames and material diagnostics.
Per-part **Materials** selects original model materials, a linked **Skin file**
or a **Package skin**. **Choose Skin…** selects the source; **Apply** resolves
its complete Quake III surface assignments without changing the model file.
**Skin entry** identifies an exact package occurrence or a unique path. Details
shows verified skin bytes and material mappings; Reload Inputs rereads changes.
Skin links use assembly schema 3, including when native animation is present.
Undo/recovery retain the links; bakes retain their resolved material assignments.
CLI part edits accept `--skin`, `--skin-kind file|package`, `--skin-entry-index`,
or `update --clear-skin`. See [Linked Skins](MODEL_ASSEMBLY.md#linked-skins).
Missing references leave an editable recipe with preview and bake disabled.
**Bake Pose to Mesh** explicitly opens one composed pose in the normal mesh editor
for authoring, staging and level placement. **Export Pose…** writes OBJ/MD2/MD3
with normal target and overwrite checks. Both explain omitted hierarchy,
independent animation, tags and embedded skin data. `model assembly` exposes the
same services, including dry runs. **Bake Animation… / Export Animation…** reviews
start time, count, FPS and clip name, sampling at `start + index / FPS` without
a duplicate endpoint. It produces a full editable sequence with saved clip
rate, or MD2/MD3 within normal format limits. MD2 requires one surface; explicitly
join compatible surfaces in the Mesh Editor when needed. Links, collision,
tags and native skins stay in the originals. Sampled topology/winding must
remain constant. `model assembly --operation bake-animation --frames 30
--sample-fps 30 --output ./baked.mesh.json` uses the same service. Native game
animation configuration is still required. For Quake III player parts,
**Native Animation…** imports or authors all 31 `animation.cfg` slots, binds
lower/upper models and selects their preview clips. Adjusted frame ranges,
loop tails, reverse playback and native millisecond timing feed the ordinary
timeline and bakes. **Apply and Export…** writes the native configuration;
undo, source save and recovery preserve it in assembly schema 2. A composed
bake needs its own frame mapping. `animation-set`, `animation-export` and
`animation-clear` provide the same [CLI workflow](MODEL_ASSEMBLY.md#quake-iii-native-animation).
**Player Package…** reviews separate native lower/upper/head models, skins,
animation configuration, a converted icon and captured material dependencies.
Open the project assets, choose the native roles and icon, then **Review Package**
and **Export PK3…**. Every output has a hash; changing options requires a new
review. Publication uses the protected deterministic package writer. Team skins,
custom sounds and gameplay acceptance remain separate. CLI `player-review` and
`player-export` share the [native player workflow](MODEL_ASSEMBLY.md#native-player-packages).
**Keep local recovery copies** shares the Mesh
Editor preference. Applied recipe edits, selected part and time are checkpointed
locally; **Recoveries…** restores a verified unsaved draft that must use a new
source path. Missing inputs remain repairable, and package parts use the current
package context. Recovering keeps the reviewed copy. Save the recipe and its
referenced assets normally; recovery does not embed model or package data.
CLI operations `recoveries`, `recover` and `discard` use the same store; the last
two require a reviewed recovery ID and SHA-256. The optional
[engine acceptance workflow](MODEL_ENGINE_ACCEPTANCE.md#baked-assembly-animation)
checks a small nested animation bake through independent MD2/MD3 geometry and
FTE render-target images. Target-game timing and original-engine gameplay
acceptance remain separate requirements.
See [Model Assemblies](MODEL_ASSEMBLY.md) for CLI examples and limits.

The Mesh Editor's **Collision** tab creates static oriented boxes. Add or fit a
box to selected components (or the whole model), then use its list and numeric
centre/size/rotation controls. **Collision** component mode also selects boxes by
their edges or table rows. Move and rotation use Geometry's chosen transform axes; scale uses labelled
local box axes. Geometry supplies numeric equivalents, pivots and snapping.
Each gesture previews before committing one undo step; Escape or an invalid
release cancels. Boxes stay static across all animation poses.
Solid thicker outlines identify the selected box;
other boxes are dashed, and all draw through the model. Source saves, undo and
local recovery retain them. Native model exports and assembly bakes report
omitted collision. **Export Collision Map…** and **Place Collision in Level**
use explicit Quake/Quake II/Quake III target conventions. Quake III also needs
your project's clip shader in compiler assets. Placement is one level undo step;
save and compile the map. Later prop edits do not update these static brushes.
`model collision` shares the authoring, export and placement services, with dry
runs and JSON diagnostics. See [Model Collision](MODEL_COLLISION.md).

Editable/OBJ faces are counter-clockwise; MDL/MD2/MD3 import and export convert
clockwise native winding without changing normals or UV corner identities.
Existing editable files retain their triangles. Review older native imports
before exporting; see [engine acceptance](MODEL_ENGINE_ACCEPTANCE.md) for
compatibility guidance, the optional headless proof and source-port findings.
MD3 export preserves all frames and tags, checks quantization, and limits surfaces
to 1,000 vertices and 2,000 triangles for the original Quake III renderer. OBJ
exports one frame and reports omitted attachment tags. MD2 refuses tagged
sources. Keep local recovery copies saves dirty documents in the
background; Recover restores a verified copy as an unsaved draft. Quake MDL
imports retain indexed skins, native groups/times and header settings. The Quake
MDL tab can add/replace/append indexed PNG, PCX, LMP/miptexture, WAL or M8 members, change durations,
group poses, edit flags and load a 768-byte RGB palette without remapping indices.
**Import Package Texture…** selects an exact entry from the current staged
package, then copies its indexed pixels into a skin slot/member with normal
undo. Existing palette and dimensions must match. The CLI accepts the same
handoff through `--package` and `--entry` or `--entry-index`, including saved
package drafts; import leaves the package unchanged.
Preview Member uses the chosen image in the model and UV views until the source
or skin selection changes. Save the schema-4 source, then Export MDL or stage a
`.mdl` package entry. `model mdl` exposes the same edits; `model build` writes the
native derivative. Preview Native Timing chooses stored software-Quake or original-GLQuake schedules for the selected native frame and skin. Seek Time and entity phase are deterministic; reduced motion prevents play but permits seeking. The CLI exposes the same sampler with `--time`. Original-engine acceptance, advanced atlas
constraints and the full production interaction/renderer audit remain unfinished.
GLQuake-style skin loading flood-fills the colour connected to the first skin's
top-left texel. Keep background padding separate from used UV regions and check
the target engine; studio previews preserve indices without emulating this
preprocessing. The optional offscreen engine workflow checks native faces,
texture orientation and poses using engine-written screenshots and deliberate
failure controls.
See `docs/MODEL_MESH.md`
and `docs/MODELLER_RELEASE.md` for the complete boundaries and release gate.

Surface's **Apply .skin File…** and **Apply Package .skin…** copy Quake III
surface-to-shader assignments into primary model materials in one undo step.
Every surface must be covered. The package picker selects an exact staged entry;
unused bindings and ignored attachment markers appear in **Last Skin Import
Details…**. Material previews refresh through the shared resolver. Save, export
and stage the edited model normally; there is no live link to the skin file.
`model skin <source.mesh.json> --file <file.skin> --output <source.mesh.json>`
provides the same edit with dry-run/JSON support. Package drafts, archives and
folders use `--package` with `--entry` or `--entry-index`. Embedded MDL skins
continue to use the Quake MDL inspector.

### Project And Compiler Commands

Use project manifests to keep source folders, package folders, outputs,
installation choices, AI settings, and compiler overrides in one place.

Common CLI commands:

```sh
vibestudio --cli project init ./samples/projects/quake-minimal
vibestudio --cli project info ./samples/projects/quake-minimal --json
vibestudio --cli compiler profiles --json
vibestudio --cli compiler plan ericw-qbsp ./samples/projects/quake-minimal/maps/start.map --dry-run --json
vibestudio --cli compiler run ericw-qbsp ./samples/projects/quake-minimal/maps/start.map --dry-run --task-state --json
```

Compiler runs capture command manifests, stdout/stderr, diagnostics, duration,
exit code, task-log entries, registered output paths, and hashes where outputs
exist. If a compiler executable is missing, the plan and run reports explain
which tool is unavailable and what path was checked.

## AI and Automation

AI features are optional and provider-neutral, and AI-free mode, the default,
keeps all of them off. Settings > AI and Automation chooses the connectors: a
text model for the Assistant and for level plans, and an image model for
textures (OpenAI, Gemini, or a local Stable Diffusion web UI such as
AUTOMATIC1111 or Forge at `http://127.0.0.1:7860`). A request to an endpoint
off this machine needs cloud connectors allowed, shows exactly what it sends
the first time a project sends there, and carries only what you chose.

**Generate Level** (on the Levels header, or in the command palette) builds a
sealed, playable level from a description for Quake, Quake II, Quake III or
Doom. The rules plan it on this machine, the same way for the same description
and seed, or the text model plans the rooms, links and placements as JSON that
is checked and repaired before anything is built. Review the layout preview,
the plan and the notes, then choose Open in Levels for a new, unsaved map, or
Save As.

**Edit with AI** (also on the Levels header) changes the open Quake-family
map from an instruction such as "add a light above each player start". The
text model proposes actions (add an entity, set or remove a key, add a box,
retexture, move, delete); each is checked against the map and listed as
Ready or Blocked with the reason. Uncheck any you do not want, then Apply
Checked: every action is an ordinary edit with its own undo step. Save
Proposal keeps the list, and Load Proposal applies a saved one with no model.

**Generate Texture** (on the Textures header) draws variants with the image
model, or starts from your own picture with no AI. Each variant is cropped,
blended to tile, resampled, and converted to the game's format and palette,
optionally with the companion maps source ports read (`_norm`, `_gloss` and
`_glow`, or `_n` and `_s` for ioquake3). Save to Project writes it where the
game reads it and records what made it under `.vibestudio/generated`.

**Generate Sound** (on the Audio header) makes sound effects from a
description. The synthesizer makes them on this machine with no AI, the same
for the same description and seed; the sound model (ElevenLabs, or a custom
endpoint) is optional. Each variant is trimmed, faded, normalized, looped
seamlessly where asked, and delivered in the game's format: a DMX lump for
Doom, an 11 kHz WAV for Quake, a 22 kHz WAV for Quake II and III. Play the
variants, then Save to Project, Save and Place in Map (a target_speaker in a
Quake II or III map), or Open in Audio Editor.

Common CLI commands:

```sh
vibestudio --cli ai status --json
vibestudio --cli map generate --prompt "gothic castle with lava pits, 8 rooms" --game quake --output ./maps/castle.map --preview ./maps/castle.png
vibestudio --cli map plan --prompt "quake 3 duel arena" --planner ai --dry-run
vibestudio --cli map ai-edit ./maps/start.map --prompt "add a light above each player start" --save-proposal ./edits.json
vibestudio --cli asset audio-generate --prompt "heavy metal door slam" --game quake --output ./mymod --preview ./door.wav
vibestudio --cli texture generate --prompt "rusted riveted metal plate" --game quake --palette-root ./id1 --dry-run
vibestudio --cli texture generate --from-image ./photo.png --game doom --surface floor --palette-root ./doom2
vibestudio --cli ai image --prompt "slipgate chamber concept art" --output ./concepts/ --dry-run
vibestudio --cli ai explain-log --text "qbsp failed: leak near entity 1" --json
```

`--dry-run` shows a request without your API key and sends nothing, and a
request that leaves this machine needs `--yes`. Generated work is reviewed
before it is written: plans are validated and repaired, textures are previewed
tiled, and every output reports where it went. See `docs/AI_AUTOMATION.md` for
the providers, the rules, and the limits.

## Accessibility And Localization Smoke Checks

Before publishing a release asset, verify:

- Text scale presets: 100%, 125%, 150%, 175%, and 200%.
- High-contrast dark and high-contrast light themes.
- Keyboard-only setup completion or skip/resume.
- Pseudo-localization and right-to-left smoke modes for Arabic and Urdu.
- Translation catalog status, including missing, unfinished, obsolete, and
  vanished message counts.
- Translation expansion stress sample, layout smoke checks, pluralization
  samples, locale formatting output, and dry-run `lupdate` extraction.
- TTS test phrase and one task-result announcement where OS support exists.
- Non-color-only status labels for project, package, compiler, AI, and setup
  states.

Useful CLI checks:

```sh
vibestudio --cli localization targets
vibestudio --cli localization report --locale ar --json
python scripts/extract_translations.py --check --dry-run
vibestudio --cli diagnostics bundle --output ./diagnostics
```

## Release Bundle Contents

Portable release assets include:

- `bin/` with the staged VibeStudio executable.
- `README.md`, `VERSION`, and `docs/`.
- `docs/OFFLINE_USER_GUIDE.md`.
- `i18n/` with seed Qt TS catalogs for the 20-language target set plus
  pseudo-localization.
- `samples/` with license-clean Doom, Quake, and Quake III-family projects.
- `licenses/THIRD_PARTY_LICENSES.md` plus VibeStudio and imported compiler
  license files.
- `platform/README.txt` with target-specific launch notes.
- `package-manifest.json` and `CHECKSUMS.sha256`.

## Troubleshooting

- If a package fails to open, inspect the Activity Center task details and run
  `vibestudio --cli package validate <path> --json`.
- If a compiler profile is unavailable, run `vibestudio --cli compiler list
  --json` and configure a tool path override.
- If setup was skipped, reopen the setup panel or use the CLI setup commands to
  resume, advance, complete, or reset it.
- If a support report is needed, run `vibestudio --cli diagnostics bundle
  --output <folder>`; the bundle excludes secrets, environment values, home
  directory contents, and project file payloads.
- If AI must stay disabled, keep AI-free mode enabled; package, compiler,
  project, validation, CLI, and launch/test preparation workflows still work.
- If a release artifact is being checked on a clean machine, run the commands in
  `platform/README.txt` and compare the results with `package-manifest.json`.
