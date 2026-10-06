# Local Language Services

The Code workspace can connect to a locally installed Language Server Protocol
(LSP) server for live diagnostics, formatting, semantic completion, Quick Info, Parameter Hints, Go to Definition,
Find All References, Rename Symbol and Code Actions. The built-in source index, completion, search, editing,
compilation and packaging remain
available without a server or AI connector.

## Connect A Tool

1. Open a project and a source file in Code.
2. Select **Language Server** in the Code output tabs.
3. Choose an absolute executable path, such as an installed `clangd` executable.
4. Set the language identifier and matching file extensions. The defaults are
   `cpp` and `c;cc;cpp;cxx;h;hh;hpp;hxx`; `.c` uses the `c` identifier.
5. If needed, expand **Arguments and log** and enter one literal argument per
   line. Spaces stay within an argument. Do not add shell quotes.
6. Choose **Connect**. The panel shows startup, connection, shared document count,
   requests, errors and the bounded server log. **Disconnect** cancels requests
   and stops the process.

Connect starts the selected executable with your account's permissions, using
the project directory as its working directory. Only choose a tool you trust.
VibeStudio shares the root path and matching named, editable Code documents,
including unsaved text in inactive tabs. Untitled documents, package copies,
outside-project files and linked source paths are excluded or rejected. The
server can independently read project files and write its own indexes or caches;
the protocol connection does not sandbox the executable.

The executable and arguments are remembered as inert preferences. Opening a
project, restoring a session or starting VibeStudio never starts a server.
Changing projects stops the connection; choose Connect again for the new root.
There is one connection with one language/extension configuration at a time.

