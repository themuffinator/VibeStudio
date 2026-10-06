# VibeStudio

<p align="center">
  <a href="VERSION"><img alt="Version" src="https://img.shields.io/badge/version-0.1.0-rc1-0A66C2?style=for-the-badge"></a>
  <img alt="Development Stage" src="https://img.shields.io/badge/stage-extremely%20early%20pre--alpha-B85C00?style=for-the-badge">
  <a href="#tech-stack"><img alt="Tech Stack" src="https://img.shields.io/badge/stack-C%2B%2B20%20%7C%20Qt6%20Widgets-00599C?style=for-the-badge"></a>
  <a href="#build-and-run"><img alt="Build" src="https://img.shields.io/badge/build-Meson%20%2B%20Ninja-4C8EDA?style=for-the-badge"></a>
  <a href="#overview"><img alt="Platforms" src="https://img.shields.io/badge/platforms-Windows%20%7C%20macOS%20%7C%20Linux-444444?style=for-the-badge"></a>
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/license-GPLv3-2EA44F?style=for-the-badge"></a>
  <a href="https://github.com/themuffinator/VibeStudio/actions/workflows/pr-ci.yml"><img alt="PR CI" src="https://img.shields.io/github/actions/workflow/status/themuffinator/VibeStudio/pr-ci.yml?label=PR%20CI&style=for-the-badge"></a>
  <a href="https://github.com/themuffinator/VibeStudio/actions/workflows/compiler-submodules.yml"><img alt="Compiler Imports" src="https://img.shields.io/github/actions/workflow/status/themuffinator/VibeStudio/compiler-submodules.yml?label=compiler%20imports&style=for-the-badge"></a>
</p>

## Introduction

VibeStudio is an open-source, cross-platform development studio for idTech1,
idTech2, and idTech3 game projects. Its goal is to unify level editing,
modelling, texture and audio workflows, scripting and shader tooling, package
management, asset inspection, compiler orchestration, diagnostics, automation,
and optional AI-assisted workflows in one integrated environment for creating,
validating, packaging, and launching classic game content.

> [!WARNING]
> VibeStudio is at an extremely early pre-alpha stage. Real format work now
> exists — idTech image decoding, DEFLATE, brush and sector geometry, model
> geometry, BSP inspection, entity definition validation, chained compiles —
> but it sits behind inspect, preview, and save-as surfaces, not production
> editors. Many studio features described below remain product goals and
> roadmap targets; the implemented surfaces are listed in Current Development
> State. It is not ready for production modding, mapping, packaging, or
> asset-authoring work.

The product direction borrows the clear, always-in-context workflow of modern
idStudio-style tools while staying grounded in the constraints and file formats
of Doom, Quake, Quake II, and Quake III-era games.

<details>
  <summary><strong>Table of Contents</strong></summary>

