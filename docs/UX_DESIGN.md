# UX Design Philosophy

VibeStudio should feel modern and powerful without making users guess what is
happening. The design principle is simple: think like the user. At every point,
the user should understand what the app is doing, whether it is safe to act,
where results will appear, and how to inspect more detail.

## Core Principles
- The user should never wonder whether VibeStudio is frozen, busy, waiting, done, or blocked.
- Common workflows should stay clean and direct.
- Efficient workflows are part of UX: reduce repeated setup, duplicate file picking, unnecessary modal stops, context switching, and manual command reconstruction.
- Accessibility is part of UX: support high-visibility themes, scaling, keyboard access, screen-reader metadata, reduced motion, and OS-backed TTS as normal product features.
- Localization is part of UX: layouts must survive longer text, right-to-left languages, non-Latin scripts, pluralization, locale formatting, and translated terminology.
- Initial setup should let users tailor the application ecosystem before work begins, without trapping them in a rigid wizard.
- Advanced details should be available without overwhelming the default view.
- Graphical elements should communicate real structure, state, or relationships.
- Every long-running task should have a visible home, a progress state, and a result.
- Errors should explain what happened, where it happened, and what the next practical action is.
- AI-assisted workflows should feel like supervised acceleration: clear context, provider, plan, cost/usage where available, proposed actions, and review state.
- AI-free users should never encounter dead ends that require cloud services.

## The Shell As It Stands

The shell is `src/app/application_shell.*`, a `QMainWindow` that now declares
`Q_OBJECT` and is processed by `moc`.

**A design system, not a stylesheet.** Every colour, radius, padding, and
font size the shell uses comes from one token set in `src/app/studio_theme.*`,
resolved from three preferences: theme, density, and text scale. The default
dark look takes its cue from idStudio: neutral charcoal panels rather than
blue-tinted ones, black-backed viewports, and an orange accent. The light
theme and both high-visibility themes are the same roles with different
values. The tokens become an application-wide `QPalette` and a generated
stylesheet keyed on object names and a `variant` property (`primary`,
`danger`, `ghost`), so a widget picks a role and never a colour. The shell runs
on Fusion, the one built-in style that honours a custom palette identically on
Windows, macOS, and Linux, with a small proxy style on top that draws check
boxes and radio buttons from the tokens, because Fusion's own frames all but
vanish on dark panels. `studio-theme-smoke` holds every theme to WCAG AA:
body and secondary text on every surface, selection text on the selection
fill, and accent text on the accent fill at 4.5:1, focus rings and state
colours at 3:1, and a focus colour that is never the selection colour.
Once the shell is built, every item view is polished and laid out again: a
list lays its rows out with the metrics it has when its first items arrive,
and polishing it later does not redo that, so a list filled while its page was
still unpolished kept unpadded row positions under padded rows. The Settings
categories, filled once at construction, overlapped each other until this.

**Painted icons that follow the theme.** `src/app/studio_icons.*` draws about
sixty line glyphs with `QPainter` on a 24-unit grid: modes, file actions,
transport, status, and navigation. They read their colour from the current
theme at paint time, so a theme switch recolours every icon on the next
repaint, disabled and selected states follow automatically, and nothing depends
on image files, SVG support, or the platform style's pixmaps, which were
invisible on dark backgrounds. Icons scale with the text-scale preference, and
buttons that show text beside an icon reserve a consistent gap after the glyph.

**Mode rail over a stacked work surface.** `ModeRail` in
`src/app/studio_layout.*` lists the ten modes (Workspace, Levels, Models,
Textures, Audio, Packages, Code, Shaders, Build, Settings) as checkable tool
buttons over a `QStackedWidget`, grouped by dividers into home, content, assets
and code, and shipping, with Settings pinned to the bottom. Order still matches
Ctrl+1 to Ctrl+0. The rail is one tab stop: Tab reaches the current mode and the
arrow keys, Home, and End move between modes; a mouse click never takes focus,
so focus rings appear only for keyboard users. A chevron at its foot collapses
it to icons only, and its width is computed from the longest label at the
current text scale, so translated or 200% labels are not cut off. The selected
mode and the compact setting are persisted.

