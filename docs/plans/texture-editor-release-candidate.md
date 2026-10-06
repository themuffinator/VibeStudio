# Texture editor release-candidate gates

Objective: achieve a release-candidate, professional-grade texture editor for
VibeStudio's idTech1, idTech2, and idTech3 workflows. The earlier raster editor
is a foundation, not evidence that this objective has been achieved.

This is the completion checklist. An unchecked gate stays required even when
the current tests pass. Claims require evidence from the current source and
runtime, with limitations recorded in the user guide and support matrix.

## Authoring and documents

- [x] Editable layered documents: add, import, duplicate, rename, reorder,
  hide, lock, opacity, blending, merge/flatten, and active-layer editing.
- [x] A bounded, versioned native project format preserves layers and editor
  metadata exactly across save/reopen; raster export cannot mark an unsaved
  layered project clean or silently destroy layers.
- [x] Reliable pixel tools: pencil/brush, erase, line/rectangle/ellipse,
  eyedropper, connected fill with tolerance, and explicit alpha behavior.
- [x] Selection clips edits; copy/cut/paste, move, clear, crop, resize, rotate,
  flip, and canvas sizing work on the correct layer/selection with undo.
- [x] Seamless texture authoring: repeat preview, wrapped painting, and offset
  operations; zoom anchored at the pointer, pan, grid, and accurate readouts.
- [x] Undo/redo covers content and structure with meaningful labels, bounded
  memory, gesture grouping, cancellation, and correct saved-state tracking.

## Safety and performance

- [x] Atomic saves with explicit overwrites, external-change protection,
  backup/recovery, failed-save retention, and tested close/new/open flows.
- [x] Per-document autosave/recovery can be inspected and restored without
  overwriting the source; corrupt and unsupported records fail safely.
- [x] Bounded decoding, layer/project allocation, recipe complexity, and undo
  storage; malformed inputs never partially replace an open document.
- [x] Long operations run asynchronously with useful progress, cooperative
  cancellation, and stale-context rejection. Measure representative editing
  and conversion workloads and fix unacceptable stalls.

## Game output and integrated workflows

- [x] PNG/TGA/PCX and native Quake miptexture/WAD2, Quake II WAL, Doom flat/patch
  export have explicit profiles, palette provenance, correct alpha semantics,
  metadata, dimensions, and generated/previewable mipmaps where applicable.
- [x] Writer outputs round-trip through independent readers or format fixtures;
  native structures and runtime constraints are checked rather than inferred
  solely from a matching encoder/decoder pair.
- [x] Project assets, packages, level textures, model materials, shader image
  references, and dependency review share the authored output. Staging never
  masquerades as a disk save; unsupported destinations are explicit.
- [x] Staged palette/image changes invalidate dependent previews. Native WAD
  handoffs preserve namespaces and lump types. Packaging/compile handoffs are
  repeatable through the same services from GUI and CLI.
- [x] CLI supports document inspection, editing, native project persistence,
  export profiles, validation, recovery, dry runs, and structured diagnostics.

## Release verification

- [x] Accessible controls and canvas actions; keyboard workflows, focus,
  scaling, both high-contrast themes, RTL, and translation expansion verified.
  Do not inject mouse/keyboard input without the user's required permission;
  direct command/state tests and widget rendering are permitted substitutes
  for automated command coverage, with any remaining manual gap stated.
- [x] Meaningful tests exercise data loss/conflicts, malformed formats,
  structural undo, transparency, export fidelity, cancellation, integration,
  and CLI behavior. The breadth of evidence must match the breadth of claims.
- [x] Meson/Ninja build, relevant full regression checks, documentation,
  localization, sample workflows, and current-platform packaging pass.
- [x] Cross-platform source/build portability and available platform checks are
  audited. Unavailable platform/manual gates are not claimed as passed.
- [x] User documentation, support matrix, architecture, CLI, stack/dependency
  records, credits for any external material, and release notes match reality.
- [x] Final requirement-by-requirement audit proves these gates on the current
  worktree. No known data-loss, correctness, or release-blocking defect remains.

## Work log

- 2026-10-04: Revalidated the initial implementation. It is single-surface,
  PNG-only, lacks native project persistence and recovery, has no clipping
  selection/layer workflow, and cancellation only discards completed results.
  Existing tests prove the initial slice, not professional release readiness.
  The previous goal turn made concrete progress; this broader goal remains
  active. Start with the document model and lossless layered persistence.
- 2026-10-04: Implemented bounded layered authoring, clipping selections,
  structural/pixel undo, `.vtexture` persistence, embedded palette metadata,
  source-conflict checks, separate PNG export, shell project opening, and
  layered CLI recipes/inspection. The first two document gates have core,
  executable CLI, and offscreen GUI evidence. Raster imports normalize to the
  documented 8-bit working representation; project saves preserve it.
- Added cooperative cancellation checkpoints and progress for fill, recipe
  strokes, palette scanlines, and layered transforms. Recipe strokes have a
  pixel-write work cap; non-dithered palette conversion has a bounded exact-RGB
  cache. A 512 × 512, 16-color conversion measured 20 ms in the current Windows
  debug test run. This single workload is not a full performance sign-off.
- Six targeted suites passed after the cancellation/inspector changes:
  texture-document, texture-project, texture-editor-ui, idtech-image,
  asset-tools, and level-package-workflow. Documentation/source layout checks,
  136-command CLI coverage, 21 translation catalogs and 174 English plurals
  passed. Evidence is under `.agents/tmp/texture-editor/evidence`; the next
  source change always requires its own appropriate verification.
- The inspector uses a section selector after rendered 100% and 200% RTL
  layouts exposed crowded horizontal tabs. A transient Code-page API mismatch
  was fixed by concurrent work; an unrelated model-editor missing `override`
  annotation was corrected here to allow the warnings-as-errors build.
- Added verified backups before native project replacement, deduplicated by
  source SHA-256. Dry runs are write-free; backup corruption/failure preserves
  the original source. The final Windows debug build and all six targeted
  suites passed again, including explicit high-depth import normalization and
  backup integrity tests. The GUI suite took 57.93 seconds during concurrent
  builds; this is test elapsed time, not editor latency. Sanitized results are
  in `.agents/tmp/texture-editor/evidence/layered-project-tests.json`.
  The temporary package-publication missing-test build break resolved when
  the concurrent change supplied its source file. The release-candidate goal
  is still active; passing these tests does not satisfy the unchecked gates.
