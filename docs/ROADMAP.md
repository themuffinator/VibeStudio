# VibeStudio Roadmap

The duplex recording engine now has an optional pinned PortAudio device adapter
with WASAPI packet timing/dropout fixes, CoreAudio atomic xrun handling and ALSA
build support. The recording worker adds permission/playback gates, grouped
durable journals, bounded telemetry, stall detection and CLI inspection.
Synthetic callback-to-journal tests cover placement and shutdown.
Native Record Tracks controls and grouped review now connect the worker to
session revision guards, one-step undo and shared CLI import. Playback handoff
waits for browser, waveform and session output shutdown. Live dry-input and
pre-clamp output meters add numeric peak/RMS, maximum/headroom and clipping
states with independent history reset. Finite loop recording now retains a
continuous device/DSP clock, effect tails and repeated automation, with durable
pass counts and exact pass-local GUI/CLI import. Buffered session audition now
shares continuous loop processing and wraps lookahead context, with GUI/CLI
sample-clock coverage. Recording review now queues repeated-pass comp sections
with validated after-cut crossfades and one-step import, shared by CLI v3 review.
Review audition now compares focused sections or full comps with optional
backing, shared transport controls and device-free CLI preview export. Saved
review JSON now restores complete editable queues with relative recording paths,
source verification and guarded Save/Save As in the GUI and CLI. Dedicated
take lanes, session-embedded comp revisions, physical platform acceptance and open-ended loops remain open in the
[full DAW plan](plans/audio-daw.md).

Session meters now share pre/post track, bus and master sample peak, RMS,
held maxima, over-range counts and correlation between live playback and
device-independent range analysis. A native Meters window and strict CLI
report the same renderer taps. True-peak/LUFS live metering, seamless live
mixer edits, sidechains and multichannel monitoring remain future work.

Session **Media…** now shares source inventory, reviewed identical relinking,
explicit replacement and unused-source cleanup with the CLI, undo and recovery.
Replacement preserves arrangement descriptors and flows through existing
rendered delivery. Bulk relink search, source streaming and automatic waveform
source round trips remain open.

Audio arranging now includes multi-track clip selection, persistent groups,
transactional batch edits, fade-preserving splits and scoped range operations
shared by GUI and CLI. Native v7 retains cut automation domains, groups and
inherited fades. The broader [DAW goal](plans/audio-daw.md) remains open: recording and
monitoring, MIDI/instruments, plugin hosting, long-media streaming and advanced
authoring still require implementation and platform acceptance.

Native model import/export now explicitly converts clockwise MDL/MD2/MD3
faces at the counter-clockwise editor/OBJ boundary. Independent byte audits and
an optional generated-data FTE server workflow cover native poses and collision
movement. Original clients, rotated-tag source-port differences, older editable
native-source review and release packaging remain separate acceptance work;
see [Model Engine Acceptance](MODEL_ENGINE_ACCEPTANCE.md).

Level-editor familiarity now exposes 24 working schemes across brush, Doom
and modern scene-editor families. QuArK has routed viewport controls; modern
held-button flight and middle-button orbit/pan use shared routing. Settings,
Controls help, aliases and CLI expose actual bindings and adaptation gaps.
Eighty-four gesture and navigation-key settings can now be overridden per profile
through staged GUI controls, validated CLI batches and portable VibeStudio
import/export. Fly, drive and mouse-look keys share conflict checks and command
overlap diagnostics. Native preference import and native acceptance remain open.
Standalone NetRadiant and Sledge now have distinct audited profiles. Temporary
hold navigation, pitch keys, arrow translation and mouse-button pan are shared
controls, with cancellation on release, lost focus and profile changes.
Q3Radiant adds separately audited classic position steering, fixed ground-plane
movement/pitch steps, and shared brush/surface/patch command bindings. Its
preferences and lifecycle handling use the same GUI/CLI services. GtkRadiant
1.4/1.5, QeRadiant, DoomEdit and BSP now have separately audited control profiles;
additional game-specific Radiant variants still need independent audits. A
searchable profile browser previews their actual controls and adaptations, and
Camera Beside Plans extends the shared layout, bookmark and CLI services.
Temporary viewport maximization and equal sizing now share the Layout/View
menus and command system, preserve underlying layouts and bookmarks, and route
the audited Hammer/J.A.C.K. and NetRadiant Custom workspace keys.
Camera Above Plans adds a wide camera above three plan views, with saved
proportions, bookmarks and accessible grouped authoring menus. Surface-based
camera construction and signed extrusion work through ordinary brush creation;
linear duplicate arrays share preview, cancellation, undo and CLI services.
Plan and camera status tags share bounded, direction-aware layouts for narrow
panes and enlarged text, including the Models surface.
Complete upstream tool/mode parity and the professional editor acceptance
matrix remain open; see [Editor Profiles](EDITOR_PROFILES.md) and
[Level Editor](LEVEL_EDITOR.md).
Plan/camera brush insertion now prepares on a cancellable worker. Numeric
primitive Apply reuses the validated preview with source, selection,
creation-layer, save-state and package guards. Scene locks and exact undo/save
remain shared with CLI creation. Full-shell publication/refresh latency and
native interaction acceptance still need improvement and broader evidence.
Textual map previews now format only their displayed prefixes, preserving total
counts and CLI output. The Objects list now formats rows on demand, resolves
selection by identity and runs cancellable, coalesced queries on a worker.
Material summaries avoid per-surface edit records, repeated normalization and
quadratic name/selected-linedef lookups; workbench updates share one statistics
result, and viewport selection refreshes the inspector once after selection
settles. Shared GUI/CLI tests cover large material palettes
and selected Doom sides.
Plan refresh now preserves shared scene arrays during read-only fitting and
geometry weighting, builds exact geometry keys in one bounded allocation and
avoids unnecessary hidden-owner expansion. Regressions cover shared panes,
10,000-brush cache reuse and inherited visibility with sparse object IDs.
Workbench refreshes share material names, statistics and usage counts across
their consumers. Details preserves the inspected section and unchanged text's
reading position across edits; material tiles and inspector suggestions still
follow live edits and undo.
Full-shell publication and realistic release-scale latency remain gates.

Static and animated model collision boxes now have authoring, fitting, source/recovery,
selection-aware undo, edge/table selection, viewport and numeric transforms,
and GUI/CLI map handoff. World move/rotation and local box scaling share pivots
and snapping. Per-frame tracks follow mesh frame operations, interpolation and
stored-pose map handoff. Arbitrary convex editing, runtime collision animation export, linked updates after
prop edits, shader verification and original-engine acceptance remain open.
This increment does not close the professional modeller release gate. See
[Model Collision](MODEL_COLLISION.md) and [release evidence](MODELLER_RELEASE.md).

