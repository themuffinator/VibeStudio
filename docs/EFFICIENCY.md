# Efficiency Philosophy

VibeStudio exists to make idTech1, idTech2, and idTech3 development faster,
clearer, and easier than stitching together many separate tools. Efficiency is
not only performance. It is the total time, attention, and uncertainty removed
from the creative loop.

The product should eventually let a creator move from idea to in-game test with
as few repeated manual steps as practical, while keeping control, attribution,
and reproducibility intact.

## Efficiency Goals

- Reduce setup time by detecting game installations, source ports, packages,
  compilers, palettes, project folders, and output conventions.
- Reduce context switching by keeping package browsing, editing, compiling,
  diagnostics, staging, and launch/testing inside one shared project graph.
- Reduce repeated work through presets, profiles, templates, batch operations,
  command manifests, reusable compiler pipelines, and CLI automation.
- Reduce uncertainty through visible task state, output paths, structured logs,
  validation summaries, dependency graphs, and detail-on-demand diagnostics.
- Reduce waiting pain by keeping heavy work asynchronous, cancelable, cached,
  incremental, and reported through the activity center.
- Reduce expert-only friction by offering clean default workflows while keeping
  raw logs, metadata, manifests, and format internals available.
- Reduce creative blank-page time through optional generative and agentic AI
  workflows that produce reviewable proposals, not hidden mutations.

## Landed Efficiency Work

These are implemented, not planned. Each names the code that does the work.

### Debounced Workspace Search

`src/app/application_shell.*`

The workspace search box previously ran its query on every `textChanged`
signal, and that query walks the project tree on disk as well as the mounted
package entry list. `QLineEdit::textChanged` now calls
`scheduleWorkspaceSearch()`, which sets a guard flag and arms a single 220 ms
`QTimer::singleShot`. Further keystrokes inside that window see the flag and
schedule nothing, so a burst of typing costs one tree walk instead of one per
character.

### Stylesheet Re-Application

`src/app/application_shell.*`

`applyPreferencesToUi()` rebuilds the entire window stylesheet and then
unpolishes and repolishes every widget, so calling it repeatedly is expensive.
A coalescing helper, `scheduleThemeRefresh()`, guards on
`m_themeRefreshScheduled` and defers the rebuild to a single queued invocation
per event-loop turn.

The call sites that fire repeatedly now go through it: `refreshPackageTree()`,
`refreshPackageStagingSummary()`, `filterPackageEntries()`, and
`refreshActivityCenter()`. That last one matters most, because the activity
centre refreshes on every streamed compiler log line — before this, a noisy
compile re-themed the entire window once per line of output. Typing in the
package filter is now one stylesheet rebuild for a burst of keystrokes rather
than one per character.

The one-shot sites — a preference change, opening or switching a project, and
the start-up path — still call `applyPreferencesToUi()` directly, because there
the user has just asked for a visual change and should see it on the same turn.

### Package Staging: Cached Plan, Lazy Bytes, Streamed Writes

`src/core/package_staging.*`

Three separate changes:

- **The staged plan is cached.** `ensurePlan()` recomputes the merged entry list
  and conflict set only when `m_planValid` is false, and `invalidatePlan()`
  clears it when an operation is appended or cleared. Summaries, compositions,
  before/after views, and the manifest all read the same computed plan instead
  of each recomputing it.
- **Base entry bytes are read on demand.** `PackageStagedEntry` no longer
  carries a resident `QByteArray`. Loading a package used to read every base
  entry's bytes into memory up front; it now records paths, sizes, and metadata
  only. `entryBytes()` reopens the source archive lazily — once, into a cached
  `std::shared_ptr<PackageArchive>` — and reads a single member when something
  actually needs its contents.
- **Writes stream.** The PAK, WAD, and ZIP writers take a `ByteSink` and an
  entry-bytes provider and push each member out as it is produced, into a
  `QSaveFile`. The old writers appended every member into one growing
  `QByteArray` before writing it. Manifest generation additionally memoizes
  SHA-256 digests by content key, so a file staged into several virtual paths is
  hashed once.

### Streamed Compiler Output

`src/core/compiler_runner.*`

A compiler run used to block on `waitForFinished()` and then call
`readAllStandardOutput()` / `readAllStandardError()`, so the log appeared only
after the tool exited. The runner now polls with `waitForFinished(100)` and
pumps both channels on every iteration, splitting complete lines out of a
pending buffer and handing each one to the log callback and the diagnostic
parser immediately. The same loop checks the cancellation callback and the
timeout, so a long q3map2 stage produces live output, can be cancelled, and can
be timed out instead of appearing frozen. Full stdout and stderr are still
captured for the manifest, and any trailing partial line is flushed after exit.

### One Build Pipeline Instead Of Three Hand-Fed Runs

`src/core/build_pipeline.*`

The Quake loop is three tools run in order — BSP, visibility, lighting — and
each one previously had to be selected and launched by hand as its own compiler
profile, with the user responsible for pointing each stage at the previous
stage's output. `runBuildPipeline()` replaces that with a declared chain.