**One page anatomy.** Every surface is assembled from the same parts: a
`PageHeader` (mode glyph, title, a one-line context summary that elides in the
middle, and the page's actions with at most one primary button), an optional
page tool bar, and a body. Workbench pages (Levels, Models, Textures, Audio,
Packages, Code, Shaders) use splitters instead of a scrolling column: a list or
outliner on the left, the viewport, preview, or editor filling the middle, and
an inspector on the right. Tab groups in side and bottom panels put their tabs
along the bottom edge, the way idStudio's docked panels do. Splitter
proportions are saved per page, and **View > Reset Layout** restores the
built-in ones.

**Empty states and quiet status strips.** A page with nothing to show replaces
its body with an `EmptyStateView` (a glyph, one sentence, and the action that
fills it) instead of three empty panes. The asset pages distinguish "no package
open" from "this package has no textures", and a failed open shows the error in
the same place. Each page's `LoadingPane` status strip appears only while it has
something to say (queued, loading, running, warning, failed, or cancelled) and
hides when idle or complete, because the page header already names what is
open. Progress bars appear only while work is running.

**One command registry behind menus, toolbar, palette, and shortcuts.**
`src/app/studio_actions.*` owns the `QAction` instances. `populateMenuBar()`
builds the menu bar and `populateToolBar()` builds an icon-only global tool bar
whose tooltips name each command and its shortcut; only Run Build Pipeline and
Launch Game carry text. A **Search commands** field at the right of the bar
opens the palette. Default shortcut sequences come from
`core/studio_semantics.h`, and command ids now match across spellings: the
shell registers camelCase ids (`shell.commandPalette`) while the registry
documents kebab-case (`shell.command-palette`), and the normalizer used to
lowercase without inserting the hyphen, so about a dozen documented shortcuts
(Ctrl+Shift+P, Ctrl+F, Ctrl+E, F2, Del, and others) never bound and the palette
listed a disabled duplicate of each command. A duplicate sequence is still
recorded as a conflict and skipped rather than silently shadowing its first
owner.

**Type-to-filter command palette.** `CommandPaletteDialog` lists every enabled
registry command plus the documented palette-only entries, filtered as the user
types, with each row showing its category, summary, and shortcut. It opens
anchored near the top of the window so the list grows down over the work
surface.

**Status bar.** Permanent chips report project, package, installation,
compiler, and AI health. Each shows a state glyph and words, such as a check
mark before the package name or a warning triangle before "Compilers 7/9",
never colour alone. Each mirrors its text into an accessible description,
carries its operation state as a style property, and elides long names in the
middle rather than widening the window. Two toggles beside them open the
**Activity** and **Inspector** panels; the Activity toggle also counts running
tasks.

**Panels as docks.** Activity (every task with its progress, log, warnings, and
cancel) and Inspector (settings, setup, and project diagnostics) are
`QDockWidget`s, tabbed together on the right and closed by default so every
work surface gets the full width. The View menu, the status-bar toggles, and the
saved window state bring them back where the user left them. Each dock carries a
`DockTitleBar`: the panel name with float and close buttons drawn from the
studio glyphs, replacing the platform's title buttons, which were a few pixels
across and nearly invisible on the dark theme. Presses that miss the buttons
fall through to the dock, so dragging the bar still moves the panel and
double-clicking it still floats it.

**The Workspace dashboard.** A start page rather than a scroll: the project's
health strip, jump tiles for every work surface, then two balanced rows of cards
sized to their content (project health beside recent projects and game
installations, and the workspace details drawer beside the recent-activity
chart), with assistant proposals at the foot.

**Real pictures instead of text stand-ins.** `src/app/studio_charts.*` provides
painted composition, pipeline, and activity-timeline widgets, and
`src/app/map_viewport.*` provides an interactive 2D map view that draws real
geometry through the shared solver in `core/map_geometry` — replacing the
`[##########........]` ASCII readiness bar as the primary display and the
text-only "preview lines" map tab entirely. A text readiness bar still appears
in the compiler tool list rows and the copyable inspector dump, where plain
text is the point. `src/app/asset_views.*` adds image, palette-swatch, and
waveform views so textures, palettes, and audio are shown rather than
described, and `src/app/model_viewport.*` does the same for models — a
`QPainter` renderer with an orthographic projection and a painter's-algorithm
depth sort, so the studio still carries no OpenGL dependency.

**Direct manipulation.** The window accepts dropped files and routes each one by
type: maps to the Levels surface, a folder holding a project manifest to
Workspace, packages to Packages, `.shader` scripts to Shaders, `.def`, `.fgd`,
and `.ent` entity definitions to the Levels entity inspector, anything else to
the Code editor. `--open <path>` on the command line routes the same way. The
package entry list has a context menu for extract, stage-replace, rename, and
delete; it had been written but never connected, and is now wired to a
right-click.

**An asset browser for packages.** The Packages page works like idStudio's
asset browser. The **Folders** tree holds folders only; selecting one lists its
contents in the middle column, folders first with their item counts and then
files with size, type, and storage, under a breadcrumb path with back, forward,
and up buttons. Activating a folder row steps into it. Typing in the filter
switches the list to a flat search across the whole package, shown with full
paths, and browsing again ends the search. Rows carry a glyph for their kind
(folder, image, audio, model, map, shader, archive, file), and ordinary readable
entries keep the normal text colour; only rows with a warning are tinted. The
inspector on the right holds Details, Preview, Staging, and Overview tabs.

**Thumbnail tiles for textures.** The Textures page shows decoded thumbnails in
a tile grid by default, with a remembered **Tiles**/**List** toggle. Thumbnails
decode six at a time on the event loop, so a WAD with thousands of textures
stays responsive while they fill in; small idTech art scales up with
nearest-neighbour filtering so its pixels stay crisp, and the cache is rebuilt
when the package or palette changes. Tiles share each row evenly and every
cell is wider than its thumbnail, so typical texture names show whole beneath
it; longer names elide in the middle. The eliding delegate used to cap every
row, tiles included, at 96 pixels, which cut names such as `metal_panel.tga`
down to `metal...l.tga`. Asset lists keep their selection across a refresh and
select the first entry when nothing was chosen, so the preview is never blank
beside a list of content.

**Moving map objects by pointing at them.** `MapViewport` is no longer
read-only. Clicking an object replaces the selection, Shift+click adds to it,
Ctrl+click toggles it, and pressing a member that is already selected promotes
it to primary without dropping the rest, so a multi-object drag can start
anywhere in the set. A press on empty space starts a rubber band whose result
is applied on release — plain replaces, Shift adds, Ctrl toggles each — which
means a plain click that hits nothing clears the selection instead of leaving a
stale one. A drag on a selected object arms on press and only starts after
`kDragThresholdPixels` (4 px) of travel, so a click that wobbles is still a
click. Arrow keys nudge the selection by one grid step, Shift+arrow by
`kCoarseNudgeMultiplier` (8) steps; Ctrl+arrow still pans, and so do bare
arrows when nothing is selected, so the older keyboard panning is never taken
away. Escape cancels a drag or rubber band in progress, then clears the
selection, then moves focus out of the widget.

**One edit, one undo command.** The viewport never edits the document. It
previews the move locally and emits `moveRequested(dx, dy, dz)` exactly once —
on drag release, or once per nudge — and the shell turns that into a single
`moveLevelMapSelectionSnapped()` call, which records one compound undo command.
One Undo therefore puts the whole drag back, not one object of it at a time.
When the move is refused the shell calls `refreshLevelMapViewport()` to throw
the preview away, so the picture can never keep showing an edit the document
did not take. A successful move records an activity entry, "Level map selection
moved", in the warning state with the detail "Unsaved map edit".

**One selection, two surfaces.** The Objects list is
`QAbstractItemView::ExtendedSelection`, and list and viewport share one
selection set: `selectionSetChanged()` carries the whole set in add order with
the primary last, `setSelectionSet()` pushes it back, and the mirroring is
guarded so the list's own selection handler cannot write the set back and fight
the drag that produced it. A **Snap** checkbox on the viewport control row,
checked by default, decides whether a drag lands on whole grid steps or follows
the raw pointer delta; `statusLines()` reports the snap state and, mid-drag,
the delta and the destination coordinates. The viewport's own static accessible
description still describes only click-to-select, drag-to-pan and Tab cycling;
it has not been rewritten for these gestures, although `accessibleSummary()`
does report the primary object and how many objects are selected with it.

**Model preview with playback.** The Models page puts a control row above
`ModelViewport`: a render-mode box offering Textured, Flat shaded and
Wireframe; an animation box whose first entry is "All frames" and which is
disabled when the mesh yielded no animations; a Play/Pause button enabled only
when `frameCount()` is above one; and a **Frame Model** fit button.
**Export OBJ**, in the page header, writes the frame currently on screen through
`exportModelFrameObj()` and refuses, in the status bar, when the format gave no
geometry. In the viewport, left-drag orbits, middle-drag or
Shift/Ctrl+left-drag pans, the wheel zooms about the pointer, `Space` toggles
playback, `Page Up`/`Page Down` step one frame, `Home` frames the model and `0`
resets the view. Animations are inferred from frame names, which is how MDL and
MD2 store them, and the animation box says so in its tooltip. Textured mode
falls back to flat shading when no skin resolves out of the package, and the
in-view readout then says "Flat shaded (no skin)" instead of naming a mode it
is not drawing. The viewport now starts in the mode the box shows; it used to
start flat shaded under a box reading Textured, so a skinned model looked
untextured until the user picked a mode. Quake MDL, Quake II MD2 and Quake III
MD3 decode geometry; MDC, MDR and IQM are header-only, so they get a distinct
"nothing to draw" paint path rather than an empty viewport that looks broken.
No control exposes the frame rate: playback runs at the widget's fixed default
of 10 fps. The readout under the viewport is one line (frame, animation,
playback, and skin size) and gives way to the triangle under the pointer while
hovering; counts live in the in-view corner. The inspector's **Summary** tab is
a Property / Value grid like the entity inspector: a **Warnings** group first
when there is anything to warn about (including a model whose named skins were
all missing from the package), then **Model** (format and version, frames,
surfaces, vertices, triangles, bounds, radius, tags), **Surfaces**,
**Animations**, **Skins** with the skin that was drawn marked by a check, and
**Header** for whatever else the decoder reported.

**Audio format grid.** The Audio page's **Format** tab is the same grid: codec,
channels, sample rate, bit depth, frames, duration, playback and export support,
and any warning about the file. Core's report also ends with a text sketch of
the waveform for the command line; the page leaves that out, because it paints
the real envelope above the grid.

**Shader stage tree.** The Shaders page's **Stage graph** is a tree of shaders,
their stages (map, blend function, and rgbGen), and the textures each stage
references. Every texture row says whether the reference was found, and in
which package, or is missing from the open package, with a warning glyph rather
than colour alone; its tooltip lists the paths that were tried. Selecting a
shader or a stage puts a section for it at the top of **Shader Details** with
its parsed fields and raw text, ahead of the whole-script report. Raw text in
detail panels keeps four-column tabs, so a stage's braces no longer sit halfway
across the panel.

**Entity inspector.** The Levels page's right-hand inspector opens on an
**Entity** tab holding a property grid in the style of idStudio's entity
inspector: collapsible **Entity**, **Keys**, **Defaults**, and **Spawnflags**
groups of Key / Value rows with alternating shading. **Entity** shows the class,
index, and, from loaded definitions, the kind, inheritance, description, size,
model, and the file and line that declared it. **Keys** lists every key set on
the entity, with `entityKeyHelpText()` as each row's tooltip. **Defaults** lists
the keys the class declares but the entity leaves unset, muted, with required
ones flagged; editing one sets it. Double-click or Enter edits a value in place,
and the edit goes through `setLevelMapEntityProperty()` as one undoable change;
**Edit Key** starts from the selected row. **Spawnflags** are check boxes whose
drawn mark, not colour, carries the state; toggling one rewrites the summed
`spawnflags` value. The inspector follows the primary selection whichever
surface set it; selecting in the Objects list used to leave it untouched. An
**Entity definitions** row beneath the grid (a path field, **Browse**, and
**Load**) reads Radiant `.def`/`.qc`, Valve `.fgd` and Quake III `.ent` files or
a folder of them; left empty it searches `entityDefinitionSearchPaths()` against
the current project. The same definitions drive an `ENTITIES` section in the
Health tab, listing up to 40 issues by `issue.code` and message, each carrying
an `entity:<id>` selector so clicking navigates to the entity. With nothing
loaded, Health says so rather than claiming the entities are clean.

**A code editor with a gutter.** The Code page's editor, `StudioCodeEditor` in
`src/app/code_editor.*`, adds a line-number gutter and a faint band on the
caret's line, both drawn from the active theme; the band is outline-strength in
the high-visibility themes. Syntax highlighting and diagnostics are unchanged,
and a diagnostic row still jumps to its line. The strip above the editor names
the document the way an editor tab does, as file name, language, and save state
(Modified, Saved, or Read-only, truncated), with the full path in its tooltip;
the page header already shows the path, so the strip no longer repeats it.

**Build stages at a glance.** Each row of the Build page's **Stages** list shows
the stage and its state glyph over its input and output file names
(`start.map` to `start.bsp`); the full paths are in the row's tooltip and in
**Build Details**, instead of wrapping each row across four lines. When the
pipeline chart fits on one row with room to spare, its boxes widen a little and
its connectors lengthen so the chain spans the panel instead of huddling at its
left edge; both are capped, so a wide window does not produce sparse boxes.

**Viewport readouts.** The map and model viewports draw corner readouts the way
idStudio's viewports do: the view, grid size, and snap state in the top-left of
the map view with what the map holds in the top-right, and the render mode in
the top-left of the model view with the file, frame, and surface, vertex, and
triangle counts in the top-right. Each sits on a backdrop drawn from the
viewport's own palette, with an outline in the high-visibility themes.

**Package comparison.** A **Compare** button in the Packages page header, and
the `package.compare` command, run `comparePackages()` over the open package
and one chosen from a file dialog. The result lands in the package detail
drawer as Summary (left, right, added, removed, changed, case-only, identical,
not compared, size delta), Entries (`packageCompareLines()`, which lists only
the rows that differ), and Warnings. Case-only path differences are their own
status rather than being folded into "changed", because a path that differs
only in case is a real portability bug on one platform and invisible on
another. A difference completes the activity task with a warning, not a
failure: the user asked what differs, and an answer is not an error. The GUI
compares the two archives as they exist on disk; the staged plan is not part of
it.

**Confirmation before damage.** `confirmDestructiveAction()` shows a warning box
whose default button is Cancel. It guards discarding staged package changes,
launching an external game executable, and project-wide find/replace — which
runs as a dry run first and reports the match and file counts inside the prompt.
Closing the window with unsaved code or unwritten staged changes prompts
separately.

**Replacing a package that already exists.** Package save-as writes over an
existing file only after a **Replace Existing Package?** question whose default
button is No, worded differently when the target is the package currently open.
What the prompt protects is the user's only copy of an archive: it states that
the new archive is written beside the target and verified before anything is
moved, and that the original becomes `<output>.bak` rather than being
discarded. Accepting sets `PackageWriteRequest::allowInPlaceOverwrite` and a
`backupPath`; declining writes nothing and says so. The write summary then
reports whether the package was replaced in place and where the backup went, so
the prompt's promise is visible after the fact and not only before it.

**Questions about changes the studio did not make.** A one-second coarse timer
drives `DocumentWatcher::poll()` over the open map, the open package, the file
in the code editor, and the project manifest — one path per role, so opening a
second map stops watching the first. A code file whose read was truncated at
the editor's 4 MiB limit is deliberately not watched, because a prompt there
would offer to reload bytes the editor never showed. A `Touched` change —
metadata moved with the bytes provably identical — is ignored outright, since
saying anything about it would train the user to dismiss the prompt that
matters. A `Removed` file is a status-bar notice, because there is nothing to
reload and therefore no question to ask. The rest ask: **Map Changed On Disk**,
**Package Changed On Disk** and **File Changed On Disk**. Each prompt protects
work the studio is holding that the disk does not have, so each defaults to No
and says what reloading would cost — "Reloading discards those edits",
"Reopening discards the staged changes" — whenever there are unsaved map edits,
staged package operations, or a dirty editor. A clean editor reloads silently;
a clean map or package still asks, but defaults to Yes. Declining re-baselines
the watcher and remembers the path, so the same change is never asked about
twice. The project manifest reloads without asking, because nothing the user
typed lives only in those panels. Every substantive change also records an
activity entry, "File changed outside the studio", in the warning state.

**The previous session's crash report.** `reportPreviousSessionCrash()` shows
**The Studio Closed Unexpectedly** once at startup when a previous session left
a marker behind and the process that owned it is gone. It offers
**Show Report**, which opens the folder holding the report, and **Dismiss**;
the report is offered, never sent, and stays on the machine unless the user
copies it somewhere. The prompt protects the user from a silent loss: a session
that vanished without a word leaves them unsure whether anything was written, so
the dialog names what happened, summarises the report through
`crashReportSummaryText()`, and records an activity entry in the failed state.
It then calls `markSessionEndedCleanly()`, so a single crash is reported once
rather than at every launch. This prompt is wired but dormant:
`installCrashHandling()` is exercised only by
`src/tests/studio_runtime_smoke_test.cpp`, and `src/main.cpp` installs session
logging alone, so no shipping run populates the state the prompt reads.

## Accessibility Baseline
Accessibility behavior should be designed into every surface:
- [x] High-contrast dark and high-contrast light themes.
- [x] Text/UI scale support at 100%, 125%, 150%, 175%, and 200%.
- [x] Comfortable, standard, and compact density presets.
- [ ] Reduced-motion setting for transitions and loading visuals — stored and
  honored by the loading pane, the map viewport selection ring, and the model
  viewport, which never starts playback on its own under reduced motion while
  leaving Page Up and Page Down frame stepping available. The shell still has no
  transitions of its own for it to suppress.
- [ ] Keyboard-visible focus and no keyboard traps. Focus rings now use a
  colour distinct from selection (2px in the high-visibility themes), the rail
  and tool buttons take focus from Tab but not from a click, and the entity
  grid edits on Enter; a full keyboard-trap audit is still outstanding.
- [x] Accessible names and self-updating accessible descriptions for the charts,
  the map viewport, the model viewport, the asset preview views, the loading
  pane, and the detail drawer.
- [ ] Accessible names, roles, descriptions, and status changes for every
  remaining custom widget.
- [ ] Screen-reader-readable task states, compiler diagnostics, validation results, and setup warnings.
- [ ] OS-backed TTS for selected summaries, errors, task outcomes, and setup guidance.
- [x] No color-only status in the charts, status chips, map viewport, list rows,
  and entity inspector, where a spawnflag's set state is a check box with a
  drawn mark; colour is always paired with a glyph, hatch, stroke, mark, or
  text cue.
- [ ] Audit every remaining surface for colour-only status.

See [`docs/ACCESSIBILITY_LOCALIZATION.md`](ACCESSIBILITY_LOCALIZATION.md) for
the detail behind each of these.

## Initial Setup Experience
The first-run setup flow should configure the studio without becoming a tour:
- [ ] Language, scale, high-visibility, motion, and TTS first.
- [ ] Role and experience presets for mapper, artist, programmer, package maintainer, shader author, audio creator, release maintainer, all-in-one, or custom.
- [x] Editor profile selection for VibeStudio default, GtkRadiant 1.6.0-style, NetRadiant Custom-style, TrenchBroom-style, and QuArK-style workflows.
- [x] Game installation detection with manual add, skip, and later paths.
- [ ] Project/package setup with output/temp/backup paths.
- [ ] Compiler/source-port probing with visible results.
- [x] AI-free mode and optional connector configuration.
- [ ] CLI integration and command-copy preferences.
- [ ] Final summary with warnings, smoke checks, and editable preferences.

## User-Visible State
Every noticeable operation should expose an appropriate state:
- [ ] Idle.
- [ ] Queued.
- [ ] Loading.
- [ ] Scanning.
- [ ] Indexing.
- [ ] Running.
- [ ] Waiting for user input.
- [ ] Cancelling.
- [ ] Completed.
- [ ] Completed with warnings.
- [ ] Failed with next-step guidance.

## Feedback Patterns
Use the right feedback surface for the job:
- [x] Inline skeletons for panes waiting on content.
- [x] Progress bars for measurable work.
- [x] Indeterminate activity indicators only when progress cannot be measured —
  the loading pane switches to a marquee bar only when a busy state reports no
  total, and reduced motion opts out of even that.
- [x] Task cards for background operations.
- [x] Activity center for queued/running/completed work.
- [x] Status chips for package, project, compiler, and install health.
- [ ] Toasts for short-lived confirmations — short-lived messages currently go
  to the status bar instead.
- [x] Persistent problem panels for actionable warnings/errors.
- [x] Expandable logs for compiler, package, AI, and CLI-backed operations.
- [x] Cancellation controls when an operation can safely stop.

## Progressive Disclosure
VibeStudio should present a clear summary first, then let users delve into
detail:
- [x] Package summary before raw entry tables.
- [x] Compiler status before raw stdout/stderr.
- [x] Project health before validation traces.
- [x] Asset preview before byte-level metadata — the texture, model, and audio
  surfaces render the asset first and keep format metadata beside it.
- [x] Package comparison totals before the per-entry comparison list.
- [x] Entity class summary and key meanings before the raw key/value pairs the
  map stores.
- [x] Map statistics and health before raw map details.
- [x] Shader stage graph before raw shader text, while preserving round-trip access.
- [x] AI proposal summary before prompts, context, and generated commands.
- [ ] Agentic workflow plan before tool calls, writes, generated assets, or validation loops.
- [x] Friendly error summary before stack traces or diagnostic dumps.

## Creative Graphical Communication
Graphical elements should help users decide and act:
- [x] Package composition charts for file types and sizes, drawn as a stacked
  proportion bar with a hatched, glyph-labelled legend.
- [ ] Asset dependency graphs for textures, shaders, models, maps, and packages
  — the dependency surface is still a list, not a graph.
- [x] Compiler pipeline diagrams for source map to output artifacts, drawn as a
  left-to-right stage graph with per-stage state glyphs.
- [x] Map health overlays for leaks, missing textures, entity problems, and compile warnings.
- [x] Shader stage diagrams for idTech3 material flow.
- [x] Timeline views for task history, drawn with duration bars.
- [x] Visual diff/staging views for package writes.
- [x] An interactive 2D map viewport with grid, sector fills, brush and patch
  outlines, things, labels, hover, multi-select, rubber band, drag-to-move,
  arrow-key nudge, and orthographic projections.
- [x] A software-rendered model viewport with wireframe, flat-shaded, and
  textured modes, animation selection, and frame playback.

## Detail Surfaces
Advanced users should be able to inspect:
- [x] Raw package metadata.
- [x] Virtual paths and physical source paths.
- [x] Parsed format structures.
- [x] Compiler command manifests.
- [x] Compiler stdout/stderr.
- [x] Hashes and reproducibility manifests.
- [x] Map entity properties, texture/material references, validation, preview lines, and undo history.
- [x] Entity class definitions, declared keys with their documented meaning, and
  spawnflag bits, plus the per-issue codes from entity validation.
- [x] Per-entry package comparison, each row carrying its status id, path, and
  size delta, with identical entries omitted.
- [x] AI prompts, selected context, responses, and proposed tool calls.
- [ ] AI provider, model, connector capability, cost/usage metadata where available, and generated asset provenance.
- [x] CLI-equivalent command for GUI compiler actions.

## MVP UX Requirements
- [x] Every package open shows loading feedback.
- [x] Every compiler run creates a visible task with progress, logs, result, and output paths.
- [x] Every validation run has a summary and expandable details.
- [x] Every extract/package operation reports exact output paths.
- [x] Every destructive or write operation is staged, previewed, or confirmed.
- [x] Empty states say what the user can do next.
- [x] Errors avoid dead ends.

Milestone 4 adds durable recent task history for completed package/compiler
operations and a release validation script that checks loading/progress,
summary/detail, graphical summary, high-visibility, keyboard, localization, and
TTS smoke coverage before a release-candidate package can pass. Milestone 5
adds visible package staging with operation summaries, blocker messages,
before/after composition, exact save-as output paths, hashes, and manifests.
Milestone 8 adds an Advanced Studio workbench with summary cards and detail
tabs for shader graphs, sprite plans, source indexes, AI proposal review, and
extension manifests so users can inspect generated plans before writes or
external commands run.

## Anti-Patterns
- Silent background work.
- Modal dead ends.
- Decorative graphs without actionable meaning.
- Spinners with no context.
- Progress that reaches 100% without showing where the result went.
- Hiding raw details from users who need them.
- Treating CLI output and GUI status as separate truths.
- Hiding AI provider choice, project context sent to providers, or generated-asset provenance.
- Making AI-required workflows without manual/local alternatives.
- Endless scrolling columns that mix unrelated surfaces instead of switching between them.
- Claiming a preset, profile, or preference is applied when nothing reads it.
