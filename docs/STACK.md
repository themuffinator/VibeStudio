# Technology Stack

3D rendering decision (2026-10-08): every 3D view and material preview draws on
the GPU through VibeStudio's own frame layer (`core/render_device`), with an
OpenGL backend (3.3 core or ES 3.0 through Qt's `QOpenGLContext`) and a Vulkan
1.0 backend (the loader opened at run time; declarations from the pinned
Khronos Vulkan-Headers). The user picks Automatic, OpenGL or Vulkan in
Settings, with `render set`, or for one run with `--renderer` or
`VIBESTUDIO_RENDER_BACKEND`. The CPU 3D rasterisers (`app/model_rasterizer`
and the material renderer's) are removed; 2D views keep `QPainter`. QRhi was
declined because it cannot render under the offscreen platform the tests use
or in the console-only CLI; bgfx stays declined (a large dependency for
backends the studio does not need). Shaders are written once in GLSL and
compiled offline to SPIR-V by `scripts/build_render_shaders.py`; no shader
compiler or Vulkan SDK is needed to build. Without a working backend a 3D view
says why and draws nothing.

Recording comp decision (2026-10-06): original C++ snapshot planning validates
queued pass-local cuts and explicit after-cut linear crossfades. Batch journal
reads avoid rescanning a take for every section. Qt Widgets review and version-3
CLI review share the planner/importer; native v7 clips, fades and source
metadata retain the result. No dependency or playback backend changes. Dedicated
alternate-take lanes remain separate work.

Recording audition decision (2026-10-06): prepare a temporary reviewed import
with the existing C++ importer, and remove backing descriptors for isolated
playback. Reuse the compensated session worker and its device shutdown
acknowledgements for native Qt audition; CLI preview streams the same span
through the guarded float32 WAV writer. No device/backend dependency, native
schema or permission requirement is added.

Recording review persistence decision (2026-10-06): retain the established
bounded v1/v2/v3 JSON as a portable editable recipe, with relative recording
paths and SHA-256 output conflict guards. Original C++ read/verify/save services
use the shared atomic publisher; Qt workers and CLI save-review reuse them.
No new dependency or native session schema. Session-embedded take/comp revisions
remain open.

Session metering decision (2026-10-06): original C++ fixed-capacity processors
tap the existing renderer for pre/post sample peak, RMS, maxima, over-range
counts and phase correlation. Live Qt playback and offline GUI/CLI analysis
share those taps. Native Qt item views and style-drawn level bars supply the UI;
no dependency or document-format change. This adds sample-peak meters, while
existing libebur128 waveform loudness analysis remains separate.

Audio range-editing decision (2026-10-06): original C++ interval transforms
operate on clip descriptors and bounded automation arrays. Native v7 adds
original-domain interpolation windows so linear/smooth cuts preserve sampled
values without curve fitting. Qt Widgets and CLI share the transactional
service and renderer. No library, device backend or external code is added;
tempo/meter maps remain independent of sample-time range edits.

Camera resize decision (2026-10-06): use original double-precision C++ box/ray
math and Qt Widgets face handles over the existing shared CPU renderer.
Immutable worker snapshots preview affine brush/patch geometry and translate
point-owned visuals. Release reuses the cancellable map placement service,
texture policy and undo. No renderer, dependency or CLI architecture changes.
Doom shared-topology previews and live unlocked UV projection remain separate
work; see [camera resizing](LEVEL_EDITOR.md#camera-selection-resizing).

Arrangement decision (2026-10-06): original `core/audio_arrangement` C++
services supply transactional clip selection/group operations to Qt Widgets and
CLI. Native v6 stores named links and fade-domain windows; older sessions retain
empty defaults. Splits evaluate the original envelope without sample rewriting.
No external code, library, rendering backend or device change is introduced.

Musical timing decision (2026-10-06): original `core/audio_tempo` C++ services
prepare bounded tempo/meter segments, integrate fractional frame durations with
compensated accumulation and share conversion/grid rules between Qt Widgets and
CLI. No new dependency or audio callback work is introduced. Sessions retain
sample-anchored media/automation; native version 5 adds timing maps, reading
versions 1–4 with constant defaults. Tempo ramps, metronome, musical media anchors
and MIDI/clock synchronization remain separate work.

Audio duplex engine decision (2026-10-06): retain the existing C++ routing graph
and add an explicit physical-clock rendering mode for live input. A prepared
device-neutral punch processor maps ADC/DAC timestamps to integer capture frames;
a bounded single-producer/single-consumer ring hands complete armed-channel sets
to storage. Existing playback/export keeps compensated rendering. Native formats
remain unchanged. The optional private PortAudio backend is pinned at
`873e3c83fbe2f57ebcf59083e627a3f8fa051ffe` with GPLv3-compatible permissive notices,
hash-checked build-time WASAPI/CoreAudio fixes and a C++ device adapter. Meson
selects WASAPI, CoreAudio or ALSA; `audio_duplex=disabled` retains device-free
operation. Callback/selection/lifecycle fixtures share the production adapter.
Recording controls and disk-worker grouping are implemented. Fixed-size dry-input
and pre-clamp output readings reuse the session meter engine and bounded telemetry;
reset epochs preserve callback ownership without changing capture. No additional
dependency or durable format is introduced. Physical acceptance remains pending;
[the integration contract](AUDIO_DUPLEX.md) records the remaining work.

Audio loop recording decision (2026-10-06): keep a monotonically advancing
device/DSP clock and wrap only authored media/automation time. Bounded loops
retain insert, monitoring and routing-delay state; automation wraps after each
path's latency correction. Continuous arm journals retain existing `.vstake`
and receipt formats. Canonical version-2 recording plans add the finite pass
count; version-2 reviewed imports add one-based pass selection with local ranges.
Single-pass version-1 files remain supported. No new dependency is introduced.

Audio processing latency decision (2026-10-06): native processors report fixed
algorithmic latency separately from creative delay. An original C++ longest-path
routing plan aligns main outputs and pre/post sends at bus/master merges; each
solo domain owns its delay history. Fresh ranges prime preallocated processing
buffers and retain their authored frame count. Automation and modulation use
the corresponding signal time. The original stereo-linked lookahead sample-peak
limiter adds a structural 0…20 ms lookahead parameter; changing it uses the normal
staged edit and playback restart. Effect and compensation state share 128 MiB.
No library, plugin SDK, device API or native-format revision is introduced.
True-peak limiting, plugin hosting and hardware/monitoring latency remain open.

Audio automation decision (2026-10-06): retain original C++ sample-clock
processing and Qt Widgets. A shared curve model drives native controls, a QPainter
preview and CLI edits. Prepared effects reserve variable delay/reverb taps from
bounded lane endpoints; `.vssession` v4 stores session curves separately from
static `.vsfx` recipes. This adds no runtime dependency or plugin SDK.

Native model acceptance decision (2026-10-05): keep counter-clockwise editable
faces and convert MDL/MD2/MD3 clockwise winding at format boundaries. An optional
original QuakeC/Python fixture invokes unmodified FTE dedicated-server/FTEQCC
tools as separate processes after real CLI/compiler export. A second fixture
uses the unmodified FTE client's EGL pbuffer with external Mesa llvmpipe and
Pillow to verify engine-written screenshots, native format identities and
deliberately broken controls. It adds no linked engine, studio renderer or
mandatory dependency. See [scope and findings](MODEL_ENGINE_ACCEPTANCE.md).

Model collision decision (2026-10-05): keep source-only oriented boxes in the
existing C++ mesh document; reuse the shared transform, level hull and placement
services. Qt Widgets provides authoring controls, existing workers handle fitting,
export and map preparation, and the software viewport draws snapshot-aligned
inspection outlines. No physics library, renderer or compiler linkage is added.
Viewport, numeric and CLI transforms share a validated box transform: world
translation/rotation and local-axis scaling preserve an oriented box without
shear, using the existing gizmo lifecycle and document history.
GUI/CLI target conventions and limits are in [Model Collision](MODEL_COLLISION.md).

Animated collision decision (2026-10-06): optional per-frame box tracks remain
in the shared mesh document, using source schema 7, bounded history and recovery.
Existing pose interpolation provides linear centres/sizes and Qt quaternion
orientations. Frame operations remap/generate tracks, and GUI/CLI use the same
candidate validation. Map handoff samples an explicit stored pose as static
brushes. No physics dependency or runtime dynamic-collision writer is added.

Model free-rotation decision (2026-10-06): retain the C++ transform service and
Qt Widgets/software viewport. A pure virtual-sphere solver captures camera and
transform bases, snaps the shortest-arc angle and emits existing XYZ transform
values. The Rotate gizmo adds a labelled free handle and dashed circle; mesh,
tag and collision commits reuse current validation, frame scope and history.
Numeric GUI/CLI transforms reproduce the result. No dependency or source schema
change is required.

Doom node readiness decision (2026-10-05): use a shared C++ structural validator
and the existing cancellable RFC 1950 decoder. Map loading caches immutable
reports; geometry serialization clears obsolete derived records so readiness
survives a plain WAD reopen. Compiler wrappers and launch planning inspect one
bounded WAD snapshot; Qt workers keep GUI preparation and pre-launch hashing
off the UI thread. No additional dependency or linked compiler is introduced.

Prepared-build decision (2026-10-05): keep orchestration in the existing C++
compiler pipeline. A shared workspace service serializes the current map and
streams the package reader into independent files with a SHA-256 inventory.
Qt Widgets workers provide GUI preparation; a modular CLI adapter uses the same
service. Quake III receives explicit filesystem flags. Quake/Quake II now use
the same service with isolated VibeMap2 paths, captured WAD2 or validated WAL assets,
and PAK publication including Quake runtime lighting. No new library or compiler
fork is introduced; Doom prepared layouts remain open. Quake-family deployment reuses
the package publisher and game launch planner, with one-operation installation
permission and an expected destination hash checked under the save lock. GUI
review and the modular CLI share the same deployment service; launch explicitly
requests windowed mode and follows verified package publication.
Classic deployment adds a bounded numbered-PAK planner, per-slot JSON receipts
and a game-folder `QLockFile`. Quake's consecutive numbering and Quake II's
0–9 scan are shared by GUI/CLI; `QSaveFile` records slot metadata after package
commit. A complete deployment review hash supports CLI automation. No new
library is introduced. Receipt and archive publication remain separate commits.
Output publication uses a shared C++ receipt service and the existing package
reader, subset staging, deterministic writer and atomic backup publisher. Qt
`QLockFile` excludes simultaneous studio writes; `QSaveFile` records build state.
Workers handle GUI hashing/review/publication, with an exact receipt hash guarding
stale reviews. Standard generated shaders and external TGA lightmaps are supported;
previous output history is retained without automatic pruning.

Connected Doom reflection decision (2026-10-05): keep vertex-graph expansion in
`level_doom_selection`, reuse native transform history for vertex and linedef
endpoint deltas, and route GUI/CLI reflection through the cancellable placement
service. Retain binary side ownership and WAD preservation; require explicit
selection expansion for attached geometry. No new library or compiler linkage.

Placement decision (2026-10-05): reuse the C++ native transform/UV services to
prepare insertion commands and independent snap offsets. Qt Widgets and existing
worker/package-preview services provide offset dialogs. GUI/CLI share format,
bounds, scene-lock and history rules. No dependency or compiler change is needed.
The `level_placement` service adds scoped cancellation and phase progress around
private candidates. Quick GUI placement also uses a Qt worker, with a delayed
progress window and guarded publication; CLI remains a synchronous caller of
that same service. Existing geometry and UV validation remain mandatory.

Scene-organization decision (2026-10-04): use document-owned C++ state, the existing
Qt Widgets tree/forms, shared CLI services and native-map metadata. No dependency
is added. Source-bound base64 JSON lives in a Quake EOF comment or Doom/Hexen
`VS_SCENE` map sidecar; serializer emission maps and undo snapshots preserve object
membership. Visibility filters drawing/picking, preserving native compiler and
package geometry. See [Level scene organization](LEVEL_SCENE.md).

Scene-lock decision (2026-10-05): keep enforcement in the shared C++ authoring
services, using scoped copy-on-write candidates and native-record comparisons.
No new library is required. Version 2 scene metadata adds inherited editing
locks; version 1 loads unlocked. Scene locks preserve compiler/package content
and do not freeze shared asset bytes or prevent history replay.

Working-import lock decision (2026-10-04): keep Qt `QLockFile` admission and
session ownership. Add a bounded, explicit recovery adapter that requires
reviewed file identity/content and native exclusion, using Windows sharing
and handle deletion or macOS/Linux `flock`. Unsupported exclusion refuses
release. Qt cleanup uses a dedicated pool drained before application teardown;
GUI and CLI share recovery services. No runtime dependency or schema change.
Network filesystems and native macOS/Linux remain acceptance gates.

Map-loading decision (2026-10-04): use the existing Qt thread and Widgets stack
for cancellable Open/Reload. Shared core callbacks and a value geometry cache
keep parsing, validation, CLI behavior and viewport geometry aligned. A joined
worker publishes one document; no new concurrency library, dependency, format
or CLI option is introduced. GUI adoption and native filesystem latency remain
separate performance limits. See [Level Editor](LEVEL_EDITOR.md#background-map-opening).

Doom camera decision (2026-10-04): retain the shared C++/Qt Models renderer and
worker pipeline. Original scanline sector decomposition and bounded Qt texture
composition provide floors, ceilings and classic Doom wall materials without
new libraries or a node-builder dependency. GUI material editing, dependency
review and CLI diagnostics share namespace/occurrence evidence. Engine rendering
effects and portable Doom dependency subsets remain explicit gaps.

Saved-draft storage decision (2026-10-04): retain Qt/C++ shared services and add a
small guarded native directory-lease adapter (`CreateFile` sharing on Windows,
`flock` on Unix) so no-write readers can exclude maintenance without creating lock
files. Unsupported exclusion refuses maintenance. GUI/CLI use the same quota,
review and compaction services; no draft schema or runtime library changes.
Native macOS/Linux and network-filesystem acceptance remain open.

This document defines the preferred implementation stack for VibeStudio. The
goal is a seamless, modern, cross-platform, user-friendly, adaptable, and
efficient development studio for idTech1, idTech2, and idTech3 projects.

The stack should stay boring where boring is valuable: stable desktop UI,
predictable builds, explicit data formats, testable services, and clear
licensing. Specialized libraries are introduced where they materially improve
rendering portability, source editing, media handling, search, or automation.

## Stack Summary

Curved-surface authoring stays in C++20/Qt Widgets: `PatchControlView` uses
QPainter, standard Qt tables expose point coordinates, and the existing
`ModelViewport` renders the tessellated UV checker. Shared core services handle
patch validation, subdivision, persistence and CLI operations. No rendering
library or new dependency was introduced. See [Level Editor](LEVEL_EDITOR.md).

| Area | Choice | Status | Rationale |
| --- | --- | --- | --- |
| Primary language | C++20 | Active | Fits idTech-era native tooling, Qt, compilers, binary formats, and high-performance editors. |
| Application framework | [Qt 6](https://doc.qt.io/qt-6/) | Active | Mature cross-platform desktop framework with UI, networking, settings, processes, models, threading, and deployment support. |
| Primary UI | [Qt Widgets](https://doc.qt.io/qt-6/qtwidgets-index.html) | Active | Best fit for dense production tools, dockable panes, model/view data, custom inspectors, and native desktop behavior. |
| Meta-object system | `Q_OBJECT` plus Meson's `qt6.preprocess` moc step for signal-emitting app classes | Active | `Q_OBJECT` supplies per-class `tr()` contexts and custom signals/slots. Dialogs that reuse standard widget signals may use explicit `QCoreApplication::translate` contexts without adding moc requirements, as in the model designer and dependency browser. Core stays moc-free, links Qt Core and Gui only, and uses explicit translation contexts. |
| Shell UI primitives | Reusable Qt Widgets loading panes, detail drawers, and shared shell semantics | Active | Shared shell components now cover operation state, progress, reduced-motion loading placeholders, collapsible details for logs, metadata, manifests, raw diagnostics, non-color status chip semantics, shortcut metadata, and command-palette entries. |
| Shell look and feel | Fusion style, an application `QPalette` and stylesheet generated from design tokens (`src/app/studio_theme.*`), and a small `QProxyStyle` for check and radio indicators | Active | Chosen over the platform styles, which ignore a custom palette differently on each OS, and over hand-written per-widget stylesheets. One token set per theme drives every widget, dialog, menu, and dock; the default dark theme takes its visual language from idStudio. No new dependency. |
| Shell icons | Vector glyphs painted with `QPainter` through a custom `QIconEngine` (`src/app/studio_icons.*`) | Active | Chosen over image assets, which need a design pipeline and QtSvg for crisp scaling, and over the platform style's standard pixmaps, which were invisible on dark themes. Glyphs recolour with the theme at paint time and scale with the text-scale preference. |
| Shell layout parts | `ModeRail`, `PageHeader`, `EmptyStateView`, `CardFrame`, `DockTitleBar`, `ElidedLabel`, and factory helpers in `src/app/studio_layout.*`; `QDockWidget` for the Activity, Inspector, and Assistant panels, docked on the trailing side by `src/app/studio_docks.*` | Active | Every work surface is assembled from the same parts, so pages share one anatomy and one set of object names for the stylesheet. `QMainWindow` keeps dock areas by side in every layout direction, so the saved window state is stored in left-to-right terms and mirrored for right-to-left sessions by swapping the left and right dock area records in the bytes `QMainWindow::saveState()` writes. That format is Qt's own and undocumented, read as Qt 6.10.1 writes it; bytes it cannot read are restored as they are, so a future change in Qt costs the mirroring, never the saved arrangement. |
| Rich animated surfaces | [Qt Quick/QML](https://doc.qt.io/qt-6/qtquick-index.html) | Planned, bounded | Use for contained high-value surfaces only, such as onboarding, visual status views, or graph-like experiences. Do not rewrite the shell around QML without a migration plan. |
| Build system | [Meson](https://mesonbuild.com/) + [Ninja](https://ninja-build.org/) | Active | Fast, readable, cross-platform, and suitable for CI. |
| Automation | Python scripts + GitHub Actions | Active | Good fit for validation, release helpers, documentation checks, and CI orchestration. |
| Project manifests | JSON | Active | Human-readable `.vibestudio/project.json` manifests store roots, folders, timestamps, selected install IDs, project-local overrides, compiler executable overrides, and registered compiler outputs. |
| User settings | Qt `QSettings` plus project overrides | Active | Application shell settings, recent projects, recent terminal activity, editor profile selection, accessibility/language preferences, installation profiles, and project-local overrides are active. |
| Accessibility | [Qt Accessibility](https://doc.qt.io/qt-6/accessible.html), OS accessibility settings, accessible custom widgets | Active/planned | Shell preference storage and accessible control metadata are active; deeper workflow audits and custom-widget coverage are planned. |
| Scaling | [Qt High DPI](https://doc.qt.io/qt-6/highdpi.html), layout-driven UI, app text scale preferences | Active/planned | Shell text scale presets are active; broader high-DPI and layout smoke coverage is planned. |
| Text to speech | Platform speech engines: Windows Speech API (SAPI 5 through COM), macOS `say`, Speech Dispatcher or eSpeak NG | Active | `app/studio_speech` speaks task outcomes, status messages, Read Aloud, and the test phrase with the engine the OS provides; nothing is linked beyond `ole32` on Windows, and Unix engines run as separate programs. [Qt TextToSpeech](https://doc.qt.io/qt-6/qttexttospeech-index.html) remains a candidate backend but is not used, since it is not part of every Qt install and could not be verified here. |
| Localization | [Qt internationalization](https://doc.qt.io/qt-6/internationalization.html), Qt Linguist, `lrelease` at build time, `QTranslator` at run time, `QLocale` | Active | Runtime loading landed this round: `i18n/meson.build` compiles each checked-in `.ts` catalog to a `.qm` with `lrelease`, and `installStudioTranslations` resolves and installs the catalog with `QTranslator`, falling back from the exact locale to the base language to the source language and applying layout direction per locale. `lrelease` is optional, so a toolchain without it still builds and simply runs in the source language. Locale preference storage, pseudo-localization, RTL smoke, `QLocale` formatting, pluralization and expansion samples, stale/untranslated reporting, and dry-run `lupdate` validation remain active; finished translations are still seed catalogs. |
| Asset index/search | [SQLite](https://sqlite.org/) through [Qt SQL](https://doc.qt.io/qt-6/qtsql-index.html), with [FTS5](https://sqlite.org/fts5.html) where available | Planned | Lightweight local database for project metadata, dependencies, search, diagnostics, and recent activity. |
| CLI parser | Lightweight Qt `QStringList` router with in-process command registry; [CLI11](https://github.com/CLIUtils/CLI11) deferred | Active | Current router keeps project/package/install/asset/map/shader/sprite/code/extension/compiler/AI/credits subcommands dependency-free with JSON output, quiet/verbose/watch/task-state switches, stable exit codes, and testable command metadata through `cli commands`; CLI11 remains deferred until shell completion and broader validation justify the dependency. |
| Level compilers | VibeStudio's own VibeMap2 (derived from ericw-tools) and VibeMap3 (continuing q3map2 from NetRadiant Custom), plus ZDBSP and ZokumBSP, as Git submodules run as separate processes | Active (integration Partial) | Owning the Quake-family compilers lets fixes land in the compiler instead of only in wrapper workarounds, while process execution keeps every compiler outside VibeStudio's binaries. Profiles, discovery and pipelines target them; no VibeMap2/VibeMap3 end-to-end proof has run inside VibeStudio yet. See [Compiler Integration](COMPILER_INTEGRATION.md#vibestudio-compilers). |
| Task execution | Qt `QProcess`, threads, signals, and a VibeStudio task model | Active/planned | The reusable operation-state model, shell activity center, compiler process runner, captured logs, cancellation plumbing, and run manifests are active; broader thread-pool/future integration is planned. |
| External change detection | `QFileSystemWatcher` hints plus authoritative fingerprint polling in `src/core/document_watch.{h,cpp}` | Active | Added this round. The open map, package, and code-editor file are registered by role; a SHA-1 content fingerprint decides what actually changed, and filesystem notifications are treated only as a reason to re-check. The class declares no `Q_OBJECT`, so core still needs no moc, and the shell drives `poll()` from a timer it already owns. |
| Package/archive layer | PakFu-derived C++ services plus focused format readers and deterministic writers | Active | Package/archive interfaces, virtual path safety, read-only folder/PAK/WAD/ZIP/PK3 entry readers, text/image/model/audio/script metadata previews, safe extraction reports, staged write-back, package manifests, and deterministic PAK/ZIP/PK3/WAD save-as writers are active. `src/core/package_compare.{h,cpp}` added entry-by-entry comparison of two packages, or of a package against a staged plan, this round. |
| Package publication | Qt Core `QTemporaryFile`, `QSaveFile`, `QLockFile`, SHA-256 and JSON recovery journals | Active | `core/package_publication` reserves output exclusively, verifies bytes, atomically publishes new files without overwrite or replaces an existing package without first moving it away, and preserves an independent original copy. Bounded GUI/CLI folder discovery finds journal metadata; selected recovery verifies content, reports cancellable progress and finishes backup publication using a reviewed journal checksum. Actual package saves remember a bounded output-folder history before writing. No direct-write fallback or new dependency. Cooperating saves are serialized; external writers, power loss and network filesystems remain release-audit boundaries. |
| Package source identity | Qt Core SHA-256 and a verified read-only `QIODevice` in `core/package_content` | Active | Full-file and 64 KiB chunk hashes detect changed inputs even with preserved size/timestamps. GUI opening, file staging and installation palette reads use cancellable workers. Package metadata previews use one coalescing Qt worker with immutable exact-entry requests, streamed sampling, Cancel/Retry and stale-result rejection. Hashes cost 32 bytes per chunk; payloads remain on disk. Archive/folder opens cap aggregate fingerprints at 64 MiB, alongside 250,000 records/implied folders, 64 MiB logical index metadata and 128 path components. ZIP central records stream through an independent verified chunk cache; directory parsing and preparation expose cancellation/progress. Combined sessions share those budgets across at most 64 layers, including hidden records and relocated paths; multi-folder map texture lookup uses the same admission. Staged documents now separately admit 256 MiB of retained generated bytes and 128 MiB of payload hashes across base, operations and history; draft objects share hashes by content identity. Retained document metadata also admits 1,000,000 logical records and 128 MiB of index/text across base, operations and history; Undo/redo slots stay reserved and draft metadata is checked before payload reads. General reader snapshots and staged folder projections now share 250,000-record/64 MiB text/128-component admission with cancellable preparation, stable entry indexes and explicit GUI/CLI refusal. Known archive wrappers freeze/flatten their backing. Internal plan construction, per-edit projection admission, remaining synchronous view preparation and process-wide accounting remain separate work. This adds no dependency and does not provide a filesystem snapshot or persistent editing storage. |
| Temporary package copies | `core/package_copy`, shared streamed extraction and Qt workers | Active; native acceptance pending | Exact planned selections, bounded batch admission, empty folders, verified publication and owned temporary lifetimes serve drag-out and disk-based authoring handoffs. Cancel or failure exposes no handoff paths. Preparation keeps quota tokens pending through the UI adoption decision; a worker then discards the batch or commits its reservations. Completed saves and extraction keep authoritative output reports. Shared `core/package_copy_budget` reservations cap initial per-window bytes/files/entries/batches, including pending work; the GUI and strict `package copy-limits` CLI share policy settings. Failed cleanup stays charged. Managed `core/package_copy_store` sessions reuse native directory ownership, bounded metadata review, explicit orphan discard and a dedicated cleanup pool. GUI and strict CLI review share the service; both discard modes exclude live owners. The same native service serializes durable cross-process reservations on a separate coordinator directory; checksummed version-2 records retain pending, completed and failed-cleanup charges through crashes. The physical store owns a checksum-reviewed shared policy, exposed by the GUI and `package copy-store-limits` without preference migration. Native drag, later consumer growth and broader shutdown acceptance remain open. No new dependency. |
| Package editing documents | `core/package_draft`, immutable content-addressed files, atomic Qt metadata commits, and bounded delta history | Active, incomplete | Portable `.vibepackage` directories preserve source content, staged operations and 256 undo/redo groups. GUI workers and draft CLI routes share verification and publication. Metadata is capped at 32 MiB/250,000 records. The shared planned-view adapter supplies browsing, previews, validation and extraction, including conflict inspection and exact row previews. Version 2 drafts retain ordinal-based edits; version 1 is readable. Source-free new documents and atomic folder edits share core services across GUI/CLI. Draft dry runs stream/hash the same payloads without writing. Live file imports stream verified independent temporary copies whose shared owners retain undo/worker readers and queue cleanup on a dedicated Qt pool drained before application exit; read-only CLI plans explicitly avoid temporary writes. Managed working sessions now reserve bytes and file slots before copying, retain live-reader leases, and expose configurable limits, bounded usage review and explicit crash-orphan discard through shared GUI/CLI services. Automatic local checkpoints reuse this format with session leases, a coalescing Qt worker, metadata inventory, verified restore and guarded discard. Post-commit compaction reclaims unreachable recovery objects. Shared storage scans bound recovery bytes/copy count, including temporary writes; reviewed storage fingerprints permit incomplete-copy cleanup without recursive deletion or automatic eviction. Saved drafts now have per-directory byte/file limits and reviewed unused-object compaction. Native directory leases preserve document/history/worker readers without creating files. Exact occurrence subsets now use core/package_selection and core/package_subset with immutable reader indexes, WAD group expansion and a shared GUI/CLI review. The shared core/package_wad_groups scanner also supplies fingerprint-bound inventories and atomic map/GL renames or complete group deletions, using existing history/draft operations and a worker-backed GUI review. New and opened WAD documents save their reviewed positional plan, including source-free drafts and subsets. Dependency rewrites, broader performance and native/filesystem verification remain release gates. Uses Qt Core and guarded operating-system APIs; no new bundled dependency. |
| Package browser | Qt Widgets list/tree models and a coalescing Qt worker | Active; broader scale/native acceptance pending | `core/package_browser` indexes admitted metadata and filters with shared query semantics and cancellation. `app/package_entry_view` exposes all matches through uniform batched Qt layout, on-demand roles, exact indexes, progress, Cancel/Retry and stale-result rejection. The same worker caches sorted folder relationships, path lookup and composition totals; `app/package_folder_view` exposes native tree roles on demand. Folder edits use revision-bound tree identity while entry filtering is pending. `app/package_staging_view` shares prepared operation/conflict vectors, uses batched fixed-height change rows and formats visible presentation on demand; wrapped overview/composition and a resizable full-details pane preserve review. Exact IDs/occurrences drive actions. Other projection handoffs, whole-shell latency and process-memory totals remain open. Uses existing Qt Core/Widgets; no new library or CLI command. |
| Package extraction | Shared positional streaming in `core/package_extraction`, Qt temporary files and owned GUI worker | Active | Stored/DEFLATE and staged inputs stream without whole-file buffers. Namespace preflight rejects collisions, links and source destinations; files publish atomically after content validation. Cancel discards the current file and preserves completed files. No new dependency. Planned snapshots support exact entry selectors and relative output mappings, reviewed in a Qt GUI table or supplied through the CLI. |
| Compression codec | In-tree DEFLATE in `src/core/deflate.{h,cpp}`; zlib and miniz declined | Active | Chosen this round over adding a third-party codec. Reading real PK3s requires inflate, and writing them well requires deflate, but a bundled or system compression library costs a packaging story, a license entry, and a platform matrix on every target. The implementation follows RFC 1951, RFC 1950, and the ZIP appnote's CRC-32, is bounds-checked against hostile input, and is deterministic so archives reproduce. The encoder gained dynamic-Huffman blocks and a `DeflateLevel::Best` level this round: every level except `Store` now measures a stored, a fixed-Huffman, and a dynamic-Huffman encoding of each block and keeps the smallest, so a block is never larger than storing its bytes would be. Entries that would not shrink are stored verbatim instead. |
| Level-map services | Native C++ parser/editor model over Doom WAD lumps and Quake-family `.map` text | Active | Provides shared GUI/CLI map inspection, entity/texture/statistics/validation surfaces, map creation, undo/redo, worker-based atomic saves with backups and source conflicts, local recovery checkpoints, and compiler profile handoff without adding a rendering dependency yet. |
| Entity definitions | `src/core/entity_definitions.{h,cpp}` reading Radiant `.def`/`.qc`, Valve `.fgd`, and Quake III `.ent` text | Active | Added this round. A classname alone tells the studio nothing, so the catalogue supplies key types, defaults, spawnflag bit names, sizes, colours, and base-class inheritance, and `validateLevelMapEntities` checks a map against it. No game's definitions ship with VibeStudio; the parser reads whatever the user points it at. |
| Advanced Studio services | Native C++ services for shader scripts, sprite workflow plans, code indexing, extension manifests, and staged AI creation proposals | Active | Provides shared GUI/CLI coverage for idTech3 shader graph data, stage edits, mounted texture validation, Doom/Quake sprite planning, source tree diagnostics, extension trust/sandbox command plans, and reviewable prompt-to-creation workflows without new dependencies. |
| 2D editor rendering | Custom `QWidget` subclasses painted with `QPainter` | Active | Chosen this round over Qt Graphics View and over an early GPU backend. The map viewport, image and palette views, waveform view, and the composition/pipeline/timeline charts are all hand-painted widgets, so 2D rendering needs no Qt module beyond Widgets and no third-party renderer. |
| Texture authoring/export | Bounded native C++ document/profile services and Qt Widgets | Active | Layers, checksummed projects/recovery, CPU paint/transforms and nine GUI/CLI output profiles share validation and guarded publication. Original native encoders generate previewable indexed mips; explicit indexed PNG uses existing Qt compression/core CRC. Optional external Pillow verifies raster fixtures; it is not bundled or required. No new runtime library or rendering backend. WAD2/Doom namespace staging, CLI package drafts, level/model material handoffs and external compiler acceptance use existing services. Project preparation is cancellable; final publication rechecks destination identity. Release evidence and limits are tracked in [Texture Editor](TEXTURE_EDITOR.md). |
| Headless map rendering | Deterministic SVG generated as text by `src/core/map_render.cpp` | Active | Chosen this round so a map picture is available from the CLI, from generated documentation, and from CI without a display or a GUI session. It is pure string generation, shares `map_geometry` with the painted viewport, and produces byte-identical output for the same input. |
| 3D rendering | Own frame layer (`core/render_device`, `core/render_opengl`, `core/render_vulkan`) with OpenGL 3.3 core / ES 3.0 through `QOpenGLContext` and Vulkan 1.0 through the run-time loader and the pinned [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | Active | Chosen 2026-10-08, replacing the CPU rasterisers. A frame is plain data (targets, passes, draws, uploads, read-backs) rendered offscreen on the backend's own thread and read back, so widgets keep presenting with `QPainter` under their overlays, and the GUI, the CLI and tests share one path. Choice: Automatic (Vulkan then OpenGL; OpenGL first on macOS), OpenGL or Vulkan, saved in Settings and overridable per run. GLSL sources in `src/core/shaders`, compiled offline to SPIR-V and checked as OpenGL text. Qt's offscreen platform and the console-only CLI offer Vulkan only. |
| Material previews | Engine rules per engine in `core/material_render*`, drawn by GLSL shaders on the 3D renderer | Active | Chosen 2026-10-08. What each engine computes per vertex and per stage (Quake III waves and texture-coordinate modifiers, Doom 3 expressions, Doom light levels, Quake light styles) is worked out on the CPU; everything per pixel (stage blending, alpha tests, depth functions, sky boxes and clouds, fog, Doom 3 interactions, Doom colormap lighting, warps) runs in shaders with exact texel fetches, so OpenGL and Vulkan agree, and both match the former CPU renderer to within two levels apart from isolated pixels at texel and triangle edges. |
| Long-term 3D rendering | [bgfx](https://bkaradzic.github.io/bgfx/overview.html) | Declined 2026-10-08 | The own OpenGL/Vulkan layer covers every 3D view the studio has. bgfx would add a large dependency and Direct3D and Metal backends nobody needs yet; a native Metal or Direct3D backend can join `RenderDevice` later if a platform requires it. |
| Text editing | [`QSyntaxHighlighter`](https://doc.qt.io/qt-6/qsyntaxhighlighter.html) with data-driven language rules; [KSyntaxHighlighting](https://api.kde.org/frameworks/syntax-highlighting/html/index.html) and [Tree-sitter](https://tree-sitter.github.io/tree-sitter/) deferred | Active | Chosen this round. `StudioSyntaxHighlighter` builds its rules from `StudioLanguageDescriptor` records, so plain text, config, idTech3 shader scripts, QuakeC, `.map` source, entity definitions, INI-style key-value files, and JSON are described as data rather than as widget code, and a new language is a new descriptor. Colours come from the active studio theme so high-contrast stays readable. KSyntaxHighlighting and Tree-sitter remain deferred until packaging cost and incremental-parsing value justify the dependencies. |
| Language services | Native C++/Qt Core stdio LSP client using `QProcess` | Active, bounded | Explicit local connections provide live synchronization, diagnostics, formatting, semantic completion, Quick Info, Parameter Hints, definitions, references, reviewed symbol rename and code actions through shared GUI/CLI services. UTF-16 positions, bounded parsing, version checks, cancellation and shutdown are enforced. Rename and code actions share Search Results and the project replacement writer, preserve open-document Undo and guard CLI writes with the whole plan's hash. Completion resolves deferred metadata/imports before acceptance; completion/formatting share document undo and save. Quick Info and signature documentation share resource-isolated Qt rendering; hints use native inline overload controls. No added library or bundled server. Code actions include lazy edit resolution; server commands remain unsupported. See [Local Language Services](LANGUAGE_SERVICES.md). |
| Audio | Native WAV/DMX, pinned dr_libs/Xiph compressed decoding, optional [Qt Multimedia](https://doc.qt.io/qt-6/qtmultimedia-index.html) playback | Active, release verification in progress | The [Audio Editor](AUDIO_EDITOR.md), `asset audio-edit`, and `asset audio-export` share bounded float processing, MP3/FLAC/Vorbis import, integer/float WAV precision, optional dither, markers and Doom/Quake-family delivery. Doom DMX joins WAD staging. Direct decoder APIs preserve native rate/channels without miniaudio's unused device/mixer layers; no runtime codec install is needed for editing. Compressed browser audition still uses Qt. Pinned r8brain-free-src 7.5 provides anti-aliased resampling. Standalone capture and Record Tracks are available; the optional pinned PortAudio backend supports explicit duplex devices, punch capture, monitoring and grouped review/import. Physical platform acceptance remains open. |
| Model formats | Native idTech and polygonal OBJ loaders, optional [Assimp](https://www.assimp.org/) for broader interchange | Active/planned | `src/core/model_mesh.{h,cpp}` decodes Quake MDL (IDPO 6), Quake II MD2 (IDP2 8), and Quake III MD3 (IDP3 15) geometry, resolves skins out of the open package, and writes one frame as Wavefront OBJ. Since 2026-10-08, in-house decoders in `src/core/model_format_*.cpp` also read Hexen II and Half-Life MDL, Heretic II FM, MDC, MDS, MDM/MDX, MDR, Ghoul 2, IQM, MD5, LWO, ASE and KVX, with skeletons in `core/model_skeleton` and MD5/IQM/ASE writers; no model library is linked (see [Native Model Formats](MODEL_FORMATS.md)). Original `core/model_obj` adds bounded polygon import with independent UV/normal corners and smoothing groups; MTL conversion remains open. Package OBJ geometry and material previews use a cancellable worker. Level dependency audits follow decoded MD2/MD3 material paths through shader scripts and images; original `core/model_skin_bindings` supports explicit Quake III `.skin` authoring import through the shared document transaction and GUI/CLI, including exact staged-package occurrences. Automatic runtime `.skin` selection in browser/level previews remains unsupported. Assimp remains optional for future adjacent import/export. |
| Static prop authoring | Native C++ geometry and JSON designs in `src/core/model_design.{h,cpp}`, Qt Widgets property editor | Active | Boxes, cylinders, and planes support X/Y/Z rotations and per-part UV transforms, producing deterministic static MD3 or OBJ without new dependencies. Schema-2 sources retain schema-1 read compatibility; a generated checker previews UVs. Generated bytes join the normal package staging plan; immutable plan readers connect model authoring to dependency audits and subset exports. Quake III placement uses the map's undo service. Designs can bake into the editable mesh document; GPU rendering and automatic compiler asset staging remain deferred. |
| Mesh authoring | `core/model_document`, `core/model_topology`, `core/model_transform`, `core/model_uv`, `core/model_export`, Qt Widgets mesh editor | Active, incomplete | Shared candidate validation, precise vertex and indexed edge selection, conforming splits, bounded all-frame distance welding with seam protection, move/rotate/scale previews with one-step undo, shared GUI/CLI origin/selection/custom pivots and translation/angle/scale snapping, UV edits, bounded history, fingerprint-checked JSON source saves, polygonal OBJ and MDL/MD2/MD3 import, and animated MD2/MD3 plus frame OBJ output. MD2 targets original-renderer limits with persistent skin dimensions, all-pose quantization checks and diagnostics; mesh handoff serializes on the cancellable document worker. GUI and CLI use the same services and package/map handoff. Uses Qt/Core/Gui and the software viewport with per-surface images. Pinned MIT/BSD xatlas supplies automatic UV charts and packing through `core/model_uv_atlas`; the private C++ library runs on the existing worker with bounded allocation, indexed-seam preservation and shape-preserving packing. Material images share the level resolver, load from immutable staged-package snapshots on a separate worker, and refresh on material/package/palette changes; `model materials` exposes matching CLI diagnostics. Checksummed local recovery uses background checkpoints and a cancellable draft chooser, with shared CLI verification. Import/edit/save/export use value-only document workers with progress and cancellation; geometry/serialization poll in batches and native decoders/codecs have bounded-call checkpoints. Worst-case responsiveness, further native formats, and full production interaction remain release gates in [Modeller Release](MODELLER_RELEASE.md). |
| Attachment authoring | `core/model_tags`, mesh document/recovery, Qt Widgets and software viewport overlays | Active, bounded | Named identity edits span all frames. Rigid pose edits, fixed pivots and snapping share GUI/CLI validation and history; imported basis handedness is retained. Table/origin selection, local-axis overlays, origin/reset/copy controls and MD3 package handoff use existing services. MD2 refuses tags and OBJ frame output reports omitted attachments. Smooth playback shares rigid interpolation with generated poses. The separate linked-assembly workflow below consumes these tags. No new library, mesh-source schema or rendering backend. |
| Frame animation | `core/model_animation`, `core/model_pose` and Qt elapsed-time playback | Active, bounded | Indexed clip edits, full-pose copying and in-between generation use document validation, history, recovery and CLI services. Session-only smooth preview samples the selected loop by elapsed time; pause/step returns to stored poses. Positions/normals and Qt quaternion tag interpolation are shared with generation. Raster snapshots retain compact projected tags so markers stay aligned when display updates coalesce. Optional mesh schema 6 stores fractional clip FPS with older-source read compatibility; selecting a clip adopts its rate in the editor and browser. Preview overrides remain session-only. No new dependency. Engine timing/configuration and large-model acceptance remain open. |
| UV authoring view | `ModelUvView`, `app/model_uv_render`, and shared `core/model_uv` / `core/model_uv_transform` | Active, bounded | Indexed seam/island analysis and clipped QPainter drawing run on a value-only cancellable worker with one active/one replaceable request and a 4,194,304-pixel image limit. Pan/zoom, aspect-correct repeating material images, shared component selection, shared or independent island pivots and delta snapping use mesh document services. Independent chart transforms preflight corner splits and copy exact geometry/normals across all poses. Version-3 editable sources retain authoring seam marks and MD2 skin dimensions and read versions 1 and 2. Native exports preserve resolved UV splits; marks stay in sources/recovery. Uses existing Qt Core/Gui/Widgets with no new dependency. |
| Rectangular UV atlases | Existing pinned xatlas with generated axis-limit adaptation | Active, bounded | Separate width/height controls and `--uv-atlas-size WIDTHxHEIGHT` use the existing document worker. Raster-mask packing retains pixel shape and uniform density; a bounded fit search uses the long axis. Original vendored source/header hashes and licence notices remain intact. No new runtime library, renderer or mesh schema. Pinned/obstacle packing and texture rebaking remain open. |
| Linked model assemblies | `core/model_assembly`, `core/model_assembly_document`, `core/model_assembly_recovery`, `ModelAssemblyDialog`, `model assembly` CLI | Active, incomplete | Separate schema-1 `.assembly.json` graph retains file/package references, nested tags, local transforms and independent frame playback settings. Bounded immutable model snapshots share pose/tag interpolation and the software viewport; explicit pose and sampled-animation baking enter ordinary mesh/package/level workflows. `core/model_assembly_animation` preflights frame storage, validates topology across samples and retains clip FPS in optional mesh schema 6. Relative source references, bounded history and guarded writes share existing services. Checksummed recovery records retain recipe/selection/time, with a coalescing worker, session lease and verified GUI/CLI restore/discard. Native engine timing configuration, animated-bake engine acceptance and maximum-assembly performance remain open. No new library or renderer. |
| Model preview widget | `ModelViewport` (`app/model_viewport_render.cpp`) on the 3D renderer, presented with `QPainter` | Active | Orthographic and perspective cameras with reversed depth. World-space geometry uploads once per mesh revision and stays cached on the device; per-corner flags carry hover and selection. Skins sample bilinear and repeating; translucent skins peel up to four layers and composite them. Wireframe is an instanced, antialiased line pass with the former CPU coverage formula; picking reads a triangle-ID target. Camera requests coalesce behind one running job; document and material changes retire stale output. The Levels camera, modeller views and Doom preview share it. |
| AI connector layer | Provider-neutral connector/model metadata plus manifest-backed workflow experiments | Active experimental | Lets users route reasoning, coding, image, audio, voice, 3D, and agentic workflows through OpenAI, Claude, Gemini, ElevenLabs, Meshy, local/offline models, or future connectors while keeping credentials redacted and outputs staged. |
| First AI provider | OpenAI connector scaffold, with future provider calls following [Responses](https://platform.openai.com/docs/api-reference/responses) and [tools/function calling](https://developers.openai.com/api/docs/guides/tools) patterns | Active experimental | OpenAI is implemented for configuration, credential discovery, model routing, safe tool descriptors, and no-write first experiments; network invocation remains opt-in future work. |
| Generative AI | Native Qt Network transports: structured JSON output for OpenAI-compatible endpoints, Claude and Gemini; image generation through OpenAI's Images API, Gemini image output and the Stable Diffusion web UI API; sound effects through ElevenLabs' API; deterministic level, texture and sound generators and schema-checked map edits that use them | Active experimental | Level plans, map edits, textures and sounds come from the user's chosen providers (or local runtimes) through the same opt-in, consent, preview and redaction rules as the Assistant, while the rules planner, picture-based texture path, saved proposals and the sound synthesizer keep every generator complete without AI. No new dependencies. |
| Tests | Meson tests, Qt Test, focused executable tests, parser fixtures, and deterministic parser fuzzing | Active | Scales from smoke tests to parser safety, CLI parity, package safety, and editor regression coverage. Fuzzing: `src/core/parser_fuzz.{h,cpp}` generates the corpus and the `parser-fuzz-smoke` and `corrupt-fixture-smoke` tests run it. Qt Test: `shell-interaction-smoke` links the `vibestudio_app` static library and drives the real window offscreen; a Qt install without the Test module skips that one test. |
| Packaging | PakFu-style scripts, Qt deployment tools, GitHub Actions artifacts | Portable staging and Windows runtime/source pairing active | Windows tooling verifies the selected Qt runtime, original notices, captured build and matching source ZIP. macOS/Linux native deployment, signing, clean-machine acceptance and release publication remain open. |

## Core Application Stack

Use C++20 and Qt 6 as the durable foundation. VibeStudio is a native desktop
tool with large projects, binary formats, compiler orchestration, custom
viewports, and low-latency editor interactions; C++ and Qt are the right center
of gravity for that work.

Qt Widgets remains the primary UI toolkit. It is the best fit for a dense
studio shell with dockable panels, inspectors, tree views, lists, logs, tables,
property editors, and long-lived desktop workflows. Qt Quick/QML is allowed for
contained surfaces where animation, transitions, or scene-graph composition
clearly improve the user experience.

The application layer now uses Qt's meta-object system. `src/meson.build` runs
`qt6.preprocess` over the app headers, and `Q_OBJECT` is declared by
`ApplicationShell`, `MapViewport`, `ModelViewport`, the asset views, the studio
charts, the command registry and palette, and the syntax highlighter. Two things
drove that: `Q_OBJECT` gives each class its own `tr()` context, which is what
makes per-class translation contexts work now that catalogs load at run time;
and the custom widgets need real signals, because a painted viewport that
reports a picked object, a dragged selection, or a hovered triangle has no
Qt-provided notification to reuse.

The core library stays moc-free. It links Qt Core and Gui only
(`qt6_core_modules` in the top-level `meson.build`), declares no `Q_OBJECT`, and
translates through `QCoreApplication::translate` with explicit context strings
such as `VibeStudioIdTechImage`. Keeping the boundary there means core stays
usable from the CLI and from tests without dragging in the widget stack.

The five core modules added this round hold that line. `model_mesh`,
`entity_definitions`, `package_compare`, `document_watch`, and `parser_fuzz` are
all in `core_sources` and link nothing beyond Qt Core and Gui; they translate
through `VibeStudioModelMesh`, `VibeStudioEntityDefinitions`,
`VibeStudioPackageCompare`, and `VibeStudioDocumentWatch` contexts, except
`parser_fuzz`, whose case ids are untranslated diagnostic tokens meant for a
test log. Gui is what lets `model_mesh` hand back a decoded skin as a `QImage`,
the same reason `idtech_image` needs it. `DocumentWatcher` is the interesting
case: it wants `QFileSystemWatcher` notifications but has no `Q_OBJECT`, so it
binds its lambdas to the watcher it owns as their context object. That keeps
core out of the moc step and also lets a smoke test drive the whole state
machine through `pollAt()` with no event loop and no real time passing.

Meson and Ninja remain canonical. Do not add CMake as a parallel first-class
build system for VibeStudio-owned code. External compiler projects may keep
their upstream build systems until a wrapper or import strategy is documented.

## UI And UX Stack

The shell should use Qt Widgets, model/view classes, custom widgets, icons,
dockable panes, persistent layouts, and reusable status/detail components. The
active shell includes a reusable `LoadingPane` for pane and preview loading
states, a reusable `DetailDrawer` for logs, metadata, manifests, raw
diagnostics, and support-copy text, shared shell semantics for status chips,
shortcuts, and command-palette entries, and compact graphical summaries for
project health, package composition, map health/statistics, and compiler
pipeline readiness. The Advanced Studio workbench adds shader, sprite, code,
AI, and extension summaries with detail-on-demand tabs while staying in Qt
Widgets.

Visual styling is centralized. `src/app/studio_theme.*` resolves the theme,
density, and text-scale preferences into one token set, installs Fusion plus a
small proxy style, and applies an application-wide palette and generated
stylesheet, so no widget hard-codes a chrome colour. The default dark theme
borrows idStudio's visual language (neutral charcoal panels, black viewports,
an orange accent, bottom-edge panel tabs, a Key / Value property grid, and an
asset browser with breadcrumbs and thumbnail tiles) as inspiration only; no
idStudio code or assets are used. `src/app/studio_icons.*` paints the icon set,
and `src/app/studio_layout.*` supplies the shared page parts. `--ui-snapshot`
renders every surface offscreen for documentation and visual review.

The UX stack must directly support the project philosophies:

- Seamless workflows: GUI, CLI, compiler runs, package operations, and project
  state share the same services.
- Modern but detailed: clean summaries first, with raw logs, manifests, graphs,
  metadata, and diagnostics available on demand.
- User awareness: every noticeable operation exposes loading, progress,
  cancellation, success, warning, failure, and detail states.
- Accessibility: high-visibility themes, scalable text/UI, keyboard access,
  screen-reader metadata, reduced motion, OS-backed TTS, and non-color-only
  status are normal shell capabilities.
- Localization: all user-facing strings should be translatable and layouts must
  support right-to-left, non-Latin scripts, and translation expansion.
- First-run setup: users can tailor language, accessibility, theme, density,
  editor profile, installations, projects, compilers, AI, CLI, and automation
  before work begins.
- Adaptability: editor profiles change layout, controls, selection behavior,
  terminology, camera behavior, and keybinds without forking editor logic.
- Graphical communication: use diagrams, timelines, dependency graphs,
  overlays, and previews to show real project state rather than decoration.

## Rendering And Viewports

Use a renderer abstraction before committing editor logic to any graphics API.
The minimum boundary should cover viewport creation, frame lifecycle, camera
state, draw batches, texture/material handles, overlays, picking, and readback
for tests.

Recommended progression:

1. MVP 2D surfaces: custom Qt Widgets and `QPainter`. **Done.** The map
   viewport, image and palette views, waveform view, and the studio charts are
   `QWidget` subclasses that paint in `paintEvent`. No Qt Graphics View scene,
   no GPU context, and no extra Qt module are involved.
   Quake-family plan brushes now use bounded projected-wire batches and a
   physical-pixel image, composited with the shared 2D line painter
   (`app/wire_lines`). QPainter presents that image with separate grid, text and interaction
   overlays. Pan/zoom, viewport scale and geometry changes refresh the relevant
   cache. Cache limits fall back to complete ordinary drawing. Selected
   brush/patch outlines use a separate bounded CPU layer with thicker
   dashed strokes; selection changes do not regenerate the base brush image.
   Both drawing paths use the same visible geometry and ownership expansion.
   Larger scenes prepare and paint brush/patch and selection images on one
   cancellable Qt worker per pane, coalescing pending navigation and rejecting
   retired scene/selection results. QPainter presents the previous navigation
   image at the current transform until the complete replacement is ready, with
   a visible and accessible updating state. Wire-cache rejection stays on the
   worker; physical targets above the image ceiling and allocation failures use
   complete synchronous painting. Small scenes also render immediately.
   This retains Qt Widgets and adds no library. Scene solving/adoption, Doom
   painting, live overlays, picking and native large-map latency remain open.
   A separate coalescing Qt worker prepares grid/member images for large Quake
   scenes or selections above 64 objects, including Doom. It shares pure object
   projection with live editing, polls cancellation and rejects retired results.
   Grid and member images each use the shared physical-pixel ceiling; compatible
   old images follow navigation until replacement. The updating state aggregates
   both workers. Primary selection, handles and picking remain live, while
   unsupported targets/transforms and allocation failures keep ordinary painting.
   Shared physical target sizing includes the fractional device origin of child
   panes. Geometry, selected outlines, grids and members preserve that phase
   when rasterizing and presenting; edge coverage stays within the existing
   image budget. Moving a pane by whole physical pixels reuses its images.
   Shared Levels/Models wire selection dashes use an endpoint anchor independent
   of the raster scan axis, avoiding pattern reversals near diagonal edges.
   Unchanged grids reuse a bounded native Qt image. Immediate/fallback member-ring stamps reuse
   exact physical subpixel phases within an 8 MiB / 512-entry cache; budget and
   transform limits retain complete ordinary drawing. Primary labels share HUD
   layout constraints. These GUI-only caches add no dependency, graphics API,
   profile setting, document format or CLI requirement. Cold navigation remains
   a separate performance target from warm repainting.
2. 3D views on the GPU: **done** (2026-10-08). `core/render_device` is the
   renderer abstraction this section asked for: a frame describes render
   targets (colour, 16-bit colour, integer and float IDs, depth), passes of
   draws with blend, depth, cull and scissor state, uploaded textures and
   vertex data (cached per owner across frames), and the targets to read back.
   One device per backend runs on its own thread: OpenGL through
   `QOpenGLContext` on a `QOffscreenSurface`, Vulkan through the loader opened
   at run time with explicit barriers and pooled targets. Both follow one set
   of conventions (y-down clip space, depth 0..1, top-first read-back,
   counter-clockwise front faces), so the same frame gives the same pixels on
   either; `render-device-smoke` checks that on every backend that starts.
   `ModelViewport` builds world-space geometry once per mesh revision, draws it
   with reversed depth and an infinite far plane, peels up to four
   translucent layers, draws wireframes as instanced antialiased segments and
   picks from a triangle-ID target. Read-back images keep the
   8,388,608-pixel ceiling; larger windows scale the image uniformly. The
   former CPU rasteriser (`app/model_rasterizer`) is removed; the UV view and
   the Levels plan views keep a 2D line painter (`app/wire_lines`) for their
   wires, dotted seams and selected dashes, with explicit overlay ordering. Opaque passes cache only
   tiles whose pixels already equal the stroke color; unknown tiles retain
   ordinary coverage and compositing. Dense UV strokes visit the layout in a
   deterministic dispersed order before adjacent subpixel strokes, keeping all
   geometry. `app/model_uv_fill` prepares selected-face winding contours by
   cancelling exact indexed internal borders, then Qt paints the union once.
   Holes, reversed UV windings, overlaps and move previews retain their coverage.
   UV output remains capped at 4,194,304 pixels, including extreme aspect ratios.
   This extends the existing CPU/Qt strategy without a new graphics dependency.
   Vertex authoring uses that worker for projection, occlusion, a 32×32 exact-pick
   index (one reference per finite vertex), and native CPU square/dotted marker
   stamps. Marker-only changes reuse the mesh image. A marker image shares the
   8,388,608-pixel ceiling, adding at most 32 MiB per image; current and in-flight
   snapshots can coexist. Qt presents it alongside the matching mesh snapshot.
   `core/model_fingerprint` streams geometry into SHA-256 with an 8 KiB buffer;
   source/recovery JSON and external-file checksums are unchanged. Immutable
   topology indexes, complete edge-selection sets and their connected vertices
   are prepared on the document worker and shared with selection, history and
   component tables. History estimates include the sets. These changes add no
   library or backend.
   `core/model_boundary_fill` adds original bounded loop traversal,
   projection-based triangulation and proposed-cap intersection checks on the
   same document worker. Selected indexed boundaries close across all poses
   through shared GUI/CLI transactions; authored attributes remain unchanged.
   This uses only the existing Qt and C++ standard library, adds no source
   schema, and is not a general mesh self-intersection or watertightness audit.
   `core/model_boundary_bridge` reuses that boundary discovery and proposed-face
   validation through internal helpers. An original bounded dynamic program
   joins two equal or unequal loops, with explicit twist and reference-pose
   alignment. Existing vertex attributes and all document integration paths
   remain shared; this adds no dependency, schema or rendering change.
   `core/model_surfaces` adds original surface rename, partition, move, duplicate,
   delete and join operations through that same all-pose transaction. The Widgets
   review dialog and dedicated `cli/model_surfaces` command share material-adoption,
   capacity and selection rules. No library, renderer or source-schema change is
   needed; material images are refreshed against resulting surface identities.
   `core/model_surface_selection` extends document selections and asynchronous
   viewport previews to whole-surface sets with a shared reference-pose pivot.
   GUI/CLI transforms use the existing document transaction, renderer, history
   and recovery. The optional recovery selection field remains backward readable;
   editable mesh source and native formats are unchanged. No new dependency.
   `core/model_material_slots` adds bounded ordered external-binding operations
   and geometry-free preview snapshots. The Widgets review and `model slots`
   CLI use normal document validation, undo and persistence. Selected-slot
   previews share the existing material worker and `model materials` resolver;
   transient view choices never change exported order. Embedded MDL controls
   remain separate. This adds no library, renderer or source-schema dependency.
   Camera requests coalesce
   and completed previews remain visible during motion; document/material changes
   retire stale results. The UI shows rendering state, keeps navigation responsive,
   and postpones picks until the displayed view is current. Selection/hover edges
   share depth testing, while wireframe deliberately remains a view through
   the mesh. Picking uses a conservative 32×32 screen index with at most
   2,097,152 cell references, plus one reference per broad face; the exact sampler
   retains subpixel coverage, perspective UVs, alpha and depth ties. Style-only
   updates reuse projection/index data. Maximum editable-grid viewport and
   component-authoring latency have reproducible tests. CPU-heavy translucent
   overlap, full save/recovery/export latency and optimized cross-platform
   throughput still need release acceptance.
   The Levels 3D preview reuses this widget:
   `buildLevelMapPreviewMesh` (`src/core/map_preview_mesh.*`) turns solved
   brush faces, tessellated patches, and Doom walls into a mesh with one
   surface per texture, and brush faces carry outward normals for backface
   culling. Render/pick tests and a fixed textured-scene timing run exercise
   the same renderer through offscreen frames, without OS screen capture.
   `core/map_geometry_cache` now reuses unchanged brush polygons across plan
   edits and camera mesh rebuilds, with exact keys and bounded retained payload.
   Separate value snapshots preserve worker cancellation and fresh package UVs;
   this adds no dependency or renderer backend. The explicit
   `level_geometry_benchmark` measures synthetic 100–10,000-brush workflows,
   with cold-load and software-drawing limits still open.
3. Production 3D/editor viewports: grow the same layer rather than add bgfx
   (declined 2026-10-08). Still open: presenting straight to a window surface
   instead of reading frames back, GPU-side picking for very large scenes,
   and a native Metal backend if OpenGL's deprecation on macOS bites.

Headless rendering is a separate path, not a fallback for the widgets. The SVG
renderer in `src/core/map_render.cpp` builds its document as text, so it runs in
core, from the CLI, and in CI with no display. It reads the same
`src/core/map_geometry.cpp` results the painted viewport does, which is what
keeps the two pictures consistent; only the output surface differs.

Do not let rendering details leak into map, model, package, or compiler
services. Those services should produce editor data; render backends should
visualize it. `map_render` is the boundary case that proves the rule: it
consumes geometry and emits a document, and it never touches QPainter.

## Data, Search, And Persistence

Use JSON for project manifests and command manifests. Keep schemas versioned
and validate them through both GUI and CLI paths.

Use Qt `QSettings` for application settings and store project-specific
overrides under the project root. The active settings slice persists shell
geometry/state, selected mode, recent project folders, recent terminal activity
records, first-run setup progress, locale, theme, text scale, UI density,
reduced motion, OS-backed TTS, selected editor profile, manual game
installation profiles, and the current project path through shared GUI/CLI
services. Settings must be exportable enough for support and issue reports
without exposing secrets.

Project manifests use `.vibestudio/project.json` at the project root. The active
schema stores a version, project ID, display name, source folders, package
folders, output/temp folders, optional selected installation ID, project-local
settings overrides, compiler executable search paths/overrides, registered
compiler outputs, and timestamps. The workspace dashboard and CLI project
reports build a summary-first health view from this manifest.

Editor profiles are registry-backed routed MVP presets with a settings-backed
global selection and project-local override path. The active schema records
layout, camera, selection, grid, terminology, default panels, workflow notes,
surface-scoped bindings, stable command IDs, and conflict smoke coverage for
the VibeStudio default, GtkRadiant 1.6.0-style, NetRadiant Custom-style,
TrenchBroom-style, and QuArK-style workflows. Full camera behavior and deep
profile-fidelity audits remain planned.

Game installation profiles are currently settings-backed. They keep a stable
profile ID, game key, engine-family default, root path, optional executable,
package path hints, palette/compiler defaults, read-only state, and read-only
validation results. Steam/GOG detection produces unsaved candidates that the
GUI can import after confirmation and the CLI can report with `install detect`;
source-port detection remains planned.

Compiler registry data is now modeled as descriptors over imported compiler
submodules, expected executable names/build paths, capability flags, user
executable overrides, and project-local executable overrides. Discovery checks
source directories, known build-output paths, extra search paths, overrides,
and PATH, and registry reports can run short version/help probes. VibeMap2
`vibemap2-bsp`, `vibemap2-vis`, and `vibemap2-light`, ZDBSP/ZokumBSP
node-builder profiles, and VibeMap3 probe/BSP profiles can produce command
plans with arguments, working directory,
expected output, warnings, and readiness. Plans and runs emit
schema-versioned JSON command manifests with command/environment details,
duration, exit code, hashes, stdout/stderr, diagnostics, task-log entries, and
registered outputs.

Use SQLite for the project asset index once package browsing and installation
profiles move beyond in-memory MVP structures. The index should cover mounted
package entries, metadata, dependency edges, compiler outputs, diagnostics,
recent activity, and search. Use FTS5 for fast text search where available, with
a graceful fallback if a platform build lacks it.

Use the reusable operation-state model for activity reporting across GUI and
CLI-backed workflows. The active model defines stable state identifiers for
idle, queued, loading, running, warning, failed, cancelled, and completed
operations, tracks progress, warnings, result summaries, cancellation
eligibility, and timestamped logs. Future package, compiler, validation, AI,
and export services should emit through this model instead of inventing local
task status enums.

Use the package/archive layer for package metadata, read-only browsing, safe
extraction, and save-as write-back. The active slice defines stable descriptors
and readers for folder, PAK, WAD, ZIP, and PK3 package formats; shared package
entry metadata; mount-layer session state; safe normalized virtual paths;
traversal rejection; and output-path joining that proves extraction targets
remain under the chosen root. Package preview and extraction build on those
readers for text samples, basic image format/dimension metadata, binary hex
summaries, GUI composition buckets by entry type and byte size, selected/all
extraction, dry-run output reporting, cancellation-aware GUI progress, and CLI
validation.

The write-back slice adds a staged package model with add, replace, rename,
delete, conflict reporting, before/after composition, blocked-state messages,
manifest export, save-as guards, deterministic PAK and ZIP/PK3 writers, and a
WAD writer covered by map-lump ordering tests. This layer is adapted from
PakFu's archive surface and credited in [`docs/CREDITS.md`](CREDITS.md); future
package writers should build on it instead of duplicating path safety rules per
format.

`src/core/package_compare.{h,cpp}` answers the question a file list cannot:
what actually differs between two packages. `comparePackages` pairs entries by
case-folded virtual path and by occurrence index, so a Doom WAD's repeated lump
names line up one map at a time, and reports each entry as identical, added,
removed, changed, or case-only. Case-only is a category of its own because a
path that differs only in case is a real portability bug: it resolves on
Windows and fails on Linux. Content is decided by size first, then by SHA-256
over verified payloads; stored ZIP checksums alone do not establish equality.
`metadataOnly` skips payload comparison. `comparePackageToPlan` takes a staged
`PackageStagingModel` as the right-hand side, so a plan can be diffed before it
is written. All archive readers use their virtual positional streaming API;
plans use `streamEntry` for generated bytes, retained imports and base entries.
The shared SHA-256 consumer verifies the declared size and checks cancellation
in 64 KiB chunks, with side/path/byte progress. The default 256 MiB threshold
is an I/O budget. Unsupported streams stay unchecked. Results
render as text or as schema-versioned JSON, and the ordering is a pure function
of the two inputs.

`src/core/package_validation.*` uses the same archive readers for full payload
validation. `inflateRawToSink` retains a DEFLATE history window plus bounded
input/output buffers, allowing integrity checks and source comparison to hash
large entries without whole-file allocation. Cancellation is checked while
streaming. Archive writers and manifest payload hashes also stream; generated
operation hashes share a per-call digest cache keyed by immutable storage
identity, without hashing a full buffer merely to look up its digest. Older
byte-array preview and standalone analysis APIs retain individual file memory
costs. Directory/plan/JSON metadata remains proportional to entries; no new
compression dependency was introduced.

`src/core/document_watch.{h,cpp}` tracks the files the studio holds open.
Paths are registered by `DocumentWatchRole` (the open map, the open package, the
code-editor file, the project manifest) with a fingerprint taken at
registration: size, modification time, and a SHA-1 over the content when the
file is small enough to hash. `poll()` recomputes that fingerprint and is the
authoritative check; `QFileSystemWatcher` notifications only mark a path as
worth re-checking, because platforms disagree about how many events one save
produces and the watcher stops reporting a path once it is deleted. Changes are
classified as modified, touched, removed, replaced, or created, and a burst is
coalesced into one event after the path has looked the same for a quiet period,
so a write-temp-then-rename save reports once rather than three times. A
`touched` result, meaning metadata moved but the bytes are provably identical,
is not a change anyone needs to be asked about, which is why the kind exists.

Compression is implemented in tree rather than taken from a library. Real PK3
and ZIP content needs inflate to read and deflate to write, and the obvious
answers were zlib or miniz. Both were declined. A bundled or system compression
library is not free: it adds a build-time dependency on every platform target, a
license file in every release bundle, a credits entry, and an update path to
track, and the portable packaging scripts would have to carry it. Weighed
against that, the codec itself is a bounded, well-specified piece of work with a
fixture-testable contract.

`src/core/deflate.{h,cpp}` implements it from the public specifications: RFC
1951 for DEFLATE, RFC 1950 for the zlib wrapper, and the ITU-T V.42 CRC-32 the
ZIP appnote uses, plus Adler-32. The decoder is bounds-checked throughout, never
trusts a length or distance taken from the stream, caps output growth, and fails
with an error on malformed input rather than aborting. Output is deterministic
for a given input and level, which is what lets the package writers produce
reproducible archives.

The encoder caught up with the decoder this round. Both now handle stored,
fixed-Huffman, and dynamic-Huffman blocks, so VibeStudio reads anything a normal
ZIP tool writes and no longer has to give up ratio to emit it. `deflateRaw`
tokenizes each chunk once, then measures a stored, a fixed-Huffman, and a
dynamic-Huffman encoding of it in bits and writes whichever is smallest, with
ties going to stored, so a block is never larger than simply storing its bytes
would be. Dynamic code lengths come from the package-merge algorithm, which
gives an optimal length-limited code rather than an approximation. The four
`DeflateLevel` values no longer imply a block type at all: `Store` skips LZ77
entirely, and `Fast`, `Default`, and `Best` differ only in how hard the hash
chain is searched and whether lazy matching is used. Every level except `Store`
can emit a dynamic block. The writer still stores an entry verbatim whenever
deflating would not shrink it, so already-compressed assets are never made
worse.

Archive saves use `DeflateStreamEncoder`, an original incremental implementation
of the same block selection with a 32 KiB dictionary and 65,535-byte block.
Dictionary offsets are rebased per block, avoiding whole-input offset limits.
Cancellation is checked during tokenization and between bounded reads/writes.
ZIP uses a read-only measurement pass before a second pass writes the chosen
method; content hashes, CRC and sizes must agree. This preserves reproducible
headers, ZIP64 rules, stored fallback and no-write dry runs without payload
spools or a new dependency. Whole-array codec APIs remain for existing consumers;
their migration and metadata-scale acceptance remain separate work.

## Accessibility, Localization, And Setup Stack

Use Qt's accessibility APIs and normal Qt widgets wherever possible so platform
assistive tools can understand the UI. Custom viewports, inspectors, graph
views, timeline views, map views, shader graphs, and package trees must add
explicit accessible names, roles, descriptions, focus handling, values, and
state changes.

Use layout-driven UI, OS font/scaling defaults, app-level text scale settings,
and high-visibility themes. Scaling and localization must be treated as layout
requirements, not post-release bug categories.

Use the platform's own speech engine for optional OS-backed TTS
(`src/app/studio_speech.*`: SAPI 5 on Windows, `say` on macOS, Speech
Dispatcher or eSpeak NG elsewhere), so no voice data ships and nothing leaves
the machine; Qt TextToSpeech remains a candidate backend. TTS should read selected
summaries, compiler errors, task outcomes, AI proposals, and setup guidance
without becoming the only way to receive that information.

Use Qt Linguist, `QTranslator`, and `QLocale` from the beginning. The active
localization slice defines a shared 47-language target set (see
[Supported Languages And Regions](ACCESSIBILITY_LOCALIZATION.md#supported-languages-and-regions)),
48 `.ts` catalogs, system-language defaults with regional resolution, separate
region formats, pseudo-localization, Arabic/Urdu/Persian/Hebrew right-to-left
smoke coverage,
locale formatting samples, pluralization samples, expansion stress samples,
representative layout-budget checks, stale/untranslated catalog status reports,
dry-run `lupdate` extraction validation, and a source scan that fails on
literals hidden from `lupdate` inside translation helpers.

Catalogs are compiled, not shipped as source. `i18n/meson.build` runs `lrelease`
over every checked-in `vibestudio_<locale>.ts` and writes the `.qm` into the
build directory, installing it under `<datadir>/vibestudio/i18n`. `lrelease` is
looked up with `required: false`: a toolchain without Qt Linguist tools still
builds, and the application runs in the source language rather than failing.

At run time `installStudioTranslations` in `src/app/studio_runtime.cpp` resolves
a catalog and installs it with `QTranslator`. It searches the
`VIBESTUDIO_I18N_DIR` override first, then the development, installed, and
portable layouts relative to the executable, then the working directory, and it
falls back from the requested locale to the base language to `en`. The matching
Qt base catalog is installed alongside it when one is present, and layout
direction is applied from the locale. Full layout expansion audits and finished
translated bundles remain planned; the catalogs themselves are still seeds.

Build the first-run setup flow as a real settings workbench. It should configure
accessibility, language, theme, density, editor profile, game installations,
projects, compilers, AI connectors, CLI behavior, and automation preferences,
then leave those choices editable.

## CLI Stack

Use the active lightweight in-process router plus command registry for the
command-line interface. The CLI is a first-class product surface, not a debug
afterthought. It exposes project, package, installation, asset, map, shader,
sprite, code, extension, compiler, localization, diagnostics, AI, credits, and
registry commands with JSON output, stable exit codes, quiet and verbose modes,
dry-run behavior, compiler watch streaming, machine-readable task state, and
examples for PowerShell and POSIX shells. CLI11 remains deferred for the fuller
parser/completion layer once that dependency earns its weight.

CLI commands must call the same services as GUI actions. Output should support
human text by default and `--json` for automation. The active router exposes
stable exit codes through `--exit-codes` and `cli exit-codes`. Long-running
commands should emit task state, progress, warnings, and reproducible manifests.

## Text, Script, And IDE Stack

Use Qt text widgets for the editors. Highlighting is `QSyntaxHighlighter`:
`StudioSyntaxHighlighter` in `src/app/syntax_highlight.{h,cpp}` compiles its
rules from `StudioLanguageDescriptor` records that declare comment tokens,
block comment delimiters, keywords, secondary keywords, and case sensitivity.
Adding a language means adding a descriptor, not writing widget code, and the
highlighter never changes document text. Its colours come from
`StudioSyntaxTheme`, which the active studio theme fills in, so high-contrast
themes stay readable instead of inheriting a hard-coded palette.

The active asset-tools slice adds diagnostic boundaries for CFG, shader
scripts, QuakeC, and idTech text assets, plus project-wide find/replace with
clean, modified, saving, saved, and failed states. The Advanced Studio slice
adds a source tree index with language service hook descriptors, diagnostics,
symbol search, compiler task suggestions, and source-port launch profile
summaries.

KSyntaxHighlighting and Tree-sitter both remain deferred. KSyntaxHighlighting
would bring better definitions at the cost of a KDE Frameworks dependency in
every portable package, which the data-driven rules above do not yet justify.
Tree-sitter earns its place when incremental parsing materially improves
diagnostics, outlines, refactoring, shader/script structure, or AI context
extraction, and not before.

The native stdio LSP client supplies external diagnostics, formatting, completion, Quick Info, Parameter Hints,
definitions, references, reviewed symbol rename and code actions.
GUI and CLI share lifecycle, document versions, UTF-16 positions, cancellation
and bounded protocol parsing through Qt Core. Formatting uses the document's
existing undo and save services; CLI writes also preserve exact untouched line
separators. Servers remain independently
installed tools selected and connected explicitly; the editor core does not
hard-code a server. See [Local Language Services](LANGUAGE_SERVICES.md) for
supported capabilities and remaining semantic editing work.

Parameter Hints uses native Qt overload controls and a compact inline panel,
with debounced trigger/retrigger requests against the current unsaved buffer.
Quick Info and signature documentation share resource-isolated Qt text rendering;
no renderer dependency or bundled language server is introduced.

Completion snippets use an original bounded C++ syntax expander and Qt document
cursors. The same expansion and ranges serve CLI previews and GUI acceptance;
native field controls, linking, Undo and recovery use existing editor services.
Regex transforms, adjusted indentation and stacked snippet sessions remain
explicit gaps, with unsupported proposals reported by the completion parser.

Document diagnostics use the existing Qt stdio client with bounded asynchronous
pull requests, unchanged-result caches and shared GUI/CLI parsing. Save/Save As
notify interested servers after successful writes and synchronization. This
adds no production dependency; workspace pulls and will-save hooks remain gaps.

## Assets, Media, And Formats

The [Audio Editor](AUDIO_EDITOR.md) uses in-tree C++ float-sample processing and
the existing native WAV/DMX readers. GUI and
`asset audio-edit` share `audio_clip` validation and effects plus `audio_export`
integer/float WAV precision, optional TPDF dither, and atomic output. The GUI's
package handoff retains PCM16 as its default delivery setting.
The editor's in-tree `AudioWaveformView` renders exact frame ranges and individual
samples with Qt Widgets/QPainter. An immutable multilevel min/max cache is built
off-thread and retained with bounded edit history; zoom, pan, and overview queries
do not scan whole clips. New/clipboard/mix operations add no media dependency.
`audio_project` adds an in-tree versioned `.vsaudio` container with float32 samples,
bounded JSON, and a SHA-256 checksum. QtCore provides atomic files and cooperating
writer locks; native saves, recovery, and `asset audio-project` add no dependency.
The 2026-10-05 multitrack foundation keeps original in-tree C++/Qt Widgets:
`audio_session` supplies immutable media/region descriptors and a shared stereo
block mixer; `audio_session_io` embeds existing `.vsaudio` payloads in a versioned
`.vssession` and streams mixdown through the existing WAV encoder. The GUI and
`asset audio-session` share these services. No new dependency or minimum Qt
version is introduced. `audio_media` adds bounded source inventory, reviewed
relink/replacement and unused-source removal through the same native schema,
decoder/SRC stack and worker-based Qt/CLI flows. `audio_transport` adds a frame clock with caller-owned
mix buffers, pause/seek/loop semantics and master meters. `audio_session_playback`
owns a dedicated Qt worker and bounded output blocks; `audio_stream_device` uses
the existing optional Qt Multimedia QAudioSink push API (Qt 6.4-compatible), with
explicit output/rate validation and no new library. It preserves partial writes,
flushes seek/loop-policy lookahead and reports starvation/device failures. Device
cursor estimates use processed time; this buffered baseline does not establish
hard-real-time performance or low-latency monitoring. Source media is still in
memory. Synchronized recording/monitoring now uses the optional native duplex
controller and reviewed grouped import; hardware acceptance remains open. Source
disk streaming, surround routing, plugins and MIDI remain explicit
[DAW decisions](plans/audio-daw.md) to implement and review. Session recovery now shares the waveform recovery service and worker.
Its version-1 `.vssession-recovery` envelope embeds a native session plus bounded
provenance/timestamp metadata and a checksum. The two document kinds share the count/byte budget, preferences,
startup discovery and verified draft review. `asset audio-session recover`
uses the same decoder and protects the reviewed copy and original session.
`core/audio_recovery_store` shares bounded verification, storage limits, live
editor leases, and digest-checked discard with the recovery manager and
`asset audio-recoveries`. It uses existing Qt Core and never follows recorded
source paths or automatically evicts checkpoints.

Stereo routing uses original in-tree `audio_routing` code, not another audio
framework. A validated DAG supports up to 32 buses within 64 strips, eight sends
per strip, pre/post taps, polarity, stereo swap and path-aware solo. Caller-owned
scratch preserves allocation-free rendering after preparation. Version 2 added
explicit routing; version 1 loads with direct-master defaults. Version 3 adds
bounded built-in effect chains and a saved tail policy; versions 1/2 load with
empty chains. Recovery embeds any supported native version without changing its envelope.
Routing GUI and CLI changes share validation and normal session undo/save guards.
Seamless live changes, surround and sidechains remain open.

Session effects decision (2026-10-05): keep stateful processors in original
`audio_effects` C++ code, with no new audio framework or minimum Qt version.
Prepared track/bus/master chains share the routing renderer, streamed output,
offline export, recovery and CLI edits. Qt Widgets stages chain edits as one
normal session command. Double-precision biquads, linked dynamics, fractional
delay and saturation use bounded preallocated state. EQ formula provenance and
the compatible W3C documentation licence are recorded in [Credits](CREDITS.md#audio-session-eq).
The original sample limiter has no lookahead/true-peak guarantee; saturation is
not oversampled. A separate lookahead sample-peak processor and routing
compensation now implement the latency decision above. External plugins and
true-peak limiting remain follow-up requirements; numeric automation is below.

Effects extension (2026-10-06): stereo reverb uses a credited public-domain
Freeverb topology/tuning reference with original decay/damping, predelay,
stereo-preserving excitation and reset behavior. Chorus, flanger, tremolo and
phaser remain native C++ processors with preallocated state. No new processing
library or Qt dependency is introduced. Portable `.vsfx` version-1 presets use
bounded JSON, nine factory recipes and shared GUI/CLI validation; file operations
reuse the atomic output guards and run asynchronously in the GUI. See
[reverb attribution and licence review](CREDITS.md#audio-reverb-reference).

Initial standalone input capture uses original `audio_capture` and optional
QAudioSource (existing Qt Multimedia), with separate input/disk workers and a
fixed sixteen-block queue. `audio_take` adds an append-only checksummed `.vstake`
journal and verified range reads for shared session import and CLI export.
It uses C++20 threading, Qt Core and guarded CRT/POSIX flush calls. macOS also
links the system CoreFoundation framework to check the bundle usage description;
Qt 6.5+ permissions and its microphone backend are required there. Core review,
editing and export retain the Qt 6.4 baseline. This does not select a new audio
library or establish synchronized duplex monitoring or hard-real-time behavior.

Qt Multimedia also auditions edited selections from float32 WAV memory when
available. `app/audio_playback` separates session lifecycle and bounded timeout/
seek behavior from the Qt device adapter. Device-free tests inject media events;
production uses QMediaPlayer/QAudioOutput and the system default output. No new
library or minimum Qt requirement is introduced. Live seek retains Qt's
millisecond resolution; exact audition boundaries are prepared on workers.
Decode/effects/encoding run on workers; sample history is bounded.
`audio_resample` uses pinned r8brain-free-src 7.5 with the double-precision Ooura
FFT backend for linear-phase conversion. This source-only MIT/permissive
dependency was chosen for professional filtering, portable C++, and an offline
Meson build with no additional system library. GUI/CLI share cancellation, size
bounds, channel timing, and selection mapping; licence notices ship with the
application. See [dependency review](DEPENDENCIES.md#r8brain-free-src).
The in-tree `audio_analysis` service measures sample peak, unweighted RMS,
signed DC offset, and full-scale events per channel/range. The 2026-10-04
metering decision adds pinned libebur128 1.2.6 as a private C static library
for BS.1770 integrated loudness, with the existing r8brain-free-src converter
providing streamed true-peak reconstruction. The latter's double precision,
8×/4×/2× interpolation and explicit zero boundaries address broadband and
boundary errors found during independent filter comparisons. Both retain C++/Qt
worker, explicit surround-role review and versioned JSON reporting. It was
chosen for its focused metering API, permissive licences and offline portable
build; no device, separate process or playback backend is required. Library
calls are serialized around upstream global table initialization; cancellation
is polled between bounded input chunks. No document schema change is needed.
See [dependency review](DEPENDENCIES.md#libebur128).

The 2026-10-05 Windows runtime decision keeps the existing raster Widgets and
optional Qt Multimedia stack. A separate deployment script derives dependencies
from the selected SDK, checks Qt SPDX content hashes and stages complete runtime
notices and native-widget translations. It excludes ambient developer SDKs,
system DLL copies and unused software-OpenGL/shader compilers. Existing signed
Qt binaries are preserved; source distribution and native/clean-machine tests
remain release gates. This changes release tooling, with no application or
Meson/Ninja replacement. See [runtime packaging](PACKAGING.md#windows-qt-and-audio-runtime).

The same day's CI integration adds an original Python build recorder and
Windows release orchestrator. The recorder invokes canonical Meson/Ninja and
binds unchanged application inputs, actual Qt import libraries, compiler options,
binary and catalogs. The orchestrator runs isolated offscreen CLI checks and
creates a checksum-bound binary/source pair before upload. This uses existing
stdlib/Qt tooling and leaves GUI/CLI architecture unchanged. Hosted execution,
vendor runtime reconstruction and durable release publication remain explicit
acceptance steps.

The in-tree `audio_delivery` service adds explicit Doom DMX and Quake-family
sound-effect presets without another library. GUI and `asset audio-export` share
conversion of a delivery copy, final quantization/dither, source protection,
and Doom WAD staging. The in-tree `audio_markers` service adds typed cue/forward
loop metadata, bounded RIFF parsing/writing, edit/SRC transforms, and native
version 2 persistence. GUI and `asset audio-markers` share it without another
dependency. Quake/II legacy cues are explicit delivery behavior; DMX omits
markers. Compressed editing remains planned.

Prefer native idTech parsers for core formats. VibeStudio must understand WAD,
PAK, PK3, BSP, MAP, shader scripts, palettes, sprites, textures, model formats,
and compiler outputs on their own terms.

The active Advanced Studio slice parses idTech3 shader scripts into editable
stage models, validates texture references against mounted folders/packages,
round-trips selected stage directive edits back to text, and plans Doom/Quake
sprite names, rotations, palette actions, frame sequences, and package staging
paths. These are native C++/Qt services; no new graphics, parser, or media
library is required for the current milestone.

Two more formats became first-class this round, and both were written from
public specifications with no game data in the tree. `core/model_mesh.cpp`
decodes MDL, MD2, and MD3 geometry (vertices, normals, texture coordinates,
frames, MD3 tags, and embedded MDL skins) and resolves external skin paths
against the open package. The other idTech 1-4 formats (MDC, MDS, MDM/MDX,
MDR, Ghoul 2, IQM, MD5, LWO, ASE, Half-Life and Hexen II MDL, FM and KVX) have
their own in-house decoders written from public source releases and
specifications; see [Native Model Formats](MODEL_FORMATS.md).
`core/entity_definitions.cpp` reads Radiant `.def`/`.qc`, Valve `.fgd`, and
Quake III `.ent` text into one catalogue model, folds base classes into derived
ones, and bounds every loop because definition files arrive from mod packages
and the internet.

Use optional helper libraries where they expand workflows without weakening
format fidelity:

- Qt image APIs for standard image loading and conversion.
- Qt image APIs plus VibeStudio palette metadata for texture previews, crop,
  resize, grayscale/indexed conversion, and batch output queues.
- Qt Multimedia for simple playback and device integration: the Audio page
  plays through it when the build has the module, and core decides what a
  player can be handed (`assetAudioPlaybackSource`), so no codec is mandatory.
  Editor and browser share the session-checked transport and device lifecycle.
  Browser preparation runs on bounded workers, preserving native WAV precision
  and compressed media; only DMX needs PCM16 wrapping. No new library is needed.
- Direct dr_libs/Xiph decoder APIs for portable compressed editing. Miniaudio stays
  deferred for future device/playback needs; its mixer and converter would
  duplicate existing services for the current import task.
- Native MDL, MD2, and MD3 loaders for package preview, dependency inspection,
  geometry decode, and single-frame Wavefront OBJ export before any generic
  model library is introduced.
- Polygonal OBJ import uses original C++/Qt parsing, bounded concave triangulation
  and normal/UV seam expansion in `core/model_obj`, without Assimp. Editable
  import and CLI share it; package OBJ previews stream and resolve per-surface
  materials on a cancellable worker. Unsupported MTL shading definitions and
  non-polygon data fail explicitly. This retains the native game export and
  package/level services as the production path.
- MDL authoring uses native C++/Qt services for indexed skins, groups, timing,
  header edits and byte-quantized export. Schema 4 retains those values through
  source/history/recovery; ordinary sources remain schema 3. The Quake MDL
  inspector and `model mdl` CLI share services, including native stored/GLQuake
  timing sampling. Selected skin images prepare on a worker; native elapsed-time
  transport shares immutable textures with model/UV raster work. Indexed image import reuses the
  existing decoder; no library, renderer or cloud dependency is added.
- Assimp for adjacent model import/export (glTF, FBX and the like), while
  keeping the native idTech 1-4 model loaders and BSP-related loaders
  authoritative for game workflows.

## AI Automation Stack

AI features are optional, disabled by default, and safe-by-design, but the
product architecture should be AI-native. VibeStudio should treat generative and
agentic AI as a core acceleration layer that can plan, draft, explain, generate,
validate, and repeat workflows through explicit VibeStudio tools.

The active MVP slice stores global AI-free mode, cloud-connector opt-in,
agentic-workflow opt-in, provider preferences, configurable model preferences,
and redacted credential environment references without storing secrets. It
also exposes provider-neutral connectors, models, credential status, safe
AI-callable tools, and manifest-backed experiments through shared core
services, the GUI inspector/preferences surface, CLI text/JSON, and smoke
tests. Text questions reach providers through `core/ai_transport` over Qt
Network: OpenAI-compatible Chat Completions (OpenAI, and local runtimes such as
Ollama), Anthropic's Messages API, and Gemini's generateContent, shared by the
Assistant panel and `vibestudio --cli ai ask`. Nothing is sent in AI-free mode,
the default.

Use Qt Network and provider-specific HTTP/streaming adapters before introducing
heavy SDK dependencies. The connector model should support OpenAI, Claude,
Gemini, ElevenLabs, Meshy, local/offline models, and future community or studio
connectors. Each connector should declare capabilities such as text reasoning,
tool calls, vision, embeddings, image generation, audio generation,
speech-to-text, voice, 3D asset generation, streaming, local execution, cost
reporting, and privacy notes.

OpenAI is the first general-purpose connector scaffold. Future networked
provider calls should follow the Responses API and tool-calling pattern so
VibeStudio can expose a small, reviewable set of actions.

AI tools must route through normal services: project summary, package metadata
search, compiler profile listing, command proposal, staged text edits, staged
asset generation requests, diagnostics explanation, shader scaffold/entity
snippet/package validation/batch conversion proposals, proposal review
surfaces, and manifest generation.
AI workflow manifests capture provider, model, prompt, context summary, tool
calls, staged outputs, approval state, validation, cancellation/retry state, and
cost/usage placeholders. AI must not write directly to project files, packages,
compiler outputs, settings, or source trees without a preview and explicit user
approval.

Extension integration starts with `vibestudio.extension.json` manifests parsed
through the same C++/Qt service layer. The active model records schema version,
trust level, sandbox model, capabilities, command descriptors, reviewed command
plans, dry-run defaults, and extension-generated staged files before any command
can mutate user projects.

AI-free mode must remain complete. Core editing, package management, compiler
orchestration, validation, CLI use, and launching must work without API keys or
cloud services.

## External Compiler Stack

Keep imported compiler sources under `external/compilers` as submodules or
explicitly credited forks. Invoke compilers through wrapper services that
capture command manifests, environment, progress, stdout/stderr, diagnostics,
outputs, and exit state.

Decision (2026-10-08): the Quake-family compilers are VibeStudio's own forks,
VibeMap2 (`external/compilers/vibemap2`, derived from ericw-tools) and VibeMap3
(`external/compilers/vibemap3`, continuing q3map2 from NetRadiant Custom),
replacing the stock ericw-tools and NetRadiant Custom q3map2 submodules. They
are developed as part of the VibeStudio project, stay separate GPL executables,
and are discovered under their own names (`vibemap2-*`, `vibemap3`) and their
pre-rename names (`vmt-*`, `q3mapx`). Stock tools remain usable through explicit
executable overrides. ZDBSP and ZokumBSP are unchanged. Fork URLs, pins and the
planned repository renames are in
[Compiler Integration](COMPILER_INTEGRATION.md#vibestudio-compilers).

VibeStudio-owned compiler orchestration should not depend on a GUI. Every
compiler run must be reproducible from the CLI.
The active wrapper slices define VibeMap2, Doom-family node-builder, and
VibeMap3 command profiles with CLI command planning, manifest writing/loading,
run/rerun execution, copyable command lines, output registration, and GUI
readiness/run summaries by engine/stage. Process execution captures
stdout/stderr, parsed diagnostics, duration, exit code, file hashes, and
manifest records through the shared core runner.

## Testing And Quality Stack

Use Meson tests as the test runner entry point. Keep fast C++ executable tests
for core services and parsers. Use Qt Test for Qt-specific behavior. Add fixture
corpora for packages, maps, scripts, textures, audio, and models as support
lands.

Fuzzing for untrusted binary parsers is now part of that runner rather than a
future item. `src/core/parser_fuzz.{h,cpp}` turns a handful of valid seed
buffers into a fixed corpus of corrupted ones through seven mutations, and the
`parser-fuzz-smoke` test runs that corpus at the inflate, idTech image, BSP,
package, level-map, and model readers. Determinism is the requirement that
shaped it: the generator is the `xorshift64*` implemented in that file, never
`QRandomGenerator` and never a clock, the seed is a compile-time constant with
an environment override, and every case carries an id naming the seed input,
the mutation, and the offset, so a CI failure is re-runnable from the log
alone. `corrupt-fixture-smoke` is
the readable counterpart, pinning the exact message each hand-built damaged
fixture produces. Both test files assemble their fixtures from published format
layouts; neither embeds or reads commercial game data.

Tests that check what the 3D renderer draws call
`exitCodeWithoutRenderer()` from `src/tests/render_test_support.h`: where
neither OpenGL nor Vulkan starts they skip (exit 77), or skip only their
drawing checks, and with `VIBESTUDIO_RENDER_REQUIRE=1` a missing renderer is a
failure instead. Most tests run on Qt's offscreen platform, where only Vulkan
draws on Windows and macOS. Hosted runners have no GPU, so CI gives the Linux
and Windows jobs Mesa's lavapipe, a Vulkan driver that runs on the processor,
and sets the variable, so every drawing check runs there. Linux installs
`mesa-vulkan-drivers`; Windows runs `scripts/install_software_vulkan.py`,
which fetches LunarG's Vulkan loader and a mesa-dist-win build of lavapipe,
both pinned by version and SHA-256, and points `VK_DRIVER_FILES` at lavapipe's
manifest; then `vibestudio --cli render backends --json` must report Vulkan as
the active renderer before the tests start. The same script sets lavapipe up
on any Windows machine without a GPU. Hosted macOS runners still skip the
drawing checks. `render-device-smoke` compares the backends with
each other on any machine that has both, and `render-cli-smoke` covers the
`render` commands. Pretend a machine has no renderer with
`QT_QPA_PLATFORM=offscreen` and `VK_ICD_FILENAMES`/`VK_DRIVER_FILES` pointing
at a missing file.

The other new core modules land in the same runner: `model-mesh-smoke`,
`entity-definitions-smoke`, `package-compare-smoke`, and
`document-watch-smoke`. The document-watch cases construct the watcher with
filesystem notifications disabled and drive `pollAt()` with stepped
millisecond values, so the coalescing state machine is tested without an event
loop and without sleeping.

The active sample-project slice keeps tiny license-clean Doom, Quake, and
Quake III-family workspaces under `samples/projects`. `scripts/validate_samples.py`
checks their manifests, package folders, and compiler command plans locally and
in PR CI using the built CLI.

Every new dependency must have:

- A documented role.
- License and attribution notes.
- Platform and packaging notes.
- A reason it beats a smaller local implementation.
- A validation path in CI or local scripts.

## Packaging And Release Stack

Use GitHub Actions and PakFu-style release scripting as the automation model.
Package with Qt deployment tools and platform-specific wrappers as needed.

The active packaging and release validation scripts are
`scripts/package_portable.py`, `scripts/generate_offline_guide.py`,
`scripts/validate_packaging.py`, `scripts/validate_release_assets.py`, and
`scripts/validate_credits.py`, validated locally and in PR CI. They create
versioned Windows, macOS, and Linux staging directories plus optional zips with
the built binary, documentation, generated offline guide, platform smoke notes,
samples, checksums, copied VibeStudio/imported-compiler license files,
schema-versioned package manifests, and checked attribution/submodule revision
pins. `scripts/build_release.py` and `scripts/package_windows_release.py` add
fresh-build evidence, Windows Qt deployment, runtime checks and matching source
archives using the runtime/source helpers. PR/nightly Windows jobs are configured
to upload the pair together; hosted execution must be verified independently.
Every Windows job builds with the MSVC 2022 toolset (14.44): the hosted
`windows-latest` image now defaults to Visual Studio 2026, whose MSVC 19.51
flags a deprecation inside Qt 6.10.1's own `qguiapplication.h` and
`qapplication.h`, which `build_release.py`'s warnings-as-errors build cannot
pass. The image carries the 2022 toolset alongside, and Qt's `msvc2022_64` kit
targets it.
These tools do not bundle external compiler executables or sign artifacts.

**Release pipeline (2026-10-07).** `VERSION` (Semantic Versioning 2.0) and
`CHANGELOG.md` (Keep a Changelog 1.1.0) are the release inputs;
`scripts/release_meta.py` derives the tag, title and every download name from
them, `scripts/version.py` and `scripts/changelog.py` maintain them, and
`scripts/release.py` resolves CI metadata, stages assets with `SHA256SUMS.txt`
and writes release notes. `.github/workflows/release.yml` builds and tests every
platform, then publishes on a `v<VERSION>` tag or a dispatched run. Native
packaging per platform: an Inno Setup 6 installer plus the verified portable ZIP
on Windows (`scripts/package_windows_installer.py`), a `macdeployqt`-deployed,
ad-hoc-signed `VibeStudio.app` in a create-dmg disk image on macOS
(`scripts/package_macos_app.py`), and a linuxdeploy AppImage built from
`meson install --destdir` on Linux (`scripts/package_appimage.py`, using the
desktop integration in `packaging/`). Decisions: keep Meson as the only build
system (no CPack), keep packaging logic in Python scripts that run the same way
locally and in CI, build the AppImage on the oldest supported runner, and ship
unsigned builds with documented first-launch steps until certificates exist.
See [Releasing](RELEASING.md).

**Documentation and brand assets.** The user manual is Markdown in
`docs/manual`, rendered on GitHub and by `scripts/build_docs_site.py`
(Python-Markdown and Pygments, build-time only) into a static HTML site that
ships in every package and works from `file://`. Brand artwork is generated by
`scripts/generate_branding.py` (fontTools, Pillow) from one geometry description
and the vendored Manrope typeface; the app icon is compiled in as a Qt resource
and a Windows icon resource. See [Branding](BRANDING.md).

The active About/Credits/license surface is backed by structured metadata in
`src/core/studio_manifest.*`, exposed through the GUI inspector and CLI
`--about`/`about` commands, and points to `LICENSE`, `docs/CREDITS.md`,
`docs/DEPENDENCIES.md`, and `docs/PACKAGING.md`.

Every release artifact must include VibeStudio license text, third-party
license files, compiler/toolchain attribution, and a generated credits bundle.

## Explicit Non-Goals

- Do not rewrite the application shell in a web stack.
- Do not make Electron, Chromium, or a webview the primary UI framework.
- Do not make AI services required for local editing, building, packaging, or
  launching.
- Do not assume a single AI provider, model family, or capability set.
- Do not make bgfx, Assimp, Tree-sitter, or KSyntaxHighlighting mandatory before
  the MVP needs them.
- Do not add a compression library for ZIP/PK3 work. The in-tree DEFLATE codec
  covers reading and writing; improve it in place rather than replacing it with
  a dependency.
- Do not make a GUI session a precondition for producing a map picture, a
  compile, or any other CLI-reachable output.
- Do not bypass shared services for quick GUI-only or CLI-only behavior.
- Do not import third-party code without updating README credits and
  `docs/CREDITS.md`.

## Reference Links

- [Qt Widgets](https://doc.qt.io/qt-6/qtwidgets-index.html)
- [Qt Quick](https://doc.qt.io/qt-6/qtquick-index.html)
- [Qt SQL](https://doc.qt.io/qt-6/qtsql-index.html)
- [Qt OpenGL / QOpenGLWidget](https://doc.qt.io/qt-6/qopenglwidget.html)
- [Qt Multimedia](https://doc.qt.io/qt-6/qtmultimedia-index.html)
- [Qt Network](https://doc.qt.io/qt-6/qtnetwork-index.html)
- [QFileSystemWatcher](https://doc.qt.io/qt-6/qfilesystemwatcher.html)
- [Qt Accessibility](https://doc.qt.io/qt-6/accessible.html)
- [Qt High DPI](https://doc.qt.io/qt-6/highdpi.html)
- [Qt TextToSpeech](https://doc.qt.io/qt-6/qttexttospeech-index.html)
- [Qt internationalization](https://doc.qt.io/qt-6/internationalization.html)
- [Qt Linguist `lrelease`](https://doc.qt.io/qt-6/linguist-lrelease.html)
- [QTranslator](https://doc.qt.io/qt-6/qtranslator.html)
- [QSyntaxHighlighter](https://doc.qt.io/qt-6/qsyntaxhighlighter.html)
- [RFC 1951, DEFLATE Compressed Data Format Specification version 1.3](https://www.rfc-editor.org/rfc/rfc1951)
- [RFC 1950, ZLIB Compressed Data Format Specification version 3.3](https://www.rfc-editor.org/rfc/rfc1950)
- [PKWARE .ZIP File Format Specification (APPNOTE.TXT)](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT)
- [Quake Specifications](https://www.gamers.org/dEngine/quake/spec/quake-spec34/)
- [Inter-Quake Model specification](http://sauerbraten.org/iqm/)
- [GtkRadiant, source of the `/*QUAKED` definition block](https://github.com/TTimo/GtkRadiant)
- [Valve Forge Game Data (`.fgd`) format](https://developer.valvesoftware.com/wiki/FGD)
- [Meson](https://mesonbuild.com/)
- [Ninja](https://ninja-build.org/)
- [bgfx](https://bkaradzic.github.io/bgfx/overview.html)
- [SQLite FTS5](https://sqlite.org/fts5.html)
- [CLI11](https://github.com/CLIUtils/CLI11)
- [KSyntaxHighlighting](https://api.kde.org/frameworks/syntax-highlighting/html/index.html)
- [Tree-sitter](https://tree-sitter.github.io/tree-sitter/)
- [miniaudio](https://miniaud.io/)
- [Assimp](https://www.assimp.org/)
- [OpenAI Responses API](https://platform.openai.com/docs/api-reference/responses)
- [OpenAI tool calling](https://developers.openai.com/api/docs/guides/tools)
- [Claude API documentation](https://platform.claude.com/docs/en/home)
- [Gemini API reference](https://ai.google.dev/api)
- [ElevenLabs documentation](https://elevenlabs.io/docs/overview/intro)
- [Meshy API documentation](https://docs.meshy.ai/en)

### UDMF integration, 2026-10-05

Lossless UDMF authoring uses original bounded C++/QtCore parsing and Qt6 Widgets.
It adds no library or mandatory cloud service. Existing level history, scene
locks, recovery, package snapshots, textured preview, ZDBSP orchestration and
node/launch validation are shared. Advanced namespace rendering and topology
creation remain separate acceptance gaps rather than a new editor architecture.
Native transforms reuse the affine service and lossless scalar writer. GUI move,
rotate and resize now use the existing cancellable placement worker and guarded
publication; CLI uses the same authoring rules. No new dependency, compiler fork
or separate document/history implementation is introduced.

## Shared formats and portable workspaces (2026-10-05)

Native workspace persistence uses bounded Qt JSON, `QLockFile`, SHA-256,
`QSaveFile` and no-replace publication. `.vibeworkspace` v1 links existing
project/module documents; it adds no database or archive dependency. The format
catalog supplies GUI classification/import filters and CLI capability reporting.
PakFu-derived DDS, FTX and SWL codecs integrate into the existing QImage pipeline;
DDS/FTX writers use texture validation/staging. No renderer, UI framework or
build-system change is involved. See [Workspaces](WORKSPACES.md) and
[Asset Formats](ASSET_FORMATS.md).


Stem delivery decision (2026-10-06): retain the existing C++ graph and atomic
Audio publication services. Track/bus taps use bounded graph pruning and shared
time ranges; no new audio dependency or native-session format is introduced.
Stateful WAV encoding continues seeded TPDF noise across successful blocks.
Multi-file output is explicitly partial-capable, with guarded per-file commits
and a version-1 JSON status/hash manifest. GUI/CLI controls share the service.
Editable session interchange and general loudness delivery remain open.

## Quake III Native Animation

Quake III player animation configuration uses original C++/Qt Core services in `core/model_q3_animation` and optional assembly schema 2 bindings. Qt Widgets provides the native-slot editor; existing assembly workers, CLI, undo/recovery and guarded exports remain authoritative. There are no new libraries or renderer changes. [Format, timing and integration limits](MODEL_ASSEMBLY.md#quake-iii-native-animation) are explicit.

Per-part `.skin` references use optional assembly schema 3 and the existing
`core/model_skin_bindings` reader. Qt Widgets provides the source/occurrence
controls; worker resolution, package snapshots, material previews, CLI, history
and guarded exports are shared. This adds no library or renderer dependency.
See [linked-skin contracts](MODEL_ASSEMBLY.md#linked-skins).

## Native Player Package Service

Native player publication uses existing Qt6/C++20 assembly, image, dependency,
package-staging and atomic-write services. `core/model_player_bundle` and the
assembly GUI/CLI add no dependency or rendering migration. Original Quake III
loader functions are compiled only in an optional independent acceptance harness;
see [player package contracts](MODEL_ASSEMBLY.md#native-player-packages) and
[licence/attribution](CREDITS.md#quake-iii-animation-configuration).

Continuous session playback loops (2026-10-06) reuse the prepared C++ graph and
`AudioTimelineLoop` mapping shared with recording. A separate unwrapped transport
clock preserves effect/routing/modulation state while the cursor repeats. Both
render clocks share boundary splitting; compensated playback primes wrapped
future context once. Dormant loop bounds reserve routing storage during prepare,
so policy changes reset without allocation. Physical loop time has a separate
64-bit guard; authored/native timeline bounds remain unchanged. No new library,
file schema or UI framework is introduced.
