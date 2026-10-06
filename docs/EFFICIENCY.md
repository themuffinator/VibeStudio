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

### Shared Asset Workbench Commands

`src/app/asset_workbench_actions.cpp`, `src/app/studio_actions.*`

Textures, Models and Audio expose their authoring, selected-asset editing and
export actions through the same command registry used by menus, search and
custom shortcuts. Tools groups these commands by module; the command palette
shows the module beside each result. Empty browsers offer Texture Editor,
Mesh Editor or Open Audio directly, with Open Package as the secondary action.
Content toolbars appear when there is content to browse.

Each asset header identifies the selected package path. Its Package menu returns
to the selected source entry, reviews staged changes or saves the current draft.
Audio handoffs preserve repeated WAD entry identity. Paths with ambiguous
occurrences in the other browsers require choosing the exact entry in Packages.
No project switching or extra package opening is needed for this handoff.
Selection-dependent editing and export commands follow loading, selection and
filter state; compressed audio remains editable when only a header preview is
available. Asset edits still use the existing editor and staging services.

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

`src/app/application_shell.*`, `src/app/studio_theme.cpp`

`applyPreferencesToUi()` changes the application theme only when the resolved
theme, density or text scale changes. Ordinary package and activity refreshes
use `scheduleThemeRefresh()` to coalesce per-item state colours once per event
loop turn; they do not rebuild the application stylesheet.

`applyStudioTheme()` also compares the generated stylesheet with the current
one. Identical styles skip replacement. For a changed style it detaches the old
application sheet, restores the requested palette and font, and installs the new
sheet through public Qt APIs. This avoids repeatedly restyling the descendants
of each cached widget in Qt's existing-sheet update path. The change applies to
all studio surfaces and retains widget-local styles and document contents.

`ui-primitives-smoke` checks bounded style-change delivery through 25 nested
widgets, dark/light/high-contrast transitions, retained text/selection and
fixed-pitch detail scaling. Package browser workflow checks also exercise live
100%/200% transitions. Timing observations are recorded in the package-manager
release audit; native platform and whole-studio performance acceptance remain.

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

### The Edit, Build, Fix Loop Without Retyping

`src/app/application_shell.*`

The Build page no longer has to be told what to build. Its input follows the
map open in Levels, a map for another game brings that game's pipeline with it,
and the launch form's map name defaults to what the build produces, so opening
a map and pressing F5 (Build and Launch) needs no typing: the build runs, the
map is copied into the game folder it loads from, and the game starts on it,
with one confirmation. After a run, the
**Problems** tab turns each compiler warning into a jump: activating one selects
the brush, patch, or entity written on that line of the map (the entities and
brushes already know their source lines), so the step from "WARNING: 6: ..." to
the object costs one key instead of a search through the file. Copy Commands,
the launch preview, and the run itself share one request builder, so none of
them can drift from what actually runs.

### Coalesced Status Chip Refresh

`src/app/application_shell.*`

