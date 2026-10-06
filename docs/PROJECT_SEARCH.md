# Project Search And Replacement

Open a project, switch to **Code**, and choose **Find in Project** or
**Ctrl+Shift+F**. The Search Results panel searches project source files and
snapshots of named, open Code documents in a background worker. UTF-8 and
BOM-marked UTF-16 use the same decoder as the editor. Search and replacement
remain available in AI-free mode.

## Find And Navigate

Enter literal, single-line text and choose **Search**. **Match case** and
**Whole words** control matching. Whole words treat letters, digits,
underscores, and combining marks as identifier characters: `player` does not
match the prefix of `player_run`.

**File filters** reveals include and exclude patterns, separated by semicolons.
A pattern without a slash, such as `*.qc`, matches a filename anywhere in the
project. A pattern with a slash, such as `scripts/*.cfg`, matches a path relative
to the project root. Wildcards can span directories; a trailing slash excludes
the whole directory. Patterns ignore case and exclusions win. These are simple
wildcards, not Git ignore rules. Empty filters search the supported source-file
extensions, including QuakeC, CFG, shader, MAP, C/C++, JSON, Lua, and Python,
plus the Files catalog's special names such as `meson.build` and `README`.

Select a result to see its full source location and line text. Activate it to
open the Code editor at its one-based line and UTF-16 column. Results remain
available while editing; activating a stale location asks you to search again.
Results identify saved files and open-document snapshots and show the encoding
in their details. **Find All References** uses a connected, supported local
language server for semantic locations and presents them in the same panel.
Disconnected or unsupported documents use this worker's case-sensitive,
whole-word matching, including comments and strings. Reference results show their
provider and exact ranges, validate source snapshots before navigation, and
cannot be applied as replacements. **Search Text** starts a new textual query.
See [Local Language Services](LANGUAGE_SERVICES.md#references) for scope and limits.

**Code Actions** (**Ctrl+.**) uses the same review and apply service for provider
quick fixes and refactorings. Choose an available action, resolve its edits when
needed, then review the action title, provider, targets and before/after spans.
Open tabs remain unsaved with Undo; disk targets use guarded atomic saves.
Changing context or disconnecting invalidates the preview. Search fields are
independent of provider edits. See [Code Actions](LANGUAGE_SERVICES.md#code-actions).

**Rename Symbol** (**F2**) uses the same review panel and apply service for a
separate provider-generated semantic edit plan. It includes synchronized unsaved
tabs and saved project files, validates all edits before review, and rejects a
stale preview. Apply gives each open document one unsaved Undo step and writes
unopened files with hash guards. File filters do not limit provider edits.
See [Rename Symbol](LANGUAGE_SERVICES.md#rename-symbol) for supported operations,
limits, partial-write reporting and the CLI plan-hash guard.

The status shows scanned files, open documents, matches, skipped files, cancellation, and
incomplete searches. **Cancel** stops the worker. The Activity center records
running and terminal states and can also cancel the current search. Changing
projects cancels the old scan and prevents its results from appearing in the
new project.

## Review And Apply

Enable **Replace with**, enter replacement text, and run Search. An empty
replacement deliberately deletes matches. Each result shows the before and
after text for its line. **Apply Preview** asks for confirmation, listing open
documents to edit and unopened files to save. It applies exact reviewed ranges
or prepared bytes without searching again. Files added after the preview are
therefore left alone.

All eligible open Code tabs override their disk files, including inactive,
clean, modified, and deleted-file tabs. Their replacements are one undoable
edit per document and remain **unsaved**. Prior unsaved work is retained. Use
Code Save or Save All to persist the result through the normal format,
external-change, recovery, and shared-map checks.

The entire batch is blocked if a targeted Code document changed or closed
since preview, a disk target was subsequently opened, the active project
changed, or a targeted Levels map has unsaved edits. The host verifies tab
identity, revision, and normalized text SHA-256 before applying. The service
checks every unopened file's byte SHA-256 before the first write and again
immediately before each atomic commit. Deletions, links, unreadable files,
and incomplete previews prevent unreviewed writes. Disk writes complete first;
then the GUI rechecks and edits the live documents while modal progress remains
open. Disk failure or cancellation leaves all pending buffer edits unapplied.
The source index is invalidated; saved disk changes reload an affected clean
Levels map. Unsaved Code replacements reach Levels only through explicit Save.

Disk replacements retain UTF-8 BOMs, UTF-16 byte order and BOMs, and untouched
CRLF/LF/lone-CR/Unicode paragraph separators, including among repeated identical
lines. Inserted paragraphs use the document's preferred separator. Untouched
bytes and final-newline state are retained. Open-document replacements use the
[Code editor's Save path](CODE_EDITOR.md) when explicitly saved.

Each file commit is atomic. A failure or cancellation after earlier commits
does not roll those files back. The report lists exactly which files were saved
and which open documents were edited without saving, with the total number of
replacements applied. A late live-document validation failure can leave earlier
disk commits saved; the report makes that partial result explicit. Search again
after replacement to refresh locations. Disk changes have no editor undo. The checks guard
against ordinary concurrent edits, not hostile filesystem swaps in the interval
between verification and rename.

## Scope And Bounds

Project search excludes untitled documents, files outside the project, archive
entry copies, and staged package operations. Save a draft inside the project to
include it. Binary files, malformed Unicode, and unsupported legacy code pages
are skipped. An unavailable or oversized eligible open snapshot makes the scan
incomplete and never falls back to stale disk. Symlink files, linked directories, and Windows junctions
are excluded. `.git`, `.hg`, `.svn`, `.agents`, `.vibestudio`, `node_modules`,
`__pycache__`, `build`, `dist`, `external`, and directories beginning with `builddir` are pruned
before descending. Additional generated/output trees need exclude filters.

Limits are 10,000 matches, 20,000 candidate files, 100,000 visited entries,
4 MiB per disk input, 4,194,304 UTF-16 code units (8 MiB) per open snapshot,
64 MiB total input, and 64 MiB prepared output. Open snapshots count their
UTF-16 storage against the shared input/output limits; disk files count bytes.
Incomplete
reads, oversized inputs, or reaching a limit block replacement. Narrow the
filters and search again. UI line previews are shortened after 8,192 characters;
CLI snippets after 2,000 characters, with `previewTruncated` in JSON. Replacement
uses the full reviewed line, subject to the preparation bounds.

Folder package mounts and independent asset inspectors can hold their own
metadata or staging snapshots. Review those surfaces after changing a shared
source file; search does not silently rebuild package staging or archived shader
entries. Build and packaging still use their normal validation services.

## CLI

The existing asset commands use the same matching, codec, and replacement
service against saved files. Running editor snapshots stay local to the GUI:

```sh
vibestudio --cli asset find ./mymod --find player --whole-word --case-sensitive --include "*.qc;*.qh" --exclude "generated/*" --json
vibestudio --cli asset replace ./mymod --find old_shader --replace new_shader --include "scripts/*" --dry-run --json
vibestudio --cli asset replace ./mymod --find obsolete_flag --delete-matches --include "*.cfg" --write --json
```

`--extensions` still limits candidate suffixes. CLI filter lists accept commas
or semicolons. Replacement defaults to a dry run; `--write` is required to save,
and `--dry-run` takes precedence. Deletion requires `--delete-matches`, which
cannot be combined with `--replace`. A CLI write prepares and applies its own
in-memory preview; separate CLI preview/write invocations do not share a plan.

JSON includes `complete`, `cancelled`, `filesSkipped`, `canApply`,
`replacementsApplied`, `writtenFiles`, `buffersScanned`, `editedBuffers`, and
`bufferEditsPending`. Each match includes `source`, `encoding`, `textSha256`,
`replacementLine`, and `previewTruncated`. CLI results have disk sources and no
editor buffers. `replacementCount` counts proposed matches;
`replacementsApplied` counts actual changed matches. Exit 0 means a complete,
successful operation, 2 means a usage error, and 4 means failed validation or
an incomplete search.

## Verification

`project-text-search-smoke` checks matching, filters, Unicode locations, exact
UTF-8/UTF-16 byte preservation, live snapshot precedence, empty replacement,
limits, cancellation, stale source hashes, path containment, partial writes,
editor-host-only buffer application, and the CLI. Link fixtures run when the host
allows creating symlinks.

`project-search-ui-smoke` checks asynchronous state, previews, navigation,
confirmation, invalidation, project changes, live and inactive document edits,
undo/redo, explicit save, deleted sources, mixed disk/buffer reports, stale
navigation, and changes during confirmation through the real shell.
`text-document-ui-smoke` also checks shared Levels protection. The search UI
checks named, focusable widgets at 100% dark and 200%
high-contrast dark with RTL and expanded translations, using direct widget calls
and offscreen rendering. It does not inject OS input or capture the desktop.