Quake-family compilation now targets VibeStudio's own compilers: VibeMap2,
derived from ericw-tools, for Quake and Quake II, and VibeMap3, continuing q3map2
from NetRadiant Custom, for Quake III. Profiles, discovery, pipelines and
prepared builds use them, and the old `ericw-*` and `q3map2*` tool and profile
ids are refused, naming the new id. The Quake/Quake II and Quake III prepared-build
proofs passed with VibeMap2 and VibeMap3 on Windows on 2026-10-08; the other
compiler proofs recorded below ran stock ericw-tools and q3map2 and have not been
repeated yet, and the known-issue catalogue still needs a re-audit against VibeMap2. See
[Compiler Integration](COMPILER_INTEGRATION.md#vibestudio-compilers).

Quake/Quake II/Quake III prepared builds now connect current map edits, generated/staged package
assets, compiler execution and package publication through one verified snapshot.
The GUI and CLI share preparation, input/output receipts, cancellable publication,
stale-review checks and overwrite backups. Real q3map2 and PK3 fixtures cover a
model present only in the draft, generated shaders and external lightmaps, and
clean rebuilds back to internal lightmaps. Quake WAD2 and Quake II WAL builds now
share capture/receipts and PAK publication, with real ericw BSP/VIS/LIGHT proof.
Doom layouts, custom output directories/formats and diagnostic remapping remain
production-loop gates. Quake/Quake II numbered PAK deployment now remembers map
slots and enforces the engines' numbering rules, sharing review, per-use
write permission, backup and launch with Quake III PK3 deployment. Complete review
hashes guard CLI automation across GUI/CLI. Recorder tests cover
launch ordering and windowed arguments; real engine behavior and search-order
conflicts remain acceptance work. See [prepared builds](LEVEL_EDITOR.md#prepared-builds-with-current-assets).

Placement now shares texture-preserving snap/duplicate/paste services, atomic
history, scene protection and native bounds validation. Offset dialogs preview
package-draft materials/models on a worker; CLI and compiler/package proofs
exercise the same results. Per-owner snapping and shared Doom vertex handling
are implemented. Quick Snap, Duplicate and Paste now prepare on workers with
delayed progress, cooperative parser/geometry cancellation and stale-state
publication guards. Large-map throughput, native interaction acceptance,
linked instances and Doom geometry duplication remain open. See
[Placement and grid alignment](LEVEL_EDITOR.md#placement-and-grid-alignment).

Persistent layers and nested groups now share inherited visibility and editing
locks, GUI/CLI controls, undo, source-bound native metadata, recovery and package
grouping. Shared authoring transactions protect owned/shared geometry and retain
atomic model, audio and texture handoffs. Linked instances and native production
acceptance remain open; see [Scene Organization](LEVEL_SCENE.md).

Direct material painting now connects the level camera, package/staging material
previews, per-surface provenance and one-command map undo. Sampling and explicit
keyboard/CLI targets share validation and persistence across brushes, patches
and binary Doom/Hexen surfaces. Native input, larger-map latency, Doom preview
fidelity and gesture interpolation remain open; see
[Material Painting](LEVEL_EDITOR.md#material-painting). Four Radiant profiles now
sample material names with middle click; Q3Radiant/GtkRadiant paint one surface
with Shift+middle. These immediate gestures use the shared material transaction,
locks, undo, picker and preview services without switching tools. Configurable
pairs, legacy-navigation compatibility, GUI/CLI persistence and offscreen checks
are included. Brush sampling now captures material, mapping and flags;
Q3Radiant/GtkRadiant paste onto hit brushes/faces with Ctrl+middle and
Ctrl+Shift+middle. The shared asynchronous clipboard adds explicit world
projection, package dimensions, atomic history and portable CLI definitions.
Native projected paste, sampled depth/light color,
patch UV copying and native input acceptance remain open.

The four-view workspace connects camera, top, front and side panes through the
shared map, selection, visibility and undo services. Layout overrides preserve
profile controls and have a matching CLI setting. The plan panes reuse solved
geometry, retain independent navigation and share package-backed camera assets.
Named camera/plan bookmarks now share validated GUI/CLI storage and portable
files. Optional linked centres, linked scale and camera-follow now share settings,
CLI operations and bookmark state. Native acceptance and large-scene
profiling remain open. See [Four-View Workspace](LEVEL_EDITOR.md#four-view-workspace).
See [Saved Level Views](LEVEL_EDITOR.md#saved-level-views) for bookmark limits.

Reusable `.vprefab` assemblies now connect selection capture, whole brush-entity
ownership, generated model references, package staging, dependency review and
one-step placement undo. GUI and CLI share bounded validation, texture lock and
unique internal target names. Generated assets passed a 27-step q3map2/PK3 proof.
Linked instances, prefab hierarchy capture, Doom prefabs, cross-dialect conversion and
production-scale/native acceptance remain open. See
[Reusable Prefabs](LEVEL_EDITOR.md#reusable-prefabs).

Move, quick/numeric rotation, flip and resize now share affine texture locking
across classic, Valve 220, brushDef and brushDef3 faces. The editor persists
separate rigid/resize lock choices and explicit conversion permission; CLI
options use the same service. A generated three-dialect q3map2-to-PK3 workflow
verified 72 compiled UV samples and 24 package payloads. Component editing and
ownership-aware snap/duplicate/paste now have shared texture-policy coverage.
Large selections and native interaction acceptance still need work. See
[Transform Texture Controls](LEVEL_EDITOR.md#transform-texture-controls).

This roadmap turns the product vision into measurable, task-oriented work. It
uses a gradual improvement philosophy: ship thin vertical slices early, validate
them with real idTech projects, then widen and deepen each surface.

## Ultimate Goal

Patch Stitching joins full boundaries with exact common-grid refinement,
optional tangent/UV matching, shared package/staging previews and one undo step.
The GUI and CLI use the same atomic multi-patch replacement service. Cap Patch
adds exact planar caps for closed loops and open arches, optional UV mapping,
material previews and atomic insertion into the source entity. Non-planar covers
and propagation across a network of seams remain open; see
[Patch Stitching](LEVEL_EDITOR.md#patch-stitching).

Merge Brushes now prepares an exact convex union, reviews individual material,
UV and flag conflicts, and commits through shared undo/persistence. The GUI and
`map merge-brushes` share bounded, cancellable geometry checks; the preview
uses package/staging assets and the Models renderer. Gaps and hidden cavities
are refused. See [Brush Merging](LEVEL_EDITOR.md#brush-merging).

Add Brush now creates box, wedge, cylinder, cone and sphere solids through the
shared convex hull, map insertion and CLI services. Its background preview uses
package/staging material images and the Models renderer. Current face dialect,
undo, source preservation and geometry validation are shared with normal map
editing. See [Brush Primitives](LEVEL_EDITOR.md#brush-primitives) and the
shared [Transform Texture Controls](LEVEL_EDITOR.md#transform-texture-controls).

Brush Surface Alignment now joins package material resolution, textured
preview, batch Shift/Scale/Rotate/Fit/Align, primitive matrix editing, map undo
and CLI save-as. The shared service refuses unknown required image dimensions
and stale drafts. Camera painting and sampling now share these transactions;
broader native acceptance remains open. See [Surface Alignment](LEVEL_EDITOR.md#surface-alignment).

Surface copy/paste now includes seamless brush wrapping through the shared
worker, package dimensions, map-wide conversion consent, undo and CLI writer.
NetRadiant Shift+middle pastes parameters; NetRadiant Custom Ctrl+middle wraps
one hit face and advances the clipboard. Custom Shift+middle pastes values onto
the hit and selection, including patch materials; Alt+Shift and Alt+Ctrl retain
materials/flags while preserving texel density. Valve axes remain native to the
target, and dependency references follow paste/undo. The profiles expose 84
preferences. Native Project now covers hit/selected brush and patch UVs with an
Alt mapping-only variant and edge-on results. Held Values/Project/Wrap strokes
now stage ordered hits with live previews, first-hit selection, advancing wrap
sources and one undo on release; Escape cancels queued work too. CLI `--stroke`
uses the same transaction. Patch-source copying/wrapping, broader
production-map measurements and native interaction acceptance remain open.
See [Surface clipboard](LEVEL_EDITOR.md#surface-clipboard).

The level camera now resolves material images and static model skins from the
open package and staged edits, with shared UV projection, background work,
cancellation and `map materials` diagnostics. Doom camera editing now includes
floors/ceilings, complex sector outlines, composite wall textures, offsets and
pegging, with exact package input evidence in dependency review. Full shader
effects, Doom engine lighting/sky/animation, dependency subset closure, automatic
project/game package merging and large-scene performance remain open. See the
[Level Editor acceptance matrix](LEVEL_EDITOR.md#acceptance-areas).

Code now supports untitled documents, reviewed Save As, format-preserving saves,
background text recovery and matching CLI operations. [Code Editor](CODE_EDITOR.md)
documents the lifecycle checks and remaining language/encoding integration gaps.

The level editor now includes map lifecycle/recovery and Quake III patch
authoring through shared GUI/CLI services. The broader professional-editor
target remains open; [Level Editor](LEVEL_EDITOR.md) tracks evidence and gaps
for component editing, materials, organization, navigation, scale and portability.

VibeStudio should become the definitive open-source development studio for
idTech1, idTech2, and idTech3 games: one seamless, modern, cross-platform,
user-friendly, adaptable, and efficient environment where a creator can manage
game installations, edit assets and maps, run compilers, package content, test
in-game, and maintain a complete mod or standalone project without constantly
switching tools.

In practical terms, the long-term goal is:

- [ ] A new user can point VibeStudio at their games and projects, then start working within minutes.
- [ ] A mapper can build, inspect, fix, package, and launch a playable map from one workspace.
- [ ] An artist can create or edit textures, sprites, models, audio, and shaders with game-aware previews.
- [ ] A programmer or scripter can edit code/scripts/configs with project-aware search and diagnostics.
- [ ] A release maintainer can validate packages, preserve credits, generate manifests, and ship reproducible builds.
- [ ] Users can choose familiar level-editor profiles inspired by GtkRadiant 1.6.0, NetRadiant Custom, TrenchBroom, and QuArK.
- [ ] Users always know what VibeStudio is doing, what is queued, what succeeded, what failed, and where output went.
- [ ] Users can start simple and delve into deeper metadata, logs, graphs, manifests, and raw format details when needed.
- [ ] Users can tailor language, accessibility, theme, editor profile, game installs, projects, compilers, AI, CLI, and automation through a complete modern setup flow.
- [ ] Users can work comfortably with high-visibility themes, UI/text scaling, keyboard navigation, assistive tools, OS-backed TTS, and localized UI.
- [ ] Advanced users can adapt the studio through profiles, toolchain settings, plugins, AI-assisted automation, and external compiler wrappers.
- [ ] Power users and CI systems can drive the same project workflows through a full-featured CLI.

## North-Star Metrics

These metrics define whether VibeStudio is moving toward the goal. When a task
changes behavior, update or add a metric-backed test where practical.

### Seamless Workflow
- [ ] Time from first launch to a usable project workspace: target under 5 minutes for a common Steam/GOG/manual install.
- [ ] Open project/package/install context is shared across package, preview, compiler, and launch surfaces.
- [ ] A compile failure links to the relevant map/script/package context when the compiler provides enough information.
- [ ] Common workflows avoid duplicate file picking after a project is configured.

### Modern UI
- [x] Primary shell supports dense studio workflows without modal-first navigation.
- [x] Core actions have consistent icons, labels, tooltips, shortcuts, disabled states, and status feedback.
- [ ] Text does not clip or overlap at 100%, 125%, 150%, and 200% scale on Windows, macOS, and Linux.
- [x] Dark theme is readable for long sessions, with accessible contrast for primary text and controls:
  neutral charcoal panels and an orange accent, with `studio-theme-smoke` holding text, selection, and
  accent-button contrast to WCAG AA (4.5:1) and focus rings to 3:1.
- [ ] Every main workflow has summary-first UI with detail-on-demand panels.
- [x] Graphical status elements communicate real project/package/compiler/asset state rather than decoration.
- [x] Streamlined chrome (October 2026): one studio bar holding the menus, history, a command search
  centred on the window, and the build and launch commands; one-line page headers with a single
  emphasised action; a marked current page in the navigation rail; quiet status items; Workspace tiles
  that state what each surface holds; a floating command palette with key caps; and page fades that
  honour reduced motion. Checked by snapshot in the dark, light, and high-visibility themes, at 200%
  text, and right to left.

### Cross-Platform Quality
- [ ] CI builds and tests pass on Windows, macOS, and Linux.
- [ ] Installer/portable artifacts are produced for all three platforms before public MVP.
  Since October 2026 the release workflow (`.github/workflows/release.yml`, [Releasing](RELEASING.md)) builds a
  Windows installer and portable ZIP, a macOS disk image and a Linux AppImage with the HTML manual inside; this
  stays open until a hosted run has published them and they have been tried on clean machines. Builds are not
  code-signed or notarised yet.
- [x] A brand system (October 2026): logo, wordmark, social preview, platform icon sets and installer art,
  generated from one script and compiled into the app and its packages ([Branding](BRANDING.md)).
- [ ] Project, package, and compiler paths work with spaces, Unicode, long paths where supported, and platform path separators.
- [ ] Platform-specific integration is optional and guarded: file associations, launchers, crash handling, updater, and shell-open.

### User Friendliness
- [ ] First-run flow supports auto-detect, manual setup, and skip/later paths.
- [ ] First-run flow exposes language, scale, high-visibility, reduced motion, TTS, role, editor profile, installs, projects, compilers, AI, and CLI settings.
- [ ] Dangerous actions use staging, previews, undoable state, backups, or explicit confirmation.
- [ ] Error messages explain what failed, where, and the next practical action.
- [ ] Built-in help can be generated from user-facing docs before public MVP.
- [ ] Any operation over roughly 250 ms gives visible feedback if it blocks an active pane.
- [ ] Any background task over roughly 1 second appears in an activity surface with status.
- [ ] Completed write/export/compiler operations report exact output paths.
- [ ] Cancelable operations expose cancellation and report cleanup state.

### Accessibility And Localization
- [ ] UI follows OS font/scaling defaults and supports 100%, 125%, 150%, 175%, and 200% app scale checks.
- [x] High-contrast dark and high-contrast light themes are selectable in preferences and repaint the
  shell, charts, map viewport, asset views, and code highlighting.
- [ ] Color-blind-aware status palette and non-color-only state indicators are used across project, package, compiler, AI, and validation surfaces.
- [ ] Core shell, setup, package tree, activity center, compiler log, preferences, and editor profile controls support keyboard-only navigation.
- [ ] Custom widgets expose accessible names, roles, descriptions, focus, values, and state changes.
- [x] OS-backed TTS can read selected summaries, diagnostics, setup guidance, and task completion/failure events.
- [ ] Reduced motion setting affects transitions, loading visuals, and timeline effects.
- [x] Translation pipeline supports pseudo-localization, right-to-left checks, pluralization, locale formatting, and stale-string reporting.
- [x] Localization target set covers the 47 languages and regional standards documented in `docs/ACCESSIBILITY_LOCALIZATION.md`, with the system language as default and region formats chosen separately.

### User-Visible Progress And Detail
- [ ] Global activity center shows queued, running, completed, warning, failed, and cancelled tasks.
- [ ] Package open, extraction, indexing, preview generation, compiler runs, validation, AI requests, and saves have explicit state transitions.
- [ ] Loading panes use skeletons/placeholders with context-specific labels.
- [ ] Task logs are available from both GUI and CLI-backed workflows.
- [ ] Users can copy or export details for support, bug reports, and reproducibility.

### Adaptability
- [ ] Game profiles are data-driven and can be edited without code changes.
- [ ] Compiler profiles are data-driven and can invoke bundled, system, or project-local tools.
- [ ] Level-editor layout/control profiles can switch without changing map data or forking editor logic.
- [ ] GtkRadiant 1.6.0, NetRadiant Custom, TrenchBroom, and QuArK-style profiles meet full layout, camera, selection, grid, and shortcut expectations. All four now have shared viewport controls, including QuArK's four-view layout and camera drive. Full upstream tool/mode parity and native acceptance remain open; the catalog also includes standalone NetRadiant and Sledge with explicit adaptations.
- [ ] Package and format support is modular, with fixture-backed tests for each reader/writer.
- [ ] External tools/plugins can declare inputs, outputs, capabilities, and trust boundaries.

### AI-Assisted Workflows
- [x] AI connector integration is optional, clearly configured, and disabled by default.
- [x] OpenAI, Claude, Gemini, ElevenLabs, Meshy, local/offline, and custom connector paths can be represented by one provider-neutral capability model.
- [x] Users can select preferred providers per capability: reasoning, coding, vision, image, audio, voice, 3D, embeddings, and local/offline.
- [ ] Prompt-based workflows produce reviewable plans, staged changes, command manifests, or diffs before writing.
- [ ] Agentic workflows expose plan, context, tool calls, staged changes, validation, cancellation, and final summary.
- [x] Levels can be generated from a description, planned by deterministic rules or by a text model's schema-checked plan, and opened as an editable map (Quake, Quake II, Quake III, Doom).
- [x] Game-ready textures can be generated by an image model (OpenAI, Gemini, or a local Stable Diffusion web UI) or from a picture, with seam blending, palette conversion, and source-port companion maps.
- [x] The open map can be edited from an instruction: a text model proposes schema-checked actions that are validated against the map, reviewed, and applied as undoable editor edits (`map ai-edit` and Edit with AI).
- [x] Game-ready sound effects can be made from a description by a deterministic synthesizer (no AI) or a sound model (ElevenLabs or a custom endpoint), delivered as Doom DMX lumps or Quake-family WAVs with seamless loops, and placed in Quake II/III maps.
- [ ] AI actions call explicit VibeStudio tools for package scans, compiler runs, text edits, and project changes.
- [ ] AI activity logs redact secrets and show what project context was used.
- [ ] AI-free mode is complete for core editing, packaging, compiling, validation, launch/testing, and CLI automation.
- [ ] At least one workflow explains compiler logs and proposes a reproducible next command.

### Full-Featured CLI
- [ ] GUI and CLI commands share the same service layer for projects, packages, compilers, validation, and automation.
- [x] CLI supports human-readable and JSON output.
- [ ] CLI commands support dry-run or staged behavior for destructive operations.
- [x] CLI help is generated or validated against documented command coverage.
- [x] CLI is capable enough for CI package validation and compiler smoke tests.

### Efficiency And Robustness
- [ ] Startup target for MVP: under 2 seconds to shell on a warm desktop machine without indexing.
- [ ] UI remains responsive during archive loading, previews, indexing, and compiler runs.
- [ ] Common post-setup development loop target: changed asset/map/script to compiler run in under 30 seconds of user interaction for common workflows.
- [ ] Common compiler failure loop target: failure to actionable summary in under 10 seconds after process exit where logs are available.
- [ ] Avoid repeated file picking after project setup; project, install, package, compiler, and output paths should be remembered and reusable.
- [ ] Batch operations and presets exist for repeated package, conversion, compiler, and validation workflows.
- [x] Large packages are loaded incrementally; avoid full extraction unless a workflow requires it.
- [x] Parser and package code is fuzzable or fixture-tested before supporting write-back.
- [x] Compiler runs capture command, environment, duration, exit code, output files, and hashes.

## MVP Definition

The fastest valuable MVP is not the full editor suite. The MVP is an integrated
idTech project workbench that proves the end-to-end loop:

1. Detect or configure a game installation.
2. Tailor language, accessibility, theme, editor profile, AI mode, CLI, and core preferences.
3. Open a project folder or package.
4. Browse package/project contents.
5. Preview common assets.
6. Edit project text assets safely.
7. Run at least one compiler/toolchain profile.
8. Capture diagnostics and produced files.
9. Package or stage the result.
10. Launch or reveal the output for in-game testing.
11. Run the same core workflow from the CLI for automation.
12. See loading/progress/results for every long-running step.
13. Open detailed logs/manifests/metadata when the summary is not enough.

MVP exit criteria:

- [ ] Runs on Windows, macOS, and Linux from CI-built artifacts.
- [x] Supports at least one complete idTech2/Quake-family compile loop (proved with stock ericw-tools, before the move to VibeMap2).
- [x] Supports at least one Doom-family node-building loop using ZDBSP or ZokumBSP.
- [x] Supports at least one idTech3 diagnostic or compile loop (proved with NetRadiant Custom q3map2, before the move to VibeMap3).
- [x] Proves the Quake-family and Quake III compile loops again with VibeStudio's own VibeMap2 and VibeMap3 (Windows, 2026-10-08).
- [ ] Reruns the artifacts, deployment and MD3 appearance compiler proofs with VibeMap2 and VibeMap3, and on macOS and Linux.
- [x] Supports package browsing for folders, PAK, WAD, and PK3/ZIP-family archives.
- [x] Supports previews for core text, image/palette, audio metadata and waveform, and model/map metadata.
- [x] Supports audio playback through Qt Multimedia when the build links it; without it, audio previews stop at metadata and waveform.
- [x] Provides project-level logs, diagnostics, and reproducible command manifests.
- [x] Provides global activity/task feedback with output paths and expandable logs.
- [x] Provides progressive disclosure for package metadata, compiler logs, project validation, and asset details.
- [x] Provides at least one useful graphical project/package/compiler summary.
- [x] Provides CLI coverage for project info, package inspection, validation, compiler runs, and command manifests.
- [x] Provides at least a documented first pass of editor profile architecture, even if full profile fidelity lands after MVP.
- [ ] Provides first-run setup with language, accessibility, high-visibility, scaling, editor profile, install/project/compiler, AI-free, and CLI choices.
- [ ] Provides high-visibility themes, app scaling settings, keyboard-accessible MVP flows, and OS-backed TTS
  architecture. Themes and the text-scale setting are real; TTS is still only a stored preference with no
  speech engine behind it.
- [x] Provides localization pipeline proof with pseudo-localization, right-to-left smoke checks, and initial translation catalog structure.
- [ ] Provides opt-in AI documentation and a safe architecture path; AI implementation may remain experimental after MVP.
- [x] Preserves credits and third-party license visibility in README, docs, About, and release bundles.

## Gradual Improvement Rules

- [ ] Prefer vertical slices over isolated subsystems.
- [ ] Each milestone should leave the app more usable than before.
- [ ] Build read-only support before write support.
- [ ] Build CLI and tests alongside GUI behavior.
- [ ] Port PakFu functionality in small credited modules with tests.
- [ ] Integrate external compilers by process execution first; source-level integration comes only after license and maintenance review.
- [x] Implement editor profile behavior as configuration over shared commands before adding profile-specific code: `core/level_editor_controls` holds each profile's layout, 2D and 3D controls, grid, and keys as data read by the shared views.
- [ ] Treat AI-assisted workflows as proposals over deterministic tools, not as hidden direct mutation.
- [ ] Treat agentic AI as supervised workflow acceleration: plan, review, stage, validate, summarize.
- [ ] Preserve an AI-free/manual path for every core workflow.
- [ ] Prefer reusable presets, manifests, templates, and batch actions when users repeat a workflow.
- [ ] Add CLI coverage alongside every workflow that can reasonably run headless.
- [ ] Treat user-visible state as part of the feature, not polish.
- [ ] Treat accessibility and localization as part of every UI feature.
- [ ] Make setup choices editable later; first-run setup should never be the only place to configure a core behavior.
- [ ] Build summary-first/detail-on-demand UI before adding advanced-only panels.
- [ ] Prefer graphical communication when it clarifies real project state, relationships, or workflow progress.
- [ ] Mark rough edges in docs and issues rather than blocking useful slices.
- [ ] Measure performance before large refactors.

## Milestone 0: Foundation Hardening

Goal: make the repository easy to build, test, credit, and extend.

### Build And CI
- [x] Create Meson/Qt6 C++20 scaffold.
- [x] Add minimal Qt Widgets shell.
- [x] Add CLI diagnostics.
- [x] Add cross-platform CI workflow.
- [x] Add compiler-submodule verification workflow.
- [x] Add release/nightly workflow skeleton adapted from PakFu.
- [x] Add `scripts/validate_docs.py` for link, credits, localization catalog, and submodule checks.
- [x] Add `scripts/validate_source_layout.py` for required directories and generated-file exclusions.
- [x] Add CI artifact retention for app binaries from PR builds.

### Documentation
- [x] Add AGENTS rules.
- [x] Add README with PakFu lineage and compiler imports.
- [x] Add credits document.
- [x] Add architecture, dependencies, support matrix, compiler integration, and installation docs.
- [x] Replace broad roadmap with metric-driven roadmap.
- [x] Add editor profile, AI automation, and CLI strategy docs.
- [x] Add stack decision record.
- [x] Add efficiency philosophy.
- [x] Add accessibility/localization philosophy.
- [x] Add initial setup flow philosophy.
- [x] Add contribution guide for task sizing, attribution, and fixture expectations.
- [x] Add issue templates for feature, bug, format support, compiler integration, and attribution updates.

### Product Shell
- [x] Add mode rail and placeholder studio shell.
- [x] Add persistent settings storage.
- [x] Add recent projects list.
- [x] Add global status/log panel.
- [x] Add global activity center with task list, progress, result, warnings, failures, and cancellation.
- [x] Add reusable operation-state model for idle/loading/running/warning/failed/cancelled/completed states.
- [x] Add reusable loading/skeleton components for panes and previews.
- [x] Add reusable detail drawer pattern for logs, metadata, manifests, and raw diagnostics.
- [x] Add status chips for project, package, compiler, install, and AI states.
- [x] Add command palette: a type-to-filter launcher over the command registry.
- [x] Add Go to File (Ctrl+P): recent files, project files, and open-package entries, opened on the surface
  that shows each.
- [x] Add Go to Symbol (Ctrl+T in Code): the open file's QuakeC and C functions, shaders, and entity classes.
- [x] Add Help > Keyboard Shortcuts: every command's live keys and the surface they work on, filterable.
- [x] List the commands last run from the palette first when its filter is empty.
- [x] Add a Levels viewport context menu: framing, Edit Key, Move, Copy Selector, and leak-trail actions on the
  object under the pointer.
- [x] Add keyboard shortcut registry with conflict detection reported by the shell self-test.
- [x] Add interaction profile registry placeholder.
- [x] Add accessibility settings for theme, text scale, density, and reduced motion. The TTS preference is
  stored and reported, but no speech engine is wired up.
- [x] Add language/locale settings placeholder.
- [x] Add first-run setup shell with skip/resume behavior.
- [x] Add AI integration disabled/experimental settings placeholder.

### Visual Communication Foundation
- [x] Add graph/diagram widget decision record: Qt Graphics View, custom widgets, or future scene graph
  ([`docs/STACK.md`](STACK.md)).
- [x] Add renderer abstraction decision record covering QPainter, QOpenGLWidget MVP previews, and bgfx
  production viewport goals ([`docs/STACK.md`](STACK.md)).
- [x] Draw every 3D view and material preview on the GPU with user-selectable OpenGL and Vulkan backends
  (2026-10-08): `core/render_device` frames, OpenGL 3.3 core / ES 3.0 and Vulkan 1.0 devices on their own
  threads, GLSL compiled offline to SPIR-V, **Settings** > **Appearance and Language** > **3D Rendering**,
  the `render backends|test|set` commands and `--renderer`; the CPU rasterisers are removed
  ([`docs/STACK.md`](STACK.md)).
- [ ] Present 3D views straight to a window surface instead of reading frames back, and measure the
  difference on large maps.
- [ ] Try the renderers on macOS (OpenGL, and Vulkan through MoltenVK) and on AMD and Linux desktop drivers.
- [x] Add project health summary.
- [x] Add package composition chart: stacked proportions by entry type and by size, with a legend.
- [x] Add compiler pipeline chart: source, stages, and artifacts with per-stage state glyphs.
- [x] Add task timeline chart driven by real activity durations.
- [x] Define icon/color semantics for success, warning, failure, running, paused, cancelled, local, cloud, staged, and read-only.

Exit criteria:
- [ ] New checkout builds with one documented command per platform.
- [x] CI proves build/test/docs/submodule/sample checks.
- [ ] Contributors can identify current MVP tasks from this roadmap.

## Milestone 1: PakFu Core Migration MVP

Goal: make VibeStudio useful as a safe package/project browser before deeper
editors arrive.

### Core Package Abstractions
- [x] Bound individual archive/folder index admission (records including skipped
  entries and implied folders, path depth, logical metadata and aggregate chunk
  fingerprints); stream ZIP central records and cancel directory preparation.
- [x] Share aggregate index admission across combined mount sessions and
  multi-folder texture audits; preserve prior sessions after failed/cancelled
  mounts and report incomplete source audits even without texture references.
- [x] Admit retained generated bytes and payload fingerprints across staged
  bases, edits and undo/redo, including grouped cancellation and draft objects.
- [x] Admit retained document records and index/text metadata across base,
  operations and undo/redo; preserve rejected edits and check draft metadata
  before payload reads.
- [x] Admit general reader snapshots and staged browser projections, including
  implied folders and diagnostics; preserve failed adoption, freeze known backing
  and expose view refusal with recoverable Undo/Redo through GUI/CLI.
- [x] Admit persisted operation/revision serials before edits; retain Undo/Redo,
  save/export and exact draft replay at the last usable counter value.
- [x] Admit archive output against the opening record/depth/metadata/fingerprint
  policy in writes and no-write dry runs; account for ZIP64 growth and canonical
  folder identities while preserving failed-save output and document history.
- [x] Admit plan row/conflict text, slot counts, parent-index growth and folder
  identity/rewrite metadata before growth; preserve refusal recovery and WAD order.
- [x] Admit individual edits and complete operation groups against the browser
  projection before history commit; preserve redo on refusal and keep bulk
  import validation on the existing worker.
- [ ] Complete helper/base allocation and cancellation audits, reduce repeated
  small-commit preparation cost, move remaining synchronous view work off the
  GUI thread, and complete aggregate-copy and native-platform acceptance.
- [x] Port or adapt PakFu archive interfaces with attribution.
- [x] Add path safety and normalized virtual paths.
- [x] Add folder package session.
- [x] Add PAK reader.
- [x] Add WAD reader for Doom IWAD/PWAD lumps and Quake/Half-Life WAD2/WAD3 texture lumps.
- [x] Add ZIP/PK3 reader covering stored and deflated entries, ZIP64, and a dependency-free inflate.
- [x] Add package entry metadata model: path, size, modified time, type hints, source package.
- [x] Add nested package detection as metadata, plus layered mounting of nested archives.

### Read-Only GUI
- [x] Add package/project tree view.
- [x] Add entry list/details view.
- [x] Add search/filter for paths and type hints.
- [x] Add package loading state with progress when entry count is known.
- [x] Add package scan task card in activity center.
- [x] Add preview pane for text.
- [x] List WAD graphics on the Textures page: a Doom WAD's flats, sprites, and patches by their namespace
  markers and its well-known global graphics, and every WAD2/WAD3 lump; an Automatic palette uses the one
  the package ships.
- [x] Add image preview pane with decoded idTech art: Doom patches/flats, Quake `.lmp`, WAD2/WAD3 miptex,
  Quake II `.wal`, `.m8`, and `.m32`, PCX, Targa, Quake and Half-Life `.spr`, and Quake II `.sp2`, with
  palette, mip level, and frame selection. A `.sp2` carries no pixels of its own, so its frames are resolved
  against the open package.
- [x] Add metadata preview for unknown/binary entries.
- [x] Add summary/detail split for entries: friendly overview first, raw metadata on demand.
- [x] Add package composition graphic by type and size.
- [x] Add extract selected/all workflow.
- [x] Add extraction progress, cancellation, completion summary, and exact output paths.

### CLI
- [x] Add `--list`.
- [x] Add `--info`.
- [x] Add `--extract`.
- [x] Add `--validate-package`.
- [x] Add JSON output mode for automation.
- [x] Add stable exit code definitions.
- [x] Add dry-run/staged write conventions before write support lands.

### Tests
- [x] Add tiny fixture PAK.
- [x] Add tiny fixture WAD.
- [x] Add tiny fixture PK3.
- [x] Add path traversal and duplicate-path tests.
- [x] Add CLI validation for package commands.
- [x] Add package loading/progress state tests.
- [x] Add extract output-path reporting tests.

Exit criteria:
- [x] User can open folder/PAK/WAD/PK3, inspect entries, preview simple content, and extract safely.
- [x] No write-back support exists until fixture tests and staging model are ready.

## Milestone 2: Project And Installation Workbench

Goal: make VibeStudio understand games and projects, not just individual files.

### Project Model
- [x] Define `.vibestudio/project.json` schema.
- [x] Add project create/open/save.
- [x] Add project root, source folders, package folders, output folders, and temp folders.
- [x] Add project-local settings override layer.
- [x] Add migration/version field for project schema.
- [x] Add project validation command.

### Game Installations
- [x] Port/adapt PakFu game profile model with attribution.
- [x] Add manual installation profile creation.
- [x] Add Steam detection.
- [x] Add GOG detection.
- [x] Add profile validation against expected base packages/executables.
- [x] Add per-profile palette and engine-family defaults.
- [x] Add first-run installation flow with skip/later path.
- [x] Index each installation's stock packages (files, Quake III shader declarations, Doom-family
  names) into a read-only game asset index with freshness checks, from the Workspace page, Project
  Health and `install register`. See [Project releases](PROJECT_RELEASES.md#the-game-asset-index).

### Workspace UX
- [x] Add workspace dashboard for active project, install, packages, recent files, and tasks.
- [x] Add project problems panel.
- [x] Add global search across mounted packages and project files.
- [x] Add changed/staged files panel.
- [x] Add reveal-in-folder and copy-virtual-path actions.
- [x] Add project health summary with install/package/compiler status.
- [x] Add project dependency graph placeholder.
- [x] Add recent activity timeline.
- [x] Add empty states for no project, no install, no packages, no compiler, and no recent tasks.
- [x] Add detail drawers for project manifest, install validation, and mounted package roots.

Exit criteria:
- [x] User can configure one game install, open one project, mount packages, and see project/package context, health, loading state, and next actions everywhere.

## Milestone 3: Compiler Orchestration MVP

Goal: prove the end-to-end build/test loop without waiting for full native
editors.

### Compiler Discovery
- [x] Add compiler registry model.
- [x] Add bundled submodule source metadata.
- [x] Add user-configured executable paths (Build > Toolchain **Locate…** and `compiler set-path`).
- [x] Add project-local compiler overrides.
- [x] Add compiler version probing.
- [x] Add capability flags for Doom node builders, VibeMap2, and VibeMap3 (first written for ericw-tools and q3map2).

### Command Manifests
- [x] Define compiler run manifest schema.
- [x] Record command, working directory, environment subset, inputs, outputs, duration, exit code, and hashes.
- [x] Add manifest save/load.
- [x] Add re-run previous command.
- [x] Add copy command line action.

### Toolchain Slices
- [x] Add VibeMap2 profile: `vibemap2-bsp` (first written for ericw-tools `qbsp`).
- [x] Add VibeMap2 profile: `vibemap2-vis` (first written for ericw-tools `vis`).
- [x] Add VibeMap2 profile: `vibemap2-light` (first written for ericw-tools `light`).
- [x] Add ZDBSP profile for a selected WAD/map.
- [x] Add ZokumBSP profile for a selected WAD/map.
- [x] Add VibeMap3 info/help/probe profile (first written for q3map2).
- [x] Add VibeMap3 compile profile for a simple `.map` (first written for q3map2).
- [x] Move the Quake-family toolchain to VibeStudio's own VibeMap2 and VibeMap3, renaming the `ericw-*` and `q3map2*` ids and refusing the old ones with the new id named.
- [ ] Rerun the Quake-family and Quake III compiler proofs with VibeMap2 and VibeMap3.
- [ ] Re-audit the known-issue catalogue against VibeMap2's upstream audit and retire the entries it resolves.
- [ ] Ship VibeMap2 and VibeMap3 programs with VibeStudio releases.

### Diagnostics
- [x] Capture stdout/stderr in task log.
- [x] Parse warnings/errors opportunistically.
- [x] Link diagnostics to files when paths are present.
- [x] Open a compiler problem at its source: a line inside a brush, patch, or entity of the open map selects
  that object in Levels, and a line in another text file opens the Code editor there.
- [x] Add task cancellation.
- [x] Add output file registration in project tree.
- [x] Show compiler run as activity-center task with stage, duration, result, and output paths.
- [x] Add compiler pipeline graphic: source map -> compile stages -> output artifacts.
- [x] Add summary-first compiler result with expandable raw stdout/stderr.
- [x] Add "copy CLI equivalent" and "copy manifest" actions.

Exit criteria:
- [x] User can run at least one compile/build command for each engine family from VibeStudio, watch progress, inspect summaries/logs, locate outputs, and keep a reproducible record.

## Milestone 3A: CLI Parity Backbone

Goal: make the CLI elegant, scriptable, and backed by the same services as the
GUI before workflows sprawl.

### Command Architecture
- [x] Define command router and subcommand hierarchy.
- [x] Evaluate CLI11 and adopt a testable in-process command registration layer now; keep full CLI11 parser/completion adoption deferred until the broader command surface justifies the dependency.
- [x] Define shared output writer for text and JSON.
- [x] Define shared error and exit-code contract.
- [x] Define command manifest writer.
- [x] Add service-layer tests that run through CLI commands.

### MVP Command Families
- [x] `project info`
- [x] `project validate`
- [x] `install list`
- [x] `package info`
- [x] `package list`
- [x] `package validate`
- [x] `compiler list`
- [x] `compiler run`
- [x] `compiler manifest`
- [x] `credits validate`

### UX And Automation
- [x] Add `--json`.
- [x] Add `--quiet`.
- [x] Add `--verbose`.
- [x] Add `--dry-run`.
- [x] Add `--manifest <path>`.
- [x] Add `--watch` or equivalent streaming output mode for long-running tasks.
- [x] Add machine-readable task state output for automation.
- [x] Add examples for PowerShell and POSIX shells.

Exit criteria:
- [x] MVP package/project/compiler workflows can be demonstrated without opening the GUI.

## Milestone 3B: AI Automation Experiments

Goal: embrace generative and agentic workflows safely and early without making
MVP depend on cloud AI.

### Provider And Configuration
- [x] Add opt-in AI settings model.
- [x] Add provider-neutral AI connector abstraction.
- [x] Add connector capability model for reasoning, code, vision, image, audio, voice, 3D, embeddings, tool calls, streaming, local/offline execution, cost/usage, and privacy notes.
- [x] Implement OpenAI as the first general-purpose connector.
- [x] Add design stubs for Claude, Gemini, ElevenLabs, Meshy, local/offline, and custom HTTP/MCP-style connectors.
- [x] Add provider routing preferences by capability.
- [x] Read API credentials from user settings or environment without logging secrets.
- [x] Keep model selection configurable rather than hard-coded.
- [x] Add global AI-free mode.
- [x] Add project-level disablement.

### Safe Tooling Surface
- [x] Define AI-callable tools for project summary.
- [x] Define AI-callable tools for package metadata search.
- [x] Define AI-callable tools for compiler profile listing.
- [x] Define AI-callable tools for proposing compiler commands.
- [x] Define AI-callable tools for staged text edits.
- [x] Define AI-callable tools for asset generation requests that always stage generated outputs before import.
- [x] Define AI workflow manifests: provider, model, prompt, context, tool calls, staged outputs, approval state, validation, cost/usage where available.
- [x] Add consent and preview UI before applying actions.
- [x] Add cancellation and retry state for long-running agentic workflows.

### First Experiments
- [x] Explain selected compiler log.
- [x] Propose next compiler command from a natural-language prompt.
- [x] Generate a project manifest draft.
- [x] Suggest missing package dependencies.
- [x] Generate a CLI command for a requested workflow.
- [x] Generate a supervised fix-and-retry plan for a compiler failure.
- [x] Generate placeholder sound/voice content through ElevenLabs when configured.
- [x] Generate placeholder model or texture concepts through Meshy when configured.
- [x] Compare output from two configured reasoning providers for the same prompt.

Exit criteria:
- [x] AI can explain a compiler log and propose a reviewable command without writing files.
- [x] AI can be globally disabled and core workflows remain usable.
- [x] Connector configuration can represent at least OpenAI plus one non-OpenAI provider without changing the workflow model.

## Milestone 4: MVP Release

Goal: ship the smallest public version that proves the complete loop.

### Packaging And Distribution
- [x] Add Windows portable package.
- [x] Add macOS portable package.
- [x] Add Linux portable package.
- [x] Add license bundle for VibeStudio and compiler/toolchain sources.
- [x] Add generated offline user guide.
- [x] Add release asset validation.

### MVP UX Completion
- [x] Add About/Credits/license surface with credits and license links.
- [x] Add Preferences for paths, theme, compilers, and installations. Compiler executables are located or
  reset on the Build page's Toolchain tab.
- [x] Add Preferences for language, scale, density, high-visibility themes, reduced motion, and TTS.
- [x] Add project recent list and reopen-last-project option.
- [x] Add first-run setup checklist and guided flow.
- [x] Add setup summary with warnings, skipped steps, detected installs, toolchain probes, AI mode, and CLI details.
- [x] Add failure-friendly empty states.
- [x] Add loading/progress coverage audit for MVP workflows.
- [x] Add summary/detail coverage audit for MVP workflows.
- [x] Add graphical project/package/compiler summary views.
- [x] Add task history persistence for recent compiler/package operations.
- [ ] Add basic keyboard navigation audit. Partly done: every command carries a registry shortcut, conflicts
  are detected, page keys are scoped to their page, controls set accessible names and focus policies, and
  `shell-interaction-smoke` presses the page keys against the real window in CI, but no manual end-to-end
  keyboard-path audit has been run.
- [x] Add high-visibility theme audit: both high-contrast themes are applied across the shell, and
  `studio-theme-smoke` measures their text, selection, accent, state, and focus contrast on every run.
- [x] Add localization/pseudo-localization audit via the `localization report` command: pseudo-localization,
  right-to-left locales, expansion ratio, layout checks, and stale-catalog reporting.
- [ ] Add OS-backed TTS smoke path. Not started: no speech engine is linked; only the preference is stored.

### MVP Validation
- [ ] Smoke-test Windows clean machine launch. Not started: clean-machine steps are documented in the portable
  package notes but never executed.
- [ ] Smoke-test macOS clean machine launch.
- [ ] Smoke-test Linux clean machine launch.
- [x] Run the offscreen `--self-test` GUI smoke check on Windows, macOS, and Linux in PR CI: the shell is
  built, every work surface is visited and repainted, and shortcut conflicts are reported.
- [x] Drive the real window in CI: `shell-interaction-smoke` builds the shell from the `vibestudio_app`
  library and checks page-scoped keys, find and filters, navigation between surfaces, unsaved-edit guards,
  viewer controls, and the map, build, problem, object loop against a stand-in compiler.
- [x] Smoke-test opening fixture PAK/WAD/PK3.
- [x] Smoke-test each compiler family with tiny sample project.
- [x] Measure startup time.
- [x] Measure package open time on small, medium, and large archives.
- [x] Verify visible feedback during package open, extraction, validation, compiler run, and AI request.
- [ ] Verify first-run setup can be completed with keyboard-only navigation.
- [x] Verify first-run setup offers high-visibility, scaling, language, TTS, AI-free, and skip/later paths.
- [x] Verify MVP shell at 100%, 125%, 150%, 175%, and 200% scale.
  - All five scales are selectable and persist, with clamping tests.
  - `--ui-snapshot` renders every surface at a chosen scale. Point `QT_QPA_FONTDIR` at the system fonts on Windows.
  - 100% and 200% were reviewed first. Their fixes: rail labels, icon sizes, readouts, and drawer headers.
  - The 125% to 175% review fixed four things: panel tab strips that elided to a few letters (they now adapt),
    a page header that held the window wider than the screen (actions now fold), a tool bar overflow chevron
    too narrow to see, and a clipped Settings category list.
  - `shell-interaction-smoke` checks the tab and header folding.
  - Status messages were clipped at 200% ("Mode: Work"). The status bar now keeps room for about forty
    characters by folding its panel toggles, then its chips, to glyphs.
  - Menu, tab, and field icons stayed 16 pixels at 200%. They now take their size from the text scale through
    the style.
  - Combo box and tree arrows were Fusion's fixed 8-pixel arrows. They are now chevrons sized from the text scale.
- [x] Verify high-contrast dark and high-contrast light themes repaint the shell, charts, map viewport, asset
  views, and code highlighting.
- [x] Verify pseudo-localization and right-to-left smoke flows.
- [ ] Verify TTS reads a test phrase and one task result where OS support exists.
- [x] Verify every MVP write/export operation reports output path.
- [x] Verify raw details/logs/manifests are reachable from summary views.
- [x] Verify credits and license bundle skeleton.

MVP release exit criteria:
- [x] A user can install/open VibeStudio, configure a game/project, browse assets, run a compiler profile, inspect diagnostics, and locate or launch output.
- [x] The app is honest about what editing surfaces are still previews or planned.

## Milestone 5: Safe Write-Back And Packaging

Goal: make package edits practical without sacrificing trust.

### Staging Model
- [x] Add staged package changes model.
- [x] Add add/import file.
- [x] Add rename.
- [x] Add delete.
- [x] Add replace.
- [x] Add conflict resolution.
- [x] Add diff/preview for staged changes.
- [x] Add graphical staging summary by operation type and package location.
- [x] Add before/after package composition view.
- [x] Add "why cannot save" blocked-state messages.
- [x] Add save-as before overwrite. Writing back over the open source package stays blocked unless the
  in-place overwrite below is confirmed.
- [x] Add in-place package overwrite once staged save-as has proven itself.
  `PackageWriteRequest::allowInPlaceOverwrite` writes the new archive beside the destination, re-reads and
  verifies the bytes and an independent original copy, and atomically replaces the
  destination while keeping the original present until commit. A recovery journal
  records both versions; the original is copied to `backupPath` (default
  `<destination>.bak`) after successful publication. The shell's
  save-as sets the flag behind a "Replace Existing Package?" prompt that defaults
  to No. The CLI requires an explicit `--in-place` for this replacement mode.
- [x] Add `package recover <journal> [--finish]` for read-only interrupted-save
  inspection and safe completion of backup publication for installed output.
  Bounded folder discovery and a cancellable GUI chooser now share the service;
  journal review checksums and explicit external-backup selection guard finishing.
  Pre-commit replacement installation and changed-output decisions remain manual.
- [x] Add package compare between two archives or between a package and its staged result. `comparePackages`
  backs the Packages "Compare" button and the `package compare` command, which reports added, removed,
  changed, case-only, and identical entries and exits with the validation code on any difference so a
  release script can gate on a match. **Review Changes** and `package compare
  <source> --staged` expose `comparePackageToPlan`, including generated assets.
  Unreadable, oversized, and ambiguous entries remain visibly unchecked and block
  a content-match exit status. Explicit metadata-only comparisons check names and
  sizes. The GUI review is searchable, cancellable, and exports JSON.
- [x] Run package comparison and saving on workers with visible progress and
  cancellation. Successful GUI saves reopen the output and reset staging before
  another edit, including in-place saves with changed entry offsets. Failed and
  cancelled saves retain the original plan. Regression coverage includes repeated
  saves, backups, cancellation, GUI scaling/RTL, and staged CLI review.

### Package Writers

- [x] Stream verified archive payloads and manifest hashing through bounded
  buffers; add incremental DEFLATE, ZIP measurement/write verification, and
  worker byte/phase progress with cancellation within one file. Prepare in-place
  manifest content before publication. Metadata scale, older preview/drag and
  standalone compression analysis remain release work.
- [x] Add portable `.vibepackage` editing drafts with independent payloads,
  grouped undo/redo, worker save/open, dirty-state choices, session restore, and
  CLI draft save/info/undo/redo. Open drafts retain history across archive exports.
- [x] Add automatic package checkpoints, visible status, a recovery chooser and
  CLI inventory/restore/discard with digest and live-session protection. Restore
  copies complete history into a new independent draft. Retire closed documents
  safely and reclaim unreachable checkpoint objects after commit.
- [x] Bound logical recovery storage and copy count without automatic eviction;
  expose usage and reviewed incomplete-copy discard through the chooser and CLI.
- [x] Retain independent live file imports/replacements through undo and worker
  snapshots, with streamed verification, cancellation and queued lifetime cleanup.
  Keep CLI diagnostics and dry runs free of working-copy writes.
- [x] Bound working-import payload bytes/file slots with cross-process reservations
  and live-reader leases; expose limits, usage and reviewed crash-orphan discard
  plus native-exclusion lock recovery and a cleanup queue drained at shutdown
  in the GUI and CLI. Retain broad performance and filesystem acceptance below.
- [x] Add per-draft byte/file limits and reviewed unused-object reclamation with
  native document/history/worker reader exclusion, shared GUI/CLI and no-write
  preflight. Keep native platform and filesystem evidence below open.
- [ ] Complete disk-full, network and power-loss acceptance, worst-case storage
  performance and native macOS/Linux maintenance verification.
- [x] Create source-free empty PAK/ZIP/PK3/WAD documents through GUI and CLI.
  Create, rename and delete complete folders with atomic preflight and grouped
  undo; preserve explicit empty folders in ZIP/PK3 and block lossy PAK output.
  Draft dry runs verify all history inputs and metadata without writing.
- [x] Connect package browsing, exact-row previews, validation, extraction,
  selected export and draft CLI reads to the planned snapshot, with visible
  conflicts and refresh on undo/redo. Asynchronous preview/drag work and a
  complete authoring-handoff audit remain release gates.
- [x] Edit exact source occurrences with Replace/Rename/Delete, persistent draft
  identity and CLI ordinal selectors. Extract repeated names with GUI path
  review or indexed CLI mappings.
- [x] Export exact occurrence subsets from planned archives/drafts through a shared
  GUI/CLI review, preserving WAD map/GL groups, namespace markers, texture name
  tables and source order. Reject malformed groups and protect source inputs.
- [x] Add reviewed map/GL rename and complete map, namespace and local texture-table
  deletion in new and opened Doom WADs, with shared GUI/CLI validation, undo and drafts.
- [x] Assemble new WAD binary/GL runs in the planned view and use that reviewed
  order for saves, drafts and subset exports, preserving named maps, sidecars
  and texture namespace anchors. Block ambiguous map ownership.
- [ ] Rewrite map metadata/script references and finish transitive
  texture/patch/game-asset dependency closure.
- [x] Package and release a project, map, model or texture folders with only the project's own
  files: releases follow map, model and shader references, ship build companions, leave out what
  the game asset index lists, flag files that replace stock ones, keep a Keep a Changelog file,
  generate release notes and an /idgames-style readme, write a distribution archive and record each
  release for the next one's diff. One planner and publisher serve the Package and Release window
  and the `release` CLI. See [Project releases](PROJECT_RELEASES.md).
- [ ] Prove releases with real game installations and projects; follow references that game code,
  QuakeC and scripts make; index Doom 3-era and other idTech 3 games without manual package lists.

- [x] Add PAK writer from PakFu lineage.
- [x] Add ZIP/PK3 writer from PakFu lineage, choosing per entry between stored and deflated output.
- [x] Add a dynamic-Huffman deflate encoder. `deflateRaw` measures the stored, fixed-Huffman, and
  dynamic-Huffman encoding of every block and keeps the smallest, with length-limited code lengths built by
  package-merge. `DeflateLevel` gained `best` beside `store`, `fast`, and `default`, and the level now only
  controls how hard the LZ77 match search works. Nothing in the GUI or CLI selects a level yet, so ZIP/PK3
  output always uses `default`.
- [x] Add WAD writer only after map-lump tests exist, covering PWAD/IWAD and WAD2/WAD3 output.
- [x] Support multi-map WAD write-back. `PackageStagedEntry::sourceOrdinal` records each lump's slot in the
  source directory, `PackageStagingModel::entryBytes` reads WAD lumps by that ordinal instead of by name, and
  the staged plan for a WAD keeps source order, so a WAD whose maps each repeat `THINGS`, `LINEDEFS` and the
  rest round-trips with every lump under its own marker. A plan that has lost its source order, and a single
  map holding one lump name twice, are still refused with a blocked message.
- [x] Add package manifest export.
- [x] Add reproducibility checks for deterministic outputs.

Exit criteria:
- [x] User can safely create and rebuild simple PAK/PK3 packages with visible staged changes.

## Milestone 6: Asset Preview And Editing Depth

Goal: widen the workbench into a real asset studio.

### Texture And Image
- [x] Add a raster authoring canvas with pencil, eraser, connected fill,
  eyedropper, crop selection, tiling preview, transforms, and bounded undo/redo.
- [x] Share atomic PNG saves and ordered JSON recipes between GUI and CLI;
  stage generated pixels, inspect staged textures, and apply them to Quake III
  map selections through existing services. See [Texture Editor](TEXTURE_EDITOR.md).
- [x] Add editable layered texture documents, clipping selection operations,
  versioned `.vtexture` persistence, palette metadata and conflict-aware saves.
- [x] Add verified texture backups, background per-document recovery, inspect/restore
  UI and CLI, and tested Save/Discard/Cancel continuations.
- [x] Add square/round brushes, explicit alpha modes, line/rectangle/ellipse,
  tolerant fill, wrapped painting/fill, cyclic offsets and anchored zoom.
- [x] Add canvas sizing and selected-pixel resize/rotation with nine anchors,
  exact indexed/RGBA transforms, shared recipes and bounded undo/redo.
- [x] Complete [texture editor release gates](plans/texture-editor-release-candidate.md).
- [x] Add GUI/CLI native texture export profiles with regenerated previewable
  mip chains, explicit palette/alpha rules, patch offsets and WAL surface flags.
- [x] Add native WAD staging with namespace and lump-type preservation, grouped
  package undo, draft persistence, and GUI/CLI handoffs.
- [ ] Add sprite frame authoring/encoding; complete the integration and engine acceptance gates.
- [x] Port/adapt image loader workflow concepts from PakFu with attribution.
- [x] Add palette-aware preview.
- [x] Add conversion workflow.
- [x] Add texture metadata inspector.
- [x] Add image loading and conversion progress feedback.
- [x] Add before/after preview for edits/conversions.
- [x] Add basic crop/resize/palette operations.
- [x] Add batch conversion queue.

### Model
- [x] Author static primitive props with editable design JSON, bounded geometry,
  per-part materials, live preview, undo/redo, and deterministic MD3/OBJ export.
- [x] Connect model design to generated-byte package staging, undoable Quake III
  placement, staged-model level preview, material navigation, and GUI/CLI tests.
- [x] Add full X/Y/Z part rotation, per-part UV scale/offset/rotation with checker
  preview, viewport part selection, selection-aware undo, and schema-1 migration.
- [ ] Expand authoring to imported meshes, vertex-level UV editing, animation, and collision
  geometry; index general package models asynchronously for the level preview.
- [x] Establish editable mesh documents with MD2/MD3 import, explicit primitive
  baking, frame-preserving face operations, selection UV edits, basic frame
  operations, bounded undo, atomic source saves, animated MD3 export, and shared
  GUI/CLI/package handoff. [Editable Meshes](MODEL_MESH.md) records exact limits.
- [x] Add checksummed mesh recovery with background checkpoints, a cancellable
  recovery chooser, unsaved-draft restoration, and matching catalog/restore CLI.
- [x] Run mesh import, edits, saves, and exports on document workers with visible
  progress, cancellation, preserved failed candidates, and deferred close.
- [x] Add indexed edge selection, conforming all-frame splits, and bounded
  distance welding with UV/normal seam protection and shared CLI diagnostics.
- [x] Add cancellable indexed topology health reports and finding selection,
  with whole-surface duplicate/unused removal, disconnected-fan/nonmanifold-edge splitting and
  consistent winding repairs; preserve all pose attributes, history and recovery
  through the same GUI/CLI service. General repair of malformed geometry remains open.
- [x] Review damaged mesh/native imports before admission, with explicit face
  removals across every pose, selective normal rebuilding, orphaned-seam removal,
  a prepared-copy preview and exact CLI diagnostics. Save a new editable source;
  protect original/existing files and reject changed inputs. Incomplete decodes,
  invalid metadata and malformed OBJ polygons still require source correction.
- [x] Inspect geometric crossings and coplanar area overlaps within and between
  surfaces across stored poses, with bounded spatial queries, cancellation,
  deterministic face-pair reports and shared GUI/CLI semantics. Health can show
  either exact face at its affected pose for ordinary authoring. Continuous
  motion, solid containment and automatic geometric repair remain separate work.
- [x] Split branching indexed edges into connected face fans while preserving
  all existing two-face connections, every face, per-pose attributes, selected
  copies and seam marks. The Health inspector and CLI use the same cancellable
  all-pose operation with preflighted storage limits and one undo step. Open
  boundaries remain explicit; this does not close geometric repair or the full
  [modeller release gate](MODELLER_RELEASE.md).
- [x] Fill selected boundary loops and bridge exactly two disjoint boundaries,
  including unequal vertex counts and explicit twist alignment. Shared GUI/CLI
  operations retain authored attributes and check new faces in every stored
  pose before one cancellable document transaction. New faces remain selected
  for UV/normal finishing. Candidate-search and geometry/workload limits remain
  explicit; these tools do not close the broader topology release gate.
- [x] Add precise vertex picking, explicit X-ray selection, a translation gizmo,
  shared GUI/CLI delta snapping, and exact orthographic view presets. Previews
  cancel without history and commit through document validation as one undo step.
- [x] Add world-axis rotation rings, axis/uniform scale handles, shared origin,
  selection-centre/custom pivots, and numeric/CLI angle and scale snapping.
- [x] Add World, Selection and Custom transform axes across numeric edits,
  move/rotate/scale previews and CLI, including reference-pose consistency,
  face extrusion/duplication and tag/collision movement. Collision size retains
  intrinsic axes.
- [x] Add free trackball rotation inside the Rotate gizmo, retaining constrained
  axis rings, fixed view/pivot/axes, axis-preserving angular snapping and the
  shared mesh/tag/collision transaction. Current/all-frame scope, cancellation,
  undo/recovery and numeric CLI reproduction use existing authoring services.
  Persistent object transforms and broader release acceptance remain open.
- [x] Add named attachment authoring across frames, tag table/picking and local-axis
  overlays, move/rotate previews, absolute origins, pose copying, explicit orientation
  reset, selection-aware history/recovery, shared CLI and MD3 package handoff.
- [x] Add indexed animation clip authoring and range preview, full-pose copying,
  and bounded in-between generation across every surface and attachment, with
  exact original poses, atomic validation, undo/recovery and CLI parity.
  Game timing configuration remains open; linked assembly authoring is tracked below.
- [x] Add session-only smooth clip preview with elapsed-time sampling, shared
  pose interpolation, loop-boundary blending, exact-pose pause/step behavior,
  compatible picking and attachment diagnostics. Cross-platform throughput and
  original-engine animation acceptance remain release work.
- [x] Add seam marking, UV island selection, face detachment, shared pivot/grid
  transforms and an asynchronous interactive UV view. Source schema 3 retains
  marks and MD2 dimensions, with schema-1/2 read compatibility and shared CLI/recovery behavior.
- [x] Render dense UV selections through exact boundary contours and the shared
  antialiased wire renderer, retain every indexed edge and accurate picks, keep
  seams/selection visible above ordinary wires, and enforce the pixel ceiling
  for extreme aspect ratios. Scoped coverage and throughput evidence belongs to
  [the modeller release audit](MODELLER_RELEASE.md); cross-platform performance
  acceptance remains open.
- [x] Add independent UV island-centre transforms and projection with complete
  chart selection, deterministic all-pose corner splitting, bounded capacity,
  cancellation, shared CLI, undo/recovery and native export. Advanced atlas
  constraints and texture rebaking remain open.
- [x] Add automatic UV charts and square/rectangular atlas packing through pinned xatlas,
  with seam preservation, all-pose corner remapping, pixel padding, bounded
  allocation/cancellation, overlap checks and GUI/CLI document history.
  Width/height limits preserve pixel proportions and relative chart density;
  existing single-size CLI commands keep square behavior.
- [x] Pack complete UV islands around fixed unselected regions and other surfaces
  sharing a material slot, with rectangular pixel padding, preserved orientation,
  uniform fitting or unchanged UV scale, bounded search, cancellation and exact
  all-pose corner remapping. GUI/CLI share ordinary history, recovery and export.
  Persistent per-corner pins, shader aliases, multi-tile packing and texture
  rebaking remain open.
- [x] Add animated MD2 export with persistent skin dimensions, ordered skin slots,
  all-pose seam sharing, target-limit and quantization checks, precision diagnostics,
  CLI parity and cancellable mesh-to-package export preparation.
- [x] Add MDL native-data import, indexed skin/member authoring, native groups and
  timing, header/palette edits and export through shared GUI/CLI document services.
  Schema 4 retains raw indices and group semantics in history/recovery; selected
  member previews are session-only. Original-engine acceptance remains part of
  the release gate.
- [x] Preview native MDL pose/skin timing with stored software-Quake and original
  GLQuake schedules, deterministic phase/seeking, cached images, reduced-motion
  behavior and the same sampler through `model mdl --time`.
- [x] Author bounded per-frame collision boxes with animate/freeze, per-pose
  fitting, shared current/all-frame GUI/CLI edits, frame-operation propagation,
  schema-7 save/recovery, interpolated preview and explicit stored-pose static
  map handoff. Runtime dynamic collision formats remain open.
- [ ] Complete the full [modeller release gate](MODELLER_RELEASE.md), including
  general topology repair, advanced atlas constraints, original-engine animation acceptance and dynamic collision export,
  additional native writers, worst-case authoring responsiveness,
  accessibility, and platform release evidence.
- [x] Add bounded `.assembly.json` recipes, nested tag composition, independent
  frame playback, selection-aware history, staged-package snapshots, guarded
  source saves and explicit static mesh/native-export handoff through GUI/CLI.
  Windows release checks cover cancellation, source protection, missing-input
  repair and expanded RTL layouts at 1×/2×. Assembly recovery is implemented.
  Sampled animation baking shares immutable inputs, topology/storage checks,
  cancellation, protected mesh/MD2/MD3 writes and GUI/CLI review. Optional mesh
  schema 6 retains fractional clip FPS through save, history and recovery.
  Optional independent FTE rendering verifies a CLI-authored nested animation
  bake as multi-surface MD3 and explicitly joined MD2, including native poses,
  blends, textures and deliberate stale-pose/attachment/UV controls. Native game
  timing, maximum-assembly performance and original-engine gameplay acceptance
  remain open. See [engine acceptance](MODEL_ENGINE_ACCEPTANCE.md#baked-assembly-animation).
- [x] Port/adapt model metadata loader workflow concepts from PakFu.
- [x] Define native idTech model loader boundary before adding optional Assimp import/export.
- [x] Add model preview viewport. `ModelViewport` first used a bounded software depth buffer presented by QPainter; since
  2026-10-08 it draws on the GPU (OpenGL or Vulkan) with the same QPainter overlays:
  orthographic/perspective projection, perspective-correct skins, per-pixel transparency, orbit/pan/zoom, frame stepping and timed
  playback, hover read-out, and wireframe, flat-shaded, and textured modes.
- [x] Move projection and indexed exact picking preparation off the GUI thread;
  rasterize complete wireframes with antialiasing, shared-edge deduplication and
  selected dashes above ordinary wires. The maximum editable-grid harness measures
  UI gaps and completed-image latency separately, including all-face selection.
  Broader scene and authoring performance gates remain open.
- [x] Stream internal geometry fingerprints, prepare shared document edge indexes,
  retain component-table selection across pose changes, and prepare vertex markers
  and exact vertex picking on the renderer worker. The maximum-mesh editor harness
  covers all-face/vertex/edge selection, mode changes and pose updates.
- [x] Add a maximum-grid document audit for source save/reopen, recovery with all
  edges selected, one-frame OBJ export/reimport and cancelled serialization.
  Windows optimized builds have repeated viewport/editor timing evidence at 1×
  and 2× scale. Varied production scenes, complete package handoff and native
  cross-platform performance gates remain open.
- [x] Add model geometry rendering: decode vertex/triangle data and draw the mesh rather than summarizing it.
  `decodeModelMesh` decodes geometry for Quake MDL (IDPO 6), Quake II MD2 (IDP2 8), and Quake III MD3
  (IDP3 15). Since 2026-10-08 MDC, MDR and IQM decode fully too, alongside the other idTech 1-4 formats
  below; animation-only files paint a no-geometry state in the viewport.
- [x] Read the model formats of every idTech generation: Hexen II and Half-Life MDL, Heretic II FM, MDC,
  MDS, MDM/MDX, MDR, Ghoul 2 GLM/GLA, IQM, MD5 mesh and animation with `.def` names, LightWave LWO, ASE and
  KVX, with companion files and skeletons posed into frames; write MD5, IQM and ASE. See
  [Native Model Formats](MODEL_FORMATS.md).
- [ ] Verify the model decoders against real game files and the exported MD5, IQM and ASE models in the
  original engines; read game animation scripts (Jedi Academy `animation.cfg`, Wolfenstein scripts,
  GZDoom MODELDEF/VOXELDEF) for clip names.
- [ ] Edit joints and weights: bone overlay, pose mode and weight painting.
- [x] Give the Mesh Editor the Levels page's sidebar layout, one or four views, and Blender, 3ds Max and
  MilkShape 3D controls profiles with customisation and CLI parity. See
  [Modeller Profiles](MODELLER_PROFILES.md).
- [x] Add skin/material dependency panel.
- [x] Add model loading state and fallback metadata view.
- [x] Add animation list where format supports it. Animations are inferred from frame-name stems, because
  MDL, MD2, and MD3 store no animation table.
- [x] Add export/conversion hooks. `exportModelFrameObj` writes one frame as Wavefront OBJ, from the Models
  surface's "Export OBJ" button and from `model export`; no `.mtl` companion is written.
- [x] Add bounded polygonal OBJ import through the shared document, package
  browser and CLI, preserving UV/normal seams, smoothing and direct material
  paths. Concave polygon triangulation, verified streaming and cancellable
  package previews have synthetic fixtures. See [OBJ interchange](MODEL_MESH.md#obj-polygon-interchange).
- [ ] Add reviewed MTL shading-to-game-material conversion and broader adjacent
  format import; unsupported records currently fail before adoption.

### Audio
- [x] Deliver aligned track/bus stems and an optional master via shared GUI/CLI,
  pre/post taps, continuous seeded integer dither, guarded per-file publication,
  cancellation and a status/hash manifest. Editable interchange remains open.
- [ ] Complete the [professional DAW capability gates](plans/audio-daw.md),
  including recording, routing, effects, MIDI/instruments, plugins, real-time
  streaming, advanced editing and session reliability.
- [ ] Complete the [audio release-candidate gates](plans/audio-editor-release-candidate.md).
- [x] Add empty/silent document creation, exact float copy/cut/paste, additive
  mixing, silence insertion, polarity/DC correction, and mono-to-stereo conversion
  through shared GUI/CLI services with named undo history.
- [x] Add frame-accurate waveform navigation, sample zoom, pan, an overview,
  channel displays, and an asynchronous multilevel waveform cache.
- [x] Add lossless `.vsaudio` project saves/reopen, external-change guards,
  asynchronous local recovery, draft restoration, and shared CLI import/inspection.
  Export/staging stay separate from saving the editable project.
- [x] Add a verified Audio recovery inventory, digest-checked draft restoration,
  live-editor discard protection, explicit reviewed cleanup, and shared CLI.
  Count/storage limits stop new checkpoints without automatically evicting work.
- [x] Offer a nonintrusive application-startup notification for available audio
  recoveries and expose the recovery preference in first-run setup.
- [x] Preserve float32 edited audition, add selection-preserving live/paused seek,
  preparation cancellation, finite backend timeouts and retryable device errors.
  Device-free lifecycle/codec fixtures cover these paths; physical-device and
  platform acceptance remain part of the release gate.
- [x] Add a PCM sample editor with range selection, trim/delete, silence, fades,
  reverse, gain/normalization, mono conversion, undo/redo, edited playback,
  atomic WAV export, shared CLI processing, and package staging. See
  [Audio Editor](AUDIO_EDITOR.md) for the bounded implementation and tests.
- [x] Add shared GUI/CLI high-quality resampling with explicit rate presets,
  float headroom, time-mapped selection, cancellation, named undo, and independent
  numerical fixtures. r8brain-free-src is pinned, credited, and included in licence bundles.
- [x] Add PCM8/16/24/32 and float32 WAV file export, reproducible optional integer
  TPDF dither, canonical padding/extensible headers, and shared CLI controls.
- [x] Add Doom/Quake-family sound delivery, bounded DMX encoding,
  transactional Doom WAD staging, and shared `asset audio-export` controls.
- [x] Share pending package sounds with Audio listing, preview, playback, browser
  export, and editor reopening; invalidate stale audition buffers after replacement.
- [x] Add cancellable whole-sound/selection analysis with per-channel sample
  peak, RMS, DC offset, full-scale counts, absolute event frames, and longest
  over-range runs; expose the same report in the editor and `asset audio-analyze`.
- [x] Add read-only true-peak and integrated loudness analysis for music/voice
  delivery, shared by the editor and CLI, with explicit surround speaker maps,
  unavailable/below-gate states, cancellation and independent signal fixtures.
- [x] Add SDK-scoped Windows Audio runtime staging, original dependency notices,
  binary provenance and native Qt/application catalog checks. Corresponding-source
  distribution, clean-machine and native-device acceptance remain release gates.
- [x] Add initial standalone capture with explicit input/channel selection and
  arming, permission checks, bounded input/disk workers, overrun reporting,
  checksummed recoverable takes and reviewed session/CLI export handoff.
- [ ] Complete synchronized overdubbing, software monitoring, calibrated latency,
  punch/loop capture, comping and native device/permission/power-loss acceptance.
  The device-neutral live mixer, punch processor and bounded capture queue now
  have synthetic coverage; [native binding and review](AUDIO_DUPLEX.md) remain open.
- [x] Add the initial mono/stereo multitrack arrangement: embedded source snapshots,
  descriptor edits, track/master mixing, frame gain/pan envelopes, versioned saves,
  undo/redo, prepared range audition and streamed WAV mixdown with shared CLI.
  Mixdown enters the existing analysis, delivery, package and level workflow;
  see [Multitrack Sessions](AUDIO_EDITOR.md#multitrack-sessions) for limits.
- [x] Connect session checkpoints and verified draft restoration to waveform
  recovery's storage limits, live leases, preferences, startup review and CLI.
  Preserve reviewed copies and original sessions; interruption, retirement and
  conflict fixtures cover the bounded in-memory arrangement. Take journals use
  separate reviewed recovery; power-loss/filesystem acceptance remains open.
- [x] Stream session audition with a shared frame transport and caller-owned mix
  buffers, selected output, pause/seek/loop, master peaks and visible dropouts.
  Fake-device and CLI diagnostics cover block timing and lifecycle. Actual-device
  acceptance, low-latency monitoring and disk-backed source streaming remain open.
- [x] Add stereo buses/sends, polarity/swap, routing-cycle validation and isolated
  solo paths through shared playback/export, version-2 sessions and GUI/CLI edits.
- [x] Add bounded non-destructive track/bus/master chains with EQ/filtering,
  compressor/gate/sample limiting, stereo delay, saturation, bypass and saved
  tail control; share undo/recovery, version-3 persistence, GUI and CLI edits.
- [x] Extend effects with stereo reverb, chorus, flanger, tremolo, phaser and
  shared factory/file presets, staged GUI controls and guarded CLI operations.
- [x] Add numeric effect parameter lanes and linear/step/smooth gain/pan/effect
  curves, a shared graphical/native point editor, CLI and version-4 persistence.
- [x] Add fixed-lookahead sample-peak limiting and processing latency compensation
  across track/bus/master inserts, sends, taps, playback and exports, with structural
  parameter validation, staged edits and diagnostics.
- [x] Add stepped tempo and bar-boundary meter maps, musical ruler/navigation
  and snapping, native v5 persistence, shared CLI edits/conversion and recovery.
  Audio/automation remain sample-anchored; ramps, metronome and MIDI remain open.
- [x] Add explicit-track clear, ripple-delete, silence insertion and section repeat,
  with optional curve-preserving automation following, separate master scope,
  undo/recovery and native v7 GUI/CLI persistence. Tempo/meter maps stay unchanged.
- [x] Add shared source usage/availability inventory, staged bit-identical relink,
  explicit all-clip replacement, rename and unused-source removal, with file
  digest guards, undo/recovery and CLI parity. No source files are deleted.
- [ ] Extend source management with bulk relink search and automatic embedded
  source round trips to the waveform editor.
- [ ] Extend effects with pre-fader placement, true-peak limiting and external
  plugin hosting; live automation recording remains open.
- [ ] Extend sessions with seamless live mixer changes,
  surround/sidechains, a low-latency device clock, live
  automation modes, long-media streaming, bulk media relinking, tempo ramps,
  musical anchoring and advanced delivery. Initial session controls do not close
  those gates.
- [x] Verify cue/forward-loop authoring, edit/SRC transforms, versioned native
  persistence, WAV and Quake/II delivery, and shared `asset audio-markers`.
  The bounded 256-cue/one-forward-loop contract passes core, CLI, browser, recovery,
  and scaled/RTL editor fixtures with optional playback enabled and disabled.
- [x] Add bounded MP3, native FLAC and Ogg Vorbis import through shared bundled
  decoders, with cancellation, native sample persistence, CLI support and
  asynchronous browser WAV export. Synthetic independent-oracle fixtures cover
  MPEG versions, gapless timing, 1–8 Vorbis channels and FLAC precision.
- [x] Add reviewed Quake II/III speaker placement with matching sound delivery,
  atomic package/map handoff, independent undo, stale-context guards, correct
  dependency roots, and shared `map place-sound` CLI validation.
- [ ] Extend sound placement to custom entity definitions and source-port
  profiles; stock Quake/Doom currently use package delivery and normal entity tools.
- [x] Add audio metadata preview.
- [x] Add playback where Qt backend supports codec. Original WAV precision is preserved; DMX is widened to PCM16; Ogg, MP3, and
  FLAC go to Qt's own decoders.
- [x] Link an audio playback backend (Qt Multimedia or miniaudio) and add transport controls. Qt Multimedia,
  optional at build time; play and pause (Space), stop, loop, volume, and a seekable playhead.
- [x] Read Doom DMX sound lumps: metadata, waveform, playback, and WAV export; PC speaker sounds report their
  tones. `MUS` and MIDI remain planned.
- [x] Evaluate miniaudio for portable playback/decoding/waveform gaps ([`docs/STACK.md`](STACK.md)).
- [x] Add loading/buffering/playback state display. Load and decode state is shown, and the transport shows
  Play or Pause, the playhead time, and Playing, Paused, or Stopped to a screen reader.
- [x] Add waveform preview: a min/max envelope painted from decoded PCM peaks.
- [x] Add convert-to-WAV helper for supported sources. Native WAV/DMX and bounded
  MP3/FLAC/Vorbis use the editor's shared decoder and PCM16 writer. Browser work
  runs asynchronously with preparation cancellation and a separate atomic output.

### Text And Scripts
- [x] Add document diagnostic pulls, bounded queues and cancellation retries,
  unchanged-result caches, inter-file refresh and visible error/retry states.
  Successful GUI Save/Save As notify interested servers after synchronization;
  shared CLI checks accept pull-only providers. Workspace pulls and will-save
  hooks remain future work.
- [x] Connect explicitly selected local stdio language servers for live-buffer
  diagnostics and semantic Go to Definition, with UTF-16 negotiation, document
  version/caret guards, cancellation, project-change disconnect and shared CLI
  checks. Protocol fixtures, GUI lifecycle tests and clangd verification cover
  the initial client. See [Local Language Services](LANGUAGE_SERVICES.md).
- [x] Add semantic completion through the same optional local connection, with
  Ctrl+Space, server triggers, local fallback, source/version/caret guards and
  one-step undo for replacement and related document edits. Shared parsing and
  CLI proposals cover UTF-16 ranges, list defaults, bounded output and explicit
  omissions. Core/GUI fixtures and clangd verify the workflow; commit characters
  remain future work. Reviewed refactorings now use Code Actions.
- [x] Resolve highlighted completion suggestions through the optional provider,
  retaining opaque data and list defaults. Show loading/unavailable state, cancel
  superseded requests and defer early acceptance until metadata and related import
  edits validate. Source/caret/version guards and combined Undo/Redo remain shared;
  CLI indexed resolve returns full proposals without writing sources.
- [x] Accept bounded completion snippets with linked/nested placeholders,
  Tab/Shift+Tab navigation, native choices, document variables and an explicit
  final stop. Share expanded CLI previews, deferred imports, one-step insertion
  Undo, mirrored-edit Undo and normal save/recovery/indexing. External changes,
  Undo and document switches retire field tracking. Regex transforms, adjusted
  indentation and stacked snippet sessions remain future work.
- [x] Add Parameter Hints through the shared optional language connection, with
  Ctrl+Shift+Space, automatic trigger/retrigger refresh, overload selection and
  textual active-argument emphasis. A compact, scalable inline panel expands
  documentation on demand; edits/caret/tab changes and cancellation guard late
  replies. GUI and CLI share bounded UTF-16 label parsing and snapshot provenance.
  Fixtures, direct widget renders, clangd and Pyright verify the read-only workflow.
- [x] Add semantic Find All References through the optional local connection.
  Shift+F12 shares Search Results and Activity with cancellable background
  previews, current unsaved buffers, exact ranges and stale-snapshot guards.
  The CLI exposes references and declaration exclusion through the same service.
  Protocol/core/GUI fixtures and clangd cover the workflow; unsupported documents
  keep whole-word text search. Results depend on the server's project index and
  cannot be used as replacement plans.
- [x] Add Quick Info through the optional local language connection: Ctrl+I
  opens selectable symbol documentation, and pointer dwell shows a short hint.
  Shared parsing and CLI output cover Markdown/plain text/code parts, bounded
  content, source hashes and validated UTF-16 ranges. Stale replies cancel;
  rendering cannot fetch resources or activate links. Fixtures, direct widget
  renders and clangd verify the initial workflow without changing saved source.
- [x] Add provider-backed Rename Symbol (F2) with preparation, an accessible name
  dialog and Search Results review. Validate whole WorkspaceEdit text plans
  against synchronized unsaved tabs and saved project files; reuse document Undo
  and guarded project replacement writes. CLI preview and reviewed-plan SHA-256
  writes share the same service. Unsupported resources, stale versions and
  malformed edits block the entire preview; cancellation reports completed saves.
- [x] Add provider-backed Code Actions (Ctrl+.) for quick fixes and refactorings,
  with an accessible picker, preferred/unavailable status, lazy edit resolution,
  current diagnostic context and cancellation. Share rename's workspace-edit
  validation, Search Results review, unsaved Undo and guarded disk saves. CLI
  action listing, selection, previews and whole-plan SHA-256 writes use the same
  services. Commands and file resource operations remain unsupported.
- [x] Add document and selection formatting through the optional local language
  connection, with remappable shortcuts, cancellable Activity/progress and
  source/version/hash guards. Valid edits form one unsaved Undo step; invalid
  or overlapping sets reject entirely. CLI previews and explicit saved-hash
  writes share exact-range encoding/atomic saves. Core/GUI fixtures and clangd
  cover formatting, cancellation, Unicode, mixed endings and normal Save.
- [x] Start with Qt text widgets and a local syntax-highlighting boundary.
- [x] Evaluate KSyntaxHighlighting packaging and theme integration.
- [x] Evaluate Tree-sitter for shader/script/config incremental parsing.
- [x] Add syntax highlighting for CFG.
- [x] Add syntax highlighting for shader scripts.
- [x] Add syntax highlighting for QuakeC.
- [x] Add project-wide find/replace.
- [x] Run project search and reference lookup asynchronously with cancellation,
  case/whole-word options, file globs, exact replacement previews, stale-file and
  unsaved-document guards, byte-preserving writes, and shared GUI/CLI tests.
  See [Project Search](PROJECT_SEARCH.md). Archive/staging replacement remains
  separate future work.
- [x] Search named live Code documents, including inactive and deleted-file
  tabs, with shared UTF-8/UTF-16 decoding and snapshot provenance. Apply reviewed
  replacements as undoable unsaved document edits plus atomic unopened-file
  writes; reject changed/closed tabs, newly opened disk targets and dirty Levels
  maps. Keep CLI saved-file behavior and explicit partial-application reports.
- [x] Add diagnostics markers from compiler output.
- [x] Add save state indicators: clean, modified, saving, saved, failed.
- [x] Add an in-file find bar, go to line, and a Ctrl+S save that never reloads the editor.
- [x] Preserve per-tab UTF-8/UTF-16 BOMs, existing line separators, Unicode spaces,
  and final-newline state; guard atomic saves against external edits and unsaved
  Levels documents. Share format inspection and hash-guarded saves with the CLI.
  See [Code Editor](CODE_EDITOR.md) for supported formats and remaining gaps.
- [x] Create independent untitled Code tabs and adopt reviewed Save As destinations
  without clearing undo, while protecting dirty Code/Levels documents.
- [x] Keep asynchronous local text recovery copies, restore verified drafts, and
  retire pending checkpoints after save/discard; expose list/export in the CLI.
- [x] Add in-file replace (Ctrl+H): replace the selected match and move on, or replace every match as one undo step.
- [x] Add toggle line comment (Ctrl+/), duplicate lines (Ctrl+D), and move lines (Alt+Up/Alt+Down), each one
  undo step.
- [x] Add auto-indent on Enter, Tab/Shift+Tab block indent, and bracket-pair highlighting to the code editor.
- [x] Reopen the last session at start (package, map, code tabs), with a preference to turn it off.
- [x] Drop files and folders onto an open package's entries to stage them into the folder under the pointer.
- [x] Drag package entries and folders out to the desktop as extracted copies.
- [x] Record the session as it changes, and after a crash offer it back from a notice bar (Reopen Last Session,
  View Report) instead of reopening files that may have caused it.
- [x] Zoom the code editor with Ctrl+=, Ctrl+-, and Ctrl+wheel, with a readout that resets it.
- [x] Let users give any command keys of their own (Help > Keyboard Shortcuts), with clash warnings, Reset and
  Reset All, and `editor keys` in the CLI.
- [x] Keep project search matches in their own Search Results tab, so opening one keeps the rest listed.
- [x] Keep a crashed session recorded until its offer is answered, ask before Reopen replaces unsaved work, and
  let a second running studio leave the first one's session alone.
- [x] Reflow the Workspace jump tiles onto more rows when four do not fit.
- [x] Show the map's textures as tiles in a Levels Textures tab (recent first, applied with Enter, Select Objects
  Using It), with `map textures --uses` in the CLI.
- [x] Go to Definition in the code editor (F12, Ctrl+click) across the project, with Alt+Left back, and Find All
  References (Shift+F12) as whole words under Search Results.
- [x] Complete names in the code editor (Ctrl+Space) from the language's keywords, the file, and the project.
- [x] Show the open file's outline beside the project files, following the caret.
- [x] Search Settings by typing (Ctrl+F), keeping the categories that mention the text and going to the first
  match with Enter.
- [x] Fold `{ }` blocks in the code editor (gutter chevrons, Ctrl+Shift+[ and ], Fold All, Unfold All), with a
  badge counting the hidden lines; the caret moving inside, or the braces unpairing, opens a fold.
- [x] Shade every use of the name at the caret in the code editor, as whole words in its own case.
- [x] Pin the opening lines of the blocks around the top of the code editor's view (sticky headers), with their
  line numbers, a click to go there, and View > Sticky Headers.
- [x] Show a breadcrumb above the code editor (folders, file, symbol at the caret), with folder menus of files and
  the symbol opening Go to Symbol.
- [x] Step through the open file's problems with F8 and Shift+F8, wrapping, with the status bar saying which.
- [x] Query map objects by property in the Levels Objects filter (`tag=3`, `class=light light>200`), with Enter
  selecting every match, and `map find` in the CLI.
- [x] Query package entries the same way in the Packages filter (`ext=wav size>1mb`), and `package list --where` in
  the CLI, through one shared query language (`core/studio_query`).
- [x] Query images the same way in the Textures filter, with `uses` and, once decoded, `w`, `h`, and `format`
  (`w>=128 format=wal`).
- [x] Query models and sounds the same way in the Models and Sounds filters (`ext=wav size>1mb`).
- [x] Query shaders in the Shaders filter by counts, directives, and stage keys (`missing>0`, `cull=none`,
  `blend:gl_one`).
- [x] Materials workbench (2026-10-08): every texture, shader and material of idTech 1 to 4 in one library, a live
  per-engine preview with animation, validation of each engine's loading rules, text and node-graph editing over one
  text, saving into package staging, and the `material` CLI family. Listing the maps that use a material, a
  per-game Quake III dialect setting (RTCW, ET, Jedi Knight effects beyond ET's implicit images) and IWAD/PWAD texture
  merging remain open.
- [x] Query project files in the Code page's Files tree (`ext=qc size>10kb`, `language=shader`), and name a key
  no item has in the status bar wherever a query empties a list.
- [x] Remember each query filter's queries between sessions, offered again as you type or with Down.
- [x] Step through the last build's problems from any page with F4 and Shift+F4.
- [x] Edit a key on several selected entities at once, as one undo step, with differing values marked, and from the
  CLI with `map edit --where "<query>"`.
- [x] Go Back and Go Forward through the pages left and the jumps made (Alt+Left, Alt+Right, the mouse's side
  buttons), putting the caret, the map selection, or the current row back.
- [x] Cut shell start-up from about 100 seconds to under 10 in the debug build: shortcuts install once per batch
  instead of once per command, and pages are built into a window that already holds their stack.
- [x] Make Door for Doom and Hexen sectors (Levels and `map make-door`), after Doom Builder's.
- [x] Draw Doom tag links, lines to the sectors they act on, as Doom Builder's association arrows; Select
  Targets and Select Sources follow them. Hexen's special-dependent arguments remain to be read.
- [x] Raise, lower, brighten, darken, and gradient Doom sectors (Page Up/Down on the map view,
  `map shift-sectors`, `map gradient-sectors`), without marking the nodes stale.
- [x] Open files in tabs, each with its own text, undo history, and caret; a dot marks unsaved changes,
  Ctrl+Page Up/Down switch and Ctrl+F4 closes, and each open file is watched for outside changes.
- [x] Fold the navigation rail to icons by itself: it opens over the page on a resting pointer or keyboard
  focus and folds when either leaves, a pin keeps the labels open, and Settings offers icons only.
- [x] Keep the Code page's Files tree on the file being edited, count the Levels selection in the map view's
  corner instead of a status message that goes stale, stop reporting the studio's own manifest writes as
  outside changes, and write the setup steps' names in translatable title case.

Exit criteria:
- [x] VibeStudio replaces PakFu for core browse/preview/package workflows and adds project/compiler context.

## Milestone 7: Level Editing MVP

Goal: introduce native editing through focused slices instead of attempting a
full Radiant/Doom Builder replacement in one step.

### Read And Inspect Maps
- [x] Load Doom map lump structure.
- [x] List the maps in a WAD and switch between them from the Levels document bar (`levelMapNamesInWad`).
- [x] Load Quake-family `.map` text structure.
- [x] Load Quake III `.map` text structure.
- [x] Show entity list.
- [x] Show texture/material references.
- [x] Show map statistics.
- [x] Show validation problems.
- [x] Parse entity definitions (FGD, DEF, ENT) into a model. `loadEntityDefinitions` reads Radiant
  `/*QUAKED` blocks from `.def` and `.qc`, Valve `.fgd` including `@include` and `@BaseClass` inheritance,
  and Quake III `.ent` entity lists into an `EntityDefinitionCatalogue` shared by the Levels "Entity" tab,
  `validateLevelMapEntities`, and the `entity definitions` and `entity validate` commands.

### Visual MVP
- [x] Add 2D map view for Doom-family maps: a painted, zoomable, selectable viewport with vertices, linedefs,
  traced sector fills, and things.
- [x] Add orthographic brush preview for Quake-family maps, with solved brush bounds and tessellated Quake III
  patches on top, front, and side projections, alongside the shared perspective camera.
- [x] Add a read-only 3D preview to Levels (`buildLevelMapPreviewMesh` on the Models surface's renderer, on the GPU since 2026-10-08):
  brushes, patches, and Doom walls, flat shaded, keeping its camera across edits.
- [x] Add selection model.
- [x] Add property inspector.
- [x] Suggest entity values: targetnames for target-style keys, targets still unnamed for `targetname`, and
  definition choices, in the inspector's editor and in Edit Key.
- [x] Add save-as for non-destructive map edits.
- [x] Add point entities and delete entities, brushes, and patches on Quake-family maps, in the Levels surface
  (context menu, Del, Edit menu) and the CLI (`map add-entity`, `map delete`), as undoable edits whose save-back
  touches only the lines involved.
- [x] Duplicate entities, brushes, and patches (Ctrl+D on Levels, `map duplicate`), keeping each copy in its
  original's text format.
- [x] Add, duplicate, and delete Doom and Hexen things (Levels and `map add-thing`, `map duplicate`,
  `map delete`), editing a selected thing's fields in the entity inspector.
- [x] Add box brushes (Levels and `map add-brush`) in the map's own face format.
- [x] Clip brushes: the Levels Clip Tool (X, a line drawn across the view), Clip Selection… (a plane square to an
  axis), and `map clip`, keeping one side or both, with the new face written in the brush's own format.
- [x] Hollow brushes into walls of a thickness (Levels Hollow… and `map hollow`).
- [x] Merge brushes that form an exact convex union, with per-face material/UV/flag
  review, package/staging previews, one undo step and `map merge-brushes` parity.
- [x] Carve (CSG subtract) brushes (Levels Carve and `map carve`), the carving brushes kept for the next cut.
- [x] Split and flip Doom linedefs (Levels and `map split-linedef`, `map flip-linedef`).
- [x] Draw Doom geometry: Draw Sector (D) and Add Sector… (`map draw-sector`) close a shape into a sector that joins
  and splits the lines it meets; Del removes vertices, linedefs, and sectors; Merge Vertices (`map merge-vertices`)
  stitches lines drawn over each other; Join and Merge Sectors (`map join-sectors`, `map merge-sectors`) make rooms
  one; a click inside a room selects its sector. Edits that renumber records are one `doom-topology` undo step each.
- [x] Add map loading state and parse/validation progress.
- [x] Add map health overlay for parse/validation issues, entity problems, leak point files, and compiler
  warnings when data is available.
- [x] Connect Entities (Levels and `map connect`), Radiant's link maker, and Select Targets / Select Sources.
- [x] Draw entity target links (`target`, `killtarget`, `pathtarget`, `combattarget`, `deathtarget` to `targetname`)
  as arrows in the viewport and in `map render --links`, heavier for the selection, dashed for `killtarget`.
- [x] Draw compiler leak trails (`.pts`/`.lin`) over the map in every projection, shown automatically after a
  leaking build of the open map and framed from Problems or Health; `map render --leak` draws them too.
- [x] Add missing-texture detection that resolves map texture references against mounted package textures.
  `auditLevelMapTextures` walks brush faces, patch shaders, and Doom sidedefs and sectors, never counts an
  engine-handled name as missing, and the Levels health list reports what the open package does not provide,
  with the paths it searched.
- [x] Add map statistics summary with detail drawer.

### Editor Profile MVP
- [x] Define editor profile schema.
- [x] Add profile selector in preferences.
- [x] Add GtkRadiant 1.6.0-style layout/control preset.
- [x] Add NetRadiant Custom-style layout/control preset.
- [x] Add TrenchBroom-style layout/control preset.
- [x] Add QuArK-style layout/control preset.
- [x] Add profile-specific keybinding tests.
- [x] Add profile-specific camera/selection smoke tests.
- [x] Make the TrenchBroom and NetRadiant Custom profiles behave like those editors: layout, a first-person
  3D camera (look, orbit, pan, fly), 2D gestures (brush drawing, Radiant's tunnel selection and camera-aiming
  middle button), grid, and keys, as data in `core/level_editor_controls`, with a Controls reference on the
  Levels bar and `editor controls` in the CLI.
- [x] Give GtkRadiant 1.6.0 its own controls, not just keys.
- [x] Give the QuArK profile separate camera/plan controls, with remaining adaptations documented.
- [x] Add camera selection face handles for Quake brushes/patches and point-object
  spacing, using shared grid, texture policy, locks, undo and save. All registered profiles
  expose the explicit gesture; semantic tests cover preview geometry, full-shell
  publication and 200% high-contrast RTL labels.
- [x] Add camera box drawing on XY/XZ/YZ construction planes across all registered profiles,
  with linked plan drafts, grid/work-zone depth, material and numeric-primitive
  handoff, scene locks, source guards, undo and save. Synthetic preview latency
  and enlarged high-contrast RTL controls have semantic tests.
- [ ] Add surface-aligned/slanted construction planes, Doom shared-topology resize
  previews and live unlocked UV projection; accept camera authoring with native
  input, screen readers and production-map/full-shell latency measurements.
- [x] Show three 2D views beside the camera with persistent layouts, linked navigation and saved views.
- [x] Reuse indexed plan selection geometry for readouts, framing and resize handles,
  preserving sparse IDs, hidden ownership, undo/reload invalidation and point/line
  extents. Real-project picking and full shell latency remain open.
- [x] Reuse bounded projected brush wires and physical-pixel images in plan views,
  sharing the model preview's CPU edge coverage. Preserve ordered ownership
  styles, invalid-brush warnings, independent picking and complete budget fallback.
  Member markers prioritize distinct visible positions. Production maps, Doom
  painting and native latency acceptance remain open.

- [x] Prepare and render larger Quake plan views on coalescing, cancellable Qt
  workers, including patch borders and selected outlines. Reject retired scene,
  projection and selection results, expose an accessible updating state, and
  measure GUI painting separately from completed images. Wire-budget fallback
  remains complete on the worker. Above-limit images, allocation fallback, scene
  solving/adoption, picking and live overlays remain responsiveness work.

- [x] Profile plan paint stages, cache unchanged native grids and exact-phase
  member-ring rasters within explicit budgets, and keep primary labels inside
  the pane near their marker without covering status tags. Add independent
  drawing references and actual-widget scaling/RTL checks. Warm repaint gains
  do not establish cold-navigation or native production frame-time acceptance.

- [x] Move cold grid/member-image preparation to an independent coalescing worker
  for large Quake scenes and large selections, including Doom. Preserve exact
  physical placement, live primary/handles/picking, aggregate accessible pending
  state, stale-result rejection and complete ordinary fallback. Measure cold GUI
  painting and completion separately; production latency remains an open gate.

- [x] Preserve fractional physical origins of child plan panes across geometry,
  selected outlines, patches, warnings, grid and member images. Keep background
  rendering available in scaled split layouts, charge edge coverage to existing
  image limits, and verify phase-changing moves and whole-pixel cache reuse
  against direct parent-target drawing. Native monitor migration remains open.

- [x] Highlight actual selected Quake brush edges and curved patch borders in
  every plan projection, including visible entity-owned geometry. Keep separate
  bounded selection drawing, non-color dashes, invalid-brush warnings, complete
  fallback, source/history isolation and undo/reload invalidation. Doom geometry
  selection outlines and native profile acceptance remain open.

### Editing MVP
- [x] Edit entity key/value pairs.
- [x] Move selected Doom vertices/linedefs in 2D.
- [x] Move selected Quake entities.
- [x] Add undo/redo command stack.
- [x] Add a Levels History tab: every edit as a step, the save point marked, Enter jumps to any step.
- [x] Add a Levels Create palette (idStudio's entity browser): point classes by prefix or Doom thing types by kind,
  placed with Enter at the view's middle or dragged onto the map.
- [x] Edit Doom sectors, linedefs (flags as named boxes), sides, and vertices in the Levels Inspector, and from
  `map edit --select sector:N|linedef:N|sidedef:N`.
- [x] Add Close Map, and recent maps on the empty Levels page.
- [x] Add Select All of This Class (entities by class, Doom things by type) to the Levels menus.
- [x] Add Select by Texture: brushes and patches, or Doom linedefs and sectors, that use a texture.
- [x] Add Snap Selection to Grid (Levels and `map snap`), each object by its own amount, as one undo step.
- [x] Add Rotate 90° Left/Right (Levels and `map rotate`): exact quarter turns of brushes, planes, Valve texture
  axes, patches, entity angles, and Doom things. These shortcuts retain legacy texture behavior.
- [x] Add numeric rotation with an asynchronous preview, explicit pivot, exact classic/Valve/primitive
  texture lock, explicit map-wide Valve 220 conversion, full model-style entity orientation, Doom
  geometry support and shared CLI/undo/persistence. Real-material preview and legacy shortcut parity remain.
- [x] Add Flip Horizontal/Vertical (Levels and `map flip`), keeping face winding and patch facing outward.
- [x] Add Resize (Levels handles on the selection's box, Resize Selection…, and `map resize`): brush planes,
  patches, entities, and things map from the old box to the new one, with textures kept in place in the world.
- [x] List patches and Doom sectors in the Levels objects list, so every selectable object has a row.
- [x] Add Cut, Copy, and Paste of map objects as .map clipboard text, interchangeable with TrenchBroom and Radiant.
- [x] Add Hide Selection (H) and Show All Hidden (Shift+H) to Levels, and the selection's size to its HUD.
- [x] Add Smaller Grid ([) and Larger Grid (]) to Levels.
- [x] Add Select All (Ctrl+A), Invert Selection (Ctrl+I), and Select None (Ctrl+Shift+A) to Levels, and Select in Open
  Map to the Textures surface.
- [x] Align textures per face: a selected brush's faces in the Levels Inspector, named by the way they face, with texture,
  shift, rotation, and scale editable in place (`map edit --select brush:N --set faceK.field=value`), Valve 220 turns
  turning the axes. Texture-matrix (brushDef) editing remains future work.
- [x] Add Apply Texture (Levels, Apply to Map Selection on Textures, and `map apply-texture`): one texture on every
  face of the selected brushes and patches, as one undo step.
- [x] Add Replace Texture (Levels and `map replace-texture`) for brush faces, patches, and Doom walls and flats,
  across the map or the selection, with a live count of the uses that will change, and Use in Open Map… from
  the Textures surface, whose In open map switch lists only the textures the map uses.
- [x] Add a Build Problems context menu: Show, Copy Message (the compiler's own line), and Copy All Problems.
- [x] Add visible edit state and undo/redo history summary.
- [x] Run compiler/profile from map editor.

### CLI And Tests
- [x] Add asynchronous Levels dependency inspection with source-object navigation,
  shader-image expansion, missing/ambiguous states, JSON, and `map dependencies`.
- [x] Export selected package files or resolved map assets through an independent
  staging plan (`package subset`, Export Selected, Export Assets), with dry runs,
  cancellation, deterministic output, and source/manifest/backup path guards.
- [x] Expand MDL/MD2/MD3 material references into images and Quake III shader
  images; inspect and export the current staged package snapshot.
- [x] Add namespace-aware WAD subset exports with explicit required-member review.
- [ ] Expand external `.skin` overrides, secondary shader references, and Doom
  composite texture/patch dependency closure.
- [x] Add `map inspect`, `map edit`, `map move`, and `map compile-plan`.
- [x] Add fixture-backed tests for Doom WAD and Quake-family map parse/edit/save-as.
- [x] Add release validation coverage for map CLI inspect, edit, move, and compile-plan.

Exit criteria:
- [x] User can inspect a map, choose a familiar editor profile, make a small safe edit, compile/build, and test the result.

## Milestone 8: Advanced Studio Surfaces

Goal: evolve from workbench into the full all-encompassing studio.

### idTech3 Shader Graph
- [x] Parse shader scripts into an editable model.
- [x] Render stage list and texture dependencies.
- [x] Add graph editor for stages and blend modes.
- [x] Add shader stage visual preview with raw text detail.
- [x] Round-trip graph edits back to text.
- [x] Validate shader references against mounted packages.

### Sprite Creator
- [x] Add Doom sprite naming workflow.
- [x] Add Quake sprite workflow.
- [x] Add palette preview and conversion.
- [x] Add frame sequencing.
- [x] Add package staging integration.

### Code IDE
- [x] Move Go to File discovery and recent-path validation off the UI thread,
  rank large lists in event-loop batches, and report cancellation/partial results.
  Share source/media discovery with `project files` and retire stale context.
- [x] Move Files traversal and metadata reads into a cancellable background catalog,
  publish rows in UI batches, preserve browsing state, and share listing/query
  behavior with `code files` on the CLI.
- [x] Run source indexing in a cancellable background worker with bounded Unicode
  decoding, live Code snapshots, shared completion/navigation, explicit partial
  results, and CLI reporting.
- [x] Add project source tree.
- [x] Add language service hooks.
- [x] Add build task integration.
- [x] Add symbol search where feasible.
- [x] Add run/debug launch profiles for source ports.
- [x] Stage a built map into the open package under `maps/` from the Build page (Add to Package), replacing
  an older build, for Save As to write.
- [x] Copy a built map into the game folder the engine loads maps from before launching, asked once per
  installation (`launch run --deploy --allow-test-maps` on the CLI), and chain build and launch as
  **Build and Launch** (F5).

### AI-Assisted Creation
- [x] Prompt-to-shader scaffold.
- [x] Prompt-to-entity-definition snippet.
- [x] Prompt-to-package-validation plan.
- [x] Prompt-to-batch-conversion recipe.
- [x] Prompt-to-CLI command generation.
- [x] AI proposal review surface with summary, context used, generated actions, and detailed prompt/response log.

### Plugin/Extension System
- [x] Define extension manifest.
- [x] Define trust and sandbox model.
- [x] Add extension discovery.
- [x] Add extension command execution.
- [x] Add extension-generated file staging.

Exit criteria:
- [x] Common mod development no longer requires VibeStudio users to leave for routine shader, sprite, script, package, and build operations.

## Continuous Quality Backlog

These tasks never fully end. Pull them forward whenever a feature touches the
related area.

### Performance
- [x] Add startup timer.
- [x] Add package open timing.
- [x] Add preview timing.
- [x] Add compiler task timing.
- [ ] Add time-to-first-feedback measurement for long-running operations.
- [x] Add task-state transition timing.
- [ ] Add cache invalidation tests.
- [ ] Add memory checks for large package previews.

### Robustness
- [x] Add fuzz target for PAK parser. The `PackageArchive::load` target in `parser-fuzz-smoke` seeds PAK,
  Doom PWAD, Quake WAD2, and stored and deflated ZIP archives, and checks the entries it reports back.
- [x] Add fuzz target for WAD parser.
- [x] Add fuzz target for ZIP/PK3 parser.
- [ ] Add fuzz targets for the new binary parsers: inflate, the idTech image decoders, and BSP/portal/leak
  inspection. Partly done: `parser-fuzz-smoke` runs a deterministic corpus, generated by a seeded xorshift
  generator over seven mutations and re-runnable through `VIBESTUDIO_FUZZ_SEED`, through `inflateRaw`,
  `inflateZlib`, `decodeIdTechImage`, `detectIdTechImageFormat`, `inspectBspBytes`, `PackageArchive::load`,
  `loadLevelMap` for both `.map` and Doom WAD input, and `decodeModelMesh`. The portal (`.prt`) and leak
  point (`.pts`/`.lin`) readers take a path rather than a buffer and are not fuzzed yet.
- [x] Add fixture tests for every claimed format.
- [x] Add session log capture: a Qt message handler mirrors warnings and above into a rotating session log.
- [x] Add crash-handler capture on top of the session log: `installCrashHandling` installs the platform's
  unhandled-exception or signal handlers plus `std::set_terminate`, writes an async-signal-safe plain-text
  report with a backtrace and the tail of the session log, recovers an unclean previous session from a
  session marker, and prunes old reports. An interactive start arms it (a preference turns it off), the next
  start offers the crashed session back from a notice bar instead of reopening it, and `diagnostics crashes`
  and **Help > Crash Reports** list the reports, which never leave the machine. `studio-runtime-smoke` and
  `shell-interaction-smoke` cover it.
- [x] Add corrupted-file fixture suite: truncated, malformed, and hostile inputs across the archive, image,
  deflate, BSP, and preview readers.

### Accessibility And Usability
- [ ] Audit keyboard navigation.
- [ ] Audit high-DPI scaling.
- [ ] Audit app text/UI scaling at 100%, 125%, 150%, 175%, and 200%.
- [x] Audit color contrast for normal, high-contrast dark, and high-contrast light themes
  (`studio-theme-smoke`, WCAG 2.2 ratios for dark, light, and both high-contrast themes).
- [ ] Add configurable font size. Partly done: the app-wide text scale covers 50% to 400%, and the code editor
  zooms on its own from 50% to 300% (Ctrl+=, Ctrl+-, Ctrl+wheel), kept between runs; other text views have no
  zoom of their own yet.
- [x] Add high-visibility theme tests (`studio-theme-smoke`: contrast, 2px focus rings, a focus colour distinct
  from selection).
- [ ] Add color-blind-aware status palette tests.
- [ ] Add reduced motion preference if animations are introduced.
- [ ] Add OS-backed TTS smoke tests.
- [x] Add tooltips for icon-only actions; toolbar actions show their label and shortcut.
- [x] Audit each editor profile for discoverable controls and non-conflicting shortcuts.
- [ ] Audit loading/progress UI for screen-reader labels and non-color-only status.
- [ ] Audit detail drawers for keyboard access.

### Localization
- [x] Add Qt translation extraction workflow.
- [x] Add pseudo-localization target.
- [x] Add right-to-left layout smoke test.
- [x] Add initial translation catalog structure for the 20-language target set.
- [x] Add locale formatting tests for dates, numbers, sizes, durations, and sorting.
- [x] Add stale/untranslated string report.
- [x] Add translation expansion stress sample and ratio reporting.
- [x] Add translation expansion layout smoke checks.

### User Awareness And Progressive Disclosure
- [ ] Add UX checklist requiring state, progress, result, next action, and details for each workflow.
- [ ] Add snapshot tests or scripted QA for loading/empty/error/success states. Partly done: `--ui-snapshot`
  renders every work surface, the Activity panel, the opened navigation rail, and the command palette to PNG
  for scripted visual review, with `--open` to load content first; there is no automated image comparison yet.
- [x] Add "copy diagnostic bundle" workflow.
- [ ] Add operation result summaries for package, compiler, validation, AI, and export tasks.
- [ ] Add graphical views only when backed by real data and actionable drill-down.

### Cross-Platform
- [x] Add path handling tests.
- [ ] Add shell-open tests.
- [x] Add portable package tests.
- [ ] Add file association documentation.
- [ ] Add platform-specific launcher docs.

### AI Safety And Privacy
- [ ] Add API key redaction tests.
- [ ] Add AI prompt/context preview tests.
- [ ] Add staged-application tests for AI-proposed edits.
- [x] Add project-level AI disablement test.

### Credits And Licensing
- [x] Validate README Credits section against `docs/CREDITS.md`.
- [x] Validate external compiler submodule revisions against `src/core/studio_manifest.cpp`.
- [x] Bundle third-party license files in release artifacts.
- [x] Add About/Credits/license surface generated from docs and structured metadata.
- [x] Require credits updates in PR checklist.

## Near-Term Task Queue

Use this queue to get to MVP quickly.

1. [x] Add persistent settings and recent projects.
2. [x] Add accessibility and language preferences: scale, high-visibility, density, reduced motion, TTS, locale.
3. [x] Add first-run setup shell with skip/resume and setup summary.
4. [x] Add global activity center and reusable operation-state model.
5. [x] Add loading/skeleton/detail-drawer UI primitives.
6. [x] Port PakFu archive interfaces and path safety.
7. [x] Add read-only folder/PAK/WAD/PK3 package browsing with visible loading/progress.
8. [x] Add text/image/binary metadata preview pane with summary/detail split.
9. [x] Add manual game installation profiles.
10. [x] Add project manifest and workspace dashboard with project health summary.
11. [x] Add compiler registry and executable discovery.
12. [x] Add CLI subcommand router, JSON output, and exit-code contract.
13. [x] Add `qbsp/vis/light` wrapper profiles (ericw-tools then, VibeMap2 now).
14. [x] Add ZDBSP or ZokumBSP wrapper profile.
15. [x] Add probe/compile wrapper profiles for Quake III (q3map2 then, VibeMap3 now).
16. [x] Add structured task logs, command manifests, and output-path reporting.
17. [x] Add package composition and compiler pipeline graphical summaries.
18. [x] Add editor profile registry and routed MVP presets.
19. [x] Add AI connector opt-in settings and provider-neutral automation design stub.
20. [x] Add MVP sample projects and CI smoke checks.
21. [x] Add portable packaging skeleton.
22. [x] Add About/Credits/license surface.
23. [x] Cut first MVP release candidate.

## Post-RC Task Queue

Work the release-candidate round exposed but did not finish. Each item has a
matching entry in the milestone or backlog section above.

- [x] Render model geometry instead of summarizing it: decode vertex/triangle data for the idTech model
  families and draw the mesh in the Models surface. Done for MDL, MD2 and MD3, and since 2026-10-08 for
  every idTech 1-4 format in [Native Model Formats](MODEL_FORMATS.md).
- [x] Link an audio playback backend and add transport, buffering, and playback state on top of the existing
  metadata and waveform preview. Qt Multimedia, optional at build time.
- [x] Add a dynamic-Huffman deflate encoder so ZIP/PK3 output is not limited to stored and fixed-Huffman
  blocks. The encoder picks the cheapest of the three block types per block; no command exposes the new
  `best` level yet.
- [x] Parse entity definitions (FGD, DEF, ENT) into a model that the entity inspector, map validation, and
  the CLI can share.
- [x] Detect missing textures by resolving map texture references against the textures in mounted packages,
  and surface the result in the map health panel.
- [x] Add in-place package overwrite, guarded by backups and an explicit confirmation, now that staged save-as
  is proven. Available from the shell's save-as and explicit CLI `--in-place`.
- [x] Add package compare between two archives, or between a package and its staged result. Archive against
  archive and staged review are available in the shell and CLI. Repeated WAD
  names read positionally; matching checksums do not skip payload verification.
  Archive and staged payloads now stream with side/path/byte progress and
  cancellation within a file. Generated-content manifest metadata also hashes
  in cancellable chunks; directory/plan/JSON scale acceptance remains open.
- [x] Add shared streaming package integrity validation to the GUI and CLI,
  with within-file cancellation, per-file hashes and explicit unchecked results.
- [x] Move package entry indexing/filtering onto a cancellable, coalescing
  worker and expose complete results through a native Qt list model with
  on-demand presentation, exact occurrences and Cancel/Retry. The native folder
  tree and composition now share worker-prepared relationships, path lookup and
  cached totals; folder actions retain exact revision/path identity during entry
  filtering. Remaining projection handoffs and full scale/native acceptance
  remain open.
- [x] Replace staging widget-item population with a native model sharing prepared
  operation/conflict vectors. Batched fixed-height change rows format visible
  text on demand, with full accessible/selectable details and exact Unstage/reveal
  identities. Complete layout still visits metadata; whole-shell timing and
  native acceptance remain open.
- [x] Cache archive/session totals and staged counts/before-after composition
  with their metadata snapshots. Summary preparation is cancellable and bounded;
  overflow is explicit in GUI/CLI and blocks oversized output without losing
  inspection/history. Exact decimal JSON totals avoid large-number precision loss.
  Aggregate independent-cache memory and full performance acceptance remain open.
- [x] Move package text/audio/model metadata previews onto one coalescing worker
  with streamed samples, progress, Cancel/Retry and exact-entry revision guards.
  Other studio preview handoffs and native acceptance remain open.
- [x] Stream exact planned drag-out and temporary authoring copies on a worker,
  with byte progress, cancellation, empty-folder preservation, bounded admission
  and batch ownership. Failed batches expose no handoff paths. Pending quota
  tokens now survive through UI acceptance; late Cancel/Close disposes the batch
  on a worker before verified quota release. Opening, staging and palette adoption
  also reject late cancellation, while committed save/extraction outcomes remain
  authoritative. Native drag and broader shutdown acceptance remain open.
- [x] Reserve initial copy payloads and file/entry/batch slots atomically across
  a studio window, expose live usage and limits, and share a strict default
  dry-run policy command through `package copy-limits`. Lower limits preserve
  existing copies; failed cleanup retains its charge. Native shutdown/drag
  acceptance remains open.
- [x] Register managed temporary-copy sessions with native ownership, bounded
  actual-usage review and explicit checksum-reviewed orphan discard through the
  GUI/CLI. Normal teardown drains cleanup; crashes leave reviewable copies.
  Native Mac/Linux, filesystem races and broad shutdown acceptance remain open.
- [x] Reserve initial temporary-copy bytes/files/entries/batches durably across
  cooperating processes using a physical store, including pending and crash
  reservations. Shared native coordination prevents overbooking; GUI/CLI policy
  review preserves existing copies and refuses stale writes. Initial counters
  include implied folders; later consumer growth and policy repair remain open.
- [x] Keep map saves and Code Save As outside the owning window's disposable
  package copies through a shared destination policy. Suggest durable map
  destinations and preserve edits on refusal or Cancel. Independent save and
  package staging remain separate actions.
- [x] Make plan replay, folder identity/rewrite, stable sorting and WAD assembly
  cancellable through existing save/snapshot workers. Publish complete derived
  caches only; preserve history and persisted folder fingerprints on retry.
  Plan/view quotas and atomic edit admission now have bounded scale probes.
  Helper allocations, repeated small-commit cost and remaining synchronous GUI
  preparation still need scale acceptance.
- [ ] Complete the [package manager release-candidate acceptance audit](plans/package-manager-release-candidate.md).
- [ ] Add fuzz targets for the new binary parsers: inflate, the idTech image decoders, and BSP/portal/leak
  inspection. Inflate, the image decoders and detector, BSP lump inspection, the package readers, the map
  loaders, and the model decoder are covered; the portal and leak point readers are not.

## Professional Level Editor Acceptance

Doom/Hexen node readiness now survives save/reopen by clearing obsolete derived
payloads, including the selected map's separate GL cache. Map Health, CLI
inspection, compiler output validation and launch preparation share bounded
structural checks for classic and extended/compressed nodes. Launch plans bind
the checked WAD hash and refuse changed files. DeePBSP and separate GL
cache validation, plus native runtime compatibility acceptance, remain open.

Connected Doom/Hexen X/Y reflection now shares geometry/heading transforms,
linedef orientation, exact undo, scene locks, worker cancellation and GUI/CLI WAD
persistence. Explicit connected selection reaches shared vertices without silently
expanding an edit. Partial attached detach/stitch and UDMF topology tools remain open; UDMF property authoring is implemented.

The full editor target is tracked in [Level Editor](LEVEL_EDITOR.md).
New map templates, repeated saves, source conflict detection, backups, owned
WAD snapshots, background recovery and matching CLI commands are implemented.
Patch authoring and convex brush component drafts now have GUI/CLI services;
their broader authoring and physical interaction acceptance remains open.
Materials/UV parity, linked scene instances, broader component tools and additional
primitives, production performance and native cross-platform acceptance remain.


UDMF property authoring now shares preview, package materials, exact undo,
scene locks, recovery, WAD persistence, CLI and node readiness. Real generated
Doom/ZDoom UDMF builds pass extended/compressed ZDBSP, package validation and
launch planning. Native UDMF move/rotate/mirror/snap/resize now preserve fractional
coordinates and source spans through the same history, worker and scene services.
UDMF thing duplication and 1–256-copy arrays retain unknown properties, comments,
fractional XYZ, scene groups and exact one-step history through GUI and CLI.
Polyobject control copies invalidate nodes; ordinary thing copies retain them.
Geometry duplication, topology creation/deletion, advanced effects, realistic performance and native
interaction acceptance remain open. See the [UDMF acceptance boundaries](LEVEL_EDITOR.md#lossless-udmf-property-authoring).
Binary Hexen polyobject control-angle semantics and thing-based node invalidation
still need parity with the UDMF control handling; full polyobject editing and
preview remain separate acceptance work.

## Module cohesion and format breadth

Implemented: shared format capabilities and texture import filters; portable
`.vibeworkspace` GUI/CLI persistence; DDS/FTX import and export, SWL import,
TIFF routing and PK4/PKZ aliases. The [PakFu comparison](ASSET_FORMATS.md#pakfu-comparison-and-remaining-work)
tracks SPAK/resources/encrypted PK3, more model geometry, IDWAV, cinematics and
binary inspectors. Each needs bounded parsing, fixture evidence, target-engine
validation and normal authoring/staging/build handoffs. Workspace v2 can add
native-editor references and per-surface layout/camera state after those editors
expose consistent capture/restore transactions; v1 does not serialize their
unsaved payloads.

## Quake III Native Animation

Quake III `animation.cfg` authoring now connects all 31 native slots, adjusted model ranges, loop tails, reverse playback and engine millisecond periods to linked assembly preview/bakes, source history/recovery and CLI. Native player packages now have a shared review and atomic publication service. Source-port dialects, Quake/Quake II game-code configuration and original-engine gameplay acceptance remain open. See [Native Animation](MODEL_ASSEMBLY.md#quake-iii-native-animation).

Per-part linked `.skin` files now share verified loose/package input resolution,
preview and bake materials, reference rebasing, undo/recovery, protected exports
and CLI. Schema 3 retains exact package occurrences with path guards and optional
native animation. The ordinary browser/level-instance runtime skin selector,
team player variants and custom player sounds remain open. See
[Linked Skins](MODEL_ASSEMBLY.md#linked-skins).

The assembly **Player Package…** workflow publishes a reviewed non-team Quake III
player PK3 through the normal package writer, retaining every native pose and
tag, skin assignments, configuration, converted icon and dependency closure.
CLI review/export and portable package drafts share the service. No release gate
is closed by this implementation; see [native player packages](MODEL_ASSEMBLY.md#native-player-packages)
and [the remaining modeller acceptance work](MODELLER_RELEASE.md).