The status chips used to be recomputed on every state change, and the compiler
chip walked every search path for every tool each time. Refreshes are now
coalesced into one per event-loop pass, and compiler discovery for the chip is
reused for five seconds unless the project, search paths, or overrides change;
a path chosen on the Toolchain tab resets that cache so the chip updates at
once.

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
`metadataOnly` (the CLI's `--metadata-only`) stops here by choice. An unreadable
side stops with an explicit unchecked result. Other equal-size files are read
and hashed with SHA-256, including ZIP/PK3 files: matching stored checksums do
not prove that the current payload is intact. Repeated WAD names resolve by
position, so each map's own lump bytes are compared.

Only what survives all of that is read and hashed with SHA-256, and that read is
bounded by `PackageCompareRequest::maxEntryBytes`, which falls back to
`kPackageCompareDefaultMaxEntryBytes` (256 MiB) when it is not positive and is
set from `--max-entry-bytes` on `vibestudio --cli package compare`. An entry
over the budget is skipped with the `entry-too-large` note and `Uncompared`
status, increments `uncomparedCount`, and prevents `identical()` from returning
true for a content comparison. Source archive reads stream through bounded
buffers and can cancel during a file; staged inputs still have per-file memory
cost. `package validate` shares the streaming reader, checks every payload by
default and offers an optional work budget. Its pass result also requires no
failed or unchecked entries and no reader warnings.

### Shell Start-Up: One Shortcut Install, Pages Built In Place

`src/app/studio_actions.*`, `ApplicationShell::buildUi()`

Building the shell once took about 100 seconds in the debug build, and nearly
all of it was shortcut wiring. `installShortcuts()` rebinds every command's
keys at once, so that conflicts never depend on registration order. It ran
after each of the roughly 350 `registerCommand()` calls, and again after each
`setShortcutScopes()`. Each run looked up the documented shortcut several
times per command through `shortcutForCommandId()`, which builds the whole
translated descriptor list on every call. The work was therefore quadratic in
the number of commands, times the cost of translating about 150 labels.

Two changes fixed it. First, the registry now reads the descriptor list once
per install into an index keyed by normalized command id. Second,
`beginBatch()` and `endBatch()` hold installation back while `buildCommands()`
registers commands and `installShortcutScopes()` sets scopes, then install
once. Queries made inside a batch answer with the keys installed before it,
and nothing in either function asks.

The rest was reparenting. The ten pages were built, added to the page stack,
which was added to the work area, which was added to the root widget, which
became the central widget. Each of those moves made Qt resolve the style
sheet, fonts, and palettes for every widget in the tree again. The window now
holds the empty stack before any page is built, so each page joins its final
parent once. The shell now builds in about 7 seconds in the debug build, most
of it style sheet polish.

### Selecting Many Rows At Once

`selectListRows()` in `src/app/application_shell.cpp`

Enter in the Levels Objects filter and in the Packages filter selects every row
the filter keeps. Selecting them one `setSelected()` at a time made the
selection model merge ranges on each call. That took about 7.5 seconds for
10,000 rows. The rows are now gathered first and selected in one
`QItemSelectionModel::select()` call, with each run of neighbouring rows as one
range. The same 10,000 rows now take about a tenth of a second. The Textures
filter likewise works out once per pass whether the open map names its
textures with their `textures/` folder, instead of once per row over every
brush face. A filter on what decoding finds runs again a quarter of a second
after thumbnails arrive, not after every batch of six.

The Levels Objects list is filled again whenever the map or its selection
changes, and it used to select each selected row as the row was added. A row
selected while the list still had new rows to lay out made it lay all of them
out again, so **Select All** on a map of 500 brushes took ten seconds and on one
of 10,000 did not finish in a quarter of an hour. The rows now go in first and
the selection follows in one call, as above: 500 brushes take a quarter of a
second and 10,000 about five, in a debug build. A selection made in the view
reaches the list the same way. In the map document, `setLevelMapSelection()`
searched the map for each object selected, and `applySelectionFlags()` searched
it again. Each kind's ids are now gathered once, which took selecting 10,000
brushes from half a second to a few milliseconds. The map view kept its own copy
of the selection by the same one-at-a-time removal of repeats, and now builds it
in one pass too.

### Highlighting Only What Changed

`StudioSyntaxHighlighter::setDiagnostics()` in `src/app/syntax_highlight.cpp`

The code editor looks for problems again a moment after typing stops. Marking
them used to highlight the whole file again whenever a problem moved to another
line, and in a 24,000-line file that stalled the editor for about four seconds
after each pause. Only the lines that gain or lose a mark are highlighted again
now. The Code Files filter likewise works out each file's size and language
once per scan of the tree, and again for a file when it is saved, instead of on
every keystroke of a query.

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

Set `QT_LOGGING_RULES=vibestudio.startup.info=true` to log shell construction
phases and cumulative/phase elapsed milliseconds. On Windows, also set
`QT_FORCE_STDERR_LOGGING=1` when collecting these logs through standard error.
Logging is off by default and records fixed phase names without project paths.
`QT_LOGGING_RULES=vibestudio.ui.refresh.info=true` also traces package detail,
listing, tree, composition and document refreshes, including the package-only
command update, plus shared command, surface,
workspace and status updates. It records function names and elapsed milliseconds,
without entry names or paths. Combine categories with semicolons in this
environment variable. Refresh timings include nested calls; do not add them as
independent CPU costs.
Compare optimized builds under comparable load; a debug whole-shell test is
not a measurement of release interaction latency.

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