Registered pipelines include `quake-full` (qbsp, vis, light through
ericw-tools), `quake-fast` (qbsp then light, visibility off by default),
`quake-bsp-only`, `quake3-full` and `quake3-bsp-only` (q3map2 BSP, vis, and
light stages), and the `doom-zdbsp` and `doom-zokumbsp` node builders. A stage
declares `inputFromStageId`, so the first stage is told where to write and the
in-place stages follow it automatically. `planBuildPipeline()` resolves stages,
inputs, and outputs without running anything, stages can be disabled per run,
`stopOnFailure` controls what happens after a failure, and per-stage callbacks
report start, finish, and log lines. Every stage still goes through the shared
compiler runner, so logs, diagnostics, hashes, and command manifests are
identical to a single-profile run. Launch plans in the same module carry the
result into the configured source port.

### Per-Block DEFLATE Block Type Selection

`src/core/deflate.*`

`deflateRaw()` cuts the input into chunks of at most `kMaxStoredBlock` bytes
(65535, RFC 1951 3.2.4) and, for each chunk, measures all three block types in
bits before writing one: a stored block (`3 + padding + 32 + 8 * length`, where
the padding depends on where the bit stream currently stands), a fixed Huffman
block, and a dynamic Huffman block (`trees.headerBits` plus the coded tokens,
or infinity when `buildDynamicTrees()` reports the tables are unusable). The
smallest wins, and ties go to stored, so a block is never larger than storing
its bytes would be.

That measurement is what decoupled the level from the block type. A level now
only tunes the LZ77 search in `matchConfigFor()`: `fast` walks a short hash
chain greedily, `default` and the new `best` use lazy matching over longer
chains, with `best` raising the chain limit to 512 and letting a held match run
to `kMaxMatch`. `store` still emits stored blocks and runs no matcher at all.
Every field in that table is a fixed constant, so the token stream depends only
on the input bytes and the output stays byte-identical for a given input and
level, which is what the package writers' reproducibility rests on.

`best` round-trips through `deflateLevelFromId()` and `deflateLevelId()`.
Nothing selects it yet: `PackageWriteRequest::compression` defaults to
`DeflateLevel::Default`, `package_staging.cpp` applies it to ZIP and PK3 output
only (`options.level = zipFamily ? request.compression : DeflateLevel::Store`,
so PAK and WAD ignore it), and the `--compression` token sits in the CLI option
table without a reader. The ZIP writer also stores a member outright whenever
the deflated payload is not strictly smaller, which keeps already-compressed
content byte-for-byte.

### Bounded Polling For External Changes

`src/core/document_watch.*`, `src/app/application_shell.*`

`ApplicationShell` drives `DocumentWatcher::poll()` from one 1000 ms
`Qt::CoarseTimer`, and `pollWatchedDocuments()` returns immediately while
nothing is registered. `registerWatchedDocument()` keeps one path per
`DocumentWatchRole`, unregistering the previous one, so the watched set stays
the handful of files the studio actually holds open rather than a growing list.

Each poll re-fingerprints every watched path, deliberately: a rewrite can land
inside the modification time's resolution and keep the same size, so checking
metadata first would miss it. What bounds the cost is the hash limit.
`fingerprintDocumentFile()` stats the file and hashes it only when its size is
at or below `kDocumentWatchHashSizeLimit` (4 MiB). Anything larger is marked
`hashSkipped` and compared on size and modification time alone, so an open
multi-hundred-megabyte PAK is never re-read by the timer. The hashing that does
happen reads `kDocumentWatchHashChunkBytes` (64 KiB) at a time into one
`QCryptographicHash`, so it never allocates in proportion to the file, and a
file that grows past the limit mid-hash stops with the `grew-while-reading`
error id instead of reading without a bound.

The rest of the saving is in prompts rather than cycles. `QFileSystemWatcher`
notifications are treated as hints that only mark a path worth re-checking —
`poll()` is authoritative — and a changed path is held back until it has looked
the same for `coalesceIntervalMsecs()` (250 ms by default), then reported once
with the number of folded-in notifications attached. An editor that truncates
and rewrites, or writes a temporary file and renames it over the target, is one
event. `Touched` — metadata moved, bytes provably identical — is not
substantive, so the shell says nothing about it at all.

### Local Drag Preview Instead Of A Re-Solved Document

`src/app/map_viewport.*`, `src/app/application_shell.*`

Moving a selection in the map viewport does not touch the document until the
mouse is released. A press arms a drag that begins only after
`kDragThresholdPixels` (4) of travel; `updateDrag()` then recomputes the
grid-snapped delta and repaints only when that snapped value actually changed,
so pointer movement inside one grid step costs nothing. `paintDragPreview()`
draws dashed ghost outlines of the selection offset by the delta, up to
`kMaxPreviewOutlines` (512) of them, plus the move vector and its numeric
readout — all from geometry that was already solved.