- 2026-10-04: Added `.vtrecovery` envelopes with bounded metadata, native layered
  payloads, checksums, source provenance that is never followed, streaming
  inspection, and cancellable restoration. The background writer keeps one
  active and one coalesced snapshot without undo buffers; retirement cancels
  in-flight work and prevents discarded drafts from reappearing.
- Added profile-local automatic checkpoints (30 seconds by default, configurable
  from 5 to 600), inspection/restoration controls, and `texture recoveries` /
  `texture recover` CLI commands. Restored drafts require a new first-save
  destination. Selected recovery files remain available, including self-recovery
  after saving newer work. New project publication uses a same-directory temporary
  file with no-overwrite publication. Known palette/path metadata is now validated
  by the core for GUI/CLI parity; project decode checks cancellation between layers.
- Save/Discard/Cancel now has direct-widget evidence for New, Open, editor Close,
  failed saves and opens, and the actual main-window save continuation. Recovery
  tests cover a real timer, unexpected owner destruction, metadata-only changes,
  stale checkpoint selections, damaged records, queue coalescing and retirement,
  and CLI dry-run/no-overwrite behavior. No input injection or OS capture was used.
- All six suites passed after the final source changes: texture-project,
  texture-document, texture-recovery, texture-editor-ui, texture-recovery-ui, and
  settings. The GUI suites passed in 37.86 and 54.66 seconds. Earlier parallel runs
  exceeded 90 seconds under concurrent work; these full-shell suites now run
  serially. Timing markers attribute much of the runtime to shared shell creation,
  not texture operation latency. This is not the broad performance sign-off.
- Widget rendering exposed and verified a fix for inspector width after changing
  font scale while the editor is open. Current evidence covers 100% dark,
  200% high-contrast dark/RTL/expanded text, and 200% high-contrast light with each
  inspector section checked. Sanitized test records and rendered evidence are in
  `.agents/tmp/texture-editor/evidence/recovery-tests.json` and the adjacent PNGs.
- Documentation, source-layout, offline-guide freshness, 21 translation catalogs,
  and 175 English plural messages passed. CLI documentation validation passed for
  142 registered commands. A newly added model-recovery translation helper blocked
  extraction; its calls were made directly extractable without changing behavior.
  Concurrent package-staging and audio-waveform registration gaps resolved during
  verification. The audio editor still has a separate deferred shell-close gap.
- 2026-10-04: Added deterministic square/round brushes with Replace RGBA and
  Blend over. Blend over samples gesture-start pixels so retracing never compounds
  alpha within one stroke. The optimized rasterizer accumulates row spans; 1,728
  direction/width/selection/wrapping cases match a separate pixel-stamp reference.
- Added inclusive line/rectangle/ellipse masks, filled or inward outlines,
  cancellable worker execution, shape previews, and two-endpoint cursor commands.
  Fixed reversed rectangle/selection drags omitting their endpoint pixels. Invalid
  typed colors no longer disable Eyedropper, Eraser, or Selection. Untouched indexed
  layers retain their format and save state after clipped/transparent paint.
- Fill now supports seed-relative tolerance in every original RGBA channel and
  optional four-connected edge wrapping. Wrapped strokes retain their path across
  repeated tiles; selections clip after wrapping. Cyclic offsets preserve exact
  indices/hidden RGBA within the selected area; half-size offset aids seam repair.
  Wheel zoom preserves the fractional image coordinate at its anchor. GUI and CLI
  share these operations, validation, cancellation and undo behavior.
- Six painting suites passed: texture-paint (2.26 s), texture-project (0.56 s),
  texture-document (3.26 s), texture-recovery (1.25 s), texture-editor-ui (61.83 s),
  and texture-recovery-ui (50.90 s). A 2048 × 2048 diagonal stroke with a 128-pixel
  round blend brush measured 42 ms in the Windows debug build. This is a measured
  workload, not the complete performance gate. The GUI tests still spend substantial
  time constructing the shared shell. Sanitized evidence: `painting-tests.json`.
- Added `core/texture_output.*` after reviewing the remaining PNG publication
  race. It captures the destination before encoding, rejects changed/deleted/newly
  appeared targets, and uses guarded new-file publication or checked atomic
  replacement. Output and existing destination inspection are bounded to 64 MiB;
  dry runs remain write-free. PNG uses it through the same GUI/CLI document service.
- The follow-up export tests passed with the new service: texture-document smoke,
  including the freshly linked CLI, passed in 1.95 s. The directly run texture GUI
  regression passed and reached its final marker at 36.26 s. Tests cover destination
  appearance, modification, deletion, repeated publication, byte bounds and dry runs.
  Evidence is in `output-tests.json` and `output-ui-direct.log`; owned source hashes
  are in `painting-output-source-hashes.json`. No input injection or OS capture was used.
- During verification, concurrent model-recovery and Quick Open declaration changes
  temporarily broke the shared application build; their owners' later changes
  resolved them. Model file/work registration also caught up. Added the missing
  Meson registration for the already implemented audio-delivery source/header to
  resolve its CLI link failure. The unrelated audio deferred shell-close gap remains.
- Final documentation/source-layout/offline-guide checks pass; all 21 catalogs
  extracted and 176 English plurals are covered. Global CLI documentation validation
  now flags the concurrent `project files` and `asset audio-export` registrations as
  missing from the CLI guide. These are recorded separately from passing texture
  CLI tests and remain part of the eventual whole-worktree release audit.

- 2026-10-04: Added `core/texture_transform.*` and shared GUI/recipe commands for
  canvas sizing, selected-pixel resizing and clockwise quarter turns. Nine image
  anchors handle both growth and shrinkage; explicit CLI canvas offsets are also
  supported. Canvas operations keep all layers aligned, including locked/hidden
  layers. Selected transforms require an editable layer and reject clipping.
- Exact transforms, selected flips, moves and clears preserve working palette
  indices and hidden RGBA. Transparent padding reuses/appends a palette entry or
  promotes a full opaque palette to RGBA. Nearest sampling uses pixel centers;
  optional smooth resampling documents its bounded Qt cancellation interval.
  Inspector previews show placement and explain disabled actions.
- Added selection-only transform undo without marking saved content dirty.
  A new alternating large/small-canvas regression reproduced an existing undo
  memory defect: undoing to a small canvas could exceed the additional 128 MiB
  budget. History now rechecks that budget after Undo and Redo, preserving the
  immediate inverse operation and evicting older states when required.
