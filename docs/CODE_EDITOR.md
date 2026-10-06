# Code Editor Documents

The Code workspace keeps a separate document, undo history, source snapshot,
encoding, and line-ending record for every tab. **Save** and **Ctrl+S** preserve
the file's format. Saving does not reload the editor or clear undo history.
The readout, its tooltip, and its accessible description identify the encoding,
byte-order mark (BOM), line endings, and save state.

## New Documents And Save As

**New Text File** in File, the Code toolbar, or its empty state creates an
independent untitled tab. **Ctrl+N** is scoped to Code. Drafts use UTF-8 without
a BOM and LF endings. Editing, in-file find/replace, line operations and undo
work before a file exists. Language outlines, diagnostics and project references
use a real file extension after saving.

**Save File As** (**Ctrl+Shift+S** in Code) chooses a destination; **Save** on a
draft opens the same picker, even for an empty document. Cancelling keeps the
draft. Closing a modified draft asks Save, Discard or Cancel. Save As preserves
the document, caret, undo history and format, then adopts the new language,
recent-file entry, watcher and project-tree location. The original file need
not still exist.

Replacing a destination requires review. Its contents and resolved path must
still match at commit; a previously absent file that appears also blocks the
write. A dirty destination Code tab or Levels map blocks saving. A clean
duplicate Code tab is closed while the saving tab retains its undo history.
Clean Levels maps and project-manifest panels refresh. The open package cannot
be a text-save destination. Complete, supported package text previews can use
Save As to create editable extracted files; staging them back remains explicit.
The destination must be outside this window's temporary package-copy storage,
including paths through directory aliases. Refusal preserves unsaved text, undo,
the current file and the copy's read-only state. Code and Levels share this
destination policy through the package service.

## Formats And Preservation

Editable formats are valid UTF-8, with or without a BOM, and UTF-16 LE/BE with a
BOM. The saved file keeps that encoding and BOM. UTF-32, invalid Unicode,
legacy code pages that are not valid UTF-8, and binary control characters open
as read-only previews with an explanation. Files above 4 MiB are also read-only
and truncated. Convert unsupported files outside the editor before editing;
opening a preview never authorizes a lossy conversion.

LF, CRLF, lone CR, and Unicode paragraph separators are recognized. A no-edit
save retains the original bytes and does not rewrite the file. Mixed-ending
files retain the separators of matched lines. Changed runs reuse corresponding
source separators, and additional lines use the most common original ending;
ties use the first encountered ending. Files with no separators use LF for new
lines. Repeated or reordered identical lines are matched in source order, so
ambiguous mixed separators can be reassigned within an edited run.

The editor preserves the presence or absence of a final newline, non-breaking
spaces, Unicode soft line breaks, and non-ASCII text. Explicitly adding or
removing a final paragraph changes that final-newline state. Encoding and line
ending conversion controls are not yet provided. Undo changes editor text;
Save serializes that text against the most recent saved snapshot.

## Save Conflicts And Other Workspaces

Each save checks the current file's SHA-256 and resolved path against the
source snapshot, even before the file watcher has reported a change. A changed
file prompts before overwrite; **Cancel** is the default and keeps both the
disk file and unsaved editor text. Overwrite approves only the disk contents
observed when the prompt opened. Another change during the prompt blocks the
write. Unreadable or oversized files and changed link targets cannot be silently
overwritten. A deleted file can be restored from its open tab after a **Recreate**
confirmation. The original resolved parent must still be present and unchanged,
and a file that reappears during review blocks recreation. This workflow uses
the tab's original format; it does not guess a new encoding.

Writes use an atomic temporary-file commit after another source check. Existing
symbolic links keep their link path and write the originally resolved target.
These checks address normal concurrent edits; they do not provide a filesystem
transaction against a hostile swap between the final check and rename.

An affected map with unsaved Levels edits blocks Code Save. Saving a shared
clean map reloads Levels. A successful save refreshes the file watcher baseline,
invalidates the source symbol/size caches, updates the tab's format snapshot,
and records the result in Activity. Build and packaging consume saved files
through their existing services; package staging still needs explicit review.
Archive entry copies remain read-only until extracted, including through Save As.