Solving stays where it belongs: `rebuildGeometry()`, which calls
`buildDoomSectorOutlines()` and `buildLevelMapBrushGeometry()`, runs only from
`setDocument()` and `updateDocument()`, never from `paintEvent()`.
`commitDrag()` emits `moveRequested(dx, dy, dz)` exactly once, and
`moveLevelMapSelectionFromViewport()` turns that into a single
`moveLevelMapSelectionSnapped()` call — one solve at the end of a drag instead
of one per mouse move, and one undo step for a drag of any size. Arrow-key
nudges go through the same single emission, scaled by `kCoarseNudgeMultiplier`
(8) when Shift is held. `cancelDrag()` on Escape drops the preview; nothing was
committed, so there is nothing to undo.

`refreshLevelMapViewport()` guards the same boundary from the other side. It
builds a key from the map's source path, name, edit state, and undo depth, and
does nothing while that key has not moved, so toggling grid, snap, or
projection does not re-solve. When the key does move it calls
`updateDocument()` for an edit to the map already open, which keeps the camera,
and `setDocument()`, which refits, only for a different map or file.

### Package Compare Reads Only What It Must

`src/core/package_compare.*`

`compareContent()` is ordered cheapest test first and stops at whichever one
settles the question. A directory pair is not compared at all. Differing sizes
end it immediately as `SizeOnly`, with no digest taken. A request that set
`metadataOnly` (the CLI's `--metadata-only`) stops here by choice. When both
sides carry a stored CRC-32 — which the ZIP family does, because
`loadZipFamily()` fills `PackageEntry::crc32` from the central directory — that
32-bit check decides it without reading a byte, and it is meaningful rather
than a coin flip because the sizes have already matched. An
unreadable side, and a repeated path the path-addressed reader cannot resolve
(`duplicate-path`), both stop before any read as well. A PAK or WAD entry
carries no stored CRC-32, so it falls through to the read.

Only what survives all of that is read and hashed with SHA-256, and that read is
bounded by `PackageCompareRequest::maxEntryBytes`, which falls back to
`kPackageCompareDefaultMaxEntryBytes` (256 MiB) when it is not positive and is
set from `--max-entry-bytes` on `vibestudio --cli package compare`. An entry
over the budget is skipped with the `entry-too-large` note instead of being
pulled into memory, so a package holding one enormous member cannot turn a
comparison into an out-of-memory failure. The cost of that budget is worth
knowing: a skipped entry counts into `uncomparedCount` and is reported as
`Identical` with content `NotCompared`, and `PackageCompareResult::identical()`
— which is what the command's exit code gates on — does not look at
`uncomparedCount`.

## Modern Acceleration Techniques

VibeStudio should combine deterministic tooling with modern automation:

- Generative AI for draft content, explanations, shader scaffolds, entity
  snippets, texture/model/audio ideation, documentation, and command recipes.
- Agentic AI for multi-step workflows such as "inspect this compiler failure,
  propose fixes, update a staged script, rerun validation, and summarize what
  changed" under explicit user control.
- Provider connectors for AI services instead of a single-provider design.
- CLI automation for repeatable project validation, package checks, compiler
  runs, batch conversion, and release preparation.
- Data-driven editor profiles and compiler profiles to avoid forcing users to
  relearn familiar workflows. The editor profiles are still declarations rather
  than applied settings; see [`docs/EDITOR_PROFILES.md`](EDITOR_PROFILES.md).
- Incremental indexing and caching for packages, assets, previews, diagnostics,
  and dependency data.
- Graphical workflow surfaces that show what changed, what is blocked, and what
  is ready to test.

## AI-Free Mode

AI acceleration is part of the core product design, but AI use must remain a
choice. Users who prefer no AI, cannot use cloud services, or work on private
projects must still get a complete local studio.

AI-free mode requirements:

- Core editing, packaging, compiling, validation, launch/testing, and CLI
  workflows work without any AI provider configured.
- No project content is sent to an AI provider unless the user opts in.
- Projects can disable AI even when the application has global AI settings.
- AI-generated files, assets, commands, and text must be marked as generated or
  proposed until accepted.
- Manual equivalents must exist for important AI-assisted workflows.

## Efficiency Metrics

Track these alongside the roadmap:

- Time from first launch to usable project workspace.
- Time from changed map/script/package asset to a compiler run.
- Time from compiler failure to an actionable diagnostic summary.
- Time from generated/proposed AI action to reviewed/staged application.
- Number of duplicate file/path selections needed after project setup.
- Number of clicks or commands for common package, compile, and launch loops.
- Startup time, package open time, indexing time, preview latency, and compiler
  task scheduling overhead.
- Percentage of long-running workflows with visible progress, cancellation, and
  output locations.
- Percentage of GUI workflows with CLI equivalents.
- Percentage of AI workflows with manual/local alternatives.

## Design Rule

Whenever a workflow is added, ask: how does this make development quicker,
easier, more reliable, or more understandable? If the answer is not clear, the
workflow needs a smaller slice, better feedback, a CLI path, a reusable preset,
or a stronger reason to exist.