- Seven suites pass with the final behavioral changes: texture-transform
  (0.43 s), texture-document including executable CLI (2.83 s), texture-paint
  (1.35 s), texture-project (0.39 s), texture-recovery (0.92 s), texture-editor-ui
  (40.96 s), and texture-recovery-ui (48.05 s). The new tests cover all nine anchors,
  independent pixel mappings, palette capacity, hidden RGBA, layer alignment,
  bounds, early/mid/final cancellation, persistence, recipe rollback and history
  pruning. The initial RGB32 assertion was corrected to compare the documented
  normalized working pixels. A later source comment clarification changes no behavior.
- The 2048 × 2048 exact quarter turn measured 35 ms and canvas placement 12 ms in
  the Windows debug build. Direct widget commands and widget-rendered captures
  verify the new controls at 100% and 200% high-contrast dark/RTL/expanded text;
  the recovery UI suite also checks high-contrast light. No input injection,
  live clipboard testing or OS capture occurred. Clipboard/platform/manual
  verification remains part of the unchecked release audit.
- Sanitized evidence is in `transform-tests.json`, with the reproduced history
  failure in `transform-history-before.log` and layout captures named
  `texture-transform-*.png` under `.agents/tmp/texture-editor/evidence`.
  Documentation/source-layout/offline-guide checks pass; 21 catalogs extracted
  and the current 174 English plural messages are covered. New concurrent
  level-materials and code-search files and an audio-analysis declaration briefly
  blocked shared builds, then resolved. A subsequent complete application rebuild
  passes. The initially missing `map materials` documentation was supplied by
  concurrent work; final CLI documentation validation passes for 153 registered
  commands. The separate audio deferred shell-close gap is still present.
  Native output and all other unchecked requirements remain required.

- 2026-10-04: Implemented nine shared export profiles, explicit alpha/matte/
  threshold behavior, palette provenance and generated-palette consent, four
  generated mip levels, Quake fullbright handling, WAL metadata and reserved
  indices, and Doom flat/patch output with bounded tall posts. Native layouts
  and classic/port constraints are credited to the reviewed primary references.
- Added an Export inspector with worker previews, selectable mips, saved settings,
  metadata validation and native-file metadata import. Encoding cancels before
  a separate short guarded publication job. Export never marks an edited project
  clean. CLI `texture profiles`, `texture validate` and `texture export` share
  the service; `texture edit` retains native metadata in project output.
- Fixed two defects exposed by integration fixtures: Qt's identity-grayscale PNG
  optimization made some saved indexed projects unreadable, and actual supplied
  palettes could be labelled generated. Projects now retain per-layer index
  tables and reopen legacy grayscale payloads. Indexed PNG exports explicitly
  retain color type 3. Matching duplicate-color indices survive conversion of
  reserved WAL index 255, and an unused matte preserves authored fullbrights.
- Eight suites passed in the regression run: idtech-image, texture-document,
  texture-project, texture-recovery, texture-export, texture-editor-ui,
  texture-recovery-ui and texture-export-ui. After final CLI/UI changes, the
  affected document/CLI, export and export-UI suites passed again in 3.78 s,
  6.61 s and 20.99 s. The 2048 × 2048 indexed four-mip export measured 346 ms in
  the Windows x64 Qt 6.10.1 / clang-cl 20 debug build. This is one workload,
  not the broad performance sign-off.
- Independent Pillow 11.3.0 readers verified eight generated PNG/TGA/PCX fixtures;
  native formats have fixed binary header/offset/post fixtures as well as decoder
  round trips. The reusable optional verifier is `src/tests/texture_export_readers.py`.
  Layout renders cover 100% dark and 200% high-contrast dark/light with RTL and
  expanded text. No input injection, live clipboard testing or OS capture occurred.
- Evidence: `.agents/tmp/texture-editor/evidence/native-export-regression-tests.json`,
  `native-export-final-tests.json`, `export-independent-readers.json`,
  `texture-export-*.png`, and `verification-native-export.txt`. Meson/Ninja,
  docs/source-layout/credits/offline-guide checks, 160-command CLI documentation,
  21 translation catalogs and 176 English plural messages pass. Optional Pillow
  is documented as an external verifier; the required runtime stack is unchanged.
- Unrelated findings: a duplicate Code-panel MOC registration blocked generation
  and was removed. Stray closing fragments in the Spanish, French, Brazilian
  Portuguese and Russian catalogs were repaired; subsequent extraction passes.
  The separate audio Save-on-main-window-close continuation gap remains visible
  in current source. Other active work in the shared checkout was preserved.

## Native staging milestone — 2026-10-04

- The editor's Stage Export action now uses the selected native profile. File
  packages accept all nine formats; WAD2 accepts miptextures with matching names
  and type `0x44`; IWAD/PWAD accepts flats/patches with namespace placement.
  Existing global pictures are validated as patches before in-place replacement.
  Namespace markers and a new texture undo together, and the versioned package
  draft retains type, insertion anchor and required namespace. Missing markers,
  incompatible namespaces and ambiguous lump names block staging/publication.
- CLI `texture stage` writes a reviewable `.vibepackage` draft, with separate
  `--replace-texture` and `--overwrite` controls, dry-run validation, target
  palette resolution, structured export/plan reports and no source-archive write.
  Draft corruption fixtures verify that invalid native metadata cannot replace
  the live plan. Fixed WAD directories, repeated map lumps, nested namespaces,
  palette replacement, grouped history and deterministic publication are tested.
- Browser imports retain native names/WAL fields/patch offsets and the selected
  mip/frame. Staging checks the package revision as well as its path and map
  selection. Palette resolution reads planned content; invalidation clears
  thumbnail palette keys and encoded previews. Refresh Palette Source explicitly
  updates editor palette metadata without recoloring authored pixels.
- Nine suites passed in `native-staging-regression-tests.json`: image decoders,
  texture document/CLI, exporter/CLI, native staging/CLI, package content,
  package drafts, package staging, texture editor UI and export UI. After the
  last integration additions, native staging/CLI passed in 2.10 s and export UI
  in 22.21 s. The added real-shell WAD2 path initially timed out because its test
  looked for the package Undo control's old QPushButton class after another
  change made it a toolbar button. Using QAbstractButton fixed the fixture;
  `native-staging-shell-tests.json` verifies browser → editor → native package
  staging in 45.02 s, including unchanged source-WAD bytes. Unexpected dialogs
  now fail that fixture visibly instead of hanging it.
- Current checks: Windows x64 Qt 6.10.1 / clang-cl 20 debug build with warnings
  as errors; documentation/source-layout/credits/offline-guide validation;
  CLI documentation for 163 commands; all 21 translation catalogs with zero
  unextractable literals; 177 English plural messages. Inspected widget renders
  include the new palette control at 200% high-contrast/RTL with expanded text.
  No input injection, live clipboard use or OS capture occurred.