[Project Search](PROJECT_SEARCH.md) snapshots all named project Code tabs,
including inactive and unsaved documents, and shares the editor's UTF-8/UTF-16
codec. Reviewed replacements in open documents are undoable and remain unsaved;
unopened files are saved atomically. Changed snapshots and unsaved Levels maps
block application. The source index also uses the editor's codec; other
language-specific analysis and external compilers still
have their own decoding assumptions.
Legacy-encoding selection remains future work.
Optional [local language services](LANGUAGE_SERVICES.md) provide diagnostics,
Quick Info, semantic completion, Go to Definition and Find All References for matching open
documents without saving buffers. Reference results use current open snapshots
and refuse stale navigation; textual lookup remains available without a server.
Diagnostics accept push or document pull providers. Pulled reports follow the
current synchronized version, with cached results, visible failures and a native
Refresh diagnostics control. Successful Save/Save As notify interested servers
after writing and synchronizing; rejected writes emit no save notification.
Quick Info (Ctrl+I) displays a symbol's type and documentation in a selectable
pane; pointer dwell requests a short hint without moving the caret. Both use the
current source snapshot and leave source, undo and save state unchanged.
Semantic snippet completions select editable fields with Tab/Shift+Tab navigation,
linked occurrences, nested defaults and native choices. The field bar exposes
progress and Finish; Escape ends navigation at the current caret. Insertion with
imports and each later mirrored edit use normal unsaved Undo steps. Ordinary
single-edit completion within a field also updates its mirrors. Document changes,
external edits and Undo/Redo retire the session. See
[Local Language Services](LANGUAGE_SERVICES.md#completion) for syntax, limits and
the handoff when another completion starts. Recovery, project indexing, saving,
compilation and package staging continue through the shared document workflow.
Format Document (Alt+Shift+F) and Format Selection (Ctrl+Alt+F) request validated
edits from the same connection. Each applies as one unsaved Undo step, preserves
the normal save/recovery workflow, and cancels when its source context changes.
A nonmodal progress dialog and Activity expose cancellation and the result.

## Project Files

The **Files** panel lists source and text filenames from a background catalog.
Directory traversal and file-size reads run off the UI thread; rows are added in
small batches so editing can continue while the tree fills. The panel and Activity
show scanning, success, cancellation, failure, and partial results. **Cancel** stops
the current scan; **Refresh Tree** starts another. Entering Code again reuses the
current catalog, including a cancelled one.

The catalog includes idTech scripts, maps and entity definitions, C/C++ sources
and headers, QuakeC headers and `progs.src`, build/configuration text, and common
text formats. A filename is only a discovery hint: opening still validates the
file's encoding, size and editability through the document service. QuakeC `.qh`
headers now use the same language classification as the file browser.

Filters read the catalog snapshot without filesystem access. Words match relative
paths; `path`, `name`, `ext`, `folder`, `size`, `language`, and `kind` support the shared
property query syntax, such as `language=cpp size>10kb`. Unknown fields have an
inline explanation. Refresh preserves the selected file, expanded folders and
filter. Switching projects retires earlier work before any results can enter the
new tree. Code Save, Save As and project replacements refresh disk metadata;
opening a newly created source file also requests a refresh when needed.

The default list stops at 4,000 files or 100,000 directory entries and explicitly
reports partial results. It excludes links and prunes `.git`, `.hg`, `.svn`,
`.agents`, `.vibestudio`, `node_modules`, `__pycache__`, `build`, `builddir*`,
`dist`, and `external` at every depth. A linked project root is rejected. Filtering
a partial catalog only searches the files already gathered; use a narrower project
root or the CLI's larger file bound to inspect more. Unopened external changes
require **Refresh Tree**.

`code files` uses the same catalog and metadata filters without reading contents:

```sh
vibestudio --cli code files ./mymod --where "ext=qc size>1kb" --max-files 4000 --json
```

`--max-files` accepts 1–20,000. JSON's `codeFiles` object contains matching `files`
with `filePath`, `relativePath`, `language` and `sizeBytes`; `filesScanned` counts
the catalog before filtering. It also reports `complete`, `cancelled`,
`entriesVisited`, `directoriesExcluded`, `linksExcluded`, `warnings` and `error`.
Exit 0 means complete, 1 a failed root, 2 invalid arguments or unknown fields,
and 4 a partial scan. `code tree` remains an alias for `code index`.

## Go To File Across The Studio

**Go to File** (**Ctrl+P**) opens immediately with recent paths while a background
worker checks their availability and gathers project files and the open package's
entry metadata. It now includes C/C++ and other source candidates, loose images,
models and audio, maps, package files, and `.vtexture` / `.vsaudio` documents.
Opening uses the existing surface routing and document validation; finding a name
does not assert that its contents are valid. Package payloads are not read during
discovery, and opening a package entry still follows the normal preview/copy path.

The picker and Activity show progress, cancellation, errors and partial results.
**Cancel Scan** keeps the gathered subset; **Refresh** scans the current context
again. Closing the picker retires its work. Project changes and package replacement
close the picker so an older entry cannot be opened against a new context.
Reopening takes a fresh snapshot, including externally added files.

Recent paths lead and are deduplicated against project files. A query typed while
scanning is retained, and incoming results preserve the selected entry when it
still matches. Ranking prefers name prefixes, then word prefixes, then letters
in order in the folder/name. Large lists rank in small event-loop batches and
show at most 200 matches; changing a query retires unfinished ranking, and stale
rows cannot be activated. The same ranking serves the symbol picker.

Discovery shares the Files panel's exclusions and link/containment rules. It
stops at 20,000 project candidates or 100,000 filesystem entries, plus at most
100,000 package metadata entries. A partial list says so: typing filters only
what was gathered and does not search the skipped remainder. Use a narrower
project root or browse the package directly when these bounds are reached.

The equivalent project catalog is available without the GUI:

```sh
vibestudio --cli project files ./mymod --where "kind=image" --max-files 20000 --json
```

`project files` defaults to 20,000 candidates, accepts the same 1–20,000 bound,
query fields and exit statuses as `code files`, and returns a `projectFiles`
JSON object with the same metadata plus `kind`. Kinds are `code`, `map`,
`package`, `image`, `model`, `audio`, `texture-project` and `audio-project`;
binary candidates have an empty `language`. WAD names are cataloged as packages;
opening inspects their contents to choose maps or resources. The CLI lists the
project root only; recent paths and entries from the GUI's open package remain
session context.

`quick-open-ui-smoke` covers asynchronous discovery, replacement/cancellation,
metadata bounds, rapid query changes, yielding during large lists, selection
preservation, shell opening and project switching. Its render checks cover
normal and enlarged high-contrast RTL layouts. Physical keyboard, screen-reader,
and non-Windows acceptance remain separate manual/platform gates.

## Project Definitions And Completion

Project symbol scanning runs in a cancellable background worker. **Index Code**
and the **Index** output panel show progress, results, warnings, and cancellation;
Activity records the operation too. Completion can request the same index while
still offering local names and language keywords. A cancelled scan stays cancelled
until **Refresh Index** or a source-context change requests new work.

The Index filter searches symbol names and relative source paths without rereading
files. Activate a symbol, file, or diagnostic to open its source location. The
list shows up to 1,000 matching symbols, 200 files and 200 diagnostics, with a
message when more results exist; narrow the filter or inspect CLI output.

Open, supported Code documents supply immutable snapshots to the worker, including
unsaved changes and files deleted on disk. Their current definitions replace the
saved versions. Edits invalidate the old index and schedule a refresh after a short
pause; saving, reloading, closing tabs, and changing projects also invalidate it.
Untitled documents and package previews do not become project source paths.
Refresh explicitly after changing an unopened source file externally or adding files.

**Go to Definition** waits for a needed index without blocking editing. It resumes
navigation only while the same document revision and caret position remain active.
Changing projects or cancelling discards that pending jump. Multiple definitions
still use the existing picker and navigation history. These built-in definitions
are textual. A tool explicitly connected in the **Language Server** output panel
takes precedence for matching documents that support semantic definitions or
completion. Ctrl+Space and server trigger characters request suggestions;
selecting one applies its replacement and related edits to the unsaved document
in one Undo step. Stale replies are discarded, and local completion remains
available when the server is disconnected, unsupported or returns no usable list.
Providers may defer documentation and imports until a suggestion is highlighted.
Resolution shows progress; accepting early waits for the complete edit set.
Escape, typing or switching source context cancels it. Resolution failures never
insert only the primary text. Resolved details remain literal tooltip text.
See [Local Language Services](LANGUAGE_SERVICES.md) for setup and current limits.

**Parameter Hints** (**Ctrl+Shift+Space**) shows call overloads and the active
argument above the editor. Provider trigger characters can open it while typing;
edits and caret movement update active hints after a short pause. Select an
overload, expand Documentation, or use Escape/Close to dismiss. Active parameters
have emphasis and a numbered text label. Source/tab/project changes retire stale
replies. This view shares the current unsaved buffer and never edits text or
changes the normal save-before-build/package workflow. See
[Parameter Hints](LANGUAGE_SERVICES.md#parameter-hints) for limits and CLI use.

F2 opens **Rename Symbol** when the connected tool supports it. The provider
validates the symbol before the name prompt, then Search Results offers a review
of all proposed project edits. Apply keeps open tabs unsaved with one Undo step
per document and saves unopened targets with source guards. Source changes or
disconnect invalidate the preview. Save modified tabs before building or staging
them into a package. The complete workflow and CLI plan-hash guard are documented
in [Rename Symbol](LANGUAGE_SERVICES.md#rename-symbol).

**Code Actions** (**Ctrl+.**) lists provider quick fixes and refactorings for the
caret or selection, including preferred actions and unavailable reasons. A
chosen action may resolve its edits asynchronously before Search Results review.
Apply uses the same unsaved-tab Undo, source guards and disk-save path as rename.
Commands and resource operations are unsupported. See
[Code Actions](LANGUAGE_SERVICES.md#code-actions) for limits and CLI selection.

The scanner accepts strict UTF-8 and BOM-marked UTF-16 through the document codec.
It prunes generated, dependency, and version-control folders before descending and
skips filesystem links and junctions. A root that itself is a link is rejected.
An explicitly open document inside an excluded folder can still supply a buffer.
Without a project, a saved file's folder supplies the root; an empty workspace
never scans the process working directory.

The symbol worker covers completion and definitions. The Files panel has its own
background metadata catalog, with shared source-directory exclusions.

Default bounds are 4,096 candidate files, 100,000 visited entries, 4 MiB per saved
file, 64 MiB of total source input, 20,000 symbols, and 10,000 diagnostics. Editor
snapshots use UTF-16 memory accounting and allow up to 8 MiB per document within
the total bound. Lines longer than 8,192 characters skip symbol matching. Reaching
a bound or skipping an unreadable/unsupported source produces an explicitly partial
index with warnings. A missing match in a partial index is not proof of absence.

`code index` shares the scanner, Unicode decoding, bounds and diagnostics. The CLI
reads saved files; GUI snapshots remain local to the running editor. For example:

```sh
vibestudio --cli code index ./mymod --find monster --max-files 4096 --json
```

`--max-files` accepts 1–20,000. JSON reports `complete`, `cancelled`, `filesSkipped`,
`entriesVisited`, `bytesRead`, and `fromBuffer` on source records. Exit 0 means a
complete scan, 1 a failed root, 2 invalid arguments, and 4 an incomplete scan.
Indexing never saves editor buffers or changes build inputs; save before compiling
or staging packages. Source-port launch and compiler task hints remain available.

## Local Recovery

Code checks modified, supported documents every five seconds and writes changed
snapshots in a background queue. Each tab has one current checkpoint; newer
queued revisions replace older ones. The readout and tab tooltip show recovery
progress or failure. Failures also appear in Activity. Recovery is enabled by
default and independent of AI and network services.

**File → Recover Text Documents** controls automatic recovery and lists copies
with their timestamp, source path and validation state. Copies may contain
private text. The `code-recovery` folder lives in application local data, or
beside an explicitly selected settings file. Turning recovery off stops new
checkpoints and keeps existing copies. An asynchronous scan is bounded to 128
records or 64 MiB and reports when more records may exist. Invalid records
cannot be restored.

Restoration verifies versioned JSON, SHA-256, size and Unicode, then opens an
unsaved draft with its format, caret and selection. It never writes the stored
source path or removes the imported copy. Save As chooses the destination.
**Discard Copy** removes a checkpoint after confirmation; copies belonging to
a running studio cannot be discarded from this browser. Saving or discarding
an owned tab retires its checkpoint even when a write is still finishing.
Cancelling a window-close prompt preserves other unsaved tabs' copies.

Edits since the last completed checkpoint can still be lost. Recovery does not
preserve undo history, unsupported previews, package staging or edits in other
surfaces. Drafts join saved-file session history only after Save As.

## CLI

`code text-info` reports the same source metadata, including the SHA-256 required
for a later guarded save:

```sh
vibestudio --cli code text-info ./scripts/game.qc --json
vibestudio --cli code text-save ./scripts/game.qc --input ./edited.qc --dry-run --json
vibestudio --cli code text-save ./scripts/game.qc --input ./edited.qc --expected-sha256 <sha256-from-text-info> --write --json
```

The input file supplies edited text in any supported encoding. The target
supplies output encoding and existing line endings. Save defaults to a preview;
`--write` requires a 64-digit hexadecimal `--expected-sha256`. `--dry-run`
takes precedence. A stale hash fails without writing, including when size and
modification time did not change. The CLI never prompts or silently creates a
missing target. GUI and CLI share `src/core/text_document.*`.

New documents, copies and recovery exports use the same destination checks:

```sh
vibestudio --cli code text-create ./scripts/new.qc --dry-run --json
vibestudio --cli code text-create ./scripts/new.qc --input ./template.qc --write --json
vibestudio --cli code text-save-as ./scripts/game.qc --output ./scripts/copy.qc --input ./edited.qc --dry-run --json
vibestudio --cli code recoveries --directory ./code-recovery --json
vibestudio --cli code text-recover ./code-recovery/<id>.vstextrecovery --output ./restored.qc --dry-run --json
```

`text-create` creates an empty UTF-8 file unless `--input` supplies initial text
and format. `text-save-as` uses the source format with optional edited `--input`.
`text-recover` exports a verified checkpoint to explicit `--output`. All default
to preview and retain the source. Their `textDocument` JSON contains `path`,
`destinationExists`, `destinationSha256`, `format` and `save`. A new destination
needs `--write`; replacing an existing destination also requires its
`--expected-sha256` from the preview or `text-info`. Stale hashes return 4.
`recoveries` reports `recoveries`, `directory`, `limited` and `error`; invalid
records or a limited scan return 4. Without `--directory` it uses the selected
settings profile's recovery folder.

An original remains untouched unless it is explicitly chosen as the destination
and its replacement is approved. These checks protect against ordinary concurrent
edits; they do not provide a filesystem transaction against hostile path swaps.

JSON uses the normal result envelope with a `textDocument` object. It contains
`path`, `encoding`, `byteOrderMark`, `sha256`, `byteCount`, `editable`,
`truncated`, `preferredLineEnding`, `lineEndings` counts, `finalNewline`, and
`error`. Format fields describe the source snapshot; `editable: false` means
decoding or size validation failed and preview metadata can be incomplete.
For a truncated read, `byteCount` is the bounded read size and `sha256` is empty.
An eligible save also reports `save.dryRun`, `succeeded`, `changed`, `conflict`,
`bytesWritten`, `outputSha256`, and `error`. Preview and unchanged saves write
zero bytes. Usage errors return 2, missing targets 3, and validation/conflict
failures 4.

`text-document-smoke` verifies exact round trips, Unicode, mixed separators,
input/output bounds, conflict hashes, and CLI behavior.
`text-document-ui-smoke` uses direct offscreen widget calls to verify tab
metadata, editing, undo/save, conflict cancellation/overwrite, and read-only
previews, plus shared-map reload and unsaved Levels protection. It renders the
readout and conflict dialog at 100% and 200% scale, high contrast, RTL, and
expanded translations. No OS input injection is used; real keyboard and
screen-reader use still requires manual acceptance testing.

`code-document-ui-smoke` covers drafts, Save As, tab merging, cancellation,
undo and recovery through direct widget/service calls. `text-recovery-smoke`
checks format-preserving restoration, corruption rejection, limits, CLI export,
queued revisions and retirement of writes already in flight.

`code-index-smoke` verifies traversal pruning, Unicode, live-buffer precedence,
deleted open sources, partial-result bounds, cancellation, worker replacement,
and CLI exit/JSON contracts. Link fixtures run when the host permits symlink
creation. `code-index-ui-smoke` checks deferred navigation, caret guards, inactive
tab edits, cancellation, explicit refresh, filtering, project switching and
offscreen scaled/RTL rendering through the real shell.

`code-files-smoke` covers filename classification, metadata queries, subtree
pruning, traversal bounds, worker cancellation/replacement, and CLI reporting.
`code-files-ui-smoke` checks event-loop row batches, selection, cached size queries,
refresh and cancellation, project switching, Code saves, and offscreen layouts at
100% and 200% with high contrast, RTL and expanded labels.