For C/C++, clangd needs the project's compiler flags and include paths for useful
analysis. Supply the appropriate `compile_commands.json`, or a literal argument
such as `--compile-commands-dir=/absolute/build/directory`. See the official
[clangd project setup guide](https://clangd.llvm.org/installation.html#project-setup).
VibeStudio does not install servers, generate a compilation database here, or
change the game's compiler configuration.

## Diagnostics And Navigation

Open documents synchronize after a short editing pause. The server receives
UTF-8 protocol text with UTF-16 positions, independent of the file's saved
UTF-8/UTF-16 encoding. This never saves a buffer or changes its undo history.
Save before compiling or staging the source into a package.

Problems merges server diagnostics with existing local checks. Versioned reports
can mark source ranges and support problem navigation while the tab identity,
revision and synchronized version still match. Editing immediately retires the
previous locations. Older versioned reports are discarded. Unversioned reports
are labeled and shown as informational rows without source jumps or markers,
because their text version cannot be proven. An empty fresh report clears the
server's previous diagnostics.

Servers can either publish diagnostics or advertise document diagnostic pulls.
For pull providers, the client requests reports after synchronization and accepts
them only for the requested document version. **Refresh diagnostics** checks all
shared documents again. Problems shows waiting, incomplete and failed states;
a failed request never appears as a clean source check. Full and unchanged
reports share range validation, including UTF-16 character boundaries.

Pull requests retain bounded result identifiers and provider data for Code
Actions. At most four diagnostic requests run at once, with queued work for
the other shared documents. Declared inter-file dependencies refresh all shared
documents after edits or closes. Server refresh requests use the same queue.
Server cancellation can retry twice with a short backoff, unless the provider
disables retriggering. Other errors and timeouts require another edit or refresh.
Pull results take precedence when a provider also sends push notifications.
Workspace-wide pulls, related-document reports and dynamic registration remain
future work; the client does not advertise related-document support.

Successful **Save** and **Save As** synchronize the current text before sending
an advertised `didSave` notification, including text only when requested. Save As
retires the old URI first. Failed or cancelled saves send no notification.
Saving also refreshes pull diagnostics. CLI diagnostic checks remain read-only
and never pretend to save a source file. CLI guarded writes currently occur
after their short-lived server connection closes; they do not send save events.

**Go to Definition** (**F12**, or the existing editor definition action) uses the
connected server for matching documents when the server supports it. It first
synchronizes current buffers. A reply opens only while the source document,
revision, caret and project context remain current. Multiple targets use the
normal picker and navigation history. Disconnected or unsupported documents use
the built-in project definition index. Server failures and empty results appear
in the status bar.

## Quick Info

**Quick Info** (**Ctrl+I**, remappable in Keyboard Shortcuts) asks the connected
server for the type and documentation at the caret. Its Code output pane shows
the provider and source position, loading progress, Cancel, and selectable
documentation. A normal pointer dwell over source text requests a short tooltip
without moving the caret, taking focus or replacing the persistent pane.

Matching named editable documents synchronize before each request, including
unsaved text. Replies must still match the tab, source revision/hash, project
context and server version. Moving the caret cancels a pending keyboard request;
moving away, typing, scrolling or dismissing the editor cancels pointer hints.
Edits, document/project changes and disconnect clear outdated documentation.
Completed pane content stays available while moving the caret in unchanged text.
None of these operations edits or saves source, changes undo, or affects compiler
and package inputs. Disconnected or unsupported documents show an explanation
when Quick Info is requested; the other local editor services remain available.

The shared parser accepts Markdown, plain text and legacy code/Markdown parts.
The GUI renders Markdown with raw HTML disabled, displays plain text literally,
removes image resources and makes links inactive. Documentation never fetches
resources or executes commands. Qt builds without a Markdown reader show the
original text. Code snippets retain left-to-right order in RTL layouts. Pointer
hints are shortened to 1,800 UTF-16 code units and 12 lines before a status hint;
the pane exposes the full bounded reply. Partial content is labeled explicitly.

## Parameter Hints

**Parameter Hints** (**Ctrl+Shift+Space**, remappable) shows call overloads and
the active argument in an inline panel above the editor. A connected provider's
trigger characters also open it automatically. While hints are active, edits,
caret movement and advertised retrigger characters request an update after a
120 ms pause. New requests cancel superseded work. Leaving the call closes the
panel when the provider returns no signature; Escape, switching documents or
studio surfaces, changing projects and disconnecting dismiss it immediately.

Use the native overload selector to inspect alternatives. Its selection is
included in subsequent provider context. The active parameter is bold and
underlined, with a separate numbered text label and accessible description.
Signature text keeps left-to-right code order in RTL layouts. **Documentation**
expands selectable call and parameter descriptions. It shares Quick Info's
resource-isolated renderer: literal strings stay literal; explicit Markdown
cannot load resources, show images or activate links. Loading and failed replies
have visible status. Invalid parameter ranges omit the complete signature so
remaining parameter indices cannot silently shift.

Hints share the current named Code buffer, language connection, source version,
caret and snapshot guards. They never modify text, Undo, recovery, build inputs
or package staging. Core editing remains available without a connected provider.

## Formatting

**Format Document** (**Alt+Shift+F**) and **Format Selection** (**Ctrl+Alt+F**)
use the connected server's corresponding formatting capability. Both shortcuts
are remappable. Selection formatting needs a nonempty selection. The request
uses the current unsaved buffer and the editor's detected indentation: spaces
use the detected width (2–8), while tabs use width 4. A server can also use its
own project configuration, such as `.clang-format`.

Formatting runs asynchronously with a named, nonmodal progress dialog and
Cancel, plus an Activity entry. Editing source, changing documents/projects or
surfaces, disconnecting, cancelling or closing the studio retires the request.
Moving the caret alone does not cancel it. The reply must still match the source
document, revision, synchronized version and normalized text hash. Malformed,
overlapping, annotated or excessive edits reject the entire reply.

Valid edits apply as one Undo step and remain unsaved. Caret and selection
positions track the changes; positions inside replacement spans are clamped to
the replacement. Normal recovery, diagnostics, indexing and save handling receive
the edit. Save before compilation or package staging. An empty or unchanged
reply reports that no changes are needed without adding an Undo step. The server
can expand a selection to surrounding syntax; returned edits are validated
against the entire document, not clipped to the original selection.

## Rename Symbol

**Rename Symbol** (**F2**, remappable and scoped to Code) asks the connected
language server to rename the symbol at the caret. When supported, the provider
first validates the position and supplies the current name. Enter the new name
in the accessible native dialog and choose **Preview Rename**. Search Results
shows the provider, old/new name, affected files and exact before/after spans.
Select a row to inspect its edit, then choose **Apply Preview** to confirm the
reviewed batch. Search fields and filters belong to the separate **Search Text**
action; they do not constrain the provider's rename. There is no textual rename
fallback: language configuration and the provider determine semantic coverage.

Matching open documents synchronize first, including inactive unsaved tabs.
Every open target must have a captured synchronized snapshot; versioned edits
must match its wire version. Unopened files use the shared text decoder. Invalid
Unicode or ranges, overlap, annotations, duplicate targets, file creation/move/
deletion, linked or outside-project paths and exceeded limits reject the entire
preview. Only text edits in existing project files or named open buffers are
supported. Preparation runs in the background with progress, Cancel and Activity.
Editing, changing source/project context or disconnecting retires pending work
and invalidates a completed preview. Re-run rename after opening a target from
the result list, because it is now an open-document target.

Apply validates every reviewed source first. Each open document gets one Undo
step and remains unsaved, retaining recovery, diagnostics and index updates.
Unopened files are saved individually through guarded atomic replacement,
preserving encoding, BOM and untouched line separators. Disk writes do not have
editor Undo; cancellation or a later write failure reports files already saved.
The batch is not a filesystem transaction. A dirty map in Levels blocks a Code
write to that same file; clean loaded maps refresh after a successful write.
Save modified Code tabs before compilation or package staging. Providers may
have incomplete indexes; review their scope and re-run after indexing or
external changes. Hashes guard snapshots after preview preparation, not the
freshness of an unversioned provider index.

Rename is bounded to 256 files, 10,000 edits, 64 MiB each of input and output,
4 Mi UTF-16 units of preview text and the shared 4 MiB per-document limits.
Individual before/after spans are shortened at 8,192 code units for display;
the complete validated edits remain in the plan. `documentChanges` takes
precedence over `changes`. The optional prepareRename default-behavior
capability is not advertised; preparation providers must supply a range.

## Code Actions

**Code Actions** (**Ctrl+.**, remappable and scoped to Code) requests quick fixes
and refactorings for the caret or selected UTF-16 range. The accessible native
picker shows preferred actions and unavailable entries with their reasons.
Choose **Preview Edits**; providers with resolve support may supply the edits
after selection. Search Results then shows the action title, provider, affected
files and exact before/after spans. **Apply Preview** confirms the reviewed batch.
Cancel covers the request, picker, resolution and background preview.

Code actions share rename's workspace-edit validation, project boundaries,
limits, all-target preflight, open-tab Undo/recovery and guarded disk saves.
Editing, switching source/project context or disconnecting invalidates the
request and completed preview. Save modified tabs before compilation or package
staging; dirty Levels maps block writes to the same source. Server commands,
including actions that combine an edit with a command, remain unavailable.
Resource creation, rename/deletion and annotated edits reject the entire plan.

The list retains at most 200 entries, 8 MiB of serialized action data and 2 MiB
per action. Invalid or oversized entries are counted; a partial list is visible,
and an entirely omitted list fails. Resolution preserves the selected action's
identity and opaque data. The client sends up to 32 overlapping diagnostics
(1 MiB total), only from a publication matching the synchronized document version.
Diagnostic data is bounded to 64 KiB per item and 2 MiB per document. Unversioned
or stale diagnostics are omitted from action context. Available actions depend
on the provider and its project index; no server is required for core editing.
When a newly synchronized document has no report yet, the client waits up to
2 seconds (or one third of the request timeout) for diagnostic context before
requesting actions. This grace period is cancellable and stays within the
original request timeout. Providers without diagnostics still receive the query.

## References

**Find All References** (**Shift+F12**) asks the connected server about the symbol
at the caret when references are supported for that document. It synchronizes
named open buffers first and includes the declaration. The existing Search
Results panel shows the provider, progress, cancellation, previews and any omitted
locations. Activity records the operation. Activate a result to open and select
its exact source range; browsing a result keeps the other results available.

Previews use current open-document snapshots, including unsaved inactive tabs,
or the shared text decoder for saved project files. Results carry source hashes
and, for open buffers, tab identities and revisions. Activation refuses a changed
snapshot. Editing source, changing document/project context or disconnecting
cancels pending lookup. Moving the caret alone does not cancel this search.

Disconnected or unsupported documents use the existing case-sensitive,
whole-word project text search, which can include comments, strings and unrelated
bindings. Connected semantic searches display empty results or errors explicitly.
Reference results are for navigation and cannot be used as replacement previews.
**Search Text** starts a separate textual query using the panel's fields and
filters; those filters do not change a server reference query. Neither lookup
saves documents or changes compiler/package inputs.

Only project locations without linked path components are accepted. Unavailable
sources, invalid ranges and output limits produce explicit partial results.
Reference replies contain no target-document version: source hashes detect
changes after preview preparation, but cannot prove that the server's index was
current when it calculated a location. Re-run lookup after indexing or external
source changes. Results depend on the server's project configuration and index.

## Completion

**Complete** (**Ctrl+Space**) uses the connected server for matching editable
documents when completion is supported. Advertised trigger characters, such as
`.` or `>`, also request suggestions after a short typing pause. Continuing to
type refreshes a visible or pending semantic list. Loading, partial results,
omitted suggestions and failures appear in the status bar. Escape dismisses the
list and cancels a pending request. Disconnected or unsupported documents, empty
results and request errors fall back to local names, project symbols and keywords.

Rows show names and supplied signature/detail text, identify deprecated entries
and indicate related edits. Documentation is available as a plain-text tooltip.
Use the list's arrow keys and Enter or Tab to select a suggestion. Acceptance
checks the document identity, revision, caret, server version and source hash.
Moving the caret, changing tabs, editing or disconnecting retires the old list.

Providers with `completionItem/resolve` support can defer detail, documentation
and related import edits. Highlighting an entry resolves it in the background;
the row and status bar show progress. Moving to another entry cancels the prior
resolve. Accepting while resolution is pending waits for the complete edit set,
then applies it as one Undo step. Escape, typing, caret movement, tab/project
changes and disconnect cancel that pending acceptance. Resolved metadata is
cached for the current list. Failure makes the entry unavailable with a reason;
the unresolved primary edit is never applied on its own.

List defaults are materialized before resolution, including opaque `data` and
edit ranges. The provider must preserve the item's identity, filtering/sorting
and primary insertion behavior. Only the three advertised lazy properties may
change. The resolved response goes through the same source-hash, Unicode,
overlap, size and command checks as an immediate suggestion. Documentation stays
literal text, including markup-looking content in tooltips.

The selected suggestion replaces the server's range and applies any supplied,
non-overlapping edits in the same document, such as an import. Insert/replace
items use their replacement range. Plain `insertText` items replace the typed
identifier prefix. All edits form one Undo step and remain unsaved, using normal
recovery, indexing and project-search invalidation. Save before compilation or
package staging. No completion edits another file or executes a server command.
Commands and unsupported indentation modes are omitted explicitly.

Snippet completions expand into editable fields. Tab and Shift+Tab move through
numbered fields; Escape ends the session at the current caret, and **Finish**
moves to the final stop. The inline field bar reports progress, offers previous/
next controls and shows a native selector when a field has choices. Linked
occurrences update together. Replacing an outer placeholder retires its nested
fields. The active selection and solid/dashed field underlines complement the
textual status and accessible descriptions.

Insertion and any related imports remain one unsaved Undo step, including after
deferred completion resolution. A subsequent field edit and all its mirrors
share another Undo step. Undo/Redo ends field navigation; Redo restores text.
Ordinary single-edit completion within the active field also updates its mirrors.
Another snippet or a completion with related edits starts a new edit context;
nested snippet sessions are not stacked. External edits, document changes,
read-only mode, leaving the fields or hiding Code retire the session. Accepted
field editing is local and follows existing recovery, indexing and save behavior.

Supported syntax includes tab stops, linked and nested defaults, choices,
escaping, and the LSP document variables for the current line/word, line number,
filename and path. Unset variables use their default; unknown names become fields.
Completion has no selected source range, so `TM_SELECTED_TEXT` is empty. No
environment, clipboard, command or network lookup supplies variables. Regex
transforms are omitted as unsupported suggestions. The parser accepts at most
65,536 input/expanded UTF-16 units, 256 syntax nodes, 16 levels of literal nesting,
128 expanded field occurrences, 32 choices per field and numeric identifiers
up to 1,000,000. Cyclic defaults reject the whole item. Excessive linked editing
ends the session with a status message and keeps the user's original edit.

The client supports stdio LSP initialization, full or incremental open/change/
close/save synchronization, push and document pull diagnostics, Location/LocationLink definitions,
Location reference lists, hover documentation, call signatures with trigger/retrigger
context and UTF-16 parameter labels, document/range formatting, prepared rename,
code-action literals and edit resolution,
plain-text and snippet completions with list defaults and related document edits,
cancellation and bounded shutdown. It negotiates UTF-16 positions. Unsupported
position encodings or synchronization modes fail with a visible explanation.
Server-initiated workspace edits are refused. Server commands, URI-opening
requests and dynamic registrations are not executed.

## CLI

The CLI uses the same client against one saved source file:

```sh
vibestudio --cli code language-server ./src/main.cpp --server /absolute/path/to/clangd --root . --language cpp --line 12 --column 8 --timeout-ms 15000 --json
vibestudio --cli code language-server ./src/main.cpp --server /absolute/path/to/clangd --root . --completion --line 12 --column 8 --json
vibestudio --cli code language-server ./src/main.cpp --server /absolute/path/to/clangd --root . --references --line 12 --column 8 --json
vibestudio --cli code language-server ./src/main.cpp --server /absolute/path/to/clangd --root . --hover --line 12 --column 8 --json
vibestudio --cli code language-server ./src/main.cpp --server /absolute/path/to/clangd --root . --signature-help --line 12 --column 8 --json
vibestudio --cli code language-server ./src/main.cpp --server /absolute/path/to/clangd --root . --format-document --json
vibestudio --cli code language-server ./src/main.cpp --server /absolute/path/to/clangd --root . --format-range --line 3 --column 1 --end-line 6 --end-column 2 --tab-size 4 --insert-spaces --json
vibestudio --cli code language-server ./src/main.cpp --server /absolute/path/to/clangd --root . --code-actions --line 12 --column 8 --json
vibestudio --cli code language-server ./src/main.cpp --server /absolute/path/to/clangd --root . --code-actions --line 12 --column 8 --action-index 1 --json
```

`--line` requests a definition at a one-based line. `--column` defaults to 1
and counts UTF-16 code units, also one-based. Add `--completion` to request
completion proposals at that position, `--references` to find semantic
references, `--hover` for symbol documentation, or `--signature-help` for call
overloads and active arguments, instead of a definition.
These query flags are mutually exclusive
and require `--line`. References include the declaration unless
`--exclude-declaration` is supplied; that flag requires `--references`.
Completion entries have stable one-based indices within the returned sorted list.
Each entry also exposes `snippet` and `tabStops`. Edits contain expanded text;
field `offset`/`length` values address the fully edited document, including imports.
`number` is the snippet field identifier, `parent` is an occurrence index or -1,
and `choices` contains available values. `caret` marks the final stop for a snippet.
CLI inspection never starts an editor field session or writes source files.
`--signature-help` selects a read-only call-signature query at the required
one-based `--line`/UTF-16 `--column`. JSON adds `signatureHelpReceived` and
`signatureHelp`, with normalized `signatures`, `activeSignature`, per-signature
`activeParameter`, parameter labels as `[start, end)` UTF-16 offsets into their
signature text, and optional `documentation` objects with `kind` and `value`.
Signature/parameter indices are zero-based. Snapshot SHA-256, request offset,
version, shortened-result and skipped-part fields accompany the result.
Text output numbers overloads and parameters from one. The query is exclusive
with other semantic query kinds; unsupported providers, malformed/limited results,
timeouts and error diagnostics return exit 4. A valid empty result is explicit.

`--completion --resolve-completion N` (1–500) resolves the selected suggestion
and returns its complete metadata and edits in the same `items` array. The
response identifies `resolvedIndex` and `resolveReceived`; unresolved items have
`needsResolve: true`. Opaque provider data is retained internally, not printed.
These completion queries remain read-only and retain the diagnostics-based exit
contract below. Unsupported resolution, changed identity, invalid edits or timeout
return exit 4; invalid selection options return exit 2.
Formatting queries are also mutually exclusive with the other queries. Use
`--format-document` without a position, or `--format-range` with a nonempty
one-based UTF-16 start and end position. `--tab-size` accepts 1–16 (default 4);
`--insert-spaces` prefers spaces (otherwise tabs). Preview is the default.
To write, add `--write --expected-sha256 <sourceFileSha256>` using the saved-file
hash from the preview or `code text-info`. `--dry-run` and `--write` conflict.
The source must match that hash before connecting and still match the captured
file snapshot before atomic publication. Encoding, BOM and untouched line
separators are preserved by the shared exact-range text save service. No missing
file is recreated and no other file is edited.

Rename uses `--rename <new-name> --line N --column N`, mutually exclusive with
other queries. The default is a preview. Review the returned `rename.changes`
and `rename.planSha256`, then repeat with
`--write --expected-plan-sha256 <planSha256>`. This hash binds all source hashes,
edit ranges, replacements, destination bytes, project, names and provider. If
the server or files produce a different plan, the command refuses to write and
returns the new preview. `--write` and `--dry-run` conflict. Rename does not wait
for diagnostics: exit 0 reports a complete preview or guarded write, exit 4 a
provider, validation, changed-plan or write failure, and exit 2 invalid options.
JSON includes every edit as normalized UTF-16 `offset`, `length`, and `text`,
plus saved/edited targets, warnings and save state. A no-change result is
successful without writes. This command uses saved files only; the GUI also
includes unsaved tabs.

`--code-actions --line N --column N` lists actions for a caret. Optional paired
`--end-line` and `--end-column` extend it to a selection. Add `--action-index N`
(one-based, 1–200) to resolve and preview one returned action. Review
`codeActions.preview.changes` and `codeActions.preview.planSha256`; repeat with
the same selection and `--write --expected-plan-sha256 <hash>` to apply. A hash
binds the title, provider, paths, source snapshots and full edit plan, so changed
ordering cannot silently apply a different action. A selected unavailable action
fails; listing unavailable entries alone succeeds. Incomplete or malformed lists
exit 4; a valid selected entry can still be previewed from a partial list.
Code action options are mutually exclusive with other queries. Like rename,
this query can complete without diagnostics; it does not assert source validity.
The CLI synchronizes saved files only. Providers that publish diagnostics after
the bounded grace period may return fewer fixes than an already connected editor.

Other queries never apply edits. Omit both positions and all query flags to inspect diagnostics
only. `--root` defaults to the source's parent directory; `--language` defaults
to `cpp`. `--server` must be absolute. `--server-args` accepts a JSON array of
literal argument strings, for example `'["--background-index=false"]'` in a
shell that preserves single-quoted arguments. No command shell is launched.

`--timeout-ms` accepts 100–120000 and defaults to 15000. The command waits for a
diagnostic publication or document pull response and, when requested, a definition,
completion, reference or hover reply. A server that neither publishes nor completes
requested diagnostics times out; silence is not reported as a clean check.
Exit 0 means a complete report with no error-severity diagnostics. Exit 2 means
invalid arguments; exit 4 means server/source validation failure, timeout,
limited diagnostics, incomplete/limited completion lists, skipped completion
items, partial/omitted reference or hover results, or reported source errors. Diagnostic
warnings alone do not fail. Formatting waits for its own reply and does not
require diagnostic publication: exit 0 means a complete formatting preview or
successful guarded write, not a clean compiler check. Received diagnostics are
included separately. Formatting failures/timeouts, invalid edits, changed source
files and save failures return exit 4; invalid options return exit 2.

The normal JSON envelope contains `languageServer` with `server`, `rootPath`,
`filePath`, `diagnosticsReceived`, `versioned`, `version`, `limited`, `diagnostics`,
`definitions`, `completionReceived`, `completions`, `referencesReceived`,
`references`, `hoverReceived`, `hover`, `formattingReceived`, `formatting`,
`renameReceived`, `rename`, `codeActionsReceived`, `codeActions`, `log`
and `error`. Locations have `filePath`, `line`, `column`,
`endLine`, and `endColumn`; lines and columns are one-based. Diagnostic severity
uses LSP values (1 error, 2 warning, 3 information, 4 hint), with `message`,
`code` and `source`. Inspect `versioned` before treating a diagnostic location as
version-verified.
`diagnosticOrigin` identifies `push` or `pull`; `diagnosticsError` reports a
diagnostic failure and `diagnosticsSkipped` counts invalid entries. Pulled
reports use the synchronized request version. Malformed, incomplete or failed
reports retain exit 4, including a reused result that has no valid cached source.

`completions` includes `version`, `incomplete`, `limited`, `skipped`, `items`,
`resolvedIndex` and `resolveReceived`.
Each item includes its `index`, `needsResolve`, label, detail, documentation, filtering/sorting strings,
kind, deprecated flag, normalized source SHA-256, request offset, resulting caret
and descending edits (`offset`, `length`, `text`). These offsets and lengths are
zero-based UTF-16 code units in normalized source text, not saved-file byte
offsets. Proposed edits must only be applied to the exact hashed source snapshot.

`references` includes `includeDeclaration`, source `version`, `complete`,
`limited`, `skipped`, `filesScanned`, `items` and `warnings`. Each item adds a
line preview, encoding, normalized `textSha256` and `source` (`command-source`
for the synchronized input snapshot or `saved-file` for other project files) to
its one-based UTF-16 range. Target ranges are checked against these snapshots;
the request version does not establish target-document version provenance.

`hover` includes `version`, `sourceSha256`, `requestOffset`, `contents`, `limited`,
`skipped` and an optional `range` (otherwise null). Each content part has `kind`
(`plaintext`, `markdown` or `code`), `value`, and a language identifier for code.
The request offset and range `offset`/`length` count zero-based UTF-16 code units
in the normalized source snapshot. Invalid ranges are omitted and reported as
partial; a valid empty server reply is a successful no-information result.

`formatting` includes source `version`, normalized `sourceSha256`, saved-byte
`sourceFileSha256`, ascending disjoint `edits` (`offset`, `length`, `text`),
normalized output `text`, `tabSize`, `insertSpaces`, `rangeOffset` (-1 for the whole
document), `rangeLength`, `dryRun`, `succeeded`, `changed`, `written`, and
`outputFileSha256`. Offsets are zero-based UTF-16 units. Same-position insertions
are combined in protocol order before applying a replacement at that position.
Failed replies never expose a partially applicable edit set.

## Bounds And Remaining Work

Each connection accepts up to 128 documents, 4 MiB of UTF-8 text per document
and 64 MiB of text in total. The GUI also bounds snapshot UTF-16 memory to
64 MiB. Synchronization validation is all-or-nothing; a failed GUI snapshot
stops the connection with an explanation. Individual protocol bodies are
bounded to 16 MiB, queued writes to 64 MiB, outstanding requests to 32,
definitions to 128 targets and diagnostics to 2,000 entries per document.
Completion parses at most 500 items and 2 Mi UTF-16 code units of returned text;
each item permits 32 related edits and 65,536 inserted UTF-16 code units in total.
Effective serialized items, including inherited defaults and opaque data, are
bounded to 256 KiB each and 8 MiB per list. Resolved items use the same bounds.
Overlapping/out-of-bounds ranges and splits inside Unicode surrogate pairs are
rejected. Accepted output must still fit the 4 MiB document limit.
References accept at most 10,000 locations. Preview preparation reads at most
4 MiB per source and 64 MiB in total, with 4 MiB of displayed result data and
8,193 UTF-16 code units per source-line preview. Full ranges and hashes are kept
even when a long preview line is shortened. GUI preparation runs in the background.
Hover replies retain at most 32 content parts and 65,536 UTF-16 code units;
truncation never splits a surrogate pair. Invalid content and optional ranges
are omitted explicitly. The GUI does not turn returned ranges into navigation
or edits.
Parameter hints retain at most 32 signatures, 128 parameters per signature and
65,536 UTF-16 units overall. Signature labels are bounded to 8,192 units;
individual descriptions are shortened at 8,192 without splitting Unicode pairs.
Invalid labels/ranges omit their whole signature. Protocol defaults select the
first signature/parameter when a supplied active index is outside its array.
Retained parameter indices and overload selection are rebuilt for retrigger
context; request context is bounded to 512 KiB.
Formatting accepts at most 10,000 edits and 4 Mi UTF-16 units of inserted text.
The result must fit the 4 MiB UTF-8 document limit. Any invalid range, annotation,
Unicode split, overlap or exceeded limit rejects all edits.
Logs retain 128 bounded lines. Startup and requests default to a 15-second
timeout; disconnect allows 1.5 seconds for graceful shutdown before termination.

Workspace-wide diagnostic pulls, related-document reports, will-save hooks and
server-initiated edits are not implemented. Snippet regex transforms, stacked snippet sessions, adjusted
indentation and completion commit characters remain pending. Some servers require these capabilities or custom setup and
will not work with this initial client. Unsupported notifications are ignored;
unsupported requests receive an explicit response.

`language-server-smoke` exercises framing, synchronization, stale reports,
version provenance, timeouts, cancellation, server failures, edit refusal and
completion, references, hover and CLI contracts using a controlled local fixture.
`language-diagnostics-smoke` covers pull-only providers, full/unchanged caches,
UTF-16 range validation, bounded queues, inter-file dependencies, refresh,
stale replies, cancellation retries, terminal failures, save ordering and CLI
exit/byte-preservation contracts. `language-diagnostics-ui-smoke` checks live
Problems rows, unsaved edits, Save/Save As, rejected writes, error/retry controls,
and direct 100%/200% widget renders with high contrast and expanded RTL labels.
`language-completion-smoke` checks range/default parsing, edit overlap, Unicode,
source hashes and limits. `language-completion-ui-smoke` checks popup metadata,
local fallback, related edits, undo/redo, unsaved bytes and stale replies, with
100%/200% popup renders. `language-server-ui-smoke`
checks explicit connection, inactive/unsaved buffers, Problems, definition
navigation, caret guards, project switching and direct QWidget renders at
100%/200% scale with high contrast, RTL and expanded translations. Physical
keyboard, screen-reader and macOS/Linux acceptance remain separate gates.
`language-completion-resolve-smoke` checks effective defaults and opaque data,
identity protection, related-edit validation, cancellation, stale versions and
read-only CLI selection. `language-completion-resolve-ui-smoke` checks automatic
highlight resolution, early acceptance, combined Undo/Redo, selection changes,
failed entries, cancellation, source/tab changes and direct 100%/200% popup
renders with high contrast and expanded RTL text.
`completion-snippet-smoke` checks linked and nested fields, choices, document
variables, Unicode ranges, malformed syntax, expansion bounds, deferred imports
and read-only CLI previews. `completion-snippet-ui-smoke` exercises field
navigation, mirrored edits, ordinary semantic and local completions, choices,
Undo/Redo, document switches, read-only state and oversized edits. Direct widget
renders cover 100% dark and 200% high contrast with expanded RTL labels.
The field bar also fits a 600-pixel pane with labeled navigation controls.
`language-references-smoke` checks decoding, live snapshots, path boundaries,
range/hash validation, cancellation and read/display limits.
`language-references-ui-smoke` covers provider status, exact-range navigation,
inactive unsaved buffers, stale results, cancellation, disconnect, local fallback
and direct 100%/200% widget renders with high contrast and expanded RTL labels.
`language-hover-smoke` checks content types, ranges, source hashes, Unicode and
limits. `language-hover-ui-smoke` covers Markdown/resource isolation, selectable
documentation, unsaved sources, pointer hints, cancellation and stale replies,
plus 100%/200% renders with high contrast and expanded RTL labels.
`language-signature-smoke` covers overload and parameter defaults, string and
UTF-16 range labels, repeated labels, Unicode, limits, retrigger context,
cancellation, stale versions and read-only CLI behavior.
`language-signature-ui-smoke` checks active-argument emphasis, native overload
selection, automatic triggers, unsaved edits, Undo, document switches,
disconnect, provider failures and resource-isolated documentation. Direct
100%/200% widget renders include high contrast, expanded RTL labels and a narrow
600-pixel pane with wrapping controls. Repeated replies and overload switches
check that emphasis stays confined to the active parameter.
`language-formatting-smoke` checks edit ordering/overlap, Unicode, limits, exact
mixed-ending saves, cancellation, stale snapshots, formatting-only servers and
CLI preview/write conflicts. `language-formatting-ui-smoke` checks selection and
unsaved-buffer formatting, one-step Undo/Redo, cancellation, failure, disconnect,
normal Save and direct 100%/200% progress renders with expanded RTL labels.
`language-rename-smoke` covers preparation, both WorkspaceEdit representations,
versions, live snapshots, path/range rejection, cancellation, mixed line endings,
whole-plan hashes and CLI guarded writes. `language-rename-ui-smoke` covers the
name/review dialogs, mixed unsaved/disk edits, Undo, stale previews, cancellation,
unsupported operations and 100%/200% review renders with high contrast and
expanded RTL labels. These tests call Qt/service methods directly.

`language-code-actions-smoke` covers capability negotiation, diagnostic data,
UTF-16 selections, bounded lists, unavailable commands, lazy resolution,
cancellation, stale responses and CLI list/preview/guarded-write behavior.
`language-code-actions-ui-smoke` covers selection, accessible picker metadata,
lazy previews, mixed unsaved/disk application, Undo, cancellation, stale context,
invalid actions and 100%/200% widget renders with high contrast and expanded RTL
labels. These also call Qt/service methods directly, without OS input injection.

Ruff 0.16.4 document pulls returned an undefined-name diagnostic at the expected
UTF-16 range and exit 4, then an explicit empty report and exit 0 for clean code.
Both UTF-16LE BOM/CRLF files remained byte-identical. Ruff ran as a separate
MIT-licensed test process; it is not a VibeStudio dependency. Controlled fixtures
cover successful-save ordering, cached results, refresh, retries and failures.

Windows verification also exercised installed clangd 20.1.7 through the CLI:
a clean C++ source resolved a definition and exited 0; an undeclared identifier
produced versioned error diagnostics and exit 4. Both original source files
remained byte-identical. A member-completion query also returned a validated
replacement for the typed prefix, with the expected error report for that
unfinished source and unchanged saved bytes. A reference query returned only the
global declaration and use, excluding comments and a same-named local binding;
`--exclude-declaration` returned only the use. Both checks exited 0 and preserved
the source bytes. A hover query returned a field's type, documentation comment and
exact source range, with a verified source hash, exit 0 and unchanged saved bytes.
Formatting checks returned document and selection edits, preserved UTF-16LE BOM
and CRLF through an explicit hash-guarded write, and produced no changes on a
second run. Preview-only files remained byte-identical.
Rename checks used a standalone C++ global declaration and use, preserving a
same-named local binding, a comment and an unrelated header. Repeated previews
produced the same plan hash; an incorrect hash blocked writing. The reviewed
write preserved UTF-16LE BOM and CRLF. Multi-file and inactive-buffer application
are additionally covered by the controlled fixture suites.
Code-action checks requested clangd's missing-semicolon quick fix immediately
after opening a UTF-16LE file. The bounded diagnostic wait supplied current
context; listing and repeated previews preserved source bytes and produced a
stable plan hash. A wrong hash blocked writing, while the reviewed write inserted
the semicolon, preserved BOM/CRLF and yielded a clean subsequent diagnostic
report. This clangd action provided edits directly; lazy resolution is covered
by controlled protocol and GUI fixtures.
Completion-resolution checks also exercised Pyright 1.1.414: a Python member
suggestion returned deferred documentation and a validated replacement preview.
The UTF-16LE BOM/CRLF source remained byte-identical. The unfinished expression
still produced exit 4 under the diagnostic contract. Installed clangd 20.1.7
did not advertise completion resolution; immediate suggestions remained usable,
and an explicit resolve request reported the unsupported capability without
changing the file. Deferred import edits are covered by the controlled fixtures.
Snippet checks requested a two-argument function through clangd 20.1.7 with
`--function-arg-placeholders=1` and `--completion-parse=always`. The latter waits
for a cold parser; the default auto mode can initially return plain-text
fallback suggestions. The CLI returned expanded text and validated ranges for
both editable arguments, with a matching normalized source hash and unchanged
UTF-16LE BOM/CRLF bytes. The unfinished expression retained diagnostic exit 4.
Parameter-hint checks queried valid C++ and Python calls through clangd 20.1.7
and Pyright 1.1.414. Both returned the second active argument, its validated label
range and documentation, with matching normalized source hashes and exit 0.
Both UTF-16LE BOM/CRLF source files remained byte-identical. Controlled fixtures
add overload selection, automatic retriggers, unsaved edits and cancellation.
This validates that configuration,
not every server or project toolchain.

The original implementation follows Microsoft's
[LSP 3.17 specification](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/).
Protocol reference and license details are recorded in [Credits](CREDITS.md).