- Evidence is under `.agents/tmp/texture-editor/evidence/`, including the three
  native-staging test reports, `native-staging-source-hashes.json`,
  `staging-translations.json`, build logs and the widget renders. The source
  checkout is shared with other ongoing editor work; the full studio/platform
  release audit is still required and is not inferred from these focused checks.
- Unrelated findings: concurrent source changes briefly left level-primitive
  build registration and package/code declarations inconsistent; subsequent
  builds pass. The separate audio Save-on-main-window-close continuation gap
  remains in `ApplicationShell::closeEvent` and `AudioEditorDialog`.
- Two disposable fixture directories left by the timed-out test were checked
  as project-contained and without links. Automatic approval review rejected
  their removal with “blocked by policy”; no alternate deletion was attempted.
  They remain at `.agents/tmp/texture-editor/runtime/texture_editor_ui_smoke_test-iwSKkT`
  and `.agents/tmp/texture-editor/runtime/texture_editor_ui_smoke_test-LjzSrb`.

## Resource and preview milestone — 2026-10-04

- This goal turn made concrete progress. The full release-candidate objective
  remains active: 12 of 21 gates are checked, with nine still required.
- Shared decoding now preflights native and Qt image dimensions, 64 MiB input
  payloads, 16,777,216 pixels per decoded surface, 33,554,432 aggregate mip/frame
  pixels, 4,096 stored sprite frames, PCX scanline storage, Qt source depth and
  finite sprite timing. Editor imports lower the per-surface limit to 4,194,304
  pixels and 4,096 pixels per side. Failed decoding clears partial surfaces.
  Repeated Doom patch columns have a work cap, and truncated columns fail.
- Package image reads reject oversized directory/stored sizes before invoking
  a reader, require a complete payload and preserve exact occurrence selection.
  Compressed prefix previews stop streaming inflation at the requested prefix;
  full reads still verify declared output length and CRC. Indexed documents,
  paste and direct exports validate palette indexes; projects preflight metadata
  before encoding layers; CLI recipe reads and operation counts stay bounded.
- Independent cached-composite comparisons and a connected-region fill oracle
  accompany the optimizations. Dirty regions avoid recompositing all layers for
  each paint stamp; fill uses direct row storage with bounded checkpoints. Large
  document edits and undo/redo prepare their composite on the worker. A hidden
  editor's Cancel action now uses explicit job state rather than button visibility.
- The release benchmark on Windows 11 / i7-13700H / Qt 6.10.1 / clang-cl 20.1.7
  measured a 4M-pixel fill at 87.9 ms, 32M-layer-pixel composite at 25.6 ms,
  stamp plus preview at 3.8 ms, project encode/decode at 793/110 ms, WAL plus
  mips at 101 ms, noisy PNG at 809 ms and noisy dithered PCX at 4.26 s. The
  longest noisy-PNG checkpoint gap was 723 ms inside a bounded Qt codec call;
  this is a cancellation limit, not a UI-thread stall. Debug fill improved from
  12.0 s to 463 ms, and layered stamp/preview from 247 ms to 5.2 ms.
- Texture selection, thumbnails and package image previews now use value-only
  workers with one active and one replaceable pending request. Palette sources
  come from immutable staged-package snapshots and matching installations.
  Edit Selected reuses the displayed native metadata and mip/frame. Thumbnail
  scaling occurs on the worker and releases the original surfaces afterwards.
  Cancel Preview, Reload Previews, loading phases and progress expose work state.
- Seven bounded-read suites passed: package archive, preview and staging; image
  decoding; texture resources, export and native staging. The expanded worker
  suite passed with a 16M-pixel browser decode, 38 heartbeat ticks and a 12 ms
  maximum gap. Recovery UI, export UI, level/package workflow and package browser
  UI passed. The new real-shell cancel/reload check found that Reload advanced
  the source revision after capturing jobs, making its own results stale. The
  revision now advances first; stale completions request the current selection.
  The repaired shell regression passed in 50.1 s, including package image
  preview, cancel/reload, browser import and unchanged native source bytes.
  Cancellation fixtures now start a new request before cancelling; a completed
  preview correctly has no active Cancel action. Additional whole-shell theme
  renders are opt-in QA, separate from the normal 90-second regression budget.
- Evidence: `.agents/tmp/texture-editor/evidence/resource-*-tests.json`,
  `resource-release-workloads.json`, `resource-debug-workloads.json`,
  `resource-performance-environment.json`, `bounded-image-read-tests.json`,
  `preview-integration-tests.json`, `preview-ui-fixed-tests.json`, and the
  build/translation reports. No
  external code or dependency was added. Existing format attribution remains.
- Remaining performance work includes thumbnail cache retention for very large
  packages, explicit editor palette refresh, aggregate external-sprite read work,
  and cancellation granularity inside bounded decoder calls. These remain part
  of the unchecked latency gate, together with current-platform release checks.
- Unrelated audio save-on-shell-close still closes only the audio dialog after
  its asynchronous save; it does not resume the parent shell close. Transient
  concurrent audio/code/level build and source-registration errors were resolved
  in the shared worktree. The earlier cleanup policy rejection remains recorded.
- Automatic approval review also rejected removal of verified disposable
  `runtime/texture_editor_ui_smoke_test-EbHLam` and
  `runtime/texture_editor_ui_smoke_test-JsokQr` under the task temporary directory,
  with “blocked by policy”; no alternate deletion was attempted. The subsequent
  layout timeout also left `texture_editor_ui_smoke_test-HugXHu` and
  `texture_editor_ui_smoke_test-ODlkYG` there. Useful builds and evidence remain
  in the designated task directory.

- 2026-10-04: Bounded source thumbnails to 64 MiB/512 entries, with a 256-physical-
  pixel tile limit and eviction of view copies. Textures and Levels request at
  most 128 visible rows each; dimension/format searches retain offscreen metadata
  without retaining images. A 10,000-row demand probe measured 3 ms at either
  end after replacing the initial linear rectangle scan. Source changes clear
  old icons immediately; supplied level previews share the same bounded cache.
- Explicit editor palette selection/refresh now resolves immutable package and
  installation snapshots on the existing cancellable worker. Cancelled/stale
  results preserve the saved palette, visible choice, pixels and clean state.
  The focused UI test verifies a responsive heartbeat with a controlled slow reader.
- Added native decode cancellation at row/packet boundaries and a 64 MiB total
  SP2 external-frame read budget, charging repeated and compressed references.
  Package image reads request at most one byte beyond the captured entry size.
  The optimized 4M-pixel PCX decode measured 101 ms with a 1.4 ms longest
  cancellation checkpoint gap. Qt decoding still has bounded opaque calls.