- [Introduction](#introduction)
- [Overview](#overview)
- [Current Development State](#current-development-state)
- [Studio Goals](#studio-goals)
- [Initial Scaffold](#initial-scaffold)
- [Imported Level Compilers](#imported-level-compilers)
- [PakFu Lineage](#pakfu-lineage)
- [Build and Run](#build-and-run)
- [CLI Quick Reference](#cli-quick-reference)
- [Documentation](#documentation)
- [Credits](#credits)
- [Tech Stack](#tech-stack)
- [License](#license)

</details>

## Overview
- Current version: `0.1.0-rc1` (see `VERSION`).
- Cross-platform targets: Windows, macOS, Linux.
- Build system: Meson + Ninja.
- UI framework: Qt6 Widgets.
- Primary scope: end-to-end idTech1-3 game development.
- Product emphasis: efficient, AI-accelerated, AI-optional workflows that reduce setup friction, repeated work, context switching, and time-to-test.
- Accessibility emphasis: high-visibility themes, scalable UI, OS-backed TTS, keyboard/screen-reader support, and localization-first design.
- Repository state: pre-alpha, with documentation, CI, compiler submodules, dependency-free idTech format readers, a painted Qt Widgets shell, and a documented CLI (`cli commands` lists the current registry).

## Current Development State

[Editor familiarity profiles](docs/EDITOR_PROFILES.md) now cover 19 choices:
VibeStudio, TrenchBroom, GtkRadiant, Q3Radiant, NetRadiant, NetRadiant Custom, QuArK, Hammer/Worldcraft,
J.A.C.K., Sledge, DarkRadiant, Doom Builder 2/X, Ultimate Doom Builder, SLADE, Eureka,
Unreal, Unity, Godot and Blender. Controls, layout, settings and CLI share the
catalog; searchable adaptation notes identify remaining upstream differences.
Per-profile gesture and camera-key customization shares validated GUI/CLI
settings, command-overlap diagnostics and portable VibeStudio import/export,
preserving the current map and view workspace.
Sledge adds temporary hold-Space navigation, pitch keys and Shift+arrow pan;
standalone NetRadiant keeps its own zoom, deletion, grid and camera defaults.
Q3Radiant adds classic position steering and fixed 32-unit/22.5-degree camera
steps, with shared surface, patch and brush commands.
The **Surfaces** tab provides queued, undoable texture shift/rotation/size/fit
controls while the map remains visible, including Q3Radiant texture shortcuts
and explicit selected-brush or inspected-face targets.
Temporary view maximization restores prior pane sizes; equal-size commands,
profile shortcuts and saved views share the [level workspace](docs/LEVEL_EDITOR.md#four-view-workspace).
Plan and camera status labels adapt to narrow panes, enlarged text and RTL layouts.
These controls do not add another engine's formats or establish production readiness.

Portable [`.vibeworkspace` files](docs/WORKSPACES.md) now restore project,
package/draft, map, saved code tabs, asset selections and active module through
shared GUI/CLI services. The [asset format catalog](docs/ASSET_FORMATS.md)
centralizes capability discovery and image routing. DDS/FTX import and export,
SWL import, TIFF routing and ZIP-family PK4/PKZ aliases extend asset exchange.
Use `asset formats`, `asset route`, `workspace create` and `workspace inspect`
for automation; exact support and remaining PakFu gaps are documented.

Textures, Models and Audio now share authoring, edit and export commands across
their headers, Tools menus, command search and custom shortcuts. Empty pages offer
direct editor entry points; each asset's Package menu returns to its source,
reviews staged changes or saves the draft. See [shared workbench workflows](docs/EFFICIENCY.md#shared-asset-workbench-commands).

Levels includes a [Scene tab](docs/LEVEL_SCENE.md) for persistent layers and nested
groups, shared undo/CLI operations, inherited editor visibility and editing locks, and membership
that follows structural edits. Saves, recovery, compilers and packages retain
hidden content; WAD map groups carry scene metadata with their native data.

[Placement and grid alignment](docs/LEVEL_EDITOR.md#placement-and-grid-alignment)
share texture locking, entity ownership, scene protection and atomic undo.
Explicit duplicate/paste offsets have asynchronous package-backed previews and
CLI parity. Quick Snap, Duplicate and Paste also run on workers, with cancellable
progress for longer edits and guards against publishing obsolete results.
Generated compiler proofs verify UVs and model/audio dependencies
through PK3 publication.

[Map opening](docs/LEVEL_EDITOR.md#background-map-opening) runs parsing, brush
solving and validation on a cancellable worker. Successful loads reuse solved
geometry in the plan; failed/cancelled opens preserve editing and recovery.
WAD routing reads directory metadata without copying asset payloads.

[Doom camera editing](docs/LEVEL_EDITOR.md#doom-camera-editing) now joins sector
floors/ceilings, composite wall materials, texture offsets/pegging and camera
painting with staged package assets and exact dependency input evidence.
`map materials --geometry` exposes the same preview diagnostics. Engine effects,
portable Doom dependency subsets and broader native acceptance remain open.

[Material Painting](docs/LEVEL_EDITOR.md#material-painting) connects Levels'
camera to exact brush-face, patch and Doom wall targets with one undo per stroke.
Sampling, keyboard target controls and `map paint-material`/`sample-material`
share validation, persistence and package/staging previews. Placed model assets
keep their source material workflow in Models; camera preview limits are explicit.
Q3Radiant/GtkRadiant add middle-click material-name sampling and Shift+middle
single-surface painting; both NetRadiant profiles add middle-click sampling.
The gestures retain the active tool and use configurable, conflict-checked
preferences shared with the CLI. The [surface clipboard](docs/LEVEL_EDITOR.md#surface-clipboard)
copies material, mapping and flags; Q3Radiant/GtkRadiant paste onto hit brushes
with Ctrl+middle or hit faces with Ctrl+Shift+middle. Surfaces offers explicit
world projection and seamless corner wrapping with package dimensions and one
undo. NetRadiant Shift+middle pastes face parameters; NetRadiant Custom Ctrl+middle
wraps one face and advances the copy source after success. Custom Shift+middle
pastes native values onto the hit and selection, including patch materials;
Alt+Shift and Alt+Ctrl transfer mapping only, preserving texel density.
Ctrl+Shift projects the hit and selection with native brush semantics and patch
UVs; Alt+Ctrl+Shift retains materials and flags. Edge-on results are reported.
Hold these Values/Project/Wrap gestures to cross surfaces with a live preview;
release commits one undo step, while Escape discards the complete stroke.
Only the first hit includes the selection, and wrapping follows the last face.
Portable `map copy-surface` and `map paste-surface --stroke` commands share
validation and writing. Patch-source wrapping and native input acceptance remain open.

Levels offers a [four-view workspace](docs/LEVEL_EDITOR.md#four-view-workspace)
with camera, top, front and side panes sharing selection, visibility and editing.
Layouts and splitter sizes persist independently of familiar interaction
profiles; `editor layout` exposes the same preference to the CLI.
The [Draw Brush camera tool](docs/LEVEL_EDITOR.md#camera-brush-creation) adds boxes
on XY/XZ/YZ planes, including empty maps, with linked plan drafts and shared
grid, work-zone depth, material choices, scene locks, undo and save. All 19
profiles offer this explicit tool; numeric primitives remain available.
Brush insertion prepares on cancellable workers; numeric Apply reuses its
validated preview with guards against changed map, selection or asset context.
The Objects list formats rows on demand and filters in the background while
retaining shared selection, hidden-object state and complete accessible details.
Optional [linked navigation](docs/LEVEL_EDITOR.md#linked-navigation) keeps plan
centres and zoom together or follows the camera. Layout-menu choices and
`editor view-links` share persistent defaults; saved views retain their own links.

[Saved Level Views](docs/LEVEL_EDITOR.md#saved-level-views) retain named camera
and plan positions per map, with a manager, portable import/export, safe Save As
handoff and `editor bookmarks` CLI operations. They share viewport services with
Models and leave geometry, selection, undo and package staging intact.

[Reusable Prefabs](docs/LEVEL_EDITOR.md#reusable-prefabs) connect Levels,
Models and Packages: capture brushes, patches and linked entities, preview
project assets, stage `.vprefab` files in packages, and place assemblies with
unique internal targets and one-step undo. Export/inspect/insert CLI commands
share the editor services. A generated model-to-prefab-to-q3map2 workflow also
verified the resulting level package.

Levels now has **New Map**, **Save**, **Save As**, and **Recover Maps**. New
Quake/Quake II/Quake III/Doom/Hexen maps can start empty or with a room and
player start. Saves preserve undo, detect outside edits, keep backups, and run
on a worker; automatic local checkpoints recover unsaved work. CLI `map new`,
`map recoveries`, and `map recover` share the same services. See the
[Level Editor quality plan](docs/LEVEL_EDITOR.md) for exact behavior and the
remaining work toward the complete editor target.

The level camera now previews package and staged material images on brushes,
patches and placed MDL/MD2/MD3 models. Original-size UV projection, shader editor
images, background loading, cancellation and `map materials` diagnostics share
the existing asset services. [Material camera details](docs/LEVEL_EDITOR.md#material-camera-and-package-assets)
cover supported behavior and remaining Doom/shader/project-search gaps.

[Surface Alignment](docs/LEVEL_EDITOR.md#surface-alignment) connects package
material dimensions, textured previews, batch brush UV edits, primitive
matrices, map undo and the `map align-textures` CLI.

[Add Brush](docs/LEVEL_EDITOR.md#brush-primitives) now previews and creates boxes,
wedges, cylinders, cones and spheres, with package materials, shared geometry
validation, dialect-aware saving, atomic undo and `map add-brush` CLI parity.

- Connected static prop authoring in **Models > Design Prop**: editable box,
  cylinder, and plane parts, full 3D rotation, UV transforms and checker preview,
  preview part selection, undo/redo, model design JSON, MD3/OBJ export,
  generated-byte package staging, and undoable Quake III level placement.
  Staged props appear in the level preview, and dependency audits follow their
  materials into shader images. See [Model Design And Level Handoff](docs/MODEL_DESIGN.md).
  All development must consider how the modules work together, with shared
  context, services, validation, staging, diagnostics, and CLI coverage.
- **Models > Mesh Editor** edits MD2/MD3 geometry and baked primitives with face
  operations, indexed edge selection/splitting, explicit boundary-loop filling
  and bridging with unequal counts and twist alignment,
  [surface rename/separation/movement/duplication/joining](docs/MODEL_SURFACES.md),
  [ordered material slot editing and alternate previews](docs/MODEL_MATERIAL_SLOTS.md),
  [per-instance MD3 compiler skins and remaps in Levels](docs/LEVEL_MODEL_APPEARANCE.md),
  [persistent whole-surface selection and shared-pivot transforms](docs/MODEL_SURFACES.md#select-and-transform-surfaces),
  distance welding with seam
  protection, precise vertex picking, move/rotate/scale gizmos with free trackball
  rotation, shared GUI/CLI
  pivots and snapping, numeric transforms, UV islands/seams, pivoted UV transforms,
  frame/clip editing, smooth clip preview, full-pose copying, generated in-between frames, attachment-tag
  authoring with move/rotate and pose copying, bounded
  undo, cancellable background operations, atomic `.mesh.json` sources, animated
  MD2/MD3 output, and shared CLI/package
  handoff. Per-surface images load from staged package snapshots with cancellation
  and refresh after package or palette changes. See [Editable Meshes](docs/MODEL_MESH.md) and the outstanding
  [modeller release gates](docs/MODELLER_RELEASE.md). **Pack Around Unselected**
  fits UV islands around existing painted regions, including other surfaces
  sharing a material slot, with preserved orientation and optional unchanged
  texel scale. GUI/CLI use the same bounded, undoable document operation.
- **Models > Assemble** links model files or staged package entries through
  named tags, with local transforms and independently timed animation. Save
  `.assembly.json` recipes with selection-aware undo, inspect input fingerprints,
  or bake a pose or sampled animation into the mesh editor for package/level
  handoff. Animation bakes retain fractional clip FPS in editable sources and
  export MD2/MD3 within normal format limits.
  `model assembly` exposes the same validation and guarded writes. Native Animation
  authors Quake III `animation.cfg` slots, binds lower/upper parts and previews
  native offsets, reversed clips and loop tails through the same assembly timeline.
  [Configuration, export and CLI details](docs/MODEL_ASSEMBLY.md#quake-iii-native-animation)
  describe the source schema and target-game limits.
  Local recovery retains the recipe, selection and time as a verified unsaved
  draft. See [Model Assemblies](docs/MODEL_ASSEMBLY.md) for limits and CLI recovery.
  Per-part [linked skins](docs/MODEL_ASSEMBLY.md#linked-skins) resolve loose or
  exact staged-package `.skin` inputs into preview and bake materials, retaining
  original model files, input fingerprints, undo/recovery and CLI parity.
  **Player Package…** reviews and atomically publishes separate native Quake III
  player models, skins, animation, icon and material dependencies as a deterministic
  PK3. [Player package contracts](docs/MODEL_ASSEMBLY.md#native-player-packages)
  describe supported non-team players and the matching CLI workflow.

- **Mesh Editor > Collision** authors static and animated oriented boxes with fitting, source
  saves, undo, recovery and an inspection overlay. Select box edges or table rows;
  move, rotate and locally scale with viewport or numeric tools and snapping.
  Animate static boxes or fit each frame; collision tracks follow playback and
  frame edits. Export a stored pose as a Quake-family clip map
  or place brushes through the shared level service; `model collision` provides
  matching CLI operations. Quake III requires your project clip shader. See
  [Model Collision](docs/MODEL_COLLISION.md) for limits and remaining engine checks.

VibeStudio is still pre-alpha, but the format layer is no longer a placeholder:
packages, images, maps, models, and compiled BSPs are parsed by in-tree readers,
drawn with real QPainter code, and driven end to end by a chained build
pipeline. What is still missing is depth — production editors and hardware
3D — not honesty about what the file readers do.

What exists today:
- Documentation for product goals, stack, roadmap, UX, accessibility,
  localization, AI connectors, setup, compiler integration, and credits.
- Cross-platform Meson/Qt6 C++20 build with moc-generated `Q_OBJECT` widgets and
  a Meson smoke-test suite (one test,
  `shell-interaction-smoke`, drives the real window with Qt Test) plus
  Python validators for doc version sync, documentation, source layout,
  credits, translation
  extraction, and English plural forms. CI also runs an offscreen `--self-test` GUI pass and the build,
  CLI-docs, credits, samples, packaging, and release-asset validators.
- Deterministic parser fuzzing and named corruption fixtures. `parser_fuzz`
  builds a reproducible corpus from a seeded xorshift64\* generator and seven
  mutations, and `parser-fuzz-smoke` drives it through `inflateRaw`,
  `inflateZlib`, `decodeIdTechImage`, `detectIdTechImageFormat`,
  `inspectBspBytes`, `PackageArchive::load`, `loadLevelMap` for both `.map` and
  Doom WAD input, and `decodeModelMesh`, asserting that a rejection carries an
  error rather than claiming success. `corrupt-fixture-smoke` pins the exact
  message for twelve hand-built damaged files. Both corpora are assembled from
  published format layouts; no game data is embedded.
- A Qt Widgets shell built around a grouped, collapsible ten-entry mode rail
  (Workspace, Levels, Models, Textures, Audio, Packages, Code, Shaders, Build,
  Settings) over a `QStackedWidget`, with a generated menu bar, an icon tool bar,
  a fuzzy command palette, keyboard shortcuts taken from the shared semantics
  registry, non-color-only status chips, dockable Activity and Inspector panels,
  drag-and-drop file/package opening, confirmation prompts before destructive
  actions, persistent window, mode, and panel layouts with **View > Reset
  Layout**, and a date-stamped session log that captures Qt warnings and above.
- A design system taking its visual language from idStudio: one token set per
  theme (charcoal and orange by default, plus light and two high-visibility
  themes held to WCAG AA contrast by a test), painted theme-aware icons, and
  shared page parts, so every surface has the same header, tool bar, splitter
  workbench, empty state, and status strip. The chrome is one row: the menus,
  history, and a command search centred on the window share the studio bar;
  page headers are a single slim line; the navigation rail marks the current
  page with an accent glyph and edge; and the Workspace page's tiles say what
  each surface holds right now. The Levels page has an idStudio-style
  Key / Value entity property grid with in-place editing and spawnflag check
  boxes, and the Models and Audio inspectors use the same grid; the Packages page
  is an asset browser with a folder tree, breadcrumbs, and back/forward/up; the
  Textures page shows decoded thumbnail tiles; the Shaders page is a shader,
  stage, and texture tree that marks textures missing from the open package; and
  both viewports carry corner readouts.
- A dependency-free DEFLATE codec (RFC 1951/1950 inflate plus a deterministic
  encoder, CRC-32, Adler-32), so compressed ZIP/PK3 entries are actually read
  and written rather than skipped. The encoder offers `store`, `fast`,
  `default`, and `best` levels that differ only in how hard the LZ77 hash-chain
  search works; each block independently picks the smallest of a stored, fixed
  Huffman, or dynamic Huffman encoding, so a block is never larger than storing
  its bytes would be. Archive saves use an incremental encoder and verified
  chunk reads, with per-file progress and cancellation during compression.
  ZIP measures then writes without buffering a complete entry; requested
  in-place manifests hash content before source replacement.
- Package browsing for folders, PAK, WAD, ZIP, and PK3, including ZIP64 central
  directories, nested-archive detection, layered mounting where a second archive
  overrides a base one, normalized virtual paths, traversal and symlink-safe
  extraction, entry filtering, detail drawers, and activity-center scan/extract
  tasks. Opening has entry, depth, metadata and fingerprint limits with
  streamed ZIP directory records and cancellable indexing. Combined sessions
  and multi-folder map texture audits share these budgets across up to 64 layers.
  Failed mounts preserve the previous session; incomplete source audits remain
  visible in JSON and CLI exit status. See
  [Opening limits](docs/PACKAGE_MANAGER.md#opening-limits).
- Package staging and save-as with add/import, replace, rename, delete, conflict
  reporting, before/after composition, schema-versioned manifests, and
  deterministic PAK, ZIP/PK3, PWAD, and WAD2/WAD3 writers. Doom WADs carrying
  more than one map now write back: lumps are resolved by their position in the
  source directory rather than by name, and each map marker keeps its own lumps
  grouped beneath it, so repeated names like `THINGS` no longer collapse onto
  the first map.
- Level dependency inspection from **Levels > Dependencies** and `map dependencies`:
  explicit texture, shader-image, model, and sound references, searchable results,
  missing/ambiguous/unreadable states, map-object selection, and JSON reports. The
  scan runs off the UI thread and can be cancelled. Quake III shader references
  include animation frames, editor/light images, and skybox faces.
- Selective asset packages from **Packages > Export Selected**, the dependency
  browser's **Export Assets**, or `package subset`. Exact entries, folder prefixes,
  and package queries select an independent save-as plan; `--map-input` selects
  the map's resolved assets. Incomplete scans and unresolved dependencies block
  map-driven export. Exact occurrence indexes preserve repeated names; reviewed
  WAD subsets retain complete map/GL groups, namespace boundaries and local
  texture name tables in source order. Asset bundles omit the source map and
  compiled BSP; external
  `.skin` overrides and dynamically selected game assets still need review.
- Reviewed **WAD Groups** edits in Packages: rename a map with its GL companion,
  or delete a complete map, namespace or local texture-table group as one undoable
  change. `package groups` supplies the IDs and fingerprint for matching CLI edits.
  New and opened WADs share the same reviewed order through drafts and saving;
  metadata/script reference rewrites remain open. See [Edit WAD Groups](docs/PACKAGE_MANAGER.md#edit-wad-groups).
- Opt-in in-place package replacement. `PackageWriteRequest::allowInPlaceOverwrite`
  verifies the new archive and an independent original copy before atomic
  replacement. The original stays readable until commit; the previous backup
  remains until the replacement is installed. Save journals retain both versions
  across process interruption. `package recover` inspects them, and `--finish`
  completes backup publication for a verified, already-installed replacement.
  Both overwrite modes retain a backup (`<destination>.bak` by default).
  The GUI asks before replacement; saving over the source through the CLI
  requires explicit `--in-place`.
- Package comparison through `comparePackages`, which pairs entries by
  case-folded, normalized virtual path and by occurrence index so a Doom WAD's
  repeated lump names line up one map at a time. Each entry comes back as
  `Identical`, `Added`, `Removed`, `Changed`, or `CaseOnly` — case-only path
  differences get their own category because they work on Windows and fail on
  case-sensitive filesystems — and the result records whether the verdict came
  from size or a verified SHA-256 of the bytes. Unreadable or over-budget
  content stays explicitly unchecked. Available as the
  Packages page **Compare** button and the `package compare` command, which
  returns `validation-failed` on any difference so a release script can gate on
  two packages matching.
- Staged package review through **Packages > Review Changes** and
  `package compare <source> --staged`. The searchable review shows paths,
  sizes, comparison evidence, and unchecked files, with JSON export. Comparison
  and saving run in background workers with cancellation. Successful GUI saves
  open the written package with clean staging, so subsequent edits use its new
  contents and entry offsets. Summary counts and before/after composition reuse
  prepared metadata; oversized totals have explicit diagnostics and exact
  representable JSON totals have decimal-string fields. The staging list shares
  prepared operations/conflicts and formats visible rows on demand, with complete
  selectable change details and exact occurrence actions. See
  [Package Manager](docs/PACKAGE_MANAGER.md).
- Streaming package integrity checks through **Packages > Validate** and
  `package validate`: complete payload sizes, ZIP CRCs, positional WAD reads,
  per-file SHA-256 evidence, progress, cancellation, and JSON reports.
- Sessions that come back: the package, the map, and the code editor's tabs
  that were open reopen at the next start, unless turned off in Settings.
- A [Texture Editor](docs/TEXTURE_EDITOR.md) with square/round brushes, explicit
  alpha modes, pencil/erase/tolerant fill, line/rectangle/ellipse, eyedropper,
  clipping selections, wrapped painting, cyclic offset, anchored zoom,
  tile preview, palette remapping, canvas sizing, anchored selection transforms,
  and bounded undo/redo. Editable layers have visibility, locks, opacity and
  blending; `.vtexture` project saves preserve them and detect external changes.
  GUI and `texture create` / `texture edit` share operations; `texture inspect`
  validates saved projects. Exported composites enter package staging, the texture
  browser, and undoable Quake-family map texturing with validated WAD2/WAL and
  Quake III PNG/TGA references. Nine export profiles cover PNG,
  TGA, PCX, Quake miptexture/WAD2, WAL and Doom flat/patch, with saved metadata,
  palette/alpha rules, mip previews and shared `texture profiles`, `validate`,
  `export`, and `stage` commands. WAD2/Doom staging preserves native lump types,
  namespaces, and grouped draft undo. The bounded texture workflow has passed
  the [release audit](docs/plans/texture-editor-release-candidate.md), with
  Windows and Linux/WSL evidence and explicit manual/platform limits.
  Local background recovery,
  verified backups, and Save/Discard/Cancel protect drafts; `texture recoveries`
  inspects checkpoints and `texture recover` restores to a new project.
- A Textures page that lists a Doom WAD's flats, sprites, and patches by their
  namespace markers, and every WAD2/WAD3 lump, decoded with the palette the
  package ships unless another is chosen.
- idTech image decoding for Doom patches, flats, `PLAYPAL` and `COLORMAP`,
  Quake `.lmp`, WAD2/WAD3 miptextures with mip chains, Quake II `.wal` with
  surface/content flags, Quake II `.m8` (embedded palette, up to 16 mip levels)
  and `.m32` (truecolour RGBA), Quake II `.sp2` sprite containers whose frame
  images are resolved out of the open package, PCX, Targa, and both Quake and
  Half-Life `.spr` frames including the Half-Life alpha-test and index-alpha
  texture formats — with the palette resolved at run time from the package the
  user opened, a clearly-labelled generated fallback when no game palette is
  present, palette quantization, and a swatch view that marks the transparent
  index.
- Model geometry decoding for Quake MDL (IDPO 6), Quake II MD2 (IDP2 8), and
  Quake III MD3 (IDP3 15): surfaces, per-frame vertex positions and normals,
  texture coordinates, MD3 tags, frame bounds, embedded MDL skins, and external
  skin path references resolved against the open package. MDC, MDR, and IQM are
  read for their headers only and report that geometry decoding is not
  implemented. Animations are inferred from frame-name stems, which is how MDL
  and MD2 store them, not read from a table the formats do not have.
- A software model viewport on the Models page. A bounded depth buffer handles
  intersecting surfaces, perspective-correct skins, and per-pixel transparency;
  QPainter presents the result without an OpenGL dependency. Filled and wireframe
  views prepare on a cancellable worker; shared wire edges draw once and selected
  dashes stay above ordinary edges at the current display scale.
  Vertex markers and exact indexed picking also prepare on the worker. Dense
  component selections retain table context, and pose changes reuse prepared
  topology; unchanged inspector refreshes preserve the completed preview.
  Textured, flat-shaded, and wireframe modes; orbit, pan, and zoom by mouse or
  keyboard; frame stepping, timed playback, and per-animation ranges; and a
  hover readout naming the surface and triangle under the pointer.
- OBJ and native package model previews load on one cancellable worker with
  verified, bounded reads, per-surface materials and native header details.
  Rapid selection changes discard retired results; MDL groups/palettes and
  MD3 attachment poses remain available to the mesh editor.
- Single-frame Wavefront OBJ export through `exportModelFrameObj`, writing `v`,
  `vt`, `vn`, and `f` records with the V axis flipped for OBJ's bottom-left
  origin. The Models page **Export OBJ** button writes the frame the viewport is
  showing; `model export` takes `--frame` and an optional `--material` name. No
  companion `.mtl` file is produced. Shared guarded writes protect source files,
  package folders and portable draft storage, detect changed destinations, and
  publish complete output. Browser preparation runs with progress/cancellation;
  omission notes identify frames, attachments and skin data retained in the source.
- Graphical asset surfaces painted with QPainter: a zoomable, pannable image
  preview with checkerboard alpha, nearest-neighbour magnification, and mip/frame
  stepping; a palette swatch grid; a per-channel audio waveform with a playhead
  and, when the build links Qt Multimedia, play, pause, loop, and volume; and composition,
  pipeline, and timeline charts that each carry a non-color cue and a text
  summary.
- Brush component authoring: solved vertices, edges and faces, convex hull
  reconstruction, orthographic/numeric editing, local undo and shared surface
  preview. Apply is one map undo command; CLI inspection and movement use the
  same geometry and persistence services. See [Level Editor](docs/LEVEL_EDITOR.md)
  for limits and remaining acceptance gates.
- Quake III patch authoring: plane, open cylinder and open cone presets;
  selectable control points with XYZ/UV editing, snapping, undo, shape-preserving
  subdivision, facing inversion and a tessellated UV checker. Apply commits one
  map undo step; saving, recovery and package dependency inspection share the
  same document. CLI `map add-patch` and `map edit-patch` use the same services.
  See [Level Editor](docs/LEVEL_EDITOR.md) for controls and remaining requirements.
- An interactive 2D map viewport: real Doom vertices, linedefs, traced sector
  fills and things, and solved Quake-family brush footprints and Quake III patch
  outlines, with click-to-select, drag-to-pan, wheel zoom, Tab cycling, three
  orthographic projections, Zoom to Selection, a **Show** menu for what the
  view draws (markers, sector fill, grid, vertices, labels, target links), and
  a high-contrast mode, and a read-only **3D** preview of the brushes,
  patches, or Doom walls to orbit. A right-click menu (also the Menu key, and on the
  objects list) holds every map action, grouped from the selection outward.
- Direct manipulation in that viewport. Shift-click extends the selection,
  Ctrl-click toggles it, a rubber band over empty space selects a set on
  release, a drag on any selected object moves the whole selection, handles
  on the selection's box resize it, and arrow keys nudge it by one grid step
  (eight with Shift). Escape or a right press cancels an in-progress drag,
  resize, or band. The widget never edits the document: it previews the
  move locally and emits `moveRequested` once, and the shell turns that into a
  single `moveLevelMapSelectionSnapped` undo command, so one undo puts every
  moved object back. A **Snap** checkbox on the Levels page decides whether the
  delta is rounded to whole grid steps, and the objects list and the viewport
  share one selection set in both directions.
- Level editing on Quake-family maps, each operation one undo step:
  - create: **Add Entity Here…** (a class from the loaded definitions or common
    ones) and **Add Brush Here…** (a box in the map's own face format), placed
    where the menu opened and snapped to the grid, or the **Create** palette
    beside the objects list, whose classes (or Doom thing types, by kind) are
    placed with Enter in the middle of the view or dragged onto it;
  - copy and remove: Ctrl+D duplicates one grid step over, Ctrl+C, Ctrl+X, and
    Ctrl+V move objects through the clipboard as .map text that TrenchBroom and
    Radiant exchange too, and Del deletes (in the entity inspector, Del removes
    only the current key);
  - transform: Move…, Resize… (or the handles on the selection's box), Snap
    to Grid, Rotate 90° Left/Right, and Flip Horizontal/Vertical, with brush
    planes, Valve 220 texture axes and offsets, patches, and entity angles
    following, and textures kept in place in the world when a brush grows;
  - cut: the **Clip Tool** (X) draws a line across the view, hatching what a
    cut takes away; Tab chooses what stays, one side or both as two brushes,
    and Enter cuts; **Clip Selection…** cuts along an axis from the keyboard,
    **Hollow…** turns a block into walls of a chosen thickness, and **Carve**
    cuts the selected brushes out of everything they overlap, so a doorway
    brush makes its opening;
  - align: the Inspector lists a selected brush's faces by the way they face,
    each with its texture, shift, rotation, and scale to edit in place;
  - retexture: a **Textures** tab of the map's textures as tiles, recent ones
    first, applied with Enter and able to select what uses one; **Apply
    Texture…** on every face of the selection; **Replace Texture…** across the
    map or the selection; and **Apply to Map Selection** and **Use in Open
    Map…** from the Textures surface;
  - review: a History tab that jumps to any step and marks the save point.
- Doom and Hexen things: **Add Thing Here…** by DoomEd number, duplicate,
  delete, and edit a thing's type, angle, and flags in the entity inspector.
  Linedefs split at their middle and flip around, sides and all (**Split
  Linedefs** and **Flip Linedefs**), each a single undo step.
- Doom and Hexen geometry: **Draw Sector** (D) puts corners down with clicks,
  snapping to vertices and the grid, and closes the shape into a sector that
  joins the lines it meets, copies the sector it was drawn in, or walls itself
  in the void; **Add Sector…** takes typed corners. A click inside a room
  selects its sector. Del removes vertices, linedefs, and sectors as Doom
  Builder does, **Join Sectors** and **Merge Sectors** make rooms one, and
  **Merge Vertices** joins vertices, stitching lines drawn over each other
  into one.
- Seeing the map: arrows for entity target links (`target`, `killtarget`, and
  kin, dashed when they remove), [ and ] to halve and double the grid, H to
  hide the selection and Shift+H to show
  everything, the selection's size in the HUD, compiler leak trails,
  **Select All** of a class or thing type, **Select by Texture** (also
  **Select in Open Map** from the Textures surface), and Ctrl+A, Ctrl+I, and
  Ctrl+Shift+A to select everything, invert, or clear. The
  objects list names every entity, brush, and patch, or every Doom vertex,
  linedef, sector, and thing.
- Save fidelity: save-back rewrites an edited value inside its quotes, removes
  only a removed key's pair, drops a deleted object's own lines (heading
  comment included), and writes added and copied objects in the map's own
  format, so everything not edited stays byte-identical, compact lines such as
  `{ "classname" "info_null"` included. Operations that would need to split a
  line another object shares are refused with a reason.
- Map parsing with a real tokenizer covering classic, Valve 220, `brushDef`,
  `brushDef3`, `patchDef2`, and `patchDef3` primitives, Doom and Hexen lump
  strides, brush-plane solving by half-space intersection, sector outline
  tracing, undo/redo with a History tab that jumps to any step and marks the
  save point, a Levels map box listing every map in a WAD, and
  save-fidelity-checked non-destructive save-as. UDMF
  (`TEXTMAP`) maps support lossless property authoring and fractional
  move/rotate/mirror/snap/resize, common-field preview, scene organization, exact
  undo, recovery and WAD persistence. Use **UDMF Properties…**, standard transform
  controls or the shared CLI; topology creation/deletion remains open.
- Bounded reuse of unchanged brush geometry across plan edits, sibling panes
  and package-backed camera previews. Texture coordinates and asset dimensions
  stay live, and cancelled requests cannot replace the current geometry cache.
  The [scale harness](docs/LEVEL_EDITOR.md#geometry-reuse-and-scale-measurements)
  measures synthetic maps up to 10,000 brushes; large-project performance
  remains an active release requirement.
- Entity definition loading for Radiant `/*QUAKED*/` blocks in `.def` and `.qc`,
  Valve `.fgd` (including `@include`, `@BaseClass` inheritance folding, helpers
  such as `base()`, `size()`, `color()`, and `model()`, and choice/flag row
  blocks), and Quake III `.ent` entity lists, with content sniffing that
  overrides the file extension. Nothing ships a game's definitions: the studio
  reads whatever the user points it at, from an explicit file or folder or from
  the project's conventional folders (`.vibestudio/definitions`, `definitions`,
  `defs`, `scripts`, `base/scripts`, `entities`). Every parse loop is bounded
  because these files come from mod packages and the internet.
- Map entity validation against a loaded catalogue. `validateLevelMapEntities`
  reports a missing or unknown `classname`, an `@BaseClass` placed in a map, a
  point class that owns brushes or a brush class that owns none, keys the class
  does not declare, values that do not fit their declared type, missing required
  keys, a malformed `spawnflags` value, spawnflag bits the class does not
  define, `target` values with no matching `targetname`, and targetnames nothing
  references. Quake-family `.map` documents skip spawnflag bits 8–11, which the
  Quake and Quake II game code owns as the skill and deathmatch filters, and
  Doom `thing:<type>` entities skip the class and key checks their binary
  records cannot answer. Results appear on the Levels page **Entity** inspector
  and in the Health tab, where each issue carries a selector that navigates to
  the entity, and through `entity definitions` and `entity validate` on the CLI.
- Deterministic headless SVG map rendering that shares the same geometry solver
  as the viewport, so a `map render` picture matches the on-screen one.
- Read-only inspection of compiled artifacts: Quake BSP29/BSP2/2PSB, Quake II
  IBSP38, and Quake III IBSP46/RBSP lumps, entities, and textures, plus the
  `.pts`/`.lin` leak point files and `.prt` portal files the compilers leave
  beside them. Every offset and count is bounds-checked against the real file.
- Chained build pipelines for Quake (`qbsp` → `vis` → `light`), Quake III
  (q3map2 BSP → vis → light), and Doom node building (ZDBSP, ZokumBSP), running
  stage by stage through the shared compiler runner with per-stage logs,
  diagnostics, leak detection, hashes, manifests, optional stages, per-stage
  extra arguments, and output registration.
- Quake/Quake II/Quake III **Prepare Build Workspace** captures the current map and complete
  package draft, including generated models, textures and sounds, as independently
  copied inputs with verified hashes. **Use in Build** uses that snapshot in the
  normal compiler pipeline. **Publish Prepared Build** reviews verified BSP,
  generated shader and external lightmap outputs with captured assets, then
  writes a target-specific PAK/PK3 with optional build sources and overwrite backups.
  Quake captures native WAD2 textures; Quake II validates WAL animation chains.
  Quake colored lighting publishes with the BSP. Classic targets share ericw-tools
  pipelines; prepared deployment uses engine-aware numbered PAK slots and remembers
  each map's slot for reviewed replacement with a backup.
  **Open Prepared Assets** opens the full folder in Packages for inspection.
  CLI: `build prepare`, `build run-prepared`, `build artifacts` and
  `build publish-prepared`, `build deploy-plan` and `build deploy-prepared`.
  **Deploy Prepared Build** reviews a complete installation PAK/PK3 and optional
  windowed launch. Read-only write permission applies to one deployment; changed
  destinations/executables require another review. **Build and Launch** opens
  that review after a successful prepared build.
  See [prepared builds](docs/LEVEL_EDITOR.md#prepared-builds-with-current-assets)
  for supported outputs and remaining engine/deployment boundaries.
- Compiler registry and executable discovery for imported ericw-tools, q3map2,
  ZDBSP, and ZokumBSP, with reviewable command plans, flag-aware expected output
  paths, streamed stdout/stderr, parsed diagnostics, schema-versioned command
  manifests, run/rerun, copy-command, and user/project executable overrides.
- Reviewable game launch plans built from a saved installation profile, plus
  optional execution of the planned command line.
- An edit, build, fix loop on the Build page. The input follows the map open in
  Levels, building a map with unsaved edits asks first, and a **Problems** tab
  lists each compiler warning or error with its location; activating one
  selects the brush, patch, or entity on that line of the map, or opens a
  script at the line, and its context menu copies one problem as the compiler
  printed it or all of them. The **Toolchain** tab shows where every compiler
  tool was found and can **Locate…** or forget one, sharing the setting with
  `compiler set-path`, and the launch form defaults its map name to what the
  build produces. Launching copies the built map into the game folder the
  engine loads maps from (asking once per installation, which is read-only
  until allowed), **Build and Launch** (F5) does the whole loop in one step, and
  **Add to Package** stages the built map into the open package for Save As.
  A leaking build leads the Problems list with the leak and draws the
  compiler's leak trail over the map in Levels.
- Keys and rows that go where the user expects. Page keys act only on their page
  (Del, F2, Ctrl+E, and Ctrl+Z to unstage on Packages; Ctrl+Z on Levels; Ctrl+S,
  Ctrl+L, F3, Ctrl+H, Ctrl+/, Ctrl+D, Alt+Up/Down, Ctrl+F4, Ctrl+Page Up/Down
  between open-file tabs, Ctrl+= and Ctrl+- to zoom the editor, Ctrl+Shift+[ and
  ] to fold and unfold a `{ }` block, and F8 and Shift+F8 to step through the
  file's problems on Code), F4 and Shift+F4 to step through the last build's
  problems from any page, Ctrl+P goes to any project file or package entry by
  name, Ctrl+T to a function, shader, or entity class in the open file, F12 or
  Ctrl+click to where a name is defined across the project, Shift+F12 to every
  use of it, Alt+Left and Alt+Right back and forward through the pages left and
  the jumps made, Ctrl+Space to complete a name, Ctrl+F finds on any page (on
  Settings it searches every setting), Escape clears a filter, the asset,
  shader, code, and map object lists have filters, **File > Open Recent**
  reopens recent projects, packages, maps, and scripts, the status chips open
  the surface behind them, and a row that names something leads to it: a package
  entry to its surface, a shader's texture to Textures, a skin to its image, a
  class to its definition, a Health issue to its entity.
- Asset analysis for MDL/MD2/MD3 model metadata with skin and material
  dependencies, WAV metadata with decoded peaks, real Ogg/Vorbis, MP3, and FLAC
  header parsing, and CFG/shader/QuakeC text diagnostics.
- A text surface with a data-driven `QSyntaxHighlighter` for config, idTech3
  shader, QuakeC, and adjacent languages, theme-aware colours, open/save of
  project files, diagnostics, and project-wide find/replace.
- Cancellable project search in **Code > Search Results**, with whole-word and
  case options, file filters, before/after replacement previews, deliberate
  deletion, and changed-file/snapshot checks. Named open Code documents override
  disk and receive undoable, unsaved edits. GUI and `asset find` / `asset replace`
  share the bounded service and UTF-8/UTF-16 codec; unopened-file replacements
  preserve BOMs, byte order and untouched line endings.
  See [Project Search](docs/PROJECT_SEARCH.md).
- Format-preserving Code saves with per-tab UTF-8/UTF-16 BOM and line-ending
  metadata, atomic writes, and checked external-edit conflicts. Unsupported
  encodings remain read-only. `code text-info` and `code text-save` expose the
  same service for automation. See [Code Editor](docs/CODE_EDITOR.md).
- Optional local language servers for live diagnostics, semantic completion,
  Quick Info, Parameter Hints, Go to Definition and Find All References, including unsaved buffers.
  Diagnostics support push and bounded document pulls, cached results and explicit
  failure/retry states. Successful Save/Save As notify interested providers.
  Ctrl+Shift+Space shows call overloads and the active argument; server triggers
  refresh the compact inline panel as you type. Documentation expands on demand.
  Document and selection formatting apply as one unsaved Undo step, with progress,
  cancellation and source-version guards. F2 previews semantic rename across
  unsaved tabs and project files; Apply uses document Undo and guarded file saves.
  Ctrl+. lists quick fixes and refactorings, including lazy edit resolution and
  unavailable reasons, then shares the same reviewed apply flow.
  Quick Info pairs pointer hints with a keyboard-accessible documentation pane. Reference
  results share Search Results with progress, cancellation and snapshot guards.
  Completion replacements and related
  edits share one Undo step. Deferred documentation and imports resolve when a
  suggestion is highlighted; early acceptance waits for the complete edit set.
  Snippet suggestions offer linked fields, Tab/Shift+Tab navigation, nested
  defaults and native choices; insertion and mirrored edits retain normal Undo.
  Explicit connections, version checks,
  cancellation and the CLI share one bounded client. See
  [Local Language Services](docs/LANGUAGE_SERVICES.md).
- Untitled Code documents, reviewed Save As with preserved undo history, and
  asynchronous local text recovery. Restored copies open as drafts; new-file,
  copy and recovery-export CLI commands use the same destination checks.
- idTech3 shader script parsing into stage graphs with package-reference
  validation and round-tripped stage edits written to a save-as path.
- Manual game installation profiles with stable IDs, game keys, engine-family
  defaults, read-only validation, GUI management, and confirmable Steam/GOG
  candidate detection.
- Project manifests at `.vibestudio/project.json` with current-project
  persistence, project-local overrides, schema migration for the settings store,
  a `--settings-file` override that keeps automation out of the user's real
  preference store, and a workspace dashboard health summary.
- Workspace workbench panels for project problems, search across project files
  and mounted package entries, Git changed/staged files, recent activity,
  reveal-in-folder, and copy-virtual-path actions.
- External-change detection for the files the studio holds open. A
  `DocumentWatcher` fingerprints the open map, the open package, the file in the
  code editor, and the project manifest, and a one-second timer polls them;
  `QFileSystemWatcher` notifications are treated as hints, and the fingerprint
  comparison decides. It distinguishes a real edit from a touch whose bytes are
  provably identical, from a removal, and from a replacement written as
  temp-then-rename, and it holds a burst of notifications back until the file
  has been still for a quarter second so one save is reported once. The shell
  then offers to reload the map, reopen the package, or reload the editor file,
  defaulting to No whenever unsaved edits or staged operations would be lost;
  declining re-baselines the path so the same change is not asked about again.
  A changed project manifest rebuilds the workspace panels without asking.
- A crash-capture layer in `src/app/studio_runtime.h`. `installCrashHandling()`
  installs `SetUnhandledExceptionFilter` plus a `SIGABRT` handler on Windows,
  `sigaction` for SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT on POSIX, and
  `std::set_terminate` everywhere; on any other platform it reports itself
  unavailable rather than failing. Everything that needs formatting is rendered
  into fixed buffers while the process is healthy, so the handler only opens,
  writes, and closes. A session marker left behind by a process that is gone is
  how the next launch detects an unclean exit. An interactive start arms it
  before the shell exists; the next start then offers the crashed session back
  from a notice bar, with **Reopen Last Session** and the report, instead of
  reopening files that may have caused the crash. Reports are written next to
  the session log and never transmitted, **Help > Crash Reports** and
  `diagnostics crashes` list them, and a preference turns capture off.
- A CLI subcommand router with 111 registered commands across the `cli`, `ui`,
  `project`, `package`, `asset`, `map`, `entity`, `model`, `bsp`, `build`,
  `launch`, `texture`, `shader`, `sprite`, `code`, `localization`,
  `diagnostics`, `extension`, `compiler`, `install`, `editor`, `about`, `ai`,
  and `credits` families, with JSON output for automation and a documented
  stable exit-code contract.
- Runtime translation loading: `lrelease` compiles the checked-in `.ts` catalogs
  into `.qm` files, the application resolves and installs the catalog for the
  selected locale at start-up, and applies right-to-left layout direction where
  the locale requires it.
- Localization target metadata and Qt TS catalogs for 20 languages plus
  pseudo-localization, with CLI reports for RTL smoke coverage, locale
  formatting, pluralization samples, expansion layout checks, catalog status,
  and dry-run Qt Linguist extraction validation.
- Portable release packaging scripts for Windows, macOS, and Linux target
  bundles with generated offline guide, platform smoke notes, checksums,
  samples, package manifests, and VibeStudio/imported-compiler license bundle.
- Imported compiler source submodules and CI that builds, tests, validates
  samples and docs, and runs the offscreen GUI smoke pass on every PR.

What does not exist yet:
- No hardware-accelerated 3D. Models use a software depth buffer with orthographic
  and perspective views. Maximum-scene preparation and rendering responsiveness
  remain release gates.
- No geometry for MDC, MDR, or IQM. Those three are header-only: counts and
  names are reported, nothing is decoded, drawn, or exported. There is no
  skeletal or bone animation. [Model assemblies](docs/MODEL_ASSEMBLY.md) link
  parts through tags with independent frame animation and explicit pose or sampled
  animation baking. Editable mesh sources retain sampled clip FPS; native game
  timing configuration remains external. An optional FTE render-target test
  verifies a small nested animation bake as MD2/MD3 with independent pose and
  texture checks; original-engine gameplay acceptance remains open.
  Vertex-frame interpolation is available; MD3 shader previews resolve static
  images without simulating the complete shader effects. Only one version per
  decoded format is accepted — IDPO 6, IDP2
  8, IDP3 15 — so a Half-Life `.mdl` is rejected rather than misread.
- Native model writing includes static primitive designs and animated editable
  meshes as MDL/MD2/MD3. MD2 retains all poses and skin slots with explicit skin
  dimensions and quantization diagnostics. MDL import/edit/export retains indexed
  skins, native groups, timing and header settings through schema-4 sources,
  undo and recovery. The Quake MDL inspector and `model mdl` CLI share those
  services, including stored/GLQuake timing preview and CLI time sampling.
  Import Package Texture copies exact indexed entries from the staged package
  into a skin slot/member; CLI skin edits also accept package folders, archives
  and saved drafts, preserving indices and validating palette/dimension matches.
  Original-engine acceptance and `.mtl` companions
  remain open work. Imported model frames can also be exported as OBJ.
- Audio playback needs Qt Multimedia at build time. Editing, decoded waveforms,
  analysis and WAV export also accept bounded MP3, native FLAC and Ogg Vorbis
  without it, using bundled dr_libs/Xiph decoders. Compressed browser previews
  remain header-only; open Edit Sound for the waveform. See the
  [format limits and omitted metadata](docs/AUDIO_EDITOR.md#formats-and-limits).
- No package editing in place. Replacing a package is a whole-archive rewrite
  through the staged plan — verified first, with the original kept as a backup —
  not an edit of the bytes already there, and there is still no binary format
  editor. Package comparison reads two archives on disk; comparing a package
  against a staged plan exists in `comparePackageToPlan` but no GUI or CLI
  surface calls it yet.
- No text-to-speech engine. The TTS preference is stored and reported, but no
  speech backend is wired up.
- No AI provider network calls. Every AI command produces a local, reviewable,
  no-write proposal; the application makes no outbound HTTP requests at all.
- Full-production acceptance remains open across the studio. Levels have brush,
  patch and binary Doom/Hexen authoring, texture alignment, clipping and shared
  compiler/package workflows; UDMF supports existing-object properties and native transforms.
  The remaining editor gaps are tracked in the
  [level acceptance matrix](docs/LEVEL_EDITOR.md#acceptance-areas).
- No translated user interface. The catalogs are seeds: the loading path works,
  but only the pseudo-locale carries translated text, so the UI still renders in
  the source language.
- Full guided first-run setup and native assistive-technology acceptance remain
  open. The texture editor's bounded authoring, integration and automated
  accessibility evidence is recorded in its
  [release audit](docs/plans/texture-editor-release-candidate.md).

Treat every feature list below as roadmap intent until the roadmap and support
matrix mark it implemented.

## Studio Goals
These are product targets, not a feature list. Some already have a working slice
behind them — package management, compiler orchestration, installation
management, 2D level viewing and object moves, entity definition validation,
texture decoding, model preview and OBJ export — and the rest are unbuilt. Read
Current Development State for what the code does today.

- Level editor for Doom-family sectors and Quake-family brush workflows.
- Modeller and model preview/editing workflows for MDL, MD2, MD3, MDC, MDR, IQM, and adjacent idTech formats.
- Texture editor for indexed, paletted, mipmapped, shader-linked, and package-contained art.
- Audio editor and preview surface for WAV, OGG, MP3, and idTech-specific audio variants.
- PakFu-derived package manager for WAD, PAK, PK3, ZIP-family archives, folders, nested packages, manifests, and safe write-back.
- Coding IDE for QuakeC, C/C++ helper code, QVM-oriented workflows, configs, scripts, and source-port project glue.
- Script editor for CFG, shader scripts, entity definitions, menu scripts, and game-specific text assets.
- Sprite creator for Doom sprites, Quake sprites, HUD graphics, palette-aware image flows, and batch conversions.
- Graphical idTech3 shader editor that round-trips with `.shader` text.
- Adaptable level editor interaction profiles inspired by [GtkRadiant 1.6.0](https://github.com/TTimo/GtkRadiant), [NetRadiant Custom](https://github.com/Garux/netradiant-custom), [TrenchBroom](https://trenchbroom.github.io/), and [QuArK](https://quark.sourceforge.io/).
- User-aware design that keeps people informed with clear loading states, progress displays, task logs, previews, cancellation, and visible outcomes.
- Modern UX with progressive disclosure: simple workflows stay clean, while advanced users can drill into detailed logs, metadata, manifests, dependency graphs, and compiler output.
- Creative graphical communication for asset relationships, package structure, compiler pipelines, map statistics, shader stages, and project health.
- Accessibility-first design with high-visibility themes, scaling controls, keyboard navigation, assistive-tool metadata, OS-backed text-to-speech, and non-color-only status.
- Localization architecture targeting 20 predominant world languages, including right-to-left and non-Latin script coverage.
- Modern initial setup flow for tailoring language, accessibility, theme, editor profile, game installs, projects, compilers, AI connectors, CLI, and automation preferences.
- Efficiency-first workflows that streamline setup, browsing, editing, compiling, packaging, validation, and launch/testing as much as possible.
- Optional generative and agentic AI-assisted workflows through a provider-neutral connector layer, with planned connector targets including [OpenAI](https://platform.openai.com/docs/quickstart), [Claude](https://platform.claude.com/docs/en/home), [Gemini](https://ai.google.dev/api), [ElevenLabs](https://elevenlabs.io/docs/overview/intro), [Meshy](https://docs.meshy.ai/en), local/offline models, and custom integrations.
- Complete AI-free workflows for users and projects that prefer deterministic local tooling.
- A full-featured CLI that shares project, package, compiler, validation, and automation services with the GUI.
- Compiler orchestration for imported map/node builders, with structured logs, reproducible command manifests, progress, and game-profile-aware output paths.
- Installation detection and management guided by PakFu's game profile work.

## Initial Scaffold
The repository currently contains:
- A Meson/Qt6 C++20 app target named `vibestudio`, with moc wiring for the
  shell's `Q_OBJECT` widgets and an `i18n` subproject that compiles the
  translation catalogs.
- A Qt Widgets studio shell with a mode rail over a `QStackedWidget`, a
  generated menu bar and toolbar, a command palette, keyboard shortcuts, status
  chips, drag-and-drop, and an activity center with task state, progress,
  warnings, cancellation, and per-task logs.
- A CLI surface for version/platform diagnostics, project/package/installation
  management, package comparison, asset and texture inspection, map inspection
  and rendering, entity definition loading and map entity validation, model
  inspection and OBJ export, BSP inspection, chained builds, game launch plans,
  shader, sprite, code, extension, localization, diagnostics, and AI workflows,
  plus credits validation, JSON output, quiet/verbose modes, watch streaming,
  and task-state automation.
- Reusable shell UI primitives for loading/progress placeholders and
  detail-on-demand logs or metadata.
- A shared package/archive layer adapted from PakFu's archive surface, with safe
  normalized virtual paths, traversal checks, and a symlink-safe extractor.
- Folder, PAK, WAD, ZIP, and PK3 mounting through shared core services used by
  both GUI and CLI, with a dependency-free DEFLATE decoder so compressed PK3
  entries read, ZIP64 support, and nested-archive detection.
- Package staging with save-as writers for PAK, ZIP/PK3, PWAD, and WAD2/WAD3
  outputs plus schema-versioned staging manifests, multi-map Doom WAD
  write-back, an opt-in verified replace that keeps the original as a backup,
  and streaming comparison against another package or the staged result, with
  per-file byte progress and cancellation.
- Painted graphical shell views for project health, package composition by
  type/size, build pipeline stages, the activity timeline, level-map statistics,
  decoded textures, palette swatches, and audio waveforms.
- Level-map services and UI/CLI surfaces for Doom WAD map lump inspection,
  Quake-family and Quake III `.map` parsing, brush and sector geometry solving,
  an interactive 2D viewport with rubber-band selection, drag-to-move, and
  grid-snapped arrow-key nudges, deterministic SVG rendering, entity/property
  lists, texture/material references, entity definition catalogues, map
  validation and entity validation, safe entity/movement edits, undo/redo state,
  non-destructive save-as, and compiler profile handoff.
- Model services and UI/CLI surfaces for polygonal OBJ, MDL, MD2, and MD3 geometry decoding,
  MDC, MDR, and IQM header reading, skin resolution against the open package, a
  software orthographic viewport with textured, flat-shaded, and wireframe modes
  and frame playback, and single-frame Wavefront OBJ export.
- Session services for external-change detection across the open map, package,
  editor file, and project manifest, and a crash-capture layer with an
  on-disk report format, previous-session detection, and report pruning.
- Compiled-artifact inspection for Quake, Quake II, and Quake III BSP files plus
  the leak point and portal files emitted alongside them.
- Chained build pipelines for Quake, Quake III, and Doom node building, and
  reviewable game launch plans built from installation profiles.
- Advanced Studio services and UI/CLI surfaces for shader script parsing and
  stage edits, sprite naming/sequencing/package plans, source indexing with
  syntax highlighting, extension discovery/command planning, and staged AI
  creation proposals.
- Editor profiles that make the Levels page work like the editor a mapper
  knows. **TrenchBroom** gives one 3D-first view, right-drag look, WASD with Q
  and X to fly, Alt+right-drag orbit, a drag over empty space that draws a
  brush, and its keys. **NetRadiant Custom** puts the camera beside a 2D view
  that draws where the camera is, with Shift to select and clicks that step
  down a stack, a right click for mouse look, the middle button to aim the
  camera, Space to clone, and its keys. **GtkRadiant 1.6.0** keeps its
  classic hands: Shift+click to select (a plain press only moves, resizes, or
  draws), Shift+Alt+click to drill, Alt+drag for an area, a right click for
  free look, the arrows with comma, period, D, and C to drive the camera,
  Delete and Insert to zoom, and Backspace to delete. Each comes from that
  editor's own defaults; each profile's controls are data in
  `core/level_editor_controls`. The Levels **Controls** button switches
  profiles and lists every gesture and key, and so does
  `vibestudio --cli editor controls`. QuArK-style still changes keys only.
- A navigation rail that folds to icons by itself and opens over the page,
  labels and all, while the pointer rests on it or the keyboard is in it; a
  pin keeps it open, and Settings offers icons only.
- AI-free-by-default settings, provider-neutral connector/model metadata,
  redacted credential discovery, safe AI-callable tool descriptors, and
  local-only proposal workflows for OpenAI, Claude, Gemini, ElevenLabs, Meshy,
  local/offline, and custom connector paths.
- An opt-in **Assistant** that asks a text model about the open work: OpenAI,
  Claude, Gemini, a local runtime such as Ollama, or any OpenAI-compatible
  endpoint. The user ticks what goes with the question (the selected code, the
  map summary, the last build's problems, the project), paths are shortened
  and key-shaped text removed, **Preview** shows the exact request, and the
  first question from a project to an endpoint off this machine asks first.
  **Explain** on the Build page and **Ask Assistant About Selection** in the
  code editor start one; `vibestudio --cli ai ask` and `ai test-connection`
  do the same from a shell. AI-free mode, the default, sends nothing.
- Tiny license-clean sample projects for Doom, Quake, and Quake III-family
  smoke checks.
- Portable packaging stages the built binary, docs, credits, licence notices,
  samples and a package manifest. Windows tooling additionally verifies the Qt
  runtime and pairs the binary ZIP with its captured application/runtime sources.
- An About/Credits/license surface shared by the GUI inspector and CLI.
- CI workflows for cross-platform build/test, an offscreen GUI self-test, sample
  validation, packaging validation and submodule verification. Windows jobs are
  configured for paired runtime/source artifacts; hosted execution and release
  publication remain separate acceptance steps.
- Documentation for architecture, compiler integration, installation management, support scope, dependencies, roadmap, and credits.
- External compiler imports preserved as Git submodules.

## Imported Level Compilers
Compiler sources are imported as submodules under `external/compilers` to keep
upstream history, licensing, and update paths intact.

| Tool | Purpose | Upstream | Imported revision |
|---|---|---|---|
| ericw-tools | Quake/idTech2-style `qbsp`, `vis`, `light`, `bspinfo`, `bsputil` | [ericwa/ericw-tools](https://github.com/ericwa/ericw-tools) | `f80b1e216a415581aea7475cb52b16b8c4859084` |
| q3map2-nrc | q3map2 compiler from NetRadiant Custom for idTech3 BSP compile, light, conversion, and packaging flow | [Garux/netradiant-custom](https://github.com/Garux/netradiant-custom) | `68ecbed64b7be78741878c730279b5471d978c7c` |
| ZDBSP | Doom-family node building, including vanilla, Hexen, GL, and UDMF-aware modes | [rheit/zdbsp](https://github.com/rheit/zdbsp) | `bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659` |
| ZokumBSP | Doom-family node, blockmap, and reject building for vanilla-conscious maps | [zokum-no/zokumbsp](https://github.com/zokum-no/zokumbsp) | `22af6defeb84ce836e0b184d6be5e80f127d9451` |

Clone with compiler sources:
```sh
git clone --recursive https://github.com/themuffinator/VibeStudio.git
```

Initialize compiler sources after a normal clone:
```sh
git submodule update --init --recursive
```

See [`docs/COMPILER_INTEGRATION.md`](docs/COMPILER_INTEGRATION.md) for the
planned wrapper layer and licensing boundaries.

## PakFu Lineage
VibeStudio uses [PakFu](https://github.com/themuffinator/PakFu) as the working
reference for repository structure, Meson/Qt stack, GitHub Actions style,
README format, archive/package handling, game installation detection, parser
hardening, release discipline, and credits policy.

PakFu code should move into VibeStudio deliberately: credited, tested, and
reshaped into modules that serve the larger studio instead of remaining a
standalone archive app in disguise.

## Build and Run

### Prerequisites
- C++20 toolchain
- Meson + Ninja
- Qt6 Core, Gui, Widgets, and Network
- Qt6 Multimedia, optional: present, the Audio page plays sounds; absent, or
  with `-Daudio_playback=disabled`, it still decodes and draws them
- Qt Linguist `lrelease`, optional: present it compiles the `.qm` translation
  catalogs, absent the build still succeeds and the application uses the source
  language
- Git submodules for imported compiler source review

### Configure And Build
```sh
meson setup builddir --backend ninja
meson compile -C builddir
meson test -C builddir --print-errorlogs
```

Windows helper:
```powershell
pwsh -NoProfile -File scripts/meson_build.ps1
```

### Run
Unix-like systems:
```sh
./builddir/src/vibestudio
./builddir/src/vibestudio --cli --help
```

Windows:
```powershell
.\builddir\src\vibestudio.exe
.\builddir\src\vibestudio.exe --cli --help
```

## CLI Quick Reference
Usage:
```text
vibestudio --cli [options]
vibestudio --cli [--json] <family> <command> [arguments]
```

Global options that apply to any invocation:
- `--settings-file <path>`: resolve every persistent setting against an explicit
  INI file instead of the user's real preference store. Read before any other
  argument, so scripted runs, CI, and tests never mutate real preferences.
- `--json`: emit machine-readable JSON for supported commands.
- `--quiet`: suppress successful narration; errors still go to stderr.
- `--verbose`: print timing and extra diagnostics for supported text commands.
- `--dry-run`: report what a write-capable command would do without touching the
  file system.
- `--overwrite`: allow a command to replace an existing output file.
- `--watch`: stream task-log entries while a long-running command is active.
- `--task-state`: include machine-readable task state objects in JSON output.

The GUI binary also accepts `--self-test`, which builds the shell, visits every
mode so each page is laid out and painted at least once, reports shortcut
conflicts, and exits. CI runs it under an offscreen platform plugin as a GUI
smoke check:
```sh
QT_QPA_PLATFORM=offscreen ./builddir/src/vibestudio --self-test
```

`--open <path>`, repeatable, opens a map, package, project folder, shader
script, entity definition file, or text file exactly as dropping it on the
window would. `--ui-snapshot <dir>` renders every work surface, plus the
Activity panel, to numbered PNG files and exits; `--ui-snapshot-size WxH` sets
the window size first. Combined with `--open` and an isolated
`--settings-file`, it produces documentation screenshots without touching the
user's session:
```sh
QT_QPA_PLATFORM=offscreen ./builddir/src/vibestudio --settings-file /tmp/snap.ini \
  --open samples/projects/quake-minimal/maps/start.map --ui-snapshot ./screens --ui-snapshot-size 1600x1000
```

Single-flag commands:
- `--version`: print the application version.
- `--help`: print CLI help.
- `--exit-codes`: print stable exit-code identifiers and meanings.
- `--studio-report`: print planned studio modules.
- `--compiler-report`: print imported compiler metadata.
- `--compiler-registry`: print compiler registry and executable discovery.
- `--platform-report`: print platform and Qt runtime details.
- `--project-init <path>`: create or refresh `.vibestudio/project.json`.
- `--project-info <path>`: print project manifest and health summary.
- `--project-validate <path>`: validate project health and return a stable
  validation exit code for blocking issues.
- `--project-installation <id>` / `--project-editor-profile <id>` /
  `--project-palette <id>` / `--project-compiler-profile <id>` /
  `--project-compiler-search-paths <paths>` /
  `--project-compiler-tool <id> --project-compiler-executable <path>` /
  `--project-ai-free <on|off>`: set project-local overrides while refreshing a
  manifest. `--project-ai-free on` stops every AI request while the project is
  open; `off` cannot switch AI on against the studio's AI-free mode.
- `--operation-states`: print reusable operation state identifiers.
- `--localization-report`: print localization targets, pseudo-localization,
  RTL smoke coverage, formatting samples, expansion stress status, and
  translation catalog status.
- `--ui-primitives`: print reusable UI primitive identifiers and use cases.
- `--ui-semantics`: print status chip, shortcut registry, and command palette
  metadata.
- `--package-formats`: print package/archive interface descriptors.
- `--check-package-path <path>`: normalize and validate a package virtual path.
- `--info <path>`: print read-only package summary for a folder, PAK, WAD, ZIP, or PK3.
- `--list <path>`: list package entries and metadata.
- `--preview-package <path> --preview-entry <virtual-path>`: print text,
  image, model, audio, script, or binary metadata for a package entry.
- `--extract <path> --output <folder> [--extract-entry <virtual-path>]`:
  extract selected entries, or every entry when no entry is passed.
- `--extract-all`: extract every readable package entry.
- `--validate-package <path>`: stream and validate package payloads, reporting integrity failures and warnings.
- `--settings-report`: print persistent settings storage and recent projects.
- `--setup-report`: print first-run setup status and summary.
- `--setup-start`: start or resume first-run setup.
- `--setup-step <id>`: resume setup at a specific step.
- `--setup-next`: advance setup to the next step.
- `--setup-skip`: skip setup for now without completing it.
- `--setup-complete`: mark setup complete.
- `--setup-reset`: reset setup progress.
- `--preferences-report`: print accessibility and language preferences.
- `--set-locale <code>`: set the preferred UI locale code.
- `--set-theme <id>`: set `system`, `dark`, `light`,
  `high-contrast-dark`, or `high-contrast-light`.
- `--set-text-scale <percent>`: set text scale from 100 to 200.
- `--set-density <id>`: set `comfortable`, `standard`, or `compact`.
- `--set-reduced-motion <on|off>`: store the reduced-motion preference.
- `--set-tts <on|off>`: store the OS-backed text-to-speech preference.
- `--installations-report`: print saved manual game installation profiles.
- `--add-installation <root>`: add or update a manual installation profile.
- `--install-game <key>` / `--install-engine <id>` / `--install-name <name>`:
  refine the profile created by `--add-installation`.
- `--select-installation <id>`: mark a saved installation profile as selected.
- `--validate-installation <id>`: validate a saved profile read-only.
- `--remove-installation <id>`: remove a saved profile without touching files.
- `--detect-installations [--detect-install-root <path>]`: scan common Steam
  and GOG roots plus optional explicit roots for confirmable candidates without
  saving profiles.
- `--recent-projects`: print remembered project folders.
- `--add-recent-project <path>`: remember a project folder.
- `--remove-recent-project <path>`: forget a project folder without touching
  files.
- `--clear-recent-projects`: clear remembered project folders without touching
  project files.

Command families (111 registered commands; `vibestudio --cli cli commands --json`
prints the authoritative list):

`cli` and `ui`
- `cli exit-codes`: print the stable exit-code contract.
- `cli commands`: print the registered command surface used for help and
  documentation validation.
- `ui semantics`: report non-color-only status chip semantics, default
  shortcuts, shortcut conflicts, and command palette entries.

`project`
- `project init <path> [--installation <id>] [--editor-profile <id>]`: create
  or refresh a project manifest and optional project-local overrides.
- `project info <path>`: print project manifest and health summary.
- `project files <project-root> [--where <query>] [--max-files <count>]`: list source,
  media, map, package and studio-document candidates using the Go to File catalog;
  supports metadata filters such as `kind=image` and JSON output.

- `project validate <path>`: validate project health and return
  `validation-failed` for blocking issues.

`install`
- `install list`: list saved manual game installation profiles.
- `install detect [--root <path>]`: detect Steam/GOG candidates read-only
  without saving profiles.
- `install add <root> [--install-game <key>] [--install-engine <id>]
  [--install-name <name>] [--install-executable <path>]
  [--install-base-packages <paths>] [--install-mod-packages <paths>]
  [--install-palette <id>] [--install-compiler-profile <id>]
  [--install-read-only <on|off>] [--install-hidden <on|off>] [--dry-run]`:
  add or update a manual installation profile.
- `install select <id>`: mark a saved profile as the selected installation.
- `install validate <id>`: validate a saved profile read-only.
- `install remove <id> [--dry-run]`: remove a saved profile without touching
  game files.

`package`
- `package recoveries [--directory <store>]`: list local checkpoint metadata.
  `package draft-recover <id> --expected-sha256 <hash> --output <new.vibepackage>`
  restores verified content and edit history; `--dry-run` creates nothing.
  Repaired drafts can explicitly retain unreadable deleted originals as history
  metadata. Recovery reports the count; Undo may reveal missing bytes and block
  export until repaired again. Current content and existing draft objects remain
  strictly verified. Reviewed
  `package recovery-discard <id> --expected-sha256 <hash>` previews removal unless
  `--write` is supplied. The GUI's **File > Recover Packages** offers the same
  recovery workflow and automatic checkpoint settings (30 seconds, 8 GiB and 32
  copies by default). Limits retain existing copies. Inventory includes storage
  usage and a `storageSha256` review token; discard also accepts
  `--expected-storage-sha256` for incomplete copies without a manifest.
- `package draft-storage <draft.vibepackage>`: verify content/history and review
  retained/unused storage, limits and checksums. `package draft-compact
  <draft.vibepackage> --expected-storage-sha256 <hash>` previews cleanup; `--write`
  requires all participating readers to finish before reclaiming unused objects.
  **Recover Packages > Saved Draft Storage…** shares review, cleanup and save
  limits (32 GiB/200,000 files per draft by default).
- `package working-imports [--directory <store>]`: review retained working sessions,
  byte/file reservations and storage checksums. `package working-discard <id>
  --expected-storage-sha256 <hash>` previews removal; `--write` verifies the session
  is no longer live before discarding reviewed files. Dry runs create no locks and
  report `leaseChecked: false`. **Recover Packages > Working Import Storage…**
  provides the same review and limits (8 GiB/50,000 files by default).
- `package working-unlock <relative-lock-path> --expected-lock-sha256 <hash>`:
  previews release of a listed working-import lock; `--write` requires the
  reviewed file and native owner exclusion. **Review Lock Files…** provides
  the GUI workflow, including interrupted empty locks. Payloads remain intact.
  Normal application shutdown drains import cleanup before exiting.
- `package create <output> [--format pak|zip|pk3|wad] [--wad-magic PWAD|IWAD|WAD2|WAD3]`:
  create an empty or staged archive or `.vibepackage` draft without a source.
  Draft format defaults to PK3; `--dry-run` writes nothing. The GUI offers
  **New Package**, with unsaved-document decisions even for an empty document.
- Package drag-out and temporary authoring copies stream on a cancellable
  worker, preserving exact entries and empty folders. Only a fully verified
  batch is handed off. A late Cancel or Close before acceptance discards the
  prepared batch and releases its reservations after verified cleanup. File >
  Temporary Package Copies reviews per-window
  reservations and limits; `package copy-limits` inspects/proposes the same policy
  with explicit `--write` to save. Review Retained Copies inspects actual payload
  usage and offers reviewed discard of unused managed sessions, including crash
  remnants. Live copies retain native ownership. `package copy-sessions` and
  `package copy-discard` share the review; deletion requires `--write`.
  Shared Storage Limits and `package copy-store-limits` cap initial reservations
  across all cooperating windows/processes using that store, including pending
  and crash-retained copies. Policy changes preserve existing copies and refuse
  stale reviews. Later consumer growth remains outside these reservations.
  See [Temporary Copies](docs/PACKAGE_MANAGER.md#temporary-copies-and-drag-out).
- Package entry listing and filtering run in the background with record
  progress and **Cancel/Retry**. The native list model retains every match,
  prepares presentation on demand and preserves exact duplicate-entry identity.
  The folder tree and composition share the prepared metadata; folder Rename/Delete
  remain available while its entry list filters.
  See [Browsing and Filtering](docs/PACKAGE_MANAGER.md#browsing-and-filtering).
- Package entry metadata previews run on a coalescing worker with progress,
  **Cancel Preview** and **Retry Preview**. Only the current planned entry
  publishes; full samples require integrity success and partial samples remain
  explicitly truncated. See [Package Manager](docs/PACKAGE_MANAGER.md#entry-previews).
- Accepted file imports/replacements retain independent working bytes through
  undo and background reads; changing or deleting the original file leaves them
  usable. Copies use the configured temporary directory and are released after
  their last reader/history reference. Drafts and checkpoints provide persistence.
  CLI inspection and dry runs use verified references without temporary imports.
- Staging also supports `--mkdir <path>`, `--rename-folder <path> --folder-to
  <unused-path>`, and `--delete-folder <path>`. GUI folder edits use the same
  atomic operation and undo history. ZIP/PK3 preserve empty folders; PAK refuses
  to discard them. WAD remains flat. `package draft-save --dry-run` checks all
  payloads and history without writing. See [Package Manager](docs/PACKAGE_MANAGER.md).
- `package info <path>` / `package list <path>`: inspect a folder, PAK, WAD,
  ZIP, or PK3 package.
- `package preview <path> <virtual-path>`: preview one package entry. Use
  `--entry-index N` instead of a path to select one exact repeated occurrence
  from `package list --json`.
- `package extract <path> --output <folder> [--entry <virtual-path>]`: extract
  one or more entries, or all entries when no entry is supplied. Repeat
  `--entry-index N` with paired `--as <relative-path>` destinations to separate
  repeated names; GUI extraction offers the same output-path review.
- `package validate <path> [--max-entry-bytes <n>]`: stream and verify package
  payloads and return `validation-failed` for corrupt, unreadable, unchecked,
  or warning-bearing inputs. Use `--json` for per-entry hashes and outcomes.
- `package stage <path> [stage options] [--resolve block|replace-existing|skip]`:
  preview staged add, replace, rename, and delete operations with before/after
  entry and composition JSON. `--replace-ordinal N --replace-file <file>`,
  `--rename-ordinal N --to <path>`, and `--delete-ordinal N` address original
  source occurrences and also work with draft-save, manifest and save-as.
- `package manifest <path> --output <manifest.json> [stage options]`: export a
  schema-versioned staged package manifest without writing an archive.
- `package save-as <path> <output> [--format pak|zip|pk3|wad] [stage options] [--in-place] [--backup <path>] [--dry-run]`:
  write or dry-run a staged package to a new path, report blockers, hashes,
  output paths, and optional manifest JSON. Writing back over the source path is
  refused unless `--in-place` is passed, which builds the archive beside the
  target, verifies it and a recovery copy, and atomically replaces the output.
  The original bytes are retained at `--backup` (default `<output>.bak`). JSON
  `outputCommitted` distinguishes publication from later bookkeeping warnings.
- `package recover <journal> [--finish] [--json]`: inspect an interrupted save.
  `--finish` completes the backup and cleanup only when the replacement is
  already installed and verified; changed files require review.
- `package compare <left> <right> [--against <path>] [--metadata-only] [--include-directories] [--max-entry-bytes <n>]`:
  compare two packages entry by entry and report added, removed, changed,
  case-only, identical, and unchecked members plus the evidence for each verdict.
  Returns `validation-failed` for differences or unverified content; explicit
  metadata-only mode compares names and sizes without reading contents.
- `package compare <source> --staged [staging options] [--json]`:
  review additions, replacements, renames, and deletions without writing output.
  Uses the same options as `package stage`; omit the second package path.

`asset` and `texture`
- `asset audio-edit <input> --operation <edit> --output <file.wav|file.vsaudio>`: edit local
  WAV/DMX audio or a package sound selected with `--entry`. Supports trim, delete,
  silence, fades, reverse, gain, normalization, mono/stereo conversion, polarity,
  DC correction, paste/mix, silence insertion, and resampling with `--sample-rate`, plus
  `--start-frame`, exclusive `--end-frame`, `--db`, `--dry-run`, `--overwrite`,
  and JSON reports. Audio > Edit Sound and Open Audio expose the same services
  with frame-accurate waveform selection, sample zoom, high-quality resampling, a shared audio clipboard,
  undo/redo, auditioning, and package staging. See the
  [Audio Editor guide](docs/AUDIO_EDITOR.md) for formats and limits.
- `asset audio-export <input> --output <file.wav|file.dmx|file.lmp>`:
  deliver a separate sound with `--preset wav|doom|quake|quake2|quake3`, optional
  final dither, reproducible dry runs, and source protection. Export Audio and
  Stage Sound expose the same conversion services in the editor.
- `asset audio-analyze <input> [--start-frame N --end-frame N --entry <path>]`:
  inspect per-channel sample/true peak, integrated loudness, RMS, DC offset, and over-range samples/runs
  without writing. The editor's Analyze command shares the exact frame-range
  measurements; JSON reports include absolute frame positions and channel order.
  Surround loudness needs reviewed `--channel-map` roles; `--no-loudness` skips it.
- `asset audio-new --output <file.vsaudio> [--sample-rate 44100 --channels 1 --frames 0]`:
  create an empty audio project or initial silence. Positive frame counts also
  support WAV output. The editor's New command shares the same validation.
- `asset audio-project <input> [--output <file.vsaudio|file.wav>]`: inspect,
  import, copy, or recover lossless audio projects and export integer/float WAV. Supports
  JSON, dry runs, explicit overwrite, and source protection. The Audio Editor
  also saves `.vsaudio` projects and local recovery copies; exporting/staging
  does not clear an unsaved project. `asset audio-edit` accepts native projects
  for processing without intermediate quantization. WAV delivery accepts `--wav-format`
  (pcm8/pcm16/pcm24/pcm32/float32), optional `--dither tpdf`, and `--dither-seed`.
- `asset audio-session new|inspect|import|edit|arrange|range|automation|effects|effect-automation|presets|tempo-map|position|mixdown|stems|recover|transport`: create and
  arrange lossless `.vssession` multitrack documents, edit clips/mixer/automation,
  and stream WAV mixdowns through shared GUI/CLI services. Stereo buses, pre/post
  sends, polarity/swap, validated solo paths and ordered built-in insert effects
  persist in version-7 sessions; version-1/2/3/4/5/6 documents remain readable.
  **Effects…** and **Master Effects…** stage EQ, dynamics, delay, saturation,
  reverb and modulation, with nine factory recipes and portable `.vsfx` presets
  and numeric parameter automation. Linear/step/smooth curves share graphical
  and native point controls, playback/export, CLI and version-7 sessions; presets
  contain static values and replacing a chain clears its old lanes.
  **Selection…** edits clips across tracks with named groups, relative moves,
  duplicates, gain/fades/mute and sample-preserving splits through fades. Native
  saves and recovery retain edit links and original fade segments.
  **Range…** clears, ripple-deletes, inserts silence or repeats a section on
  explicit tracks, with optional curve-preserving automation following and a
  separate master choice. Tempo/meter markers stay in place. GUI/CLI edits share
  undo/recovery, exact fades, native v7 persistence and rendered delivery.
  **Media…** reviews embedded source usage, identical relinking, replacement
  across all clips, source names and unused-source removal. File digests guard
  reviewed inputs; replacement keeps timing, fades, groups and automation.
  Missing original files leave embedded playback available, and source cleanup
  never deletes files. GUI/CLI changes share undo/recovery and rendered delivery.
  **Meters…** shows pre/post track, bus and master sample peaks, RMS, held maxima,
  over-range counts and stereo correlation. Device-free range analysis and
  `asset audio-session meters` share the renderer and exact latency-aware bounds.
  **Tempo / Meter…** edits stepped tempo and bar-boundary signature maps with
  musical ruler, snapping and bar.beat.tick navigation shared by GUI and CLI.
  Map edits preserve audio and automation sample positions through undo/recovery.
  A lookahead sample-peak limiter and automatic route compensation keep parallel
  tracks, buses, sends, playback and delivered WAVs on the authored timeline.
  Session loops retain delay/reverb, routing and modulation state while sources
  and automation repeat; wrapped lookahead primes once. Device-free transport
  diagnostics use the same continuous processing and sample digest.
  **Export Stems…** delivers aligned pre/post track and bus WAVs plus an optional
  master mix, with integer dither, guarded batch publication and a delivery
  manifest. GUI/CLI reports retain partial results on cancellation or failure.
  **Audio → Multitrack…**
  provides the same initial mono/stereo workflow; **To Session** and **Edit Mixdown**
  connect waveform editing, analysis, game delivery, packaging and level placement.
  Session checkpoints share the waveform recovery preference, storage budget and
  startup review. `recover` verifies `--expected-sha256` and writes a separate
  native session while preserving its original session and reviewed copy.
  See [session limits](docs/AUDIO_EDITOR.md#multitrack-sessions) and the open
  [professional DAW plan](docs/plans/audio-daw.md).
- `asset audio-recording inspect <folder.vsrecord>`: inspect a grouped recording
  plan, final receipt, timing and independently verified per-arm take prefixes.
  Missing/interrupted receipts remain inspectable; this never opens a device.
  `asset audio-recording import <session> --review <plan.json> --output <session>`
  shares the native Record Tracks review's hash/range/channel/target validation,
  grouping and clip replacement. Finite loop recording preserves continuous
  monitoring/effects and journals; version-2 review JSON selects one-based
  `loopPass` with local frame ranges. Review also queues repeated-pass comp sections
  with validated after-cut linear crossfades, one-step undo and version-3 CLI review.
  Saved review files reopen the complete ordered selection queue with source
  verification and guarded Save/Save As; CLI `asset audio-recording save-review`
  shares the portable JSON format and relative recording paths.
  Review audition compares focused sections or the full comp with optional backing,
  pause/seek/repeat, and `asset audio-recording preview` exports the same reviewed
  span as guarded float32 WAV. Source journals are retained. Dry-run and guarded overwrite are supported;
  see [the recording contract](docs/AUDIO_DUPLEX.md).
- `asset audio-take inspect|export <take.vstake>`: verify recorded blocks or export
  a reviewed frame/channel range to a separate native project or float32 WAV.
  **Multitrack → Single Take…** selects and arms an explicit input, saves a
  recoverable take and imports reviewed ranges through session undo/save. See
  [recording limits and recovery](docs/AUDIO_EDITOR.md#recording-and-recorded-takes);
  Record Tracks provides synchronized backing, monitoring and finite loops.
  Native-device acceptance remains open.

- `asset audio-recoveries [--directory <folder>]`: verify and inspect local audio
  checkpoints. The editor's **Recoveries…** manager restores reviewed drafts and
  offers explicit discard. Count/storage limits preserve existing copies; live
  editor leases and reviewed hashes protect active or changed records. CLI
  discard defaults to a dry run and requires `--write` to commit; session records
  require `--kind session` (the default kind remains `waveform`).
- `asset audio-markers <input> [--markers <JSON> --output <separate file>]`:
  inspect or author named cues and one forward loop, with JSON, dry runs, and
  native/WAV output. The editor's **Markers…** and **Select Loop** commands share
  validation, undo, recovery, and edit/resampling transforms. WAV and Quake/II
  presets report retained metadata; Doom DMX has no marker container.
- `asset inspect <package> <virtual-path>`: inspect image, model, audio, text,
  script, or binary metadata for one package entry.
- `asset convert <package> --output <folder> [--entry <virtual-path>]`:
  batch-convert package images with optional `--format`, `--crop`, `--resize`,
  `--palette`, `--dry-run`, and `--overwrite`.
- `asset audio-wav <package> <virtual-path> --output <file.wav>`: export
  readable WAV/PCM package entries, and a WAD's Doom DMX sounds, as 16-bit PCM
  WAV with dry-run and overwrite controls.
- `asset find` / `asset replace`: search or safely replace project text/script
  assets with file/line matches and save-state reporting.
- `texture decode <package> <virtual-path> [--palette <id>] [--output <file.png>] [--dry-run] [--overwrite]`:
  decode a Doom patch or flat, Quake `.lmp`, WAD2 or WAD3 miptexture, Quake II
  `.wal`, `.m8`, `.m32`, or `.sp2`, PCX, Targa, or a Quake or Half-Life `.spr`
  entry, report dimensions, mip levels, frames, transparency, and which palette
  was used, and optionally write a PNG.
- `texture palette [<package>] [--palette <id>]`: resolve the palette used to
  decode indexed art and report whether it came from the package, which virtual
  path it was read from, which paths were searched, and whether a generated
  stand-in was substituted.

`map`, `entity`, and `bsp`
- `map place-sound <map> --package <archive-or-folder> --sound sound/name.wav --game quake2|quake3 --origin x,y,z --output <separate.map>`:
  validate a package WAV and place a speaker in a separate map output. Use
  `--mode loop-on|loop-off|triggered`, with `--targetname` for inactive sounds,
  and `--dry-run` to preview without writing. Map and package inputs are protected.
- `map inspect`, `map edit`, `map move`, and `map compile-plan`: inspect Doom
  WAD map lumps and Quake-family `.map` files, make safe non-destructive edits,
  and hand off to compiler profile plans; on a Doom map `map edit --select
  sector:N`, `linedef:N`, `sidedef:N`, or `thing:N` sets that record's fields,
  such as `--set floorheight=32` or `--set front.middle=DOOR1`. `map edit
  --where "<query>"` sets a key on every entity the query keeps, as the Levels
  inspector does for several selected entities. `map inspect --json` lists
  entities, brushes, patches, target links, and Doom vertices, linedefs,
  sidedefs, sectors, and things.
- `map place-sound <map> --package <archive|folder|draft> --sound sound/name.wav --origin x,y,z --output <separate.map>`:
  validate a mono 22050 Hz PCM16 package sound and add a Quake II/III speaker.
  Supports explicit `--game quake2|quake3`, `--mode loop-on|loop-off|triggered`,
  `--targetname`, deterministic `--dry-run` and `--json`. Inactive modes require
  a target name. The Audio editor's **Stage & Place in Level** converts and stages
  the sound with the same entity validation; map and package keep independent
  undo histories and saves. See [sound placement](docs/AUDIO_EDITOR.md#place-a-sound-in-a-level).
- `map add-entity <path> --class <classname> --origin x,y,z [--set key=value] --output <path>`
  and `map delete <path> --object kind:id [--object kind:id] --output <path>`:
  add a point entity to a Quake-family `.map`, or delete entities, brushes, and
  patches from one, writing the result to a new path. An entity takes its
  brushes with it, and `worldspawn` is never deleted.
- `map duplicate <path> --object kind:id [--object kind:id] [--delta x,y,z] --output <path>`:
  copy entities, brushes, and patches, moved by the delta. A copied brush joins
  the entity that holds the original, and an entity's copy brings its brushes.
  On a Doom or Hexen map, `map duplicate` and `map delete` take things.
- `map replace-texture <path> --from <texture> --to <texture> [--object kind:id] --output <path>`:
  replace every use of a texture on brush faces, patches, or Doom walls and
  flats, across the map or only the given objects, writing each name in place.
- `map rotate <path> --object kind:id [--axis x|y|z] [--turns <n>] --output <path>`:
  turn objects by quarter turns about an axis through their centre; brush
  planes and Valve 220 texture axes turn with them, and so do entity angles
  about z.
- `map flip <path> --object kind:id --axis x|y|z --output <path>`: mirror
  objects through their centre; faces swap two points and patches reverse their
  columns so everything still faces out. Doom/Hexen/UDMF X/Y reflection reverses
  linedef endpoints while preserving sidedefs, actions and native texture offsets.
  Use `--connected` to explicitly expand attached geometry; the GUI provides
  **Select Connected Geometry**. WAD node data requires rebuilding after reflection.
- `map snap <path> --object kind:id [--object kind:id] [--grid <units>] --output <path>`:
  move entities, brushes, patches, things, or vertices onto the grid, each by
  its own amount.
- `map apply-texture <path> --object kind:id --texture <name> --output <path>`:
  put a texture on every face of the given brushes and on the given patches,
  the way a texture browser applies one.
- `map split-linedef <wad> --map <name> --object linedef:N --output <path>` and
  `map flip-linedef <wad> --map <name> --object linedef:N --output <path>`:
  split a Doom linedef at its middle, texture offsets carried on, or turn it
  around, sides and all; run a node builder before playing the result.
- `map draw-sector <wad> --map <name> --points "x,y x,y x,y" --output <path>`:
  draw a Doom sector from its corners, joining the vertices and splitting the
  linedefs it meets.
- `map connect <path> --object entity:N --object entity:M --output <path>`: make
  entities target the last one given; without a targetname it takes the name a
  source already targets, or else `t<N>`.
- `map shift-sectors <wad> --map <name> --object sector:N --field floor|ceiling|light --by N --output <path>`
  and `map gradient-sectors <wad> --map <name> --object sector:N ... --field floor|ceiling|light --output <path>`:
  raise or lower Doom sectors, or spread a field evenly from the first to the last.
- `map make-door <wad> --map <name> --object sector:N [--door-texture NAME] [--track-texture NAME] [--ceiling-flat NAME] [--keep-offsets] --output <path>`:
  make Doom sectors doors, Doom Builder style.
- `map join-sectors <wad> --map <name> --object sector:N --object sector:M --output <path>` and
  `map merge-sectors` with the same arguments: make Doom sectors one, merging
  also taking away the lines between them.
- `map merge-vertices <wad> --map <name> --object vertex:N --object vertex:M --output <path>`:
  join Doom vertices into the last one given, stitching lines drawn over each
  other into one two-sided line.
- `map carve <path> --object kind:id --output <path>`: carve the given brushes
  out of every brush they overlap, as CSG subtraction does, so a doorway brush
  cuts its opening; the carving brushes stay.
- `map cap-patch <path> --patch 0 --boundary first-row --boundary last-row --output <path>`:
  add exact planar caps with shared GUI/CLI validation, UV choices and one undo step.
  See [Patch Caps](docs/LEVEL_EDITOR.md#patch-caps).
- `map move`, `map rotate`, `map flip` and `map resize` share exact brush texture
  locking with the editor: `--texture-lock on|off` and explicit `--allow-valve220`
  conversion. Texture Lock defaults on; resize stretching defaults off. Settings,
  undo, model/material references and compiler/package handoffs stay connected.
  See [Transform Texture Controls](docs/LEVEL_EDITOR.md#transform-texture-controls).
- `map stitch-patches <path> --first 0:last-column --second 1:first-column --output <path>`:
  join patch boundaries with exact grid refinement and optional UV/tangent matching;
  the GUI uses the same service. See [Patch Stitching](docs/LEVEL_EDITOR.md#patch-stitching).
- `map merge-brushes <path> --object brush:0 --object brush:1 --output <path>`:
  merge an exact convex union, reviewing material/UV/flag conflicts with
  `--dry-run --json` and resolving them with `--face-source outputFace=brushId:sourceFace`.
  The [Merge Brushes dialog](docs/LEVEL_EDITOR.md#brush-merging) shares this
  service, package/staging previews and one-step undo.
- `map hollow <path> --object kind:id --thickness <units> --output <path>`:
  turn a brush into walls of that thickness, one per face, so a block becomes
  a room.
- `map clip <path> --object kind:id (--axis x|y|z --at <units> | --points "a b c") [--keep back|front|both] --output <path>`:
  cut brushes with a plane, keeping the part below or above it, or both as two
  brushes; the new face is written in the brush's own format.
- `map resize <path> --object kind:id (--size x,y,z | --mins x,y,z --maxs x,y,z) --output <path>`:
  fit objects to new bounds, or a new size from their lower corner. Faces keep
  their textures where they are in the world, so a longer wall shows more of
  its texture rather than a stretched one.
- `map add-brush <path> --mins x,y,z --maxs x,y,z --texture <name> --output <path>`:
  add a box brush to worldspawn, or choose `--shape wedge|cylinder|cone|sphere`.
  `--axis x|y|z`, `--sides 3..64` and sphere `--bands 2..16` control geometry.
  Uses the map's current classic, Valve 220, `brushDef` or `brushDef3` format.
  The GUI has the same [primitive builder](docs/LEVEL_EDITOR.md#brush-primitives)
  with package materials, a background preview and one undo step.
- `map add-thing <wad> --map <name> --type <DoomEd number> --origin x,y [--angle <degrees>] --output <path>`:
  add a thing to a Doom or Hexen map, on every skill (and, for Hexen, every
  class and game mode).
- `map textures <path> [--package <path>] [--root <path>] [--search-paths <paths>] [--project-root <path>] [--no-decode]`:
  check every texture a map references against the textures a package or folder
  actually provides, with up to 64 folder roots sharing index limits.
  Incomplete sources report `textures.sourceIndexComplete: false` and exit 4;
  `--uses <texture>` instead lists the selectors of the objects that use one texture.
- `package list <path> --where "<query>"`: list only the entries a query
  matches, with the Packages filter's syntax (`--where "ext=wav size>1mb"`).
- `map find <path> --where "<query>" [--map <name>]`: list the selectors of the
  objects a query matches, with the Levels Objects filter's syntax:
  `key=value`, `key:text`, `key!=value`, `key<n`, `key>n`, and plain words, all
  of which must hold (`--where "class=light light>200"`, `--where tag=3`).
- `map render <path> [--projection top-xy|front-xz|side-zy] [--width <px>] [--height <px>] [--grid <units>] [--no-grid] [--labels] [--links] [--high-contrast] [--highlight <object>] [--leak <file.pts>] [--output <file.svg>] [--dry-run] [--overwrite]`:
  render a deterministic SVG picture of a map. Without `--output` the SVG goes
  to stdout. `--leak` draws a compiler leak point file (`.pts` or `.lin`) over
  the map and frames the picture to include it, and `--links` draws entity
  target links as arrows (dashed for `killtarget`).
- `entity definitions <paths…> [--definitions <path>] [--definition-paths "<a;b>"] [--project-root <path>] [--class <classname>] [--no-recursive]`:
  load Radiant `.def`/`.qc`, Valve `.fgd`, and Quake III `.ent` catalogues and
  list the classes they declare with key and spawnflag counts. `--class` prints
  one class in full. With no path, `--project-root` falls back to the project's
  conventional definition folders.
- `entity validate <map> --definitions <path> [--project-root <path>] [--no-recursive] [--strict]`:
  check a map's entities against the catalogue — classnames, declared keys,
  value types, required keys, spawnflag bits, and `target`/`targetname`
  references. Returns `validation-failed` when any error is reported; `--strict`
  makes warnings, such as a classname the catalogue does not declare, fail too.
- `bsp inspect <path.bsp>`: inspect a compiled Quake, Quake II, or Quake III
  BSP — magic, version, lump table, entities, and textures — plus any
  `.pts`/`.lin` leak point file and `.prt` portal file sitting beside it.
  Returns `validation-failed` when the BSP does not parse.

`model`
- `model animations <source> [--clip N]`: list saved clip indices and frame ranges.
  Animation edits author clips, copy full poses, and generate in-between frames
  across all surfaces and attachments, with undo/recovery and matching CLI behavior.
- `model materials <source> --package <archive-folder-or-draft> [--palette <id>]`: inspect
  per-surface images, shader references, dimensions, and preview problems; text/JSON.
  Select external slots with `--surface N --material-slot N`, MDL skin pixels with
  `--skin N --member N`, or package `.skin` bindings with `--entry`/`--entry-index`.
  These modes share the Models > Skin inspector and preserve original bindings;
  see [browser appearance contracts](docs/MODEL_MATERIAL_SLOTS.md#model-browser-appearances).
- `model topology <source> [--surface N]`: list indexed edge endpoints and incident
  faces, plus duplicate faces, unused vertices, disconnected fans, winding
  conflicts, nonmanifold edges and boundaries. Health inspection and five explicit surface repairs
  share the editor, CLI, undo and recovery services. Mesh editing also includes
  conforming edge splits and all-frame distance welding with seam protection.
  Split Nonmanifold Edges preserves two-face connections and every animated
  corner while separating branching connections; it can create open boundaries.
- `model repair-import <source> [--output new.mesh.json]`: review explicit
  repairs for invalid/collapsed faces, unusable normals and orphaned seams in
  mesh/native sources. Without an output it is read-only; `--dry-run` validates
  a new destination. Original and existing files cannot be overwritten.
- `model intersections <source> [--frame all|N] [--surface all|N]`: inspect
  face crossings and coplanar overlaps within and between surfaces across stored
  poses. Health exposes the same cancellable scan and exact face/pose navigation.
  Shared boundaries and isolated point contacts are allowed; bounded failures
  preserve the source and do not publish a partial report.
- `model uv <source> [--surface N]`: inspect UV islands, bounds, face/vertex
  indices, and marked seams. UV edits support seam marking/clearing, face
  detachment, island expansion, explicit pivots, and offset snapping. Use
  `--uv-pivot-mode islands` for independent chart centres; `--uv-islands`
  explicitly expands partial component selections before the transform.
- `model recoveries [--directory <folder>]`: list local mesh recovery headers.
- `model recover <copy.vsmeshrecovery> --output <new.mesh.json>`: verify a
  recovery payload and restore to a new source, with dry-run and JSON support.
  Existing files, original sources, and recovery copies are protected.
- `model import <input> --output <source.mesh.json>`: import polygonal OBJ, MDL/MD2/MD3 or bake
  primitives into an editable mesh document.
- `model edit <source.mesh.json> --operation <name> --output <source.mesh.json>`:
  apply a validated geometry, UV, material, or frame edit. See
  [Editable Meshes](docs/MODEL_MESH.md) for selectors and operation options.
- `model mdl <source.mesh.json> [--operation ... --output <source.mesh.json>]`:
  inspect or edit exact indexed skins, native groups/timing, palette and header
  settings. See [MDL operations](docs/MODEL_MESH.md#native-mdl-cli).
- `model slots <source.mesh.json>`: list or edit ordered external material slots,
  retaining duplicates with set/insert/remove/move/replace/clear operations.
  `model materials --surface N --material-slot N` previews an alternate without
  changing source or export order. See [Material Slots](docs/MODEL_MATERIAL_SLOTS.md).
- `model edit <source.mesh.json> --operation transform --surfaces all|0,2`:
  transform whole surfaces around one pivot using normal snapping, frame scope,
  dry-run and output rules; JSON includes the selected surface indices.
- `model surfaces <source.mesh.json>`: list surface indices and material slots,
  or apply reviewed all-pose rename, separate, move, duplicate, delete or join
  operations with the same validation as the GUI. See
  [Surface Authoring](docs/MODEL_SURFACES.md) for selections and CLI examples.
- `model skin <source.mesh.json> --file <file.skin> --output <source.mesh.json>`:
  apply complete Quake III surface-to-shader assignments with dry-run/JSON
  support. Package/draft inputs use `--package` with `--entry` or an exact
  `--entry-index`. See [skin assignments](docs/MODEL_MESH.md#quake-iii-skin-assignments).
- `model build <design.model.json|source.mesh.json> --output <model.mdl|model.md2|model.md3|model.obj>`:
  build a static prop or animated mesh, with `--dry-run`, `--overwrite`, and JSON.
  `--frame N` selects the OBJ frame; MD3 includes all frames and tags.
- `map place-model <map> --package <source> --entry <model.md3> --origin x,y,z --output <map>`:
  place a decoded package model in a Quake III map through the shared map service.
- `model inspect <package> <virtual-path> [--palette <id>]` or
  `model inspect --file <path.md3>`: decode MDL, MD2, and MD3 geometry and
  report format, version, frames, surfaces, vertices, triangles, tags, skins,
  inferred animations, bounds, and warnings. MDC, MDR, and IQM report their
  header only. Returns `unavailable` when the entry is not a recognised idTech
  model.
- `model export <package> <virtual-path> [--frame <n>] [--material <name>] [--output <file.obj>] [--dry-run] [--overwrite]`:
  write one frame as a Wavefront OBJ, or print it to stdout when `--output` is
  omitted. Returns `unavailable` for the header-only formats, which have no
  geometry to export. Archives, folders and `.vibepackage` drafts share source
  protection; choose an output outside their input storage. Dry runs validate the
  same destination without creating it. JSON includes `notes`; text stdout keeps
  raw OBJ clean by sending those notes to stderr when no output file is requested.

`build` and `launch`
- `build list`: list the chained pipelines and their stages. Current pipelines
  are `quake-full`, `quake-fast`, `quake-bsp-only`, `quake3-full`,
  `quake3-bsp-only`, `doom-zdbsp`, and `doom-zokumbsp`.
- `build plan <pipeline> --input <path>`: plan a pipeline without running
  anything. Optional flags include `--output <path>`, `--working-directory`,
  `--workspace-root`, `--disable-stage <id>`, `--stage-args <id>=<args>`,
  `--compiler-search-paths <paths>`, and `--timeout-ms <ms>`.
- `build run <pipeline> --input <path>`: run the pipeline stage by stage through
  the shared compiler runner, chaining each stage output into the next, with
  `--watch` streaming, per-stage logs, diagnostics, leak detection,
  `--manifest <folder>` records, and `--register-output`.
- `launch plan [--installation <id>] [--launch-profile <id>] [--map <name>] [--mod <dir>] [--basedir <dir>] [--bsp <path>] [--executable <path>] [--extra-args "<args>"] [--deploy [--allow-test-maps]]`:
  build a reviewable launch command line without starting anything. Without
  `--map`, a `--bsp` path names the map. Doom-family launches turn a map lump
  into `-warp`'s numbers (`MAP07` becomes `-warp 07`, `E2M3` becomes
  `-warp 2 3`) and pass the installation's IWAD file to `-iwad` (its first base
  package, or a known IWAD found in its folder; `--basedir` may name the IWAD). `--deploy` adds where the built map
  would be copied (`<root>/<mod or base game>/maps`).
- `launch run [same options]`: start the configured game installation with the
  planned command line and report the process id. With `--deploy` the built map
  is copied into the game folder first; a read-only installation refuses the
  copy unless `--allow-test-maps` is given, which saves that permission on the
  profile.

`compiler`
- `compiler list`: print compiler registry and executable discovery.
- `compiler profiles`: list wrapper profiles, currently ericw-tools
  `qbsp`/`vis`/`light`, ZDBSP nodes, ZokumBSP nodes, and q3map2 BSP, vis, and
  light stages.
- `compiler set-path <tool> --executable <path>` / `compiler clear-path <tool>`:
  manage user-configured compiler executable overrides.
- `compiler plan <profile> --input <path>`: build a reviewable compiler command
  plan. Optional flags include `--output <path>`, `--extra-args "<args>"`, and
  `--workspace-root <path>`.
- `compiler manifest <profile> --input <path>`: print or write a
  schema-versioned command manifest. Add `--manifest <path>` to save JSON.
- `compiler run <profile> --input <path>`: execute a profile through the shared
  runner, capture stdout/stderr, diagnostics, hashes, duration, and exit code,
  stream `--watch` task logs, emit `--task-state` JSON, and optionally write
  `--manifest <path>` and `--register-output`.
- `compiler rerun <manifest-path>`: re-run a saved command manifest.
- `compiler copy-command <manifest-path|profile>`: print a reproducible command
  line for support, automation, or CI.

`shader`, `sprite`, `code`, and `editor`
- `shader inspect <shader-file> [--package <path>]`: parse idTech3 shader
  scripts into stage graphs, previews, raw detail, and package-reference
  validation reports.
- `shader set-stage`: round-trip a stage directive edit with `--shader`,
  `--stage`, `--directive`, `--value`, and `--output` without modifying the
  source in place.
- `sprite plan --engine doom|quake --name <name>`: create Doom lump naming or
  Quake `.spr` frame plans with palette, sequence, and package staging notes.
- `code files <project-root> [--where <query>] [--max-files <count>]`: list source filenames
  and metadata using the Files panel's bounded background catalog service. Filters
  such as `language=cpp size>10kb` use cached metadata; partial scans report warnings
  and exit 4. The GUI retains selection and folder expansion across refreshes.
- `code language-server <file> --server <absolute-executable>`: inspect diagnostics
  through a local stdio language server. Use `--root`, `--language`, JSON-array
  `--server-args`, `--timeout-ms`, and optional one-based `--line`/`--column` for
  definition lookup, `--hover` for symbol documentation, `--signature-help` for call
  overloads and active arguments, `--completion` for validated edit proposals, or `--references`
  for semantic locations and source hashes. `--exclude-declaration` limits reference
  queries to uses. `--completion --resolve-completion N` retrieves deferred
  metadata and related edits for one indexed suggestion without writing files.
  Hover, signature, completion and reference queries require `--line` and cannot be combined.
  `--format-document` and `--format-range` preview formatting without requiring
  diagnostic publication; `--write --expected-sha256 <saved-file-hash>` applies
  validated edits through the shared atomic text-save service. `--rename <new-name>`
  previews project edits; `--write --expected-plan-sha256 <plan-hash>` applies the
  reviewed plan. `--code-actions` lists quick fixes/refactorings for a caret or
  selection; `--action-index N` resolves and previews one, with the same plan-hash
  requirement for writes. Rename and code actions also run independently of diagnostics. Other queries wait for a real diagnostic publication;
  `--json` exposes report versions, locations, logs and failures. See
  [Local Language Services](docs/LANGUAGE_SERVICES.md).
- `code index <project-root> [--find <symbol>] [--max-files <count>]`: scan source trees, language
  hooks, diagnostics, symbols, build task hints, and source-port launch profiles.
  Scans are bounded and report incomplete results. The GUI runs the same scanner
  asynchronously with cancellation and snapshots of open Code documents.
- `editor profiles` / `editor current` / `editor select <id>`: list, print, or
  select the routed editor interaction profile.

`localization` and `diagnostics`
- `localization targets` / `localization report`: list the target language set
  or report pseudo-localization, RTL, locale formatting, expansion, compiled
  `.qm` coverage, and TS catalog status.
- `diagnostics bundle [--output <folder>]`: print or write a redacted support
  bundle with version, platform, command, module, operation-state, and
  localization diagnostics.
- `editor keys [--reset]`: list the keys the user gave commands in place of
  their defaults, or clear them all.
- `diagnostics crashes`: list the crash reports kept on this machine, newest
  first, with each one's time, version, reason, and report path (`--json` adds
  the backtrace and the logged lines).

`extension`, `about`, and `credits`
- `extension discover`, `extension inspect`, and `extension run`: load
  `vibestudio.extension.json` manifests, report trust/sandbox metadata, and
  dry-run or execute reviewed extension command plans.
- `about show`: print version, platform, imported compiler, and credits
  metadata.
- `credits validate`: validate README and `docs/CREDITS.md` coverage plus
  compiler pins, `.gitmodules`, and checked-out submodule revisions.

`ai` (every command is local and no-write; no provider is ever contacted)
- `ai status` / `ai connectors` / `ai tools`: print AI-free mode and opt-in
  settings, provider-neutral connector descriptors, and safe AI-callable
  VibeStudio tool descriptors.
- `ai explain-log --log <path>|--text <text>`: explain compiler output as a
  no-write workflow manifest.
- `ai propose-command --prompt <text>`: propose a reviewable compiler command
  from natural language.
- `ai propose-manifest <project-root>`: draft a project manifest preview without
  writing it.
- `ai package-deps <package>`: suggest likely missing package dependencies from
  metadata.
- `ai cli-command --prompt <text>`, `ai fix-plan`, `ai asset-request`, and
  `ai compare`: generate staged, reviewable automation proposals.
- `ai shader-scaffold`, `ai entity-snippet`, `ai package-plan`,
  `ai batch-recipe`, and `ai review`: generate and inspect staged Advanced
  Studio creation proposals with summary, context, actions, prompts, and
  response detail.

## Documentation
- [`AGENTS.md`](AGENTS.md): contributor and automation rules.
- [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md): task sizing, validation,
  accessibility/localization, and credits expectations.
- [`docs/ACCESSIBILITY_LOCALIZATION.md`](docs/ACCESSIBILITY_LOCALIZATION.md): accessibility, high-visibility, scaling, TTS, and localization goals.
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md): studio architecture and module boundaries.
- [`docs/AI_AUTOMATION.md`](docs/AI_AUTOMATION.md): provider-neutral generative and agentic AI connector strategy.
- [`docs/CLI_STRATEGY.md`](docs/CLI_STRATEGY.md): full-featured CLI strategy and command coverage goals.
- [`docs/COMPILER_INTEGRATION.md`](docs/COMPILER_INTEGRATION.md): compiler imports, wrapper plans, and licensing boundaries.
- [`docs/DEPENDENCIES.md`](docs/DEPENDENCIES.md): dependency baseline.
- [`docs/EFFICIENCY.md`](docs/EFFICIENCY.md): efficiency philosophy, acceleration techniques, AI-free mode, and speed metrics.
- [`docs/EDITOR_PROFILES.md`](docs/EDITOR_PROFILES.md): adaptable level-editor layout and control profiles.
- [`docs/GAME_INSTALLATIONS.md`](docs/GAME_INSTALLATIONS.md): installation detection and profile management plan.
- [`docs/INITIAL_SETUP.md`](docs/INITIAL_SETUP.md): first-run setup and ecosystem tailoring flow.
- [`docs/LANGUAGE_SERVICES.md`](docs/LANGUAGE_SERVICES.md): optional local language server setup, live diagnostics, completion, Parameter Hints, navigation and CLI.
- [`docs/PACKAGING.md`](docs/PACKAGING.md): portable staging, recorded builds, Windows runtime/source pairs and remaining release gates.
- [`docs/RELEASE_CANDIDATE.md`](docs/RELEASE_CANDIDATE.md): current MVP release-candidate scope, gaps, and validation gate.
- [`docs/ROADMAP.md`](docs/ROADMAP.md): metric-driven roadmap, MVP definition, and task checklist.
- [`docs/STACK.md`](docs/STACK.md): preferred technology stack and stack decision rationale.
- [`docs/SUPPORT_MATRIX.md`](docs/SUPPORT_MATRIX.md): initial format and workflow support target.
- [`docs/UX_DESIGN.md`](docs/UX_DESIGN.md): user-aware modern UX, progress feedback, progressive disclosure, and visual communication philosophy.
- [`docs/CREDITS.md`](docs/CREDITS.md): complete attribution list.

## Credits

The level generator follows the pipeline design of
[Quake-MapGen](https://github.com/themuffinator/Quake-MapGen) (MIT, `1252548`,
2026-08-26, reviewed 2026-10-06), and the texture generator the prompt design
and derived-layer idea of [TexAI](https://github.com/themuffinator/TexAI)
(GPL-3.0, `7f56a4b`, 2026-02-14); the sound synthesizer follows the preset
kinds and voice model of DrPetter's [sfxr](https://drpetter.se/project_sfxr.html)
(MIT, 2007, reviewed 2026-10-06). All are original implementations with no
code copied. See [Generative Level, Texture, And Sound Design](docs/CREDITS.md#generative-level-texture-and-sound-design-2026-10-06).

Placed MD3 compiler appearances follow NetRadiant Custom's
[q3map2 model contract](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/model.cpp)
(GPL-2.0-or-later) and its bundled
[Assimp MD3 importer](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/libs/assimp/code/AssetLib/MD3/MD3Loader.cpp)
(BSD-3-Clause), revision `68ecbed`, reviewed 2026-10-06. See
[the implementation and licence review](docs/CREDITS.md#placed-model-compiler-appearances).

ZIP/PK3 metadata parsing follows [PKWARE APPNOTE 6.3.10](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT)
(2022-11-01). Legacy filename decoding uses the [Unicode CP437 mapping](https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/PC/CP437.TXT)
(table 2.00, 1996-04-24), under [Unicode License V3](docs/licenses/UNICODE-LICENSE.txt).
See [archive-format credits](docs/CREDITS.md#compression-and-archive-formats) for the implementation and licence review.

Session reverb derives its topology and delay tuning from Jezar at Dreampoint's
public-domain [Freeverb Components](https://github.com/sinshu/freeverb/tree/cfcea55553fb59ac57ebf2a237f72cad4296f2b0/Components)
(June 2000), with original stereo, decay, damping and history handling. Details
and licence review are in [Credits](docs/CREDITS.md#audio-reverb-reference).

Session EQ uses an original C++ implementation of Robert Bristow-Johnson's
[Audio EQ Cookbook coefficients](https://www.w3.org/TR/2021/NOTE-audio-eq-cookbook-20210608/),
W3C Note dated 2021-06-08, under the GPL-compatible W3C Software and Document
License (2015). Reference, review and preserved notices are in
[Credits](docs/CREDITS.md#audio-session-eq).

- Model `.skin` authoring references Quake III's
  [skin reader](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_image.c),
  [surface normalization](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_model.c)
  and [shader lookup](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_mesh.c),
  GPL-2.0-or-later, local `master` reviewed 2026-10-05. No upstream code or game
  assets copied; see [Model Skin Bindings](docs/CREDITS.md#model-skin-bindings).
  The same credited reader supplies non-destructive linked skins in model assemblies.

- Model native winding and tag conventions follow id Software's
  [Quake](https://github.com/id-Software/Quake/blob/master/WinQuake/gl_rmain.c),
  [Quake II](https://github.com/id-Software/Quake-2/blob/master/ref_gl/gl_rmain.c)
  and [Quake III](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_model.c)
  sources, GPL-2.0-or-later, `master` reviewed 2026-10-05. The optional
  [FTE](https://github.com/fte-team/fteqw) dedicated-server and offscreen EGL
  rendering tests use public QuakeC/MenuQC interfaces and generated assets.
  The nested assembly bake fixture and shared headless runner extend those
  tests with original pose, normal and pixel oracles, reviewed 2026-10-06.
  Native skin-padding guidance follows Quake's
  [GL skin loader](https://github.com/id-Software/Quake/blob/master/WinQuake/gl_model.c),
  under the same licence/review date. FTE retains its GPL notices and stays a
  separate external tool; source/interface
  provenance and compatibility findings are in
  [Credits](docs/CREDITS.md#model-native-engine-acceptance) and
  [Model Engine Acceptance](docs/MODEL_ENGINE_ACCEPTANCE.md).

- Additional level-editor familiarity references: [Hammer/Worldcraft](https://developer.valvesoftware.com/wiki/Hammer_Hotkey_Reference),
  [J.A.C.K.](https://jack.hlfx.ru/en/main.html), [DarkRadiant](https://www.darkradiant.net/userguide/),
  [Doom Builder/UDB](https://github.com/UltimateDoomBuilder/UltimateDoomBuilder),
  [SLADE](https://github.com/sirjuddington/SLADE), [Eureka](https://github.com/ioan-chera/eureka-editor),
  [Unreal](https://dev.epicgames.com/documentation/en-us/unreal-engine/viewport-controls-in-unreal-engine),
  [Unity](https://docs.unity3d.com/Manual/SceneViewNavigation.html),
  [Godot](https://docs.godotengine.org/en/stable/tutorials/3d/introduction_to_3d.html) and
  [Blender](https://docs.blender.org/manual/en/latest/editors/3dview/navigate/index.html).
  Independent behavior adaptations only; no upstream code/assets incorporated.
  Reference revisions, licenses and differences are recorded in [Credits](docs/CREDITS.md)
  and [Editor Profiles](docs/EDITOR_PROFILES.md).

- Standalone [NetRadiant controls](https://github.com/xonotic/netradiant/tree/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant),
  revision `b4b295d7a37797cc2752e48aa8ce42492e7016f0` (GPL-2.0-or-later), and
  [Sledge 2.0.7.2 navigation](https://github.com/LogicAndTrick/sledge/tree/8762a6de07a9fa486d51aff0913cdc0306fd775c/Sledge.BspEditor.Rendering/Viewport),
  revision `8762a6de07a9fa486d51aff0913cdc0306fd775c` (BSD-3-Clause), inform
  independent profile and temporary-navigation behavior. Both licences were
  reviewed for GPLv3 compatibility on 2026-10-05. No upstream code or assets
  copied; [pinned modules and differences](docs/CREDITS.md#editor-workflow-inspirations)
  accompany the implementation.

- [id Software Q3Radiant](https://github.com/id-Software/Quake-III-Arena/tree/dbe4ddb10315479fc00086f08e25d968b4b43c49/q3radiant),
  revision `dbe4ddb10315479fc00086f08e25d968b4b43c49`, informs classic profile,
  position-steering and fixed camera-step behavior. `MainFrm.cpp` also informs
  the profile's texture shift, rotation and fit shortcuts; quick surface
  operations retain VibeStudio's documented UV mapping rules. `CamWnd.cpp`, `MainFrm.cpp`
  and the plan/preferences modules are GPL-2.0-or-later, reviewed as GPLv3-compatible
  on 2026-10-06. The Qt implementation is independent; no game assets are used.
  [Detailed credits and remaining differences](docs/CREDITS.md#editor-workflow-inspirations).

- Level workspace maximization and sizing keys follow the
  [original Hammer 3.4 reference](https://documentation.help/Valve-Hammer-Editor-3.4/Hotkey_Reference.htm),
  [J.A.C.K. 1.1 manual](https://valvedev.info/tools/jack/jack_manual.pdf) and
  [NetRadiant Custom command registration](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/radiant/mainframe.cpp).
  Original VibeStudio implementation; behavioral facts only. Licenses and review
  date are recorded in [Credits](docs/CREDITS.md#editor-workflow-inspirations).

### Additional asset exchange credits

DDS decoding and FTX/SWL format knowledge and image writing derive from
[PakFu's format modules](https://github.com/themuffinator/PakFu/tree/13111e4c07513548a29fd7eb74fc9d004c44aa59/src/formats),
GPL-3.0, local working snapshot reviewed 2026-10-05 based on `13111e4c`.
The shared capability catalog follows its explicit support-tier approach.
See [codec provenance](docs/CREDITS.md#pakfu-image-exchange-and-capability-catalog).


- Model clip-brush conventions reference [ericw-tools' Quake hull handling](https://github.com/ericwa/ericw-tools/blob/f80b1e216a415581aea7475cb52b16b8c4859084/qbsp/brush.cc), id Software's [Quake II contents/masks](https://github.com/id-Software/Quake-2/blob/master/game/q_shared.h) and [Quake III shader-driven contents](https://github.com/id-Software/Quake-III-Arena/blob/master/q3map/map.c). GPL-2.0-or-later reference interfaces, reviewed 2026-10-05; no upstream implementation or game assets imported. See [collision credits](docs/CREDITS.md#model-collision).
- Collision compiler tests independently read BSP layouts documented by [ericw-tools](https://github.com/ericwa/ericw-tools/tree/f80b1e216a415581aea7475cb52b16b8c4859084/include/common) and [NetRadiant Custom](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/q3map2.h), GPL-2.0-or-later references reviewed 2026-10-05. Generated assets and test implementations are original; [pinned files and licenses](docs/CREDITS.md#model-collision) accompany the workflow.

- UDMF property authoring follows format facts in [UDMF v1.1](https://github.com/rheit/zdoom/blob/master/specs/udmf.txt)
  (James Haley, 2009-03-29, GFDL-1.2-or-later; reference only) and the interface in
  [ZDBSP `processor_udmf.cpp`](https://github.com/rheit/zdbsp/blob/bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659/processor_udmf.cpp)
  (Christoph Oelckers, GPL-2.0-or-later, pinned revision above; reviewed 2026-10-05).
  The parser, native transform encoding and fixtures are original. Native mirrors
  reuse the credited [Doom reflection rules](docs/CREDITS.md#doom-geometry-reflection).
  Polyobject control roles follow the pinned [ZDBSP processor](https://github.com/rheit/zdbsp/blob/bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659/processor.cpp)
  format facts (GPL-2.0-or-later, Randy Heit, reviewed 2026-10-05).
  See [detailed credits](docs/CREDITS.md#udmf-parser-and-property-authoring).

- Doom node readiness uses original bounded readers of the layouts written by
  [ZDBSP `processor.cpp`](https://github.com/rheit/zdbsp/blob/bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659/processor.cpp),
  revision `bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659`, reviewed 2026-10-05.
  GPL-2.0-or-later (Randy Heit), compatible with VibeStudio's GPL-3.0;
  no upstream implementation copied. Editor saves, compiler output checks and
  Doom launch plans share this validation. See [node readiness](docs/LEVEL_EDITOR.md#doom-node-readiness).

- Doom detached reflection workflow: [Ultimate Doom Builder, EditSelectionMode.cs](https://github.com/jewalky/UltimateDoomBuilder/blob/6d9f6038db30adfee0edd74221b74b2de4837f6f/Source/Plugins/BuilderModes/ClassicModes/EditSelectionMode.cs),
  revision `6d9f6038db30adfee0edd74221b74b2de4837f6f`, GPL-3.0, reviewed 2026-10-05.
  Endpoint reversal with retained side ownership is a behaviour reference; no
  C# source is copied. [Detailed credit and scope](docs/CREDITS.md#doom-geometry-reflection).
- Prepared Quake-family builds use interface facts from [ericw-tools settings and filesystem handling](https://github.com/ericwa/ericw-tools/tree/f80b1e216a415581aea7475cb52b16b8c4859084/common), [qbsp](https://github.com/ericwa/ericw-tools/tree/f80b1e216a415581aea7475cb52b16b8c4859084/qbsp), and the BSP dialect handling in [VIS](https://github.com/ericwa/ericw-tools/tree/f80b1e216a415581aea7475cb52b16b8c4859084/vis) and [LIGHT](https://github.com/ericwa/ericw-tools/tree/f80b1e216a415581aea7475cb52b16b8c4859084/light), revision `f80b1e216a415581aea7475cb52b16b8c4859084`, and id Software's [Quake WAD2/miptex layouts](https://github.com/id-Software/Quake/tree/master/WinQuake). GPL-2.0-or-later references reviewed for GPL-3.0 compatibility on 2026-10-05; no upstream implementation or game assets copied. See [detailed credits](docs/CREDITS.md#prepared-quake-family-builds).
- Prepared-build deployment launch flags were checked against id Software's [Quake III filesystem](https://github.com/id-Software/Quake-III-Arena/blob/master/code/qcommon/files.c), [startup command handling](https://github.com/id-Software/Quake-III-Arena/blob/master/code/qcommon/common.c) and [renderer settings](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_init.c), local `master` reviewed 2026-10-05. GPL-2.0-or-later interface references; no upstream implementation copied. See [Credits](docs/CREDITS.md#core-technology).
- Classic PAK numbering and launch interfaces follow id Software's [Quake filesystem](https://github.com/id-Software/Quake/blob/master/WinQuake/common.c) and [Quake II filesystem](https://github.com/id-Software/Quake-2/blob/master/qcommon/files.c), with their video/startup source references. Local `master` snapshots reviewed 2026-10-05, GPL-2.0-or-later compatible with GPL-3.0; no implementation copied. See [Classic Prepared Deployment credits](docs/CREDITS.md#classic-prepared-deployment).

Doom camera texture layout and wall anchors reference
[Chocolate Doom 3.1.0](https://github.com/chocolate-doom/chocolate-doom/tree/chocolate-doom-3.1.0/src/doom)
(`r_data.c`, `r_segs.c`, GPL-2.0-or-later; compatibility reviewed 2026-10-04).
The [credits record](docs/CREDITS.md#doom-camera-materials) identifies the
adaptations and original VibeStudio components.

Shared WAD subset/edit map-group recognition and new-document assembly use the format contracts in
[ZDBSP `wad.cpp`](https://github.com/rheit/zdbsp/blob/bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659/wad.cpp)
(revision `bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659`, GPL-2.0-or-later, reviewed
for GPL-3.0 compatibility on 2026-10-04). VibeStudio owns the implementation;
no upstream source was copied.

Automatic mesh UV charting and packing use Jonathan Young's
[xatlas](https://github.com/jpcy/xatlas/tree/f700c7790aaa030e794b52ba7791a05c085faf0c),
revision `f700c7790aaa030e794b52ba7791a05c085faf0c`, with MIT-licensed
thekla_atlas/NVIDIA and Fast-BVH ancestry and BSD-3-Clause OpenNL code. These
terms were reviewed for GPLv3 compatibility on 2026-10-04. Original notices and
the reviewed indexed-seam, shape-preserving and rectangular-packing build
adaptations (the latter reviewed 2026-10-06) are documented in the
[integration record](external/modelling/xatlas/VIBESTUDIO.md).

- Project search preserves the public [Qt non-path glob contract](https://doc.qt.io/qt-6/qregularexpression.html#WildcardConversionOption-enum)
  on Qt 6.0–6.5 through an original compatibility adapter. Qt 6.4.2/6.10.1,
  GPL-3.0/LGPL-3.0, reviewed 2026-10-04; no Qt code or documentation prose copied.

Saved-draft reader protection uses an original GPL-3.0 VibeStudio adapter to the
public [Windows directory sharing](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew),
[Linux flock](https://man7.org/linux/man-pages/man2/flock.2.html) and
[Apple flock](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/flock.2.html)
contracts, reviewed 2026-10-04. No external source code or documentation prose was
copied. These operating-system APIs add no bundled library. Working-import
lock recovery additionally uses original adapters to
[Windows handle deletion](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle),
cross-checked against the [Qt 6.10.1 lock implementation](https://github.com/qt/qtbase/tree/v6.10.1/src/corelib/io)
(GPL-3.0/LGPL-3.0, compatible with this GPL-3.0 repository), reviewed
2026-10-04. No source or documentation prose was copied.


Multitrack streaming uses an original adapter to the public
[QAudioSink API](https://doc.qt.io/qt-6.10/qaudiosink.html), checked against Qt
6.4.2/6.10.1 (GPL-3.0/LGPL-3.0), 2026-10-05. No Qt code or prose was copied.
See [Audio Editor](docs/AUDIO_EDITOR.md#multitrack-sessions) for frame transport,
selected output, buffering and device-acceptance limits.

Recording uses original adapters to Qt's
[QAudioSource](https://doc.qt.io/qt-6.10/qaudiosource.html) and
[permissions](https://doc.qt.io/qt-6.10/permissions.html), plus guarded OS flush and
macOS bundle-description APIs, reviewed 2026-10-05. Existing Qt GPL-3.0/LGPL-3.0
and system-runtime dependencies remain compatible; no upstream code or prose was
copied. See [recording API credits](docs/CREDITS.md#recording-apis).

Editor and browser audio transport follow the public [Qt Multimedia media API](https://doc.qt.io/qt-6.10/qmediaplayer.html)
and [device API](https://doc.qt.io/qt-6.10/qmediadevices.html), reviewed against Qt
6.10.1 headers on 2026-10-04. Qt 6.4.2's
[FFmpeg end-of-stream behavior](https://github.com/qt/qtmultimedia/blob/v6.4.2/src/plugins/multimedia/ffmpeg/qffmpegmediaplayer.cpp)
also informed verification of the original deferred loop-restart fallback.
The existing GPL-3.0-compatible Qt dependency is unchanged; no Qt source or
documentation text was copied. See [credits](docs/CREDITS.md#core-technology).

Audio level placement and speaker dependency paths follow id Software's
[Quake II speaker](https://github.com/id-Software/Quake-2/blob/master/game/g_target.c),
[sound loader](https://github.com/id-Software/Quake-2/blob/master/client/snd_mem.c),
and [Quake III speaker](https://github.com/id-Software/Quake-III-Arena/blob/master/code/game/g_target.c)
contracts (master reviewed 2026-10-04, GPL-2.0-or-later, compatible with GPL-3.0).
The transaction, validation and UI code are original; no game data or entity
definitions are shipped. See [source and licence details](docs/CREDITS.md#audio-level-placement).

Windows runtime packaging preserves [Qt's SPDX inventories](https://doc.qt.io/qt-6/sbom.html)
(CC0-1.0) and original Qt/FFmpeg licence notices, reviewed against Qt 6.10.1 on
2026-10-05. The original PE comparison follows Microsoft's
[PE/COFF specification](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
without copying code or prose. See [runtime distribution credits](docs/CREDITS.md#runtime-distribution)
and the [deployment workflow](docs/PACKAGING.md#windows-qt-and-audio-runtime).

The local source-distribution audit also retains unchanged
[Qt 6.10.1 sources](https://download.qt.io/official_releases/qt/6.10/6.10.1/submodules/),
[Qt build recipes](https://github.com/qt/qt5/tree/56657e036f579ae0b858ae8c9659a0f7b7a53407),
[FFmpeg n7.1.2](https://github.com/FFmpeg/FFmpeg/tree/f893221c8d89cb798b829bebe71d55e1a3f242fd)
and [zlib 1.3.1](https://github.com/madler/zlib/releases/tag/v1.3.1), with original
notices and pinned hashes. Reviewed 2026-10-05; the selected LGPL/permissive
profile is compatible with GPL-3.0. See the
[source and licence details](docs/CREDITS.md#runtime-distribution).

Release source companions additionally preserve
[QtTools 6.10.1](https://github.com/qt/qttools/tree/9e0030f889168f7a0ec1bb47a7d7138a497b3c96)
and [QtShaderTools 6.10.1](https://github.com/qt/qtshadertools/tree/86c4b079a05c2dbe5fdb6f46ad9df8ef297487a9)
build-tool sources and notices. `lrelease` and `qsb` use GPL-3.0 with Qt's GPL
exception; Qt library components offer LGPL-3.0/GPL-3.0 alternatives.
Reviewed 2026-10-05; these add no application runtime dependency. See the
[source-companion workflow](docs/PACKAGING.md#source-companion).

Windows release orchestration records the fresh Meson build and verifies the
paired binary/source artifacts. Its SDK selection follows the documented
`QT_ROOT_DIR` output of [install-qt-action v4](https://github.com/jurplel/install-qt-action/tree/v4)
(MIT; reviewed 2026-10-05), already used by CI. The orchestration and fixtures
are original Python code; no action implementation was copied.

The optional synchronized recording backend uses [PortAudio](https://github.com/PortAudio/portaudio/tree/873e3c83fbe2f57ebcf59083e627a3f8fa051ffe),
revision `873e3c83fbe2f57ebcf59083e627a3f8fa051ffe`, under its GPLv3-compatible
MIT-style license. Original notices and hashes accompany the private Meson build;
reviewed WASAPI/CoreAudio fixes are generated separately. See the
[backend integration record](external/audio/portaudio/VIBESTUDIO.md).

Audio loudness metering uses Jan Kokemüller's [libebur128 1.2.6](https://github.com/jiixyj/libebur128/tree/67b33abe1558160ed76ada1322329b0e9e058b02),
revision `67b33abe1558160ed76ada1322329b0e9e058b02`. Its MIT library/R128Scan
notices and BSD-3-Clause internal queue were reviewed for GPLv3 compatibility
on 2026-10-04 and are preserved with the unmodified sources. See the
[metering credits](docs/CREDITS.md#audio-loudness-metering).

Compressed audio import uses David Reid's [dr_mp3 0.7.4 and dr_flac 0.13.4](https://github.com/mackron/dr_libs/tree/dfe8377631000664666519fdb83da193fd8037f4),
revision `dfe8377631000664666519fdb83da193fd8037f4`, under MIT-0;
dr_mp3 derives from the public-domain [minimp3](https://github.com/lieff/minimp3).
Vorbis uses Xiph.Org's BSD-3-Clause [libvorbis](https://github.com/xiph/vorbis/tree/c2aa86b05e981c96bf381fc6aa11cdd03eccc2fb),
revision `c2aa86b05e981c96bf381fc6aa11cdd03eccc2fb`, and
[libogg 1.3.6](https://github.com/xiph/ogg/tree/be05b13e98b048f0b5a0f5fa8ce514d56db5f822),
revision `be05b13e98b048f0b5a0f5fa8ce514d56db5f822`.
The licences were reviewed for GPLv3 compatibility on 2026-10-04. Unmodified
sources, hashes and notices live in `external/audio`; see [decoder credits](docs/CREDITS.md#compressed-audio-decoding).

The retained, unbuilt evaluation copy of Sean Barrett's
[stb_vorbis 1.22](https://github.com/nothings/stb/blob/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_vorbis.c)
uses the MIT alternative at revision `2c980bb59875b0d32144a71867fbdebb2f77cd20`.
It is excluded from the application and portable packages.

The original local language client, diagnostic/hover/signature/completion/snippet/formatting/rename/code-action parsers, workspace-edit and reference services follow Microsoft's
[LSP 3.17 specification](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/)
([CC-BY-4.0](https://github.com/microsoft/language-server-protocol/blob/gh-pages/License.txt),
reviewed 2026-10-04). Protocol facts only; no upstream code or prose was copied.
See [the full attribution](docs/CREDITS.md#language-server-protocol).
Completion-resolution and parameter-hint verification also use
[Pyright 1.1.414](https://github.com/microsoft/pyright/tree/1.1.414)
([MIT](https://github.com/microsoft/pyright/blob/1.1.414/LICENSE.txt), reviewed
2026-10-04) as an isolated test executable; it is not bundled with VibeStudio.
Pull-diagnostic verification uses [Ruff 0.16.4](https://github.com/astral-sh/ruff/tree/0.16.4)
under its [MIT license and third-party notices](https://github.com/astral-sh/ruff/blob/0.16.4/LICENSE),
reviewed 2026-10-04, as a separate test process. It is not bundled or linked.

Game sound delivery follows reader behaviour in
[Chocolate Doom 3.1.0 `i_sdlsound.c`](https://github.com/chocolate-doom/chocolate-doom/blob/chocolate-doom-3.1.0/src/i_sdlsound.c),
[Quake `WinQuake/snd_mem.c`](https://github.com/id-Software/Quake/blob/master/WinQuake/snd_mem.c),
[Quake II `client/snd_mem.c`](https://github.com/id-Software/Quake-2/blob/master/client/snd_mem.c), and
[Quake III `code/client/snd_mem.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/client/snd_mem.c).
These GPL-2.0-or-later references are compatible with VibeStudio's GPL-3.0;
reviewed 2026-10-04 (the id repositories' master snapshots on that date).
Only format facts and compatibility behaviour are used; no engine code or game
assets are copied. Preset defaults and the delivery writer are VibeStudio's own.

WAV precision export follows factual Microsoft RIFF/WAVE and
[WAVEFORMATEXTENSIBLE layouts](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ksmedia/ns-ksmedia-waveformatextensible)
(2023-03-13 documentation, reviewed 2026-10-04), cross-checked with
[Peter Kabal's WAVE reference](https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/WAVE.html)
(2022-09-27). The writer and dither are original code; no external source or
documentation text was copied.

Cue/loop metadata follows Microsoft's public
[RIFF 1991](https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/Docs/riffmci.pdf)
and [1994 update](https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/Docs/RIFFNEW.pdf)
format facts, plus the GPL-2.0-or-later Quake/II cue-and-length convention in the
engine references above (reviewed 2026-10-04). Implementation and editing rules
are original; no source code or specification prose is copied.

Level texture projection conventions follow [NetRadiant Custom's q3map2 mapping](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/map.cpp), GPL-2.0-or-later, revision `68ecbed64b7be78741878c730279b5471d978c7c` (reviewed 2026-10-04). See [the detailed credits](docs/CREDITS.md) for source modules and preserved notices.

Sample rate converter designed by Aleksey Vaneev of Voxengo.
[r8brain-free-src 7.5](https://github.com/avaneev/r8brain-free-src/tree/cb2abb9977efe2471979b380ed95daa56ab4fdb9)
is included under MIT at revision `cb2abb9977efe2471979b380ed95daa56ab4fdb9`
(reviewed 2026-10-04), using [Takuya Ooura's FFT](https://www.kurims.kyoto-u.ac.jp/~ooura/fft.html)
under its permissive use/copy/modify/distribute licence. Both are compatible with
GPL-3.0; notices are preserved in `external/audio/r8brain-free-src` and release
licence bundles. See [integration details](external/audio/r8brain-free-src/VIBESTUDIO.md).

Patch authoring follows the format and 31-point control-axis limit in
[NetRadiant Custom's q3map2](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/q3map2.h),
revision `68ecbed64b7be78741878c730279b5471d978c7c` (GPL-2.0-or-later),
reviewed 2026-10-04. The editor, presets and Bézier splitting are original code.
Patch material conversion and package lookup also follow its
[patch and brush token parsing](https://github.com/Garux/netradiant-custom/tree/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2)
at the same revision and license.

- Empty Hexen ACS behavior data follows the layout checked against
  [Chocolate Doom 3.1.0, `P_LoadACScripts`](https://github.com/chocolate-doom/chocolate-doom/blob/chocolate-doom-3.1.0/src/hexen/p_acs.c)
  (GPL-2.0-or-later, compatible with this repository's GPL-3.0; reviewed
  2026-10-04). The starter map and recovery implementation are original; no
  upstream source or commercial assets are incorporated.
- Animated MD2 export follows [Quake II's format and renderer sources](https://github.com/id-Software/Quake-2/blob/master/qcommon/qfiles.h)
  and [Quake II Tools' texel-centre convention](https://github.com/id-Software/Quake-2-Tools/blob/master/qdata/models.c),
  reusing the published [normal table](https://github.com/id-Software/Quake-2/blob/master/ref_gl/anorms.h).
  All are GPL-2.0-or-later, `master` reviewed 2026-10-04, compatible with GPL-3.0.
  The writer, validation, and fixtures are original; details are in [Credits](docs/CREDITS.md).
- MDL native group/indexed-skin preservation and export follow Quake's
  [`modelgen.h`](https://github.com/id-Software/Quake/blob/master/WinQuake/modelgen.h)
  and [`model.c`](https://github.com/id-Software/Quake/blob/master/WinQuake/model.c),
  with renderer limits and compatibility from
  [`gl_model.c`](https://github.com/id-Software/Quake/blob/master/WinQuake/gl_model.c),
  [`gl_model.h`](https://github.com/id-Software/Quake/blob/master/WinQuake/gl_model.h)
  and [`gl_draw.c`](https://github.com/id-Software/Quake/blob/master/WinQuake/gl_draw.c)
  (GPL-2.0-or-later, `master` reviewed 2026-10-04, compatible with GPL-3.0).
  Native timing sampling also follows
  [`r_alias.c`](https://github.com/id-Software/Quake/blob/master/WinQuake/r_alias.c)
  and [`gl_rmain.c`](https://github.com/id-Software/Quake/blob/master/WinQuake/gl_rmain.c)
  under the same licence and review date. Storage, timing edits/sampling, seam packing, the writer and synthetic fixtures are
  original; no game assets are included.
- OBJ polygon import follows [Wavefront Advanced Visualizer 3.0 Appendix B1](https://www.martinreddy.net/gfx/3d/OBJ.spec)
  (reviewed 2026-10-05). The parser, bounded triangulation and synthetic fixtures
  are original GPL-3.0 repository code. The reference has no explicit licence
  grant; only format facts are used, with no reference code or prose incorporated.
  See [OBJ interchange](docs/MODEL_MESH.md#obj-polygon-interchange) for supported
  records and the remaining material-library conversion gap.
- Static model design and animated mesh MD3 export use the public layout in
  [id Software's Quake III `qfiles.h`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/qcommon/qfiles.h)
  (GPL-2.0-or-later, `master` reviewed 2026-10-03). The connected staging and
  material workflow also draws conceptual inspiration from
  [PakFu's model surface representation](https://github.com/themuffinator/PakFu/blob/13111e4c07513548a29fd7eb74fc9d004c44aa59/src/formats/model.h)
  (GPL-3.0, revision `13111e4c`, local checkout reviewed 2026-10-03).
  Implementations and primitive fixtures are original; no upstream code or
  commercial game assets were imported. Details are in [Credits](docs/CREDITS.md).
  The animated writer's original-Quake-III surface limits also follow
  [`R_LoadMD3`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_model.c)
  (GPL-2.0-or-later, `master` reviewed 2026-10-04).
  Attachment orientation in `core/model_tags` follows the local-basis convention in
  [`CG_PositionEntityOnTag`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/cgame/cg_ents.c)
  (same license, `master` reviewed 2026-10-04); authoring code and fixtures are original.
  Native player animation configuration follows Quake III's
  [`CG_ParseAnimationFile` / `CG_RunLerpFrame`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/cgame/cg_players.c)
  and [animation roster](https://github.com/id-Software/Quake-III-Arena/blob/master/code/game/bg_public.h)
  (GPL-2.0-or-later, `master` reviewed 2026-10-06). The bounded production
  implementation and synthetic fixtures are original; see
  [Native Animation](docs/MODEL_ASSEMBLY.md#quake-iii-native-animation).
  Native player package layout and its optional loader oracle also reference
  that `cg_players.c`, plus skin parsing/TGA behavior in
  [`tr_image.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_image.c),
  [`tr_shader.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_shader.c),
  and [`qfiles.h`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/qcommon/qfiles.h)
  (GPL-2.0-or-later, `master` reviewed 2026-10-06). Production code is original;
  extracted test functions retain their upstream notices. See
  [the credited scope](docs/CREDITS.md#quake-iii-animation-configuration).
  Its optional original-engine acceptance harness also extracts the token parser
  from [`q_shared.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/game/q_shared.c)
  (same licence and review date), preserving upstream headers in test outputs.

- Level dependency graphs and selective export take workflow inspiration from
  [PakFu's CLI and asset graph](https://github.com/themuffinator/PakFu/blob/13111e4c07513548a29fd7eb74fc9d004c44aa59/src/cli/cli.cpp)
  and its archive search index (GPL-3.0, revision `13111e4c`, local checkout reviewed
  2026-10-03). These are original VibeStudio implementations. Shader skybox and
  light-image references follow the [Quake III Shader Manual, revision 12](https://icculus.org/gtkradiant/documentation/Q3AShader_Manual/index.htm).
- Creator: [themuffinator](https://github.com/themuffinator) (DarkMatter Productions)
- Structural, archive-tooling, and installation-profile reference: [PakFu](https://github.com/themuffinator/PakFu), with the current package interface, virtual-path safety, and staged package write-back concepts adapted from its archive direction at `c82dfb0ef0b5d7442e243ace8cd83bc45f82f257`; the game installation profile/detection model is a VibeStudio-owned adaptation of PakFu's profile-driven workflow ideas.
- Imported compiler/toolchain sources: [ericw-tools](https://github.com/ericwa/ericw-tools), q3map2 from [NetRadiant Custom](https://github.com/Garux/netradiant-custom), [ZDBSP](https://github.com/rheit/zdbsp), and [ZokumBSP](https://github.com/zokum-no/zokumbsp)
- Prepared build filesystem flags follow q3map2's [path initialization](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/path_init.cpp) and shader-list behavior in its [shader loader](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/shaders.cpp), NetRadiant Custom revision `68ecbed64b7be78741878c730279b5471d978c7c` (GPL-2.0-or-later, reviewed for GPL-3.0 compatibility 2026-10-05). Interface reference only; no upstream code copied.
- Prepared output naming follows that revision's [map shader writer](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/shaders.cpp), [external lightmap writer](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/lightmaps_ydnar.cpp) and `q3map2.h`. BSP header validation follows id Software's [Quake III `qfiles.h`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/qcommon/qfiles.h) and q3map2's [Quake Live extension](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/bspfile_ibsp.cpp). GPL-2.0-or-later format/interface references, reviewed 2026-10-05; no upstream implementation copied. Output receipts, retained history and publication orchestration are VibeStudio code.
- Editor workflow inspirations: [GtkRadiant](https://github.com/TTimo/GtkRadiant), [NetRadiant Custom](https://github.com/Garux/netradiant-custom), [TrenchBroom](https://trenchbroom.github.io/), [QuArK](https://quark.sourceforge.io/), and, for Doom map editing behaviour such as Make Door, [Ultimate Doom Builder](https://github.com/UltimateDoomBuilder/UltimateDoomBuilder) (behaviour only, no code used). The TrenchBroom, NetRadiant Custom, and GtkRadiant 1.6.0 editor profiles follow those editors' default mouse and key bindings, read from their sources (TrenchBroom `master` `90de03c`, September 2026; NetRadiant Custom `68ecbed`; GtkRadiant `1.6-release` `270af88`, August 2024); facts about behaviour only, no code used
- Studio interface inspiration: [idStudio](https://idstudio.idsoftware.com/) (id Software's DOOM Eternal editor, public beta August 2024) for the shell's visual language; inspiration only, with no idStudio code, icons, or assets used. [Visual Studio Code](https://github.com/microsoft/vscode) (MIT) for the one-row studio bar with a centred command search, after its Command Center (pattern only, reviewed 2026-10-06; no code, icons, or assets used)
- Optional AI automation references: [OpenAI API documentation](https://platform.openai.com/docs/quickstart), [Claude API docs](https://platform.claude.com/docs/en/home), [Gemini API docs](https://ai.google.dev/api), [ElevenLabs docs](https://elevenlabs.io/docs/overview/intro), and [Meshy docs](https://docs.meshy.ai/en)
- File-format references: public specifications and wikis (Doom Wiki, Boom's generalized linedef reference, Quake Wiki, Valve Developer Community, IETF RFCs, Xiph), plus behaviour notes from GPL source ports such as [Chocolate Doom](https://github.com/chocolate-doom/chocolate-doom) for DMX sound lumps; layouts are reimplemented and no port code is copied
- Ogg preview metadata and synthetic header fixtures follow [RFC 3533](https://www.rfc-editor.org/rfc/rfc3533) (Internet Society implementation-use notice, May 2003), [Vorbis I section 4.2.2](https://xiph.org/vorbis/doc/Vorbis_I_spec.html) (Xiph specification use permission, 1994–2015), and [RFC 7845 section 4](https://www.rfc-editor.org/rfc/rfc7845) (IETF Trust Legal Provisions, April 2016), reviewed 2026-10-06. Original GPL-3.0 implementation; no specification prose, external code or sound assets copied.
- Full attribution list: [`docs/CREDITS.md`](docs/CREDITS.md)
- Optional independent texture fixture checks use [Pillow 11.3.0](https://github.com/python-pillow/Pillow/tree/11.3.0) (MIT-CMU, reviewed 2026-10-04); no Pillow code is bundled or linked into VibeStudio.
- Indexed PNG structure follows the [PNG third edition, 24 June 2025](https://www.w3.org/TR/2025/REC-png-3-20250624/) (W3C Software and Document License 2023). Grayscale project round-trip behavior was checked against [Qt 6.10.1's PNG handler](https://github.com/qt/qtbase/blob/v6.10.1/src/gui/image/qpnghandler.cpp) (GPL-3.0/LGPL-3.0). Reviewed 2026-10-04; implementations are original, with no sample or Qt code copied.
- Texture export layouts and engine constraints: [Quake](https://github.com/id-Software/Quake), [Quake II](https://github.com/id-Software/Quake-2), [Quake III Arena](https://github.com/id-Software/Quake-III-Arena) and [Chocolate Doom 3.1.0](https://github.com/chocolate-doom/chocolate-doom/tree/chocolate-doom-3.1.0) (GPL-2.0-or-later), plus [GZDoom's patch reader](https://github.com/ZDoom/gzdoom/blob/master/src/common/textures/formats/patchtexture.cpp) (BSD-3-Clause), reviewed 2026-10-04. Format knowledge only; encoders are independently implemented. Exact files and scope are in the credits list above.

The credits above are a maintenance requirement, not a courtesy footer. When
code, assets, documentation, algorithms, file-format knowledge, UI patterns, or
compiler changes are borrowed or derived from another project, update this
section and `docs/CREDITS.md` in the same change.

Theme replacement uses public Qt APIs after checking Qt 6.10.1's
[stylesheet lifecycle](https://github.com/qt/qtbase/blob/v6.10.1/src/widgets/kernel/qapplication.cpp)
and [refresh behavior](https://github.com/qt/qtbase/blob/v6.10.1/src/widgets/styles/qstylesheetstyle.cpp).
Reviewed 2026-10-06 under the GPL-3.0/LGPL-3.0 alternatives; the workaround and
tests are original. Progress bars also preserve Qt 6.10.1's
[Fusion label rendering](https://github.com/qt/qtbase/blob/v6.10.1/src/widgets/styles/qfusionstyle.cpp)
so text changes colour across the fill. This reference was reviewed on the same
date under the same licences; no Qt implementation was copied.
See [Credits](docs/CREDITS.md#core-technology).

Surface clipboard sampling and face/brush paste behavior also credit the pinned
GPL-2.0-or-later [Q3Radiant DRAG.CPP](https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/q3radiant/DRAG.CPP)
and [GtkRadiant drag.cpp](https://github.com/TTimo/GtkRadiant/blob/270af88f3c2471f6773bded0b5760a3115b52965/radiant/drag.cpp),
reviewed 2026-10-06. The clipboard and projection integration are original
VibeStudio implementations; the detailed credit records native differences.
Seamless wrapping and NetRadiant parameter paste also reference the GPL-2.0-or-later
[NetRadiant Custom surface dialog](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/radiant/surfacedialog.cpp)
and [NetRadiant surface dialog](https://github.com/xonotic/netradiant/blob/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant/surfacedialog.cpp),
reviewed 2026-10-06. Selected-value and mapping-only behavior additionally follows
Custom's pinned [select.cpp](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/radiant/select.cpp),
[brush_primit.cpp](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/radiant/brush_primit.cpp)
and [brush_primit.h](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/radiant/brush_primit.h)
under the same licence and review date. The transfer service and rigid UV
transport solver are original VibeStudio code.
Native projected paste additionally references
[`Patch::ProjectTexture`](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/radiant/patch.cpp)
and `Texdef_ProjectTexture`/`BPTexdef_fromST011` in that pinned `brush_primit.cpp`.
GPL-2.0-or-later compatibility was rechecked on 2026-10-06. Patch projection,
mixed undo and compiled UV tests are original implementations; see the
[surface clipboard credits](docs/CREDITS.md#level-material-gesture-references).
Held transfer ordering additionally references `TexManipulator_` in the same
GPL-2.0-or-later [selection.cpp](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/radiant/selection.cpp),
reviewed for GPL-3.0 compatibility on 2026-10-06. Private stroke transactions,
asynchronous previews, stable picking and CLI replay are original implementations.

Material gesture defaults also credit the pinned GPL-2.0-or-later Q3Radiant,
GtkRadiant, NetRadiant and NetRadiant Custom sources documented in
[Level material gesture references](docs/CREDITS.md#level-material-gesture-references),
audited on 2026-10-06; behavior was independently implemented.

## Tech Stack
- Language: C++20.
- Application framework: Qt 6 (Core, Gui, Widgets, Network, and optionally
  Multimedia for audio playback), with `moc` run over
  the shell's `Q_OBJECT` widgets through Meson's `qt6.preprocess`.
- Primary UI: Qt Widgets, with bounded Qt Quick/QML use only where it clearly improves a contained workflow.
- Build: Meson + Ninja, with an `i18n` subdirectory that runs `lrelease` over the
  checked-in `.ts` catalogs to produce the `.qm` files `QTranslator` loads.
  `lrelease` is optional: without it the build succeeds and the application falls
  back to the source language.
- Automation: Python validation scripts and GitHub Actions.
- Project data: JSON manifests, Qt settings with schema migration and a
  `--settings-file` override, and planned SQLite/FTS indexing.
- CLI: active lightweight subcommand router with a testable command registry,
  JSON output, stable exit codes, quiet/verbose modes, dry-run behavior, watch
  streaming, task-state output, and deferred CLI11 adoption.
- Compression: an in-tree DEFLATE codec written from RFC 1951/1950 rather than
  linked against zlib, so ZIP/PK3 reading and writing stays dependency-free,
  deterministic, and fixture-testable, with CRC-32 and Adler-32 alongside it.
  The original incremental encoder follows the 32 KiB history and block rules
  in [RFC 1951 sections 3.2.4–3.2.7](https://www.rfc-editor.org/rfc/rfc1951)
  (version 1.3, May 1996; reviewed 2026-10-04); no upstream implementation was
  copied. VibeStudio's implementation remains GPL-3.0 licensed.
- Rendering: real QPainter work today — the map viewport, model viewport, image
  and palette views, waveform, and charts are all custom-painted widgets, and
  headless map rendering emits SVG as plain text with no Qt paint device at all.
  The model viewport rasterizes orthographic and perspective triangles with a
  software depth buffer and matching nearest-surface picking, so there is still no OpenGL
  dependency anywhere; a thin QOpenGLWidget preview and a later bgfx backend
  remain planned.
- Text/IDE: Qt text widgets with a data-driven `QSyntaxHighlighter` for config,
  shader, and QuakeC sources, plus an optional local stdio LSP client for live
  diagnostics, formatting, semantic completion, Quick Info, Parameter Hints, definitions and references. KSyntaxHighlighting, Tree-sitter and
  broader semantic editing remain planned.
- Media: native idTech image/model/WAV/DMX parsers, pinned dr_libs and Xiph
  compressed-audio decoding, and r8brain sample-rate conversion. MDL/MD2/MD3
  geometry is drawn and exported as OBJ. Optional Qt Multimedia plays browser
  assets and edited float32 selections. Device-free editing remains complete;
  optional Assimp support is still planned.
- Accessibility/localization: Qt accessibility APIs, Qt High DPI behavior, Qt
  Linguist with `QTranslator` installed at start-up, compiled `.qm` catalogs,
  `QLocale`, layout direction applied for RTL locales, seed TS catalogs, and CLI
  localization diagnostics. Qt TextToSpeech is a stack target, not a dependency
  yet; the TTS preference is stored but no speech backend is wired up.
- AI automation: optional provider-neutral connector/model/workflow layer, with
  OpenAI described first for configuration and reviewable no-write experiments
  including shader/entity/package/batch proposals, and planned Claude, Gemini,
  ElevenLabs, Meshy, local/offline, and custom connector paths. No HTTP client
  is wired up: every proposal is generated locally and no provider is contacted.
- External compiler imports: Git submodules.

See [`docs/STACK.md`](docs/STACK.md) for the full stack decision record.

## License
VibeStudio-owned code is distributed under GPLv3 (`LICENSE`).

External compiler submodules and future third-party components retain their own
licenses. Review the license files in each submodule before linking, modifying,
or redistributing compiled binaries as part of VibeStudio packages.