- Auditing the browser found a legacy direct PNG save on the UI thread. It now
  uses a pixel snapshot, cancellable target inspection/encoding and a separate
  guarded publication worker. PNG output allocation is capped while writing;
  browser exports retain the 16M-pixel decode limit. Incompressible maximum-size
  RGBA pixels stop at the encoded limit; failed/cancelled output cannot replace
  the destination. The shared PNG sink also serves authoring GUI/CLI exports.
- The first combined run passed decoder/resource/palette/cache/worker/document/
  native-export/export-UI/level-material checks. Whole-shell editor/recovery tests
  timed out at 90 seconds after shell construction took 59–69 seconds. The model
  material test also exposed a concurrent adapter-contract mismatch; its current
  source distinguishes durable imports from read-only source verification.
  After rebuilding, editor UI (57.02 s), recovery UI (59.98 s), and model materials
  (1.46 s) passed. The editor test includes 66 native WAD textures and an offscreen
  dimension query. A three-times timeout multiplier accommodated the busy shared
  host; these successful runs also fit the normal 90-second limit.
- Final output verification passes: document/CLI (2.34 s), native export (5.65 s),
  export inspector UI (30.79 s), and browser PNG output UI (16.41 s after its last
  layout change). Rendering exposed clipped wrapped text and a disappearing Close
  action; the dialog now sizes to wrapped content and retains one action that
  changes from Cancel to Close. The strengthened test and inspected widget PNGs
  cover 100% dark and 200% high-contrast dark/light, expanded text and RTL.
- The optimized benchmark passes all 17 workloads. A noisy 4M-pixel PNG encoded
  in 795 ms with a 3.1 ms maximum checkpoint gap; cancellation returned in 4.5 ms.
  The corresponding PCX decode took 102 ms with a 0.6 ms maximum gap. Opaque Qt
  PNG decoding still measured a 66 ms gap, and per-layer project encoding 112 ms.
  These measurements use Windows 11, Qt 6.10.1, clang-cl 20.1.7 `/O2` and the
  i7-13700H recorded in `resource-performance-environment.json`. They are workload
  evidence; whole-shell release performance remains an open audit item.
- Evidence under `.agents/tmp/texture-editor/evidence`: `bounded-preview-final.json`,
  `png-output-regression.json`, `png-output-verified.json`,
  `png-output-action-verified.json`, `png-output-release-tests.json`,
  `png-output-release-workloads.json`, `texture-png-output-*.png`, and the
  adjacent build/documentation/localization reports. Earlier failures remain in
  their reports for comparison; the later affected-suite results supersede them.
  The Windows warnings-as-errors application build passes. No external code or
  dependency was added. The full goal remains active with 12 gates checked and
  nine pending.
- Unrelated audio save-on-shell-close still lacks a parent close continuation.
  Other sections of the release notes retain obsolete broad editor/rendering
  claims and need their owners' final audit. Existing policy-blocked temporary
  folders remain; subsequent timed-out tests also left synthetic fixtures under
  the same task runtime directory. After confirming absolute paths, synthetic
  fixture contents and zero links, automatic approval review rejected cleanup
  of `texture_editor_ui_smoke_test-{CQQiOj,KcfMRr,HugXHu,ODlkYG,hnboBP,RNdnrb}`
  and `texture_recovery_ui_smoke_test-{UOxkPL,wPRygw}` with “blocked by policy”.
  All remain under `.agents/tmp/texture-editor/runtime`. No cleanup workaround
  was attempted.

## Project and compiler handoff milestone — 2026-10-04

- Added `core/texture_handoff.*`: destination/profile validation and atomic
  in-memory staging plus map application. Quake III PNG/TGA and Quake II WAL
  require the portable `textures/` prefix and lower-case profile extension;
  WAD2 miptextures use matching embedded/lump names. A failed handoff changes
  neither document. Updating pixels under an existing selected reference works
  without a redundant map undo command. Standalone map apply keeps its existing
  no-change diagnostic. The UI shows invalid-destination reasons in a tooltip.
- The new handoff suite follows a layered project through direct brushes, a
  shader, an authored/placed MD3, dependency review, package undo/redo, draft
  reopening, deterministic PK3 publication and saved-map material previews.
  Native WAD2/WAL references resolve in level previews and dependency review;
  invalid paths, selection failures and unapproved replacement roll back.
- `texture_compiler_workflow.py` passes 22 CLI/compiler steps with synthetic
  assets. ericw-tools 2.0.0-alpha8 preserves all four authored mip levels exactly
  in Quake BSP output and compiles Quake II WAL references. q3map2
  2.5.17n-git-e62c6f4b consumes PNG/TGA shader images through BSP/VIS/LIGHT,
  followed by dependency review and a verified five-file PK3. Compiler hashes,
  versions, commands, logs and output hashes are recorded. This is compiler
  acceptance; no target game was launched or rendering acceptance inferred.
- Current focused results: handoff 0.72 s, native staging/CLI 1.77 s, package
  drafts 1.05 s, map editing 93.92 s, level materials 0.76 s, model materials UI
  2.80 s, and strengthened texture editor UI 99.09 s all pass. The first UI run
  missed an offscreen dimension-query result within its 15-second wait; the
  diagnostic rerun passed. Shell construction took 53.4 s on that debug run.
  Whole-shell release performance remains an open gate, not hidden by the
  three-times test timeout multiplier.
- Concurrent package header changes caused stale-object crashes during draft
  persistence; a clean build resolves them and all draft paths pass. Temporary
  link failures while new package UI registration landed also resolve in the
  current warnings-as-errors application build. No unrelated source was reverted.
- Evidence: `.agents/tmp/texture-editor/evidence/handoff-current-tests.json`,
  `handoff-preview-tests.json`, `handoff-current-build.txt`,
  `handoff-translations.json`, `handoff-plurals.txt`, and
  `.agents/tmp/texture-editor/compiler-handoff/verified.json` with adjacent
  step logs. Documentation/source/credits/offline guide checks and the CLI's
  175 registered commands pass. All 21 catalogs extract with 178 English
  plurals. No dependency or borrowed code was introduced.
- Two integrated-workflow gates are now complete: 14 of 21 gates are checked,
  seven remain, and the full release-candidate goal stays active. The unrelated
  audio Save-on-shell-close continuation and stale broad release-note claims
  remain outside this texture change. Earlier policy-blocked temporary folders
  remain under the designated task runtime directory.
- The two new crash fixtures (`texture_handoff_smoke_test-ipzYFq` and
  `texture_handoff_smoke_test-kdpECU`) were verified as synthetic test files
  under `.agents/tmp/texture-editor/runtime`, with no links. Automatic approval
  review rejected their removal with “blocked by policy”. They remain there;
  no alternate deletion was attempted.
- The final application rebuild passes (`handoff-final-build.txt`) and the
  final translation check has no errors. During concurrent editor work, the
  final source-layout check reports missing Meson registration for
  `core/language_signature.cpp/.h` and header installation for
  `core/model_tags.h`; its earlier successful result does not supersede these
  latest findings. See `handoff-source-layout-final.txt`. The full build/source
  release gate stays open. Texture changes and relevant binary/source hashes
  are recorded in `handoff-source-hashes.json`.

## Optimized latency and cancellable project saves — 2026-10-04

- Added opt-in `vibestudio.startup` timing by shell/UI phase and browser timing
  JSON. The optimized shell constructed in 1.04 s; a 66-native-texture dimension
  query completed in 399 ms with a 37 ms maximum UI heartbeat gap. This resolves
  the remaining release-build startup/query investigation. The earlier 53-second
  debug constructor and busy-host timeout remain recorded as debug observations.
- Added `texture-editor-latency-smoke` with a noisy 4M-pixel, eight-layer document.
  Its initial save took 13.95 s while responsive, but preparation offered no
  cancellation. Native projects now use separate cancellable preparation and
  guarded publication. Layer PNG writes share the bounded export sink; project
  reads and checksums check cancellation every 64 KiB. GUI and CLI share the
  preparation/publication services. No format, dependency or external code changed.
- The destination identity is captured before encoding and checked again before
  backups/publication. Competing creation, modification and deletion fail safely.
  Cancellation preserves dirty content, saved identity and recovery, creates no
  project/backup, and does not invoke a deferred close/open continuation. Only
  successful publication acknowledges the save. Existing backup/dry-run/conflict
  and actual main-window save continuation regressions still pass.
- Nine affected optimized suites pass: document/CLI, resource bounds, performance,
  project persistence, recovery/CLI, editor UI, editor latency, recovery UI and
  export UI. The 17 core workloads pass. Project encoding's maximum checkpoint
  gap fell from 111 ms to 50 ms; Qt PNG decoding remains a bounded opaque call
  measured at 64 ms. All timing claims describe this Windows/Qt/compiler/CPU
  configuration, not an untested universal latency guarantee.
- In the large-document UI workload, paint/refresh took 4.2 ms, undo/refresh
  22.1 ms, save 14.37 s, reopen 6.65 s, dithered PCX preview 7.60 s, and PNG
  publication 2.42 s. Those longer operations remained on workers: the largest
  observed UI gap was 34.2 ms. Requested save/open/export cancellation returned
  in 4.1/16.2/14.9 ms respectively. These tests call commands directly, exercise
  real workers and verify pixels/state; they inject no input or OS captures.
- Evidence: `.agents/tmp/texture-editor/evidence/performance-release-baseline-tests.json`,
  `performance-release-phases-tests.json`, the adjacent `texture-ui-timings.json`,
  `performance-release-resources-tests.json`, `editor-latency-baseline-tests.json`,
  `cancellable-project-regression-tests.json`, and `cancellable-project-build.txt`.
  The environment remains recorded in `resource-performance-environment.json`.
  Documentation, credits, source registration, all 21 translation catalogs,
  178 English plural forms and the current 178-command CLI documentation pass.
- The latency/cancellation gate is now complete on the available Windows host:
  15 of 21 gates are checked and six remain. Cross-platform, accessibility/manual,
  packaging, full current-source regression and final release acceptance remain
  separate required gates. The full goal remains active.
- A later timing-only test enhancement records preparation versus publication
  separately. Its build encountered a concurrent level-editor `FourViews` enum
  without a matching application-shell switch case. The nine-suite passing
  result precedes that unrelated build break. The concurrent level changes then
  resolved the mismatch and the optimized application rebuild passed. The final
  latency test also passes: save preparation 12.34 s, publication plus refresh
  138 ms, with a maximum UI gap of 24.1 ms across its ten workloads. Save/open/
  export cancellation returned in 5.6/14.9/15.2 ms. PNG publication plus refresh
  took 24.1 ms. See `editor-latency-phases-rebuild.txt` and
  `editor-latency-phases-tests.json`. A simultaneous two-job Ubuntu core build
  was running; the measurements include that host load.

## Portability and accessibility audit — 2026-10-04

- All 32 relevant optimized Windows suites pass in
  `evidence/texture-release-regression-tests.json`: texture authoring, native
  output, persistence/recovery, resource/worker/cache/latency checks, CLI,
  package/level/model handoffs and the affected shared decoder/language tests.
  The 66-image browser query took 547 ms with a 60 ms maximum heartbeat gap;
  the shell constructed in 1.04 s. These supersede the earlier debug startup
  concern, while preserving its evidence. Inspected widget renders remain
  readable at 100% and 200% high-contrast/RTL/expanded text.
- The available Ubuntu 24.04.3 WSL environment uses GCC 13.3 and Qt 6.4.2.
  Its warnings-as-errors build exposed shared-code portability defects:
  missing aggregate defaults, an unbounded scalar audio read warning,
  Qt-version-specific UTC constants, text-vector size narrowing and a newer
  wildcard API. Small compatibility fixes retain existing behavior; project
  glob fixtures now cover nested paths, Unicode, ranges and negated classes.
  Nine affected timestamp/package/project suites pass on Windows in
  `evidence/qt64-portability-windows-tests.json`. Linux verification is still
  running; this entry does not claim that build or runtime acceptance passed.
- The focused accessibility audit reproduced mouse-only toolbar buttons and
  unnamed overflow buttons (`evidence/accessibility-before.txt`). Texture
  toolbar buttons now participate in Tab navigation, including named overflow
  controls at enlarged scales. Shared image/palette preview roles are explicit.
  The new test covers all inspector sections, focus-chain metadata, document
  shortcut scope and changing canvas/palette descriptions across three themes.
  Its post-fix verification remains pending below; no input was injected.
- Current-platform portable package structure, CLI build validation, all three
  sample projects and release asset validation pass. The audio CLI fixture used
  an output inside its folder-package source; it now uses the existing separate
  disposable output fixture. Package validators check manifests, licenses,
  catalogs and checksums; they do not prove clean-machine runtime deployment or
  native macOS/Linux execution. Evidence is in `current-platform-*.txt` and
  `release-assets-validation.txt`. Source/docs/credits checks also pass, all
  21 catalogs extract without errors, and 183 English plural messages are covered.

- The final available Windows regression now passes all **40 suites**, including
  the post-fix accessibility audit, the affected text/glob tests and all earlier
  texture/package/material checks. `texture-accessibility-smoke` checks 94
  controls at normal scale and 92 at enlarged/RTL scale, including the actual
  overflow controls. Shared palette/image roles and their readouts pass. Source
  review confirms canvas key dispatch and theme focus outlines; direct command
  tests exercise the same authoring paths. Physical keyboard, live clipboard
  and native screen-reader acceptance remain explicitly unverified under the
  permitted substitute strategy. Evidence: `release-audit-regression-build.txt`,
  `release-audit-regression-tests.json` and `release-audit-final/` widget renders.
  The accessibility and meaningful-regression gates are complete: **17 of 21**
  gates are checked; packaging/platform/final-documentation/final-audit remain.
- The additional local Windows runtime package is staged under
  `.agents/tmp/texture-editor/runtime-package/vibestudio-0.1.0-rc1-win64-x86_64`.
  `windeployqt 6.10.1` staged Qt/plugin dependencies and the VC redistributable
  installer; the installer was not run. All 21 VibeStudio catalogs were compiled
  with the canonical Meson targets and included. The packaged CLI validation and
  offscreen shell both pass with Qt absent from PATH. Process module inspection
  observes all six Qt libraries plus the offscreen plugin inside the bundle.
  The shell's ready phase took 879 ms on this run. All **222** staged files were
  independently rehashed after updating its manifest and licence/SBOM records.
  This is a local, unsigned host validation package; native-window, screen-reader,
  clean-machine, signing and non-Windows package acceptance are not inferred.
  See `runtime-package-verification.json` and `runtime-package-checksums.json`.
- The deployed binary also passes the complete **22-step** generated-texture
  compiler workflow with the development Qt directories removed from PATH.
  All four Quake BSP mip levels remain exact, Quake II WAL/BSP references verify,
  and Quake III PNG/TGA shader inputs pass BSP/VIS/LIGHT and PK3 validation.
  Evidence is under `.agents/tmp/texture-editor/packaged-compiler-handoff`.
  This exercises the packaged texture CLI through final compiler/package output;
  it does not claim a game-rendering test.
- Linux/GCC/Qt 6.4 compilation passes for the entire shared core, texture canvas,
  editor, export/recovery/preview worker, shared image views and runtime source.
  Paint, transforms, resource limits, all 17 performance workloads, handoff,
  projects and game-installation suites pass. The portable project-search core
  fixtures also pass, including symlink and nested-glob checks. A synthetic
  plural translator omitted its nonempty-state override; the test fixture was
  corrected and passes on Windows, with Linux rebuild pending. An initial full
  application configure encountered a concurrently registered but not yet written
  `language_diagnostics.cpp`; the source subsequently arrived and the full Linux
  application/UI build is running. No Linux GUI/runtime acceptance is claimed yet.

## Current requirement audit

Completed 2026-10-04 for the documented, bounded texture workflow. All 21
gates are satisfied by the available automated evidence and the explicitly
permitted manual-test substitutes. This accepts layered 8-bit texture authoring
and the documented idTech output profiles; it does not accept the whole studio
or claim the unavailable platform/manual checks below.

The work log above preserves intermediate failures and pending decisions. This
section supersedes those historical status statements. Evidence paths below
are relative to `.agents/tmp/texture-editor/` in the development checkout.

| Requirement | Acceptance evidence |
| --- | --- |
| Editable layers | Document/project/UI suites cover add/import/duplicate/rename/reorder, visibility, locks, opacity/blending, merge/flatten and active-layer edits. |
| Lossless projects | Exact indexed/RGBA round trips preserve layers, palettes and export metadata. Invalid state rolls back; raster export never marks a layered project saved. |
| Pixel tools | Independent paint fixtures and document/UI tests cover square/round brushes, replace/blend alpha, erase, shapes, tolerant connected fill, eyedropper and cancellation. |
| Selection and canvas operations | Independent pixel mappings and GUI/CLI tests cover clipping, copy/cut/paste/move/clear, crop, resize, rotation, flips, nine canvas anchors and undo. |
| Seamless authoring and navigation | Wrapped strokes/fill, offsets, repeated preview, anchored zoom, pan, grid and pixel readouts pass command and state checks. |
| Bounded saved-state-aware undo | Structural and pixel edits retain saved revisions, grouped gestures, inverse operations and memory limits, including alternating canvas sizes and saturated history. |
| Atomic saves and transitions | Destination appearance/change/deletion, backup integrity, cancellation, failed saves, dirty-state retention and Save/Discard/Cancel across New/Open/editor/shell close pass. |
| Autosave and recovery | Real timers, coalesced workers, metadata-only edits, retirement, corrupt records, source isolation, restoration to a new destination and deferred shell-close continuation pass. |
| Resource and decoder limits | Preflight and malformed fixtures cover decoded surfaces, aggregate mip/frame data, palettes, complete package reads, projects, recipes, layer totals and history. Limits remain explicit in the guide. |
| Latency and cancellation | Seventeen core workloads and ten large-document UI workloads pass on the available builds. Background preparation, preview, cache and publication paths retain responsive UI heartbeats and stale-context rejection. Final guarded publication is intentionally non-cancellable. |
| Native output profiles | Nine GUI/CLI profiles verify dimensions, palette provenance/consent, alpha, native names/flags/offsets and previewable regenerated mip chains. |
| Independent output verification | Fixed native structures plus eight independent Pillow raster fixtures pass on Windows and Linux. Compiler output verifies all four Quake mip levels and Quake II/III references. No game-rendering claim follows. |
| Cross-editor handoffs | Layered projects, package drafts, native WAD2/WAL, Quake III shader images, level placement, model materials and dependency review retain authored output. Restaging unchanged references avoids redundant map history. |
| Namespaces and invalidation | WAD2 type 0x44, Doom namespaces/global-picture replacement, grouped undo, staged palette resolution, durable drafts and dependent material refresh pass. The packaged CLI passes the 22-step compiler/package workflow. |
| First-class CLI | Executable tests cover inspection, recipes, native projects, profiles, export/staging, validation, recovery, dry runs and structured failures through shared services. |
| Accessibility and interaction | Direct commands, accessible names/roles, scoped shortcuts, Tab focus chains, toolbar overflow, canvas readouts and every inspector section are checked at 100%/200%, in dark and both high-contrast themes, RTL and expanded text. Physical input, clipboard and native assistive technology remain unverified. |
| Meaningful regression | All 42 relevant suites have passing Windows and Linux results. Later integration checks cover the shared package/level/model changes; corrected fixtures check actual displayed inspector pages and model-surface pixels. Original failures remain in evidence. |
| Build, packaging and release checks | Optimized warnings-as-errors Meson/Ninja applications build on Windows and Linux/WSL. Documentation, source registration, credits, CLI documentation, catalogs/plurals, three sample projects, portable packaging and release-asset structure checks pass. The local Windows runtime bundle launches with Qt absent from PATH and is independently rehashed. |
| Cross-platform audit | Windows Qt 6.10.1/clang-cl 20.1.7 and Ubuntu 24.04.3 WSL Qt 6.4.2/GCC 13.3 are verified. Portable C++/Qt and explicit endian layouts remain intact. Missing Qt Save As defaults, invalid file timestamps and child-process path assumptions were corrected. Unavailable environments are listed below. |
| Aligned documentation and credits | Texture guide, CLI, support matrix, architecture, setup/accessibility, roadmap, stack/dependency records, README credits and release notes match the supported boundary. The offline guide is regenerated and checked. No runtime dependency was added for these acceptance fixes. |
| Final audit | The full texture goal is accepted within these documented bounds, with no known remaining texture data-loss or correctness blocker. Source/binary fingerprints bind the evidence to tested revisions in the concurrently edited worktree. Other studio development remains independently subject to acceptance. |

### Final evidence

| Evidence | Result and scope |
| --- | --- |
| `evidence/acceptance-windows-tests.json` | 42/42 pass in one optimized run; before/after snapshots contain 613 unchanged source files. |
| `evidence/acceptance-linux-tests.json` and `acceptance-linux-recheck-tests.json` | 38 initial passes plus four corrected rechecks. Three shutdown crashes came from a stale Code Index object compiled across a shared header edit; rebuilding it resolved all three. The model fixture excludes font-antialiasing pixels in the HUD. |
| `evidence/release-handoff-windows-tests.json` and `release-handoff-linux-tests.json` | Final integration runs: 17 Windows passes; 16 Linux passes plus the inspector-layout recheck below. No production texture change was needed. |
| `evidence/release-layout-windows-tests.json` and `release-layout-linux-tests.json` | Inspector checks activate every page even with captures disabled. Qt 6.4 had retained scrollbar ranges for never-shown pages. The test is compiled with canonical Meson commands against the verified release libraries; its build report records their hashes. |
| `evidence/release-handoff-windows-snapshot.json` and `release-handoff-linux-snapshot.json` | Exact application hashes and source inventories. Texture production sources are unchanged from the passing baseline. Later unrelated work is not silently included in the validated binary. |
| `evidence/final-independent-readers.json` and `final-linux-independent-readers.json` | Eight PNG/TGA/PCX outputs pass independent Pillow 11.3.0 checks on each platform. Native binary fixtures supplement these readers. |
| `release-compiler-handoff/verified.json` and adjacent logs | Packaged CLI passes all 22 steps with ericw-tools 2.0.0-alpha8 and q3map2 2.5.17n-git-e62c6f4b: exact Quake BSP mips, Quake II WAL/BSP references, Quake III PNG/TGA shader BSP/VIS/LIGHT and verified PK3. |
| `evidence/release-translations.json`, `release-plurals.txt`, `acceptance-compiled-catalogs-final.txt` | 21 catalogs extract with no errors, 195 English plurals are covered, and 21 Windows QM files are compiled. Presence does not mean translations are finished. |
| `evidence/release-validation.json` | Final documentation/source/credits/offline guide/CLI/sample/package checks, with individual logs. CLI coverage is 186 registered commands. |
| `evidence/runtime-package-verification.json` and `runtime-package-checksums.json` | CLI and offscreen shell pass without development Qt on PATH. All observed Qt modules load from the bundle; catalog/licence/SBOM records and all staged file hashes verify. |

The representative Windows browser run constructs the shell in 1,734 ms and
queries 66 native-image dimensions in 536 ms, with a 33 ms maximum heartbeat
gap. The 4,194,304-pixel, eight-layer UI workload records a 11.88 ms paint
refresh, 64.53 ms undo, 16.96 s save and 4.86 s reopen. Its largest UI heartbeat
gap is 21.64 ms; total worker time is not a UI-stall measurement. These are
host measurements under concurrent build load, not universal speed guarantees.
See `evidence/acceptance-windows/texture-ui-timings.json` and the latency JSON
inside `acceptance-windows-tests.json` for the full workload/cancellation data.

### Acceptance boundaries and unrelated findings

- Authoring is limited to 8-bit RGBA/indexed working pixels, 4,194,304 canvas
  pixels, 33,554,432 aggregate layer pixels and 32 layers. Pressure-sensitive
  painting, high-precision editing, sprite animation, WAD3 encoding and Doom
  wall-definition composition remain outside the implemented feature set.
- Physical mouse/keyboard operation, live clipboard transfer, native screen
  readers, native-window behavior, macOS/ARM, clean-machine installation,
  signing and target-game rendering remain unverified. Direct commands and
  widget render targets used no input injection or OS capture. No game launched.
- Linux evidence uses a Windows filesystem mounted at `/mnt/e` under WSL;
  native Linux filesystem durability is not established. Linux audio playback
  is disabled and missing `lrelease` uses source-language fallback. The local
  Windows package is unsigned, includes the unrun VC redistributable installer,
  and has not been published.
- Shared compiler discovery is slow with inherited Windows paths under WSL:
  the same binary's ready phase measured 29.17 s versus 1.80 s with a
  process-local Linux-only PATH. No system environment setting was changed.
- Audio Save during main-window close still closes its own dialog without
  resuming the parent close; the user must close the main window again.
  Texture Save resumes its parent close and has dedicated regression coverage.
- Later concurrent Doom-preview source additions briefly broke both shared
  builds (unused helpers and a plural-helper signature mismatch). Those edits
  are outside the validated application snapshots and need their own acceptance.
  The fixture-only rebuild preserves the already verified application libraries
  and records their hashes. Passing texture evidence is not a claim that every
  later unrelated worktree edit has been rebuilt or accepted.
- Automatic approval review rejected cleanup of earlier disposable fixtures
  and seven final diagnostic outputs with “blocked by policy”. The fixtures
  remain under the task's `runtime/` directory and the diagnostic outputs remain
  at the task root; no alternate deletion was attempted. Useful evidence and
  the runtime bundle remain in the designated project task directory.
