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

**One asset workflow.** Textures, Models and Audio offer direct authoring from
their empty states and retain Open Package as a secondary route. Browser filters
and content toolbars appear with content. Shared empty-state action rows stack
when enlarged or translated labels need more space.
Empty-state content scrolls when its full text and actions exceed the window height.
Workbench bodies also scroll when enlarged controls exceed the available space;
their headers and content toolbars stay outside that scrolling area. A wide
control in one module therefore does not impose its width on every other module.
At 150–200% text scale, the global Build and Launch toolbar labels fold to their
icons while keeping full tooltips and menu names. Command search also has a
bounded width, with its full shortcut in the tooltip. Page headers keep the
selected package and virtual path visible, with the complete context in accessible text
and tooltips. Their Package menu exposes Show Selected Asset in Package, Review
Changes and Save Draft. Returning to Packages keeps the shared project and staged
document; repeated audio entries retain their exact occurrence. Ambiguous paths
from browsers without occurrence identity require a selection in Packages.

Authoring, editing and export buttons share their commands and enabled state with
Tools submenus, the command palette and custom shortcuts. Palette results identify
the module. Editing and exporting require an eligible selection; a new asynchronous
preview invalidates the preceding image's actions immediately. Compressed audio
does not require a PCM waveform preview before editing. Editor undo histories
remain separate from package staging and level edits; review and save each changed
document before building or publishing. Workspace files preserve references to
these surfaces, not an atomic transaction across every editor.

Before an input map is chosen, Build's Stage Plan describes the waiting stages
and any missing tools. It does not present an unresolved dry-run report as a
failed build. Command and result details appear when an input or completed run
provides them.
The Launch and Test form wraps long rows and scrolls on smaller windows, so its
settings do not force every module to inherit an oversized minimum width.

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
body and secondary text on every surface, selection text on both selection
fills, and accent text on the accent fill at 4.5:1, focus rings and state
colours at 3:1, and a focus colour that is never a selection colour.
Selection has two strengths. The palette's Highlight (`selection`) is what
canvases mark selected elements with (map, model, UV, brush, and patch views,
the waveform) and what progress bars fill with, so it stays a strong burnt
orange. Selected rows in lists, trees, menus, and combo popups take
`rowSelection`, a quieter ember, through the stylesheet, so a long list with one
row picked reads as calm chrome rather than a block of colour; the test also
keeps a selected row apart from the list behind it.
Once the shell is built, every item view is polished and laid out again: a
list lays its rows out with the metrics it has when its first items arrive,
and polishing it later does not redo that, so a list filled while its page was
still unpolished kept unpadded row positions under padded rows. The Settings
categories, filled once at construction, overlapped each other until this.

Progress bars keep native Fusion painting and font-based sizing. Separate text
colours preserve legibility over the filled and empty portions; the standard
dark fill includes margin for the native gradient. No shared fixed height cap
constrains scaled labels. Package and map-loading dialogs use this same surface.

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
so focus rings appear only for keyboard users. Each entry is a rounded
highlight inset from the rail's edges; the current page's glyph takes the
accent (the selection text colour in the high-visibility themes, whose fill is
the accent itself), and a short accent bar marks it at the rail's outer edge
(`StudioIconTone::Navigation` and `ModeRail::paintEvent()`).

**The rail folds to icons by itself.** By default (Settings > Navigation:
*Collapse to icons automatically*) the rail rests as a column of icons, and
its labels open over the page, never pushing it along:
- when the pointer rests on the rail for a moment (`ModeRail::kOpenDelayMsecs`,
  so a pointer only passing over it does not open it);
- as soon as keyboard focus comes into the rail.

It folds again when the pointer leaves (after a short grace) and focus is
elsewhere, when a page is picked with the pointer, or on Escape, which also
hands the keyboard back to the page. After a click it stays folded under the
pointer until the pointer has left, so a dialog closing over it does not open
it again. `RailHost` places it: the page starts where the rail's resting width
ends, and the open rail lies over the page's edge with a soft shade beside it
(a solid line in the high-visibility themes).
- **Always show labels:** the pin at the rail's foot keeps the labels open
  beside the page. In a window too narrow to spare them, a pinned rail acts as
  the automatic one.
- **Icons only:** set in Settings, the rail never opens, and each icon's
  tooltip names it.
- **Pin and reset:** the pin is a checkable button with a constant name, so a
  screen reader hears whether it is on. Reset Layout brings the automatic rail
  back.
- **Stable layout:** icons stay in place as the labels slide in; reduced
  motion makes the slide a single step. The open width fits the longest,
  possibly translated, label at the current text scale.

The behaviour (`shell/modeRailBehaviour`: automatic, expanded, or compact) and
the selected mode are persisted.

**Room runs short gracefully.** At large text scales or with docks open, the
shared parts give ground in a set order instead of clipping or widening the
window:
- A panel's tabs keep every label while they fit. Then they drop their glyphs,
  then keep only the current tab's label with the others as glyphs, then show
  glyphs alone. Each hidden label stays the tab's tooltip and spoken name.
- A page header folds its secondary actions to their glyphs, then the primary
  one. Each folded label moves into the tooltip, so the header never asks for
  more width than the folded actions need.
- A tool bar hides what does not fit behind its overflow chevron. The studio
  bar's command search narrows first, its label eliding and then its key caps
  going, before anything moves into the overflow.
- The status bar keeps room for about forty characters of message. Short of
  that, the Activity and Inspector toggles fold to their glyphs first. Then the
  status items fold, AI first and project last, each to the glyph of what it
  reports on plus its state mark. A folded label moves into the tooltip, and
  the item's spoken name and description stay the same.
- The Settings category list is as wide as its longest name.
- The Workspace dashboard's jump tiles (a `ReflowGrid`) sit four to a row
  while they fit, and wrap onto more rows when they do not; a tile's context
  line elides in the middle. Its cards stand in two columns, weighted 3:2,
  while two fit, and stack in one when not; the short lists in them (recent
  projects, installations) are only as tall as their rows.

**One page anatomy.** Every surface is assembled from the same parts: a
`PageHeader` (one slim row: mode glyph, title, a divider, a context summary that
elides in the middle, and the page's actions with at most one primary button),
an optional page tool bar, and a body. Every header stands as tall as one with
buttons, so switching pages never makes the work area jump, and while a page
shows its empty state the header's primary action steps back to the secondary
look (`PageHeader::setPrimaryActionMuted()`), leaving the empty state's own call
to action as the one emphasised button on screen. Workbench pages (Levels, Models, Textures, Audio,
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
open. A strip is one slim line: the state in words and in its colour, the
title, then the detail beside it, wrapping only when it must; its leading edge
carries the state's colour as well. Progress bars and skeleton rows appear only
while work is running.

**Notices above the page.** Something the user should know and may act on,
which belongs to no one page, shows in a `NoticeBar` across the top of the
work area: a state glyph, a bold title that names the state in words, the
message, its actions, and a close button, with the state's colour on its
leading edge. The bar paints that edge itself, on the right in a right-to-left
layout, where a style sheet border would have stayed on the left. It sits
above whichever page is showing, so switching pages never hides it, and it
stays until acted on or dismissed (the close button, or Esc while focus is in
it). It never takes focus by itself; it is announced as an alert instead. A
crashed previous session is the first thing it carries.

**One row of chrome.** The window spends a single row on chrome above the
pages, the studio bar, as modern editors' title bars do:
- the menus lead it (the shell's own `QMenuBar`, placed in the bar; on macOS it
  is still the native menu bar), then Back and Forward and the open commands;
- a **Search commands** field sits in the middle of the window
  (`CommandSearchButton`, kept centred by `keepCentredInToolBar()`), showing its
  label on the leading side and the palette's keys as key caps on the trailing
  side; it widens with the window, up to a limit, and gives way first when room
  runs short;
- Run Build Pipeline and Launch Game, the commands that start real work, close
  the row with their labels (glyphs alone from 150% text scale).

The window title names the page and the project ("Levels — Foundry —
VibeStudio") for the task bar and window switchers, so the status bar no longer
repeats the page name when the page changes.

**A quiet status bar.** The status items sit flat on the bar: each names its
state in words with a state mark, takes its state's colour, and only a state
that wants attention (a warning or a failure) gains a quiet tint behind it.
Hovering lifts an item; the high-visibility themes outline every state.

**Pages fade in.** Choosing another page fades it in from the surface colour
over a few frames (`PageTransition`, about 170 ms), so moving between surfaces
reads as a transition rather than a jump cut. The fade lies over the page,
takes no input and no focus, so the page answers clicks and keys at once;
reduced motion turns it off, and offscreen renders (tests, snapshots) never
play it.

**One command registry behind menus, toolbar, palette, and shortcuts.**
`src/app/studio_actions.*` owns the `QAction` instances. `populateMenuBar()`
builds the menus and `populateToolBar()` the studio bar's command buttons, whose
tooltips name each command and its shortcut. Default shortcut sequences come from
`core/studio_semantics.h`, and command ids now match across spellings: the
shell registers camelCase ids (`shell.commandPalette`) while the registry
documents kebab-case (`shell.command-palette`), and the normalizer used to
lowercase without inserting the hyphen, so about a dozen documented shortcuts
(Ctrl+Shift+P, Ctrl+F, Ctrl+E, F2, Del, and others) never bound and the palette
listed a disabled duplicate of each command. A duplicate sequence is still
recorded as a conflict and skipped rather than silently shadowing its first
owner.

**The Assistant, beside Activity and Inspector.** The Assistant dock asks a
text model about the open work. Its first line says who answers and where
("Claude · claude-opus-5-5 · sends to api.anthropic.com", "on this machine"),
or, when nothing can be sent, why not and where to change it. Under it, *Send
with the question* lists what the open work offers (the selected code or the
file, the map summary and selection, the last build's problems, the project),
each with its size; the page in front of the user decides what starts
ticked, and a list only ever grows as tall as its rows. **Preview** shows the
exact request and sends nothing. While a question is out, the status line
counts the seconds and Cancel appears; the answer lands in the conversation as
Markdown, with its time and token counts, and a question that went unanswered
stays in view, marked. The AI status chip, **Explain** on the Build page, the
problem list's **Explain with Assistant**, and the code editor's **Ask
Assistant About Selection** all open it, the last two with their context
already chosen and a question proposed, never sent.

**Type-to-filter command palette.** `CommandPaletteDialog` lists every enabled
registry command plus the documented palette-only entries, filtered as the user
types, with each row showing the command's glyph, its name, its category, and
its shortcut as key caps (plain text when a narrow palette has no room for
them); the summary is the row's tooltip. It opens anchored near the top of the
window so the list grows down over the work surface, as a floating panel with
rounded corners and a soft shadow where the window system composites
translucent windows (`prepareFloatingPanel()`; X11 keeps square corners, since
a compositor cannot be relied on there). Go to File shares the same panel and
rows. With an empty filter, the commands last run from it (up to six, newest
first, remembered across sessions) lead the list.

**Keys that belong to a page.** A page's own keys fire only while keyboard focus
is inside that page: the registry installs them as
`Qt::WidgetWithChildrenShortcut` actions on the page widget. Del, F2, and Ctrl+E
act on the Packages page, Ctrl+Z and Ctrl+Y (or Ctrl+Shift+Z) undo and redo map
edits on Levels, Ctrl+S, Ctrl+L, F3, and Shift+F3 work on the Code page, and
Escape cancels the selected task from the Build page or the Activity panel. Del
pressed on the Levels page used to stage a package deletion the user could not
see. Which keys belong to a surface is recorded once, as `surfaceScoped`
shortcut descriptors in `core/studio_semantics.h`, and the shell scopes them
from there. Keys on different surfaces may share a sequence, so Ctrl+Z on the
Packages page takes the most recent staged change back out of the plan (**Undo
Staged Change**) while on Levels it stays map undo; a window-wide key still owns
its sequence everywhere. Switching surfaces moves keyboard focus into the new
page, so its keys work at once. Ctrl+F is **Find**: it focuses the current
page's search or filter field, opens the find bar on the Code page, and falls
back to the Workspace search. Escape clears a filter field.

**Settings are found by typing.** A search field heads the Settings category
list. As it is typed in, the categories whose settings never mention the text
step aside. A setting mentions it in its label, check box, group title,
tooltip, or spoken name. The first category left is shown, with its first
matching setting scrolled into view. Enter moves focus to that setting, so
Space or typing changes it at once. When the match is a group of settings or a
line of text, focus goes to the first control in or after it. When nothing
matches, a line under the list says so, and clearing the field brings every
category back. Ctrl+F on Settings focuses the search.

**Build problems from anywhere.** F4 shows the last build's next problem
from any page, and Shift+F4 the one before, wrapping at either end. Each is
shown where it points: the map object on Levels, the file line on Code, the
leak trail over the map, or the stage that printed it. The status bar gives
its place among the rest and what it says. The fix, build, and fix loop never
has to go back to the Build page to find the next problem, and a look at that
page, which lists the problems again, does not lose F4's place. A problem that
names no line, or a leak whose trail cannot be read, is shown on the Build page
instead, with its stage's output or the reason. When there is none to show,
the status bar says why: nothing has been built from this input with this
pipeline, the build is still running, or it found no problems. A focused combo
box keeps F4 for opening its list, as combo boxes do on Windows.

**Go Back and Go Forward.** Each page left and each jump made is a place to come
back to. Alt+Left, the mouse's back button, or **Go Back** in the View menu
returns to the page before and to where the user was on it: the file and caret
on Code, the selection on Levels while the same load of the map is unedited
since (another map of the same WAD, the map opened again, or an edit can give
its ids to other objects), and the current row on Textures, Models, and Audio.
Alt+Right, the forward button, or **Go Forward** goes the other way. The two
arrows at the left of the toolbar do the same; each is enabled while there is
somewhere to go, and its tip says where, such as "Go back to weapons.qc, line
12". The status bar says where each step arrived. A jump on Code (Go to
Definition, Go to Line, a symbol, an outline row, a problem or search result,
another tab) is remembered as a page change is. Stepping through a file's
problems with F8 is not, so a run of F8 does not bury the way back, and neither
is a file shown again after it reloads or a tab brought forward to ask about its
unsaved edits. A read-only copy of a package entry comes back only while its tab
is open, since reopened from its temporary folder it would be an ordinary file
whose edits never reach the package. A quick double press of the mouse's back
button goes back two places. As in a browser, a move of the user's own after
going back leaves nothing to go forward to. The last 50 places are kept, and a
place in a file that has gone is passed over. With nowhere to go, Alt+Left and
Alt+Right say so.

**Every row goes somewhere.** A list row that names something leads to it. A
package entry opens where it belongs: textures, models, and sounds on their own
surfaces, selected; maps, shaders, and scripts from a temporary copy, scripts
read-only. A shader's texture reference opens the texture, and a shader row
opens its script at the shader's line; a model's skin row opens the skin; the
entity inspector's **Defined in** row opens the definition file at its line; a
Health issue selects its entity; a timeline row selects its task; and
**Preferences** opens the appearance settings. The Packages **Preview** tab
shows pixels for an image and text for a script.

**Go to File.** Ctrl+P opens a launcher shaped like the command palette that
finds files rather than commands: recent files first, then the project's files,
then the open package's entries, each row with its folder and where it comes
from. It matches the way the palette does (names that start with the text, then
words that do, then letters in order anywhere in the path), shows the best 200
matches, and opens the chosen file on the surface that shows it: a texture on
Textures, a map on Levels, a script in the code editor, a package entry through
the same routing as a double-click in Packages. Supported loose textures, models
and audio route to their corresponding asset surfaces. Unsupported compiled files
are excluded rather than opened as text.

**Keyboard Shortcuts.** **Help > Keyboard Shortcuts** lists every command,
with its keys as the platform writes them and where they work: **Everywhere**,
or only while focus is on one surface (Levels, Packages, Code, or Build and
Activity), the same scoping `installShortcutScopes()` applies. The keys come
from the live registry, so an editor profile's bindings show as they are, and
the keys the Levels view and the code editor handle themselves (F, Home, Tab
cycling, arrow nudges, Space to pan, the staged Escape, Ctrl+Tab out of the
editor, Ctrl+wheel zoom) are listed beside them. A filter narrows the table by
command, key, or surface.

**Keys of your own.** **Change Keys** (or Enter on a row) asks for new keys for
the chosen command: the keys pressed are shown as they are recorded, with a
line saying whether they are free wherever the command works or which commands
have them now, since assigning takes them from all of them. A surface inside
another counts as the same place (the map view is inside Levels), because Qt
would run neither of two commands sharing a key there. Keys need Ctrl or Alt,
or are function keys: a plain key is typed in fields and used by the views
themselves (F frames the Levels selection, Enter confirms a field), which the
table cannot see. A key a command the studio relies on holds (Escape cancelling
a task) cannot be taken. **No Keys** leaves
a command without any; it still runs from its menu and the palette. The user's
own keys replace the command's profile and default keys, are installed before
every built-in key so they always win, and show bold in the table with "your
own keys" in the row's spoken text. **Reset** puts back one command's default
keys (and the command they were taken from gets its own back), and **Reset All**
asks once and puts back every default. The few keys the studio relies on, such
as Escape cancelling a task, cannot be changed. The keys live in settings,
apart from the editor profile, so they survive a profile switch; the mode
rail's tooltips follow them, and `vibestudio --cli editor keys` lists them
(`--reset` clears them) for when a rebinding went wrong.

**Go to Symbol.** Ctrl+T in the code editor opens the same picker over the open
file's symbols instead: QuakeC and C-style functions, shader names in a
`.shader`, and entity classes in `.def` (`/*QUAKED`) and `.fgd` files. With no
filter the list reads in file order like an outline, each row with its line
and kind; typing narrows it the same way, and Enter puts the caret on the
symbol's line. The symbols come from `codeSymbolsInText()`, the same scanner
`code index` uses.

**Go to Definition.** F12, or Ctrl+click, on a name in the code editor opens
where it is defined: the open file's own definitions first, read from its text
as it is now with unsaved edits, then the rest of the project's, gathered once
by the same scanner and again after a save. One definition opens straight away;
several open the picker, each row naming its file, line, and folder. Alt+Left,
or the mouse's back button, returns to where the caret was, as many steps back
as were taken (see **Go Back and Go Forward**), and a name defined nowhere says
so rather than doing nothing. Shift+F12 lists every use of the name under
**Search Results**, as a whole word and matching case (so `player_run` is not a
use of `player`), and says how many it found in how many saved files, since the
search reads files as saved.

**Outline.** Beside **Files**, an **Outline** tab lists the open file's
functions, shaders, or entity classes in file order, rebuilt a moment after
typing stops, so one just typed joins it. The one holding the caret is current,
so the outline shows where in the file the caret is; Enter moves the caret to
another, and the filter narrows a long file's list.

**Breadcrumb.** Above the editor, a breadcrumb gives the open file's place:
its folders from the project root, the file, and the function, shader, or
class that holds the caret. The last part changes as the caret moves, and the
language and save state follow at the right. A folder opens a menu of what it
holds. Subfolders open onto their own contents, and choosing a file opens it.
The file opens a menu of the files beside it, with the open one checked. The
symbol opens Go to Symbol. A file outside the project starts at its own folder.
A copy out of a package shows only its name, since its folder means nothing to
the user.

**Problems.** A problem the editor finds is underlined in place, and hovering
over it shows what it reports. F8 moves the caret to the next problem after the
caret's line, and Shift+F8 to the one before, wrapping at either end. The
diagnostics list below follows with that row selected. The status bar gives the
problem's place and text: "Problem 2 of 5: line 14: unterminated quoted
string". The readout above the editor counts the file's problems while it has
any, and a click on the count goes to the next one, as F8 does. Problems on
the same line are stepped through one by one. The list
follows the text a moment after typing stops, so F8 goes to where the problems
are now, not where they were at the last save. A file without problems says
so rather than doing nothing.

**Uses.** While the caret rests on a name used more than once in the file,
every use is shaded. The match is on whole words in the name's own case, so a
variable's uses show at a glance without a search, and `counter` never shades
`counter_max`. A selection turns the shading off, so the selection stays easy
to see. The uses shaded are those in view and a screenful either side, which
scrolling brings up to date, so typing in a large file never waits for a
search of all of it.

**Sticky headers.** While the top of the view is inside `{ }` blocks whose
opening lines have scrolled away, those lines stay pinned above the text,
outermost first and at most three, drawn with their own highlighting. The
gutter beside them gives their own line numbers. A brace alone on its line is
stood for by the line before it, so a QuakeC function pins its name line and
a shader stage its shader's name. A click on a pinned line moves the caret
there. **View > Sticky Headers** turns them off and on, remembered between
sessions. A caret that would sit under them, from Up at the top of the view
or a find, is scrolled out below them, and a click in the gutter beside them
goes to the pinned line rather than the one scrolled underneath.

**Folding.** A `{ }` block of three lines or more can fold to hide the lines
inside it. That covers a QuakeC function, a shader and each of its stages, and
an entity definition. The opening and closing lines stay in view. Braces in
comments and quoted strings do not count. Each line that can fold has a
chevron in a column of the gutter beside the line numbers. The chevron points
down while the block is open, and points right, drawn stronger, once it is
folded. The folded line ends in a badge counting the lines it hides. A click on
the chevron folds or unfolds; a click on the badge unfolds. Ctrl+Shift+[ folds
the innermost open block holding the caret, so pressing it again folds the one
around it. Ctrl+Shift+] unfolds the block on the caret's line. **Fold All** and
**Unfold All** are in the View menu and the palette. Folding is only a way of
seeing the file: the arrow keys step over a fold, and moving the caret into
one opens it. Find, Go to Line, a build problem, and the outline all move the
caret, so they open it too. A fold stays with its line through edits
elsewhere, and a new line typed at the very start of a folded line leaves the
fold with its line. It opens once its braces no longer pair, and moving or
duplicating its lines opens it first, so no hidden line changes unseen. A file
larger than 512 KB does not fold, since working out its braces after every
edit would slow typing.

**Completion.** Ctrl+Space offers the names that start with what is typed,
matched without case: the language's keywords (a shader's `blendFunc` and
`rgbGen`, QuakeC's `local` and `self`), the names already in the file as it
reads now, and the project's symbols from the same index Go to Definition
uses. Typing narrows the list and closes it once nothing matches; Enter or Tab
puts the highlighted name in, replacing what was typed in one undo step so the
name keeps its own case; Escape closes it.

**Filters and recent files.** The Models, Audio, and Shaders lists, the Code
file tree, and the Levels objects list have filter fields like the Packages and
Textures ones. **File > Open Recent** lists recent projects, packages, maps, and
code files, up to eight of each and newest first, leaves out files that no
longer exist, and can clear the list; **Close Project** closes the project while
keeping it among the recent ones. Files and folders dragged from the desktop
onto an open package's entry list or folder tree are staged into it, into the
folder under the pointer or the one the list shows, a folder keeping its layout;
the status bar says where while the drag hovers, a clash asks once for the whole
drop (two dropped files landing on one name clash with each other too), and
Save As writes the result; a clash left blocking is said, since staged is not
saved. Onto a Doom WAD a file whose name cannot be a lump name (8 Latin-1
characters) is left out and said so, rather than failing Save As later.
Dropped anywhere else, a file opens as before. The other way, rows dragged out
of the entry list reach the desktop or another program as copies: the files
are extracted into a fresh folder of the session's temporary folder for each
drag, a folder row keeping its layout, so a drag holds exactly what was dragged
and never overwrites the copy an open tab shows; a drag too large to extract
at once says so and points at Extract Selected. Copies are kept per package by
its path, since every mod ships a `pak0.pak`. In the Packages staging list, Delete or the
context menu takes a change back out of the plan.

**The last session comes back.** The studio records what is open as it
changes, within a second, and again at close: the package, the map (and which
map, for a WAD), and the code editor's tabs with the one in front; temporary
copies of package entries are left out, a map or package opened from inside
another package included. Recording as it goes means a crash loses none of it. The next start reopens them on the page the studio closed
on, and says so in the status bar, naming only what did open (the map a WAD
opened on, when it no longer holds the recorded one) and counting any file that
has gone since, or failed to open, rather than failing on it. A path given on the command line is what the user asked
for, so it skips the session, and the self-test and snapshot runs neither
reopen nor record one. The record names the studio that made it: a second
studio started while the first runs neither reopens that session nor records
over it, and takes it over once the first has closed. **Settings > Appearance
and Language > Startup and Recovery > Reopen the last session at start** turns
it off.

**Status bar.** Permanent chips report project, package, installation, compiler,
and AI health. Each shows a state glyph and words, such as a check mark before
the package name or a warning triangle before "Compilers 7/9", never colour
alone. A chip is a button that opens the surface able to act on what it reports:
the project chip opens Workspace, the installation chip the Workspace
installation list, the package chip Packages, the compiler chip the Build page's
Toolchain tab, and the AI chip the AI settings. Each mirrors its text into an
accessible description, carries its operation state as a style property, and
elides long names in the middle rather than widening the window. Two toggles
beside them open the **Activity** and **Inspector** panels; the Activity toggle
also counts running tasks.

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
chart), with assistant proposals at the foot. Each tile (`NavigationTile`)
shows its surface's glyph in a tinted well, its name, and what it holds right
now, in the words its page header uses ("foundry.map · 24 entities · 43
brushes", "foundry.pk3 · 34 entries", "No shader script loaded"), so the tiles
inform rather than repeat the rail; the whole line is the tile's tooltip and
spoken description. Each recent-activity row leads with its state glyph and
task, trails with when it ran and how long it took, and grows its duration bar
from the leading end, so a right-to-left layout mirrors the whole row; times
and durations keep their own direction, reading "0 ms", never "ms 0". The
glyph column widens with the text size, so large text never clips a glyph.

**Real pictures instead of text stand-ins.** `src/app/studio_charts.*` provides
painted composition, pipeline, and activity-timeline widgets, and
`src/app/map_viewport.*` provides an interactive 2D map view that draws real
geometry through the shared solver in `core/map_geometry` — replacing the
`[##########........]` ASCII readiness bar as the primary display and the
text-only "preview lines" map tab entirely. A text readiness bar still appears
in the copyable inspector dump, where plain text is the point; the Toolchain
tab's profile rows carry a state glyph instead. `src/app/asset_views.*` adds
image, palette-swatch, and waveform views so textures, palettes, and audio are
shown rather than described, and `src/app/model_viewport.*` does the same for
models — orthographic and perspective projection into a bounded software depth
buffer, presented by `QPainter`. Intersections, texture transparency and selection
hatches respect depth; picking uses the same coverage and texture sampling.
The studio still carries no OpenGL dependency.

**Direct manipulation.** The window accepts dropped files and routes each one by
type: maps to the Levels surface, a folder holding a project manifest to
Workspace, packages to Packages, `.shader` scripts to Shaders, `.def`, `.fgd`,
and `.ent` entity definitions to the Levels entity inspector, loose textures,
models, and sounds to Textures, Models, and Audio (their folder is browsed as a
folder package and the file selected, or just selected when that folder is
already open), and anything else to the Code editor, which used to receive
loose images as unreadable text. `--open <path>` on the command line routes the
same way. The
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

**Textures from WADs.** The Textures page lists a Doom WAD's flats, sprites,
and wall patches by their namespace markers, its well-known global graphics,
and every lump of a Quake or Half-Life texture WAD, so a resource WAD reads
like any other texture package. The **Palette** choice starts on
**Automatic**: the project's palette if it names one, else the palette the
package ships, else, image by image, the one its format implies, read from the
selected game installation's IWAD or pak0 when the installation is of that
family, so a Doom WAD decodes with its own `PLAYPAL` and a PWAD without one in
its IWAD's colours ("Palette read from the game installation: PLAYPAL in
doom2.wad"),
without the user knowing which game it came from; the swatches and the line
under the preview follow the image on show and say where its palette came
from.

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

**Closing and coming back.** **File > Close Map** closes the open map, asking
about unsaved edits first with the same guard as opening another, and the
empty Levels page then lists the six most recently opened maps that still
exist, under its **Open Map** button; Enter or a double-click opens one.

**Map actions where the user is pointing.** Right-clicking the viewport opens
the map's actions in place, in groups that run from the selection outward:
**Cut**, **Copy**, **Paste**, **Duplicate**, and **Delete**; **Add Entity
Here…** and **Select All** of the selected entity's class (or thing's type); a
**Transform** submenu holding **Move…**, **Snap to Grid**, and **Rotate 90°
Left** and **Right**, with **Edit Key…**, **Replace Texture…**, and **Copy
Selector** (the object's `entity:1`-style selector, ready for `map edit` and
`map move` on the command line); then **Zoom to Selection**, **Zoom to Fit**,
**Hide**, and **Show All**; and, while a leak trail is drawn, **Frame Leak
Trail** and **Clear Leak Trail**. A right press settles the selection first, the
way a plain left press does: an unselected object replaces the selection and a
member of the set is promoted to primary, so the menu acts on what is under the
pointer. Items that do not apply stay listed but disabled, so the menu keeps its
shape. The Menu key or Shift+F10 opens the same menu from the keyboard, and the
objects list offers it too, for the rows it has selected. Enter or a
double-click on an objects-list row frames that object in the view, leaving
focus in the list so the keyboard can walk on to the next row.

**Map logic you can make.** **Connect Entities** (Edit menu and palette)
wires the selection the way Radiant's does: every selected entity but the one
picked last gets a `target` naming it, and it keeps its `targetname`, takes the
name a source already targets (so a trigger that fired a door fires both), or is
given the first free `t<N>`, all in one undo step; a selected brush stands for
its entity. **Select Targets** and **Select Sources** walk the wiring from the
selection to what it fires, or to what fires it.

**Doom tags you can see.** On a Doom map the wiring is tags, and the view
draws it as Doom Builder's associations: from the middle of a line with a
special and a tag, an arrow to the middle of each sector carrying that tag,
whose outline is traced dashed, with "tag N" beside the arrowhead so the link
reads without colour. With **Target Links** on, every tag link is drawn; off,
only those touching the selection. **Select Targets** on a line selects the
sectors it acts on, and **Select Sources** on a sector the lines that act on it.
A door used by hand (vanilla's DR and D1 doors, and Boom's generalized doors
with a D1 or DR trigger) acts on the sector behind its line whatever tag the
line carries, so its tag draws no link, and **Make Door** leaves its door lines
untagged.
Hexen maps are left out for now, since a Hexen line's first argument is a sector
tag for some specials and a script or thing number for others.

**Map logic you can see.** Entities that name each other are joined by
arrows, the way Radiant-style editors show a map's wiring: from each `target`,
`killtarget`, `pathtarget`, `combattarget`, or `deathtarget` key to every entity
whose `targetname` matches. A brush entity is anchored at the centre of its
brushes. The arrowhead stops short of the target's marker; a solid line fires
its target and a dashed one removes it (`killtarget`), so the meaning never
rests on colour. Links that touch the selection are drawn heavier and stay
drawn even with **Target Links** off, so turning the rest off leaves just the
selected entity's wiring. A dragged entity takes its links with it, and an edit
that renames a `targetname` redraws them at once. `levelMapTargetLinks()` in
the core finds the links for both the viewport and `map render --links`, and
**Export Image** includes them when the view shows them. The objects list names
each entity's wiring as well ("named lamp", "fires door1"), so typing a name in
its filter finds both ends of a link.

**Adding and deleting.** **Add Entity Here…** asks for a class in an editable
list, the loaded definitions' point classes or a few every Quake-family game
knows, with the last class added first. The entity lands at the pointer,
snapped to the grid; the axis the view hides comes from the selected entity,
so a new light sits level with the one beside it, or from the middle of the
map. It is selected at once, so the inspector is ready for its keys. Del
deletes the selection as one undo step, and an entity takes its brushes and
patches with it. Del means the most local thing: in the entity inspector it
removes the key on the current row and never the entity, and it leaves
`classname` alone. `worldspawn` holds the world, so Delete Selection is off
while it is selected, and an object whose brace shares a line with other text
is refused with a reason rather than risk the file. **Duplicate** (Ctrl+D, and
in the context menu) copies the selection one grid step right and down on
screen, in whichever plane the view shows, and selects the copies so the next
drag or nudge moves them: a copied brush joins the entity that holds the
original, an entity's copy brings its brushes and patches, and each copy is
written in its original's exact text format.

**The clipboard speaks .map.** Ctrl+C on Levels puts the selection on the
clipboard as .map text, the format TrenchBroom and Radiant use: a selected
entity as a block with its keys, brushes, and patches, and other selected
brushes and patches as bare blocks, each in its own source format. Ctrl+V adds
what the clipboard holds at its own coordinates and selects it, so a drag or a
nudge places it next: entity blocks become entities, and bare blocks (what
TrenchBroom copies for worldspawn brushes) join the open map's worldspawn.
Ctrl+X copies and then deletes, as one undo step for the delete. Pasting text
that is not a map, or a block that never closes, is refused with a reason. In
the entity inspector Ctrl+C copies the current row as a `"key" "value"` line
instead, and cut and paste say where they work rather than act on the map.

**Replace Texture.** **Replace Texture…** (Edit menu, palette, and the map's
context menu) asks which texture to replace, from the map's own list and
starting from the selected brush's texture or sector's floor, and what to use
instead. A count under the fields says how many uses will change and follows
every edit, with **Only the selected objects** narrowing it, and Replace stays
off while nothing in scope uses the name. It covers brush faces and patches,
or a Doom map's wall textures and flats, compares names without case, and
writes each new name in place: after a `brushDef` texture matrix, inside the
quotes of a `brushDef3` name, on a patch's shader line, or into the binary
SIDEDEFS and SECTORS records, leaving every other face line exactly as it was.
Doom names longer than eight characters, names the map tokenizer would split
(spaces, quotes, braces, brackets, parentheses, or `//`), and faces written
several to a line are refused with a reason. From the Textures
surface, a texture's context menu offers **Use in Open Map…**, which opens the
same dialog on Levels with that texture as the replacement, named the way the
map names its textures: without the extension, with or without the
`textures/` folder as the map's own names have it, and as a bare lump name for
a Doom map. **Apply to Map Selection**, beside it, puts the texture on every
face of the brushes and patches selected in Levels, in the same naming, and
shows Levels; **Apply Texture…** on Levels does the same from a list that
starts with the texture current on the Textures surface, then the last one
applied, then the map's own. Each is one undo step, and faces that already use
the texture are left alone. The Textures surface's **In open map** switch goes the other way:
it lists only the textures the map open in Levels uses, matched through the
same naming, and each row's tooltip says how many times the map uses it.

**Hide and show, and how big.** H hides the selected entities, brushes,
patches, or things from the view (an entity takes its brushes with it), and
Shift+H brings everything back; both are in the View menu and the map's context
menu, which says how many objects are hidden. Hiding never edits the map: the
viewport draws and picks from a copy without the hidden objects, so clicks,
rubber bands, Tab, and framing all pass them by, while the objects list keeps
them, dimmed and marked "hidden", and selecting one there shows it again. The
HUD counts hidden objects, and with a selection it also shows its width and
height in the view plane ("Selection 128 × 64"), the first thing a mapper checks
after a drag.

**Rotate 90°.** **Rotate 90° Left** and **Rotate 90° Right** (Edit menu,
palette, and the map's context menu) turn the selection a quarter turn as the
view shows it, about the centre of its bounds: about z in the top view, and
about the axis each side view looks along. Brush points and `brushDef3` planes
turn, Valve 220 texture axes turn with their faces and their offsets take up
the move, so textures stay put on them, patches turn their control points, and,
about z, entities turn their
`angle` or the yaw of their `angles` and Doom things their angle. Quarter turns
are exact: four of them give back the very same file. **Flip Horizontal** and
**Flip Vertical** mirror the selection along the view's own axes; each
point-defined face swaps two of its points and each patch reverses its columns,
so the mirrored brushes still close and face outward, and headings mirror too
(x about north, y about east).

**Resize.** When the selection can change size in the view (a Quake-family
selection, or Doom things alone, spanning both drawn axes), eight square
handles sit on the corners and edges of its box, drawn only once the box is
big enough on screen to grab. Hovering one shows the matching resize cursor and
names the axes it changes; dragging it moves those edges with the pointer, on
the grid while Snap is on, never past the opposite edge, while ghost outlines
show each object stretched and the new box carries its size. Release makes one
undo step; Escape or a right press throws the preview away. Brush points and
planes, patch control points, entity origins, and thing positions all map from
the old box to the new one, while textures stay where they are in the world, so
a longer wall shows more of its texture rather than a stretched one. The
keyboard path is **Resize Selection…** (Edit menu, palette, and the map's
Transform submenu): the current size and corner, a field per axis in map units,
and which point stays in place (lower corner, centre, or upper corner). The
command line has the same service as `map resize`.

**Clip.** **Clip Tool** (X on Levels, and the Edit menu, where it shows a
check while on) turns a drag in the viewport into a clip line instead of a
selection: the line lands on the grid while Snap is on and stands for the
plane square to the view through it. The part a cut would take away is tinted
and hatched, so it reads without colour, and Tab chooses what stays: the other
side, or both halves as two brushes, when the line is just drawn. Enter cuts
the selected brushes, as one undo step and one History row ("Clip brush:3",
"Split brush:3 in two"), and selects the pieces; the tool stays on for the next
cut, and Escape leaves it. The HUD says "Clip" while the tool is on, and the
status line says what Enter will do. **Clip Selection…** (Edit menu, palette,
and the Transform submenu) is the keyboard way: an axis, a position that starts
at the middle of the selection on the grid, and which part to keep. Each piece
is the brush's own text with a new face on the plane, written the way its other
faces are, so a clipped Valve 220 or `brushDef3` brush stays in its format.
**Hollow…** (Edit menu, palette, and the Transform submenu) asks for a wall
thickness, one grid step to begin with, and turns each selected brush into one
wall per face: the brush with that face pulled in by the thickness and turned
to face the inside, textured like the face it grew from, so a block becomes a
room in one undo step and the walls come back selected. When Clip, Hollow, or
Carve has to leave part of the selection alone (a brush written on shared
lines, say), the status bar says how many brushes it left and why the first
one was left. **Carve** (Edit menu,
palette, and the Transform submenu) is CSG subtraction: the selected brushes cut
themselves out of every brush they overlap, each of which gives way to its
parts outside them, and the faces the cut makes take the carving faces'
textures. Hidden brushes are left alone, as Radiant's subtract leaves them, and
a brush entity whose last brush the carve takes goes with it, as it does when
Del takes its last brush. The carving brushes stay selected, so Del removes a
doorway brush once its opening is made, and one undo puts every carved brush
back whole.

**Select All, Invert, None.** Ctrl+A on Levels selects every object Select
All can reach: each point entity, every brush and patch, or every thing on a
Doom map, leaving hidden objects out, since selecting one would show it again.
A brush entity is picked through its brushes and patches, as Radiant selects
primitives, so a move or a delete never reaches it twice. Ctrl+I selects those
same objects less the ones selected now, a selected entity's brushes counting
as selected, and Ctrl+Shift+A clears the selection. Moving a brush entity moves
its brushes and patches, and its origin only when it has one: an origin added
to a Quake brush entity would move it a second time in the game. The Textures surface's **Select in Open Map**
selects what uses a texture in the map and shows Levels.

**Move…** moves the whole selection by a typed delta, starting from one grid
step along x, the way a drag, Snap, or Rotate moves all of it. **Select by
Texture…** (Edit menu) asks for one of the map's textures, starting from the
primary object's, and selects every brush and patch, or every Doom linedef and
sector, that uses it; the map's context menu offers the same as **Select All
Using** the primary object's texture.

**Object queries.** The Levels Objects filter narrows the list by name, as
every filter does, until a term tests a property. Then it reads the whole
field as a query, and every term must hold. `key=value` matches a value
exactly and `key:text` one containing it, both ignoring case. `key!=value`
holds when no value is equal, and `key<n` and `key>n` compare numbers.
Examples: `class=light light>200`, `texture:brick`, `tag=3`, `name:imp`.
Every object also answers to `kind` (`kind=sector`) and to its selector, so
`entity:3`, written as `map find` prints it, keeps that one object;
`entity=3` still finds the brushes of entity 3.
Each query filter remembers the queries used in it, per filter and between
sessions. Typing narrows them in a list under the field, and Down in the field
lists them all, so a query such as `missing>0` is one keystroke away the next
time. A key that none of the list's items has, usually a slip of typing such as
`claass=light`, is named in the status bar rather than leaving an unexplained
empty list. It is named once for each query typed, so a refresh of the list
under it does not say it again over whatever the status bar says then.
The Packages filter reads the same queries over package entries. An entry
answers to `path`, `name`, `ext`, `folder`, `type`, `storage`, `size`, `packed`,
and `kind`. A size can take `k`, `m`, or `g`, counted in 1024s, so
`ext=wav size>1mb` lists the large sounds and `folder:textures ext=tga` the
texture images. Enter in the filter selects every entry it keeps and moves
focus to the list, where Del, F2, and Extract act on all of them.
`vibestudio --cli package list --where` runs the same query.
The Textures filter adds `uses`, how often the open map uses the image, and,
once decoding has made an image's thumbnail, its `w`, `h`, and `format`. A
filter on those fills in as the thumbnails are made, so `w>=128 format=wal`
finds the large Quake II textures of a package just opened.
The Models and Sounds filters read the same entry queries (`ext=md3`,
`ext=wav size>1mb`), a loose file answering from disk.
The Shaders filter reads them over each shader: `name`, the counts `stages`,
`textures`, and `missing` (textures the open package lacks), every directive by
its first word (`cull=none`, `surfaceparm=nolightmap`, `qer_editorimage:brick`),
and its stages' `blend`, `map`, and `rgbgen`. A matching shader keeps its whole
tree of stages and textures in view, so `missing>0` shows each broken reference
under the shader that makes it.
The Code page's Files tree reads them over project files: `name`, `ext`,
`folder` and `path` from the project root, `size`, and `language` (`ext=qc
size>10kb`, `language=shader`), keeping the folders of the files it finds open.
Words beside the terms search the path, as they do alone, so `weapons ext=qc`
finds the QuakeC files under `weapons/`.
While any filter is typed, the Objects tab counts what it keeps, for example
"Objects (3 of 120)".
Entities answer to their own keys and `class`. Brushes and patches answer to
`texture`. Doom things answer to `type`, `name`, and `angle`. Linedefs answer
to `special` (or `action`), `tag`, and every side's `texture`, and sectors to
`floor`, `ceiling`, `light`, `special` (or `effect`), and `tag`. Enter selects
every object the filter keeps, and the view's selection follows; when it keeps
none, the selection is left as it was. Objects hidden in the view stay out, as
they do for Select All, and the status bar counts them. **Select by Texture…**,
**Select All of This Class**, and the Levels Textures tab's **Select Objects
Using It** leave them out the same way. **Select Matching Objects** in the Edit
menu does the same. A row of the Levels inspector offers **Find Objects With
This Value** on its menu. That menu opens with a right-click, the Menu key, or
Shift+F10. The filter then holds the row's query, such as `kind=entity
light=300` or, for a sector's light level, `kind=sector light=160` (so a
sector's special never finds a linedef's action), with focus in the filter so
Enter selects the objects it keeps. **Copy Value** copies the value.
`vibestudio --cli map find` runs the same query.

**Snap to Grid.** **Snap Selection to Grid** (Edit menu, palette, and the map's
context menu) moves each selected object onto the viewport's grid by its own
amount: an entity's origin, a brush's or patch's lower corner, a thing's or a
vertex's position. Objects already on the grid stay put, the whole snap is one
undo step and one History row, and the status bar says how many moved.

**Blocking out with brushes.** **Add Brush Here…** (and **Add Brush…** in the
Edit menu) asks for a width, depth, and height, each starting at two grid
steps, and a texture from the map's own list, preset to its most used one. The
box is centred where the menu opened (or on the view) and on the middle of the
map along the hidden axis, snapped so its corner is on the grid, and joins
worldspawn selected, ready to drag, nudge, snap, or turn. Its faces are written
the way the map's own brushes are (classic, Valve 220 with TrenchBroom's
default axes, `brushDef`, or `brushDef3`, with the Quake II and III flag
numbers when the map uses them), and each face's points are wound so its plane
faces out of the box.

**3D preview.** The **3D** button at the end of the Levels view bar (and
**3D Preview** in the View menu) swaps the 2D view for the Models surface's
renderer showing the open map: solved brushes and tessellated patches, or a
Doom map's walls standing between its sector heights, flat shaded over a
ground grid. It is a preview, not an editor, though it shares the selection:
the selected objects are drawn in the highlight colour with a hatch over their
shading, and a click (a press that does not become an orbit) selects the
brush, patch, or Doom linedef under the pointer, which the objects list, the
inspector, and the 2D view follow. W (**3D Wireframe** in the View menu)
switches between flat shading and edges, to see through to what lies behind.
Drag orbits, the wheel zooms, the
readout names the texture under the pointer, Zoom to Fit frames the model, and
the 2D view's projection, grid, Snap, Show, and Zoom to Selection controls grey
out until the button is pressed again. Edits made
while it shows (from the menus, the objects list, or the inspector) rebuild it
without moving its camera; another map is framed afresh. Brush faces carry
their outward normals, so the inside of every brush is culled and the
painter's sort never sets a back face against a front one; very large maps
stop at a triangle limit and say so.

**Linedefs on Doom maps.** With linedefs selected, **Split Linedefs** and
**Flip Linedefs** (Edit menu, palette, and the map's context menu) work on them
in place: a split puts a vertex at each linedef's middle and gives the second
half its own copies of the sides, so the halves can be textured apart; a flip
turns a linedef around, and on a two-sided line its sides with it, so a line
facing the void faces its sector again. Both are single undo steps, and both
say in the status bar and on save that the node lumps need rebuilding.

**Drawing Doom geometry.** **Draw Sector** (D on Levels, the Edit menu, and the
map's context menu, where it shows a check) turns clicks into corners: each
lands on a vertex within a few pixels of the pointer, the shape's first corner
included, and otherwise on the grid. The shape so far is drawn over the map
with its corners boxed and the first one ringed, a dashed edge follows the
pointer with its length beside it, and a dotted one shows how the shape would
close. A click on the first corner, or Enter, closes it into a sector, which
comes back selected with the tool still on for the next room; Backspace or a
right-click takes the last corner back, and Escape drops the shape, then leaves
the tool. The new sector joins the vertices and splits the linedefs its corners
land on, shares the linedefs its edges run along, and copies the sector it was
drawn in, or its neighbour's floor and ceiling in the void; drawn exactly over a
sector, it takes that sector's tag and special too. A shape the map cannot take,
one crossing a linedef or reaching over two areas say, stays on screen with the
reason in the status bar, so a corner can be taken back instead of drawing it
all again. Vertices no linedef uses, the ones node builders add along walls,
are passed over, so drawing along a built map's walls works as it does on a
fresh one.
**Add Sector…** does the same from typed corners, a square around the view to
begin with. A click inside a room, with no drag, selects its sector (a drag
from there still boxes objects in), and the room under the pointer is traced
dotted, so rooms are picked the way Doom Builder's sectors mode picks them. Del
removes vertices, linedefs, and sectors the way Doom Builder does, **Merge
Vertices** joins the selected vertices into the one picked last, and **Join
Sectors** and **Merge Sectors** make the selected rooms one sector, merging also
taking the lines between them away. **Raise Floor** and **Lower Floor** (Page Up and
Page Down on the map view), **Raise Ceiling** and **Lower Ceiling** (with Ctrl),
and **Brighten** and **Darken Sectors** change the selected rooms by 8, or 16
for light, and by 1 with Shift, as Doom Builder's keys do; each press is one
undo step and says what it did in the status bar. The keys belong to the map
view alone, so the objects list and the Inspector keep Page Up and Page Down for
paging. **Gradient Floors**, **Ceilings**, and **Brightness** spread a field
evenly over three or more rooms in the order they were picked. **Make Door…** (the Edit menu, and the
context menu with a sector selected) asks for the door texture, the track
texture, and optionally a ceiling flat, each offering the map's own names and
remembering the last answers, then makes each selected room a door as Doom
Builder's does, in one undo step: the ceiling comes down to the floor, the lines
to other rooms turn to face out and open the door when used, and the room's own
walls become tracks that stay put as the door rises.

**Things on Doom maps.** A Doom or Hexen map adds, copies, and deletes things
the same way: **Add Thing Here…** asks for a DoomEd number from an editable
list (the player and deathmatch starts and teleport destination every idTech1
game shares, plus Doom's usual monsters, weapons, and pickups for a Doom-format
map, or any number typed in) and places it at the pointer on every skill, and
for Hexen every class and game mode. Ctrl+D and Del work on selected things,
while vertices, linedefs, and sectors hold the geometry together and are left
alone. A thing is listed once in the objects list, and selecting it shows its
type, angle, flags, and position in the entity inspector, whose edits go
straight to the THINGS record. UDMF maps use the dedicated lossless property editor.

**One edit, one undo command.** The viewport never edits the document. It
previews the move locally and emits `moveRequested(dx, dy, dz)` exactly once —
on drag release, or once per nudge — and the shell turns that into a single
`moveLevelMapSelectionSnapped()` call, which records one compound undo command.
One Undo therefore puts the whole drag back, not one object of it at a time.
When the move is refused the shell calls `refreshLevelMapViewport()` to throw
the preview away, so the picture can never keep showing an edit the document
did not take. A successful move records an activity entry, "Level map selection
moved", in the warning state with the detail "Unsaved map edit".

**History you can walk.** The Levels inspector's **History** tab lists every
edit to the open map as a numbered step, oldest first, after a first row for
the map as opened (or, past the undo limit, the oldest state still reachable).
The current step is bold with an arrow, undone steps follow it dimmed with an
undo glyph, and the step that matches the last save carries a save glyph and
says "saved", so the difference between the file on disk and the map on screen
is visible at a glance. Enter on any row undoes or redoes until that step is
the last one applied, and the status bar says how many edits moved.

**One selection, two surfaces.** The Objects list is
`QAbstractItemView::ExtendedSelection`, and list and viewport share one
selection set: `selectionSetChanged()` carries the whole set in add order with
the primary last, `setSelectionSet()` pushes it back, and the mirroring is
guarded so the list's own selection handler cannot write the set back and fight
the drag that produced it. A **Snap** checkbox on the viewport control row,
checked by default, decides whether a drag lands on whole grid steps or follows
the raw pointer delta; `statusLines()` reports the snap state and, mid-drag,
the delta and the destination coordinates. The viewport's accessible
description names every gesture, including F to frame the selection and Home to
frame the map, and `accessibleSummary()` reports the primary object and how
many objects are selected with it.

**Model preview with playback.** The Models page puts a control row above
`ModelViewport`: a render-mode box offering Textured, Flat shaded and Wireframe;
an animation box whose first entry is "All frames" and which is disabled when
the mesh yielded no animations; a Play/Pause button enabled only when
`frameCount()` is above one; and a **Frame Model** fit button. **Export OBJ**,
in the page header, writes the frame currently on screen through
`exportModelFrameObj()` and refuses, in the status bar, when the format gave no
geometry. In the viewport, left-drag orbits, middle-drag or Shift/Ctrl+left-drag
pans, the wheel zooms about the pointer, `Space` toggles playback, `Page
Up`/`Page Down` step one frame, `Home` frames the model and `0` resets the view.
Animations are inferred from frame names, which is how MDL and MD2 store them,
and the animation box says so in its tooltip. Textured mode falls back to flat
shading when no skin resolves out of the package, and the in-view readout then
says "Flat shaded (no skin)" instead of naming a mode it is not drawing. The
viewport now starts in the mode the box shows; it used to start flat shaded
under a box reading Textured, so a skinned model looked untextured until the
user picked a mode. Quake MDL, Quake II MD2 and Quake III MD3 decode geometry;
MDC, MDR and IQM are header-only, so they get a distinct "nothing to draw" paint
path rather than an empty viewport that looks broken. A speed box beside Play
sets the frame rate (1 to 60 fps, 10 by default), and **Grid**, **Axes**,
**Edges**, and **Cull backfaces** switches choose what the view draws besides
the model. Under reduced motion Play is disabled, and its tooltip says why and
that Page Up and Page Down still step frames. The readout under the viewport is
one line (frame, animation, playback, and skin size) and gives way to the
triangle under the pointer while hovering; counts live in the in-view corner.
The inspector's **Summary** tab is a Property / Value grid like the entity
inspector: a **Warnings** group first when there is anything to warn about
(including a model whose named skins were all missing from the package), then
**Model** (format and version, frames, surfaces, vertices, triangles, bounds,
radius, tags), **Surfaces**, **Animations**, **Skins** with the skin that was
drawn marked by a check, and **Header** for whatever else the decoder reported.

**Audio format grid.** The Audio page's **Format** tab is the same grid: codec,
channels, sample rate, bit depth, frames, duration, playback and export support,
and any warning about the file. Core's report also ends with a text sketch of
the waveform for the command line; the page leaves that out, because it paints
the real envelope above the grid.

**Audio transport.** The Audio page's toolbar is a transport: **Play Sound**
(Space, which becomes **Pause Sound** while a sound plays), **Stop**, **Loop**,
the playhead time against the length (`0:00.12 / 0:01.00`), and **Volume**. The
buttons run the `audio.*` commands, so the menu, the palette, and the toolbar
never disagree about what is available. Playback starts from the playhead, and
Stop puts the playhead back where playback started, the way audio editors do; a
click or a drag in the waveform moves it, and so do the arrow, Page, Home, and
End keys once the waveform has focus. Choosing another sound stops the one that
is playing. The player is made on the first Play and holds a converted copy of
one entry, so a sound in a package plays without being extracted. A Doom
resource WAD with no maps now opens as a package, so its `DS*` sounds land in
this list rather than in Levels.

**Shader stage tree.** The Shaders page's **Stage graph** is a tree of shaders,
their stages (map, blend function, and rgbGen), and the textures each stage
references. Every texture row says whether the reference was found, and in
which package, or is missing from the open package, with a warning glyph rather
than colour alone; its tooltip lists the paths that were tried. Selecting a
shader or a stage puts a section for it at the top of **Shader Details** with
its parsed fields and raw text, ahead of the whole-script report. Raw text in
detail panels keeps four-column tabs, so a stage's braces no longer sit halfway
across the panel.

**Textures beside the map.** A **Textures** tab beside the objects list shows
the open map's textures as tiles, the ones applied lately first (they stay
while this session runs, even once the map no longer uses them) and then the
map's own by how often they are used, each with its picture from the open
package, or a framed placeholder when the package has none. The filter narrows
them; Enter or a double-click puts one on the selection, as **Apply Texture…**
does; and its menu selects the objects that use it, offers it as the
replacement in **Replace Texture…**, or shows it on the Textures surface. A
tile's tooltip and spoken name say how often the map uses it, whether it was
applied lately, and whether the open package has its picture.

**Create palette.** Beside the objects list, a **Create** tab lists what the
open map can place, in the manner of idStudio's entity browser: the loaded
definitions' point classes filed by prefix (`info_`, `light`, `monster_`), or on
a Doom map its thing types by kind (starts, monsters, weapons, ammunition,
health and armor, power-ups, keys, obstacles), each with its description or
DoomEd number as a tooltip. The filter narrows it, and a group's own name
("Monsters") shows everything in the group; Enter or a double-click places the
entry in the middle of the view, and dragging one onto the map shows a snapped
marker where it will land and places it there. A drag is always a copy, so the
palette keeps its row even with Shift held, and a Doom map takes things in the
Top view only, since the side views show heights a thing does not have. Either
way it is one undo step. A UDMF map shows a placement limitation in place of the list and directs users
to UDMF Properties for existing things.

**Brush faces.** With a brush selected, the **Inspector** lists its faces,
each a collapsed group named for the way it faces (top, north, east, or sloped
with its normal) and its texture, so the textures read at a glance and one face
opens to edit. Texture, shift, rotation, and scale edit in place, the texture
field offering the map's own names; each edit is one undo step that rewrites
only that face's line. A Valve 220 face shifts through its axis offsets and
turns its axes about the face, as TrenchBroom does. After an edit the row stays
current and its group stays open, so the next value is one keystroke away. A
click on a wall in the 3D preview selects its brush and opens the face clicked
in the Inspector, at its texture, the way Radiant picks a face.

**Doom fields.** On a Doom or Hexen map the same **Inspector** tab edits
whatever is selected: a thing's type (named from the thing table), angle,
position, and flags as boxes for skills, ambush, and multiplayer (Hexen adds
classes and game modes); a sector's heights, flats, light, special, and tag; a
linedef's special and tag (or Hexen's five arguments), its flag word, each flag
as a box with what it does written beside it, and both sides' sector, upper,
middle, and lower textures, and offsets; or a vertex's position, which its
linedefs follow. Texture fields offer the names the map already uses. Each edit
is one undo step, and a side that packed sidedefs share with other lines is
copied first, so only the line being edited changes.

**Entity inspector.** The Levels page's right-hand inspector opens on an
**Inspector** tab holding a property grid in the style of idStudio's entity
inspector: collapsible **Entity**, **Keys**, **Defaults**, and **Spawnflags**
groups of Key / Value rows with alternating shading. **Entity** shows the class,
index, and, from loaded definitions, the kind, inheritance, description, size,
model, and the file and line that declared it. **Keys** lists every key set on
the entity, with `entityKeyHelpText()` as each row's tooltip. **Defaults** lists
the keys the class declares but the entity leaves unset, muted, with required
ones flagged; editing one sets it. Double-click or Enter edits a value in place,
and the edit goes through `setLevelMapEntityProperty()` as one undoable change;
A colour key (`_color`, `color`, or one its definition types as a colour)
shows a swatch beside its numbers, and double-clicking it opens a colour picker
that writes the choice back the way the key had it: 0-255 or 0-1 fractions,
with any brightness after the colour kept. Enter still edits the numbers.
**Edit Key** starts from the selected row, offering the entity's keys and its
class's. Values come with suggestions where the map or the definition knows
better than a blank field: a `target`, `killtarget`, `pathtarget`,
`combattarget`, or `deathtarget` lists every `targetname` in the map, a
`targetname` lists the names something fires at that no entity carries yet,
and a key with FGD choices lists them. The in-place editor opens with the list
showing and narrows it as the user types; Edit Key's value step shows the same
list and still takes any text. **Spawnflags** are check boxes whose
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

**Several entities at once.** With more than one entity selected, the inspector
shows them together, as Radiant and TrenchBroom do: an **Entities** group counts
them and names their class, or how many classes they span, and **Keys** lists
every key any of them sets. A value they do not all share is shown in italics,
spoken as differing, and its tooltip says how the values spread ("300 on 2; not
set on 1"). An edit there, the **Edit Key** dialog, or Delete acts on all of
them as one undo step, through `setLevelMapEntitiesProperty()` and
`removeLevelMapEntitiesProperty()`, and the selection stays as it was, so the
next key goes to the same entities. When they share a class, **Defaults** lists
what none of them sets, and a spawnflag set on only some of them shows partly
checked; checking it sets that bit on all of them, each keeping its other flags.
`vibestudio --cli map edit --where "<query>"` does the same from the command
line, to every entity (or Doom thing) the query keeps: `--where class=light
--set light=300`. Brushes, Doom things, sectors, and lines are still shown one
at a time: with several selected, a note at the top of the inspector says it
shows the last one selected and that an edit there changes it alone.

**Open files in tabs.** The Code page keeps a tab per open file above the
editor, in the order they were opened, dragged to reorder. Each tab has its own
document, so its text, undo history, caret, and scroll position survive a trip
to another file; opening a file that is already open brings its tab forward
instead of re-reading it. A dot after the name marks unsaved changes, and the
tab's tooltip and accessible name say so in words; two open files with the same
name show their folders. Ctrl+Page Down and Ctrl+Page Up step through the tabs,
Ctrl+F4 or the tab's close button closes one, a middle click closes one, and
the tab's menu adds Close Others and Copy Path. Closing a changed file asks
Save, Discard, or Cancel first, and so does closing the window, once per
changed file. Every open file is watched on its own: one changed outside the
studio reloads in its tab, wherever it is, and asks first only when that tab
has unsaved changes. Ctrl+Tab is deliberately not a tab switch, because in the
editor, where Tab types a tab, it is the keyboard's way out.

**Search results stay put.** Find in the project lists its matches under a
**Search Results** tab beside **Problems**, and switches to it; opening a match
opens its file at the match and leaves the list as it was, so the next match is
one step away, while **Problems** keeps showing the open file's own diagnostics.
With no file open the editor is read-only, so nothing typed there can surface
later in another file.

**Editor zoom.** Ctrl+= and Ctrl+- step the code editor's text through a
browser's zoom levels, 50% to 300%, and Ctrl with the mouse wheel does the
same; **View > Reset Editor Zoom** goes back to 100%. The zoom scales the
studio's monospace font for the editor alone, on top of the app-wide text
scale, and the gutter and tab stops follow it, as does every open tab. While
the level is not 100% it shows at the end of the editor's readout, where a
click resets it, and the level is kept between runs.

**A code editor with a gutter.** The Code page's editor, `StudioCodeEditor` in
`src/app/code_editor.*`, adds a line-number gutter and a faint band on the
caret's line, both drawn from the active theme; the band is outline-strength in
the high-visibility themes. Syntax highlighting and diagnostics are unchanged,
and a diagnostic row still jumps to its line. The strip above the editor names
the document the way an editor tab does, as file name, language, and save state
(Modified, Saved, or Read-only, truncated), with the full path in its tooltip;
the page header already shows the path, so the strip no longer repeats it.
Ctrl+S saves through `QSaveFile` and hands the watcher the new bytes, so the
studio's own save is never reported back as an outside change, and it neither
reloads the editor nor clears its undo history. Modified follows the document's
own modification flag, so undoing back to the saved text clears it. Ctrl+F
opens a find bar above the editor that selects matches as the user types and
counts them ("3 of 7"): Enter and Shift+Enter, or F3 and Shift+F3, step
through them, **Aa** matches case, and Escape closes the bar and returns to the
text. Ctrl+H opens the same bar with a replace row and selects the match at or
after the caret: Enter in the replace field replaces that match and selects the
next, a selection that is not a match only moves on, and Ctrl+Enter (or
**Replace All**) replaces every match as one undo step and says how many.
Ctrl+L goes to a line. Opening the file that is already open brings its
tab forward and keeps its unsaved edits. Ctrl+/ comments or uncomments the selected lines with the file
type's own line comment (`//`, or `;` for INI-style files) at their shared
indentation, Ctrl+D copies them below themselves, and Alt+Up and Alt+Down move
them; each is one undo step, and a read-only package copy says why nothing
happens. Typing follows the file's own indentation: Enter keeps the line's
indent and goes one unit deeper after an opening brace, a closing brace typed
on a blank indented line steps back out, and Tab and Shift+Tab indent and
outdent every selected line together, keeping them selected. The unit is the
file's (a tab, or the width of its first space-indented line), a tab when
nothing is indented yet. The bracket beside the caret and its partner are
highlighted, bold on a tinted background.

**Build stages at a glance.** Each row of the Build page's **Stages** list shows
the stage and its state glyph over its input and output file names
(`start.map` to `start.bsp`); the full paths are in the row's tooltip and in
**Build Details**, instead of wrapping each row across four lines. When the
pipeline chart fits on one row with room to spare, its boxes widen a little and
its connectors lengthen so the chain spans the panel instead of huddling at its
left edge; both are capped, so a wide window does not produce sparse boxes.

**Map, build, problem, fix.** The Build page's input follows the map open in
Levels (the last Save As copy when there is one, because that is the file a
compile reads) until the user browses to or types another file; **Use Open Map**
hands it back. Opening a map for another game switches to that game's pipeline
only when the current one cannot build it. Building a map that has unsaved edits
asks first, offering **Save As…**, **Build Saved File**, or Cancel, because the
compilers read the file on disk and would otherwise leave the edits out without
a word; a **Run Profile** compile from Levels asks the same with **Compile Saved
File**. A finished run keeps a **Problems** tab beside **Stages**, which comes
forward when a compiler reported anything: each warning or error with its stage
and location, its text without the `WARNING: 6:` the compiler starts it with,
which the row's icon and location already give. Activating one whose line falls
inside a brush, patch, or entity
of the open map selects and frames that object in Levels; a line in any other
text file opens the Code editor there; and a problem with no location shows its
stage's output. A problem's context menu offers the same **Show** step, **Copy
Message** (the line exactly as the compiler printed it, for a bug report or a
search), and **Copy All Problems** (every row as it is read aloud, one per
line). **Show Output** opens the folder holding the compiled map, and
the launch form's Map field defaults to what the build produces, shown as its
placeholder: the BSP's name, or the open Doom map's lump, which the launch turns
into `-warp`'s numbers (`07` for `MAP07`, `2 3` for `E2M3`).
An **Automatic** launch profile beside it follows the installation's engine.
Copy Commands, Inspect Artifacts, the launch preview, and Launch Game build
their request the way a run does, tool overrides included, so what is shown is
what runs. **Add to Package** stages the built map, with its `.lit` and `.lux`,
into the open package under `maps/`, replacing an older build already there,
so Save As writes the release; it is enabled only with a package open and a
built map to add.

**Copy, then play.** A Quake-family engine loads a map only from a game folder's
`maps` directory, so the launch form has a **Game folder** field (empty means
the base game, `id1` for Quake; a mod's folder name sends the map there and adds
`-game`, and each installation remembers the folder last used) and **Copy the
built map into the game before launching**, checked by default and disabled for
Doom-family games, whose ports read the built WAD where it is. The launch plan
is a property grid: a **Launch** group (installation, profile, program,
arguments, working folder, and whether it is ready), a **Map copy** group
listing each file as new, replacing one, or already up to date, and a
**Problems** group only when something is wrong, where a map that has not been
built yet is one row rather than one per plan. **Copy Command Line** copies the
planned command for a shell or a script. An installation profile is read-only
until the user allows test maps: the first launch that needs a copy asks **Allow
Test Maps**, naming the one folder that will be written, and the answer is saved
on the profile; the Workspace installation list shows it and its **Allow test
maps** check box takes it back. The launch confirmation then lists the copy
above the command line, and the copy goes through a temporary file and a rename,
so nothing else in the installation is touched and a failed copy leaves no
half-written map. **Build and Launch** (F5, and a button beside Run Pipeline)
runs the pipeline and, when it succeeds, goes straight to that launch; a failed
or cancelled build says so and launches nothing.

**Leaks you can see.** A leaking Quake-family build puts the leak first in
**Problems** ("The map leaks: info_player_start is reachable from outside the
map") and, when the build was of the map open in Levels, draws the leak point
file over the map in every projection: a line in its own colour on a casing that
keeps it readable over any brush, a filled circle where the file starts, a
hollow square where it ends, and a **Leak** label. Activating the leak frames
the trail; the Levels **Health** list leads with it too, and a rebuild that no
longer leaks takes it away. **Load Leak Trail…** and **Clear Leak Trail** in
the Build menu do the same by hand, Export Image includes the trail, and
`map render --leak` draws it in the headless SVG.

**The toolchain, found or located.** The Build page's **Toolchain** tab lists
every compiler tool with its status, the executable the studio runs, and where
that path comes from: found automatically, chosen by the user, or set by the
project manifest, which wins while its project is open. **Locate…** saves a
chosen executable as a user override, the same setting
`vibestudio --cli compiler set-path` writes; **Use Automatic** forgets it, and
**Rescan** looks again after a tool is installed. With nothing selected, the
first missing tool is selected, so **Locate…** acts on the tool that needs it.
Below the tools, each compiler profile shows its engine, stage, and readiness
behind a state glyph. The readiness chart that repeated this above the table is
gone.

**Viewer controls.** The Textures preview has a view bar: a sprite frame box,
**Zoom to Fit**, **Actual Size**, **Zoom In**, **Zoom Out**, and **Pixel grid**
and **Transparency** switches. The Levels viewport bar keeps what the view
draws in one **Show** menu of checkable items (Entities or Things, Sector Fill,
Grid, Vertices, Labels, Target Links), so **Zoom to Fit**, **Zoom to
Selection** (F in the view), and **Export Image** stay in view at laptop widths
instead of spilling off the bar. The menu follows the map's game: for a
Quake-family map the point-object item reads **Entities** rather than Things,
**Sector Fill**, which only Doom maps have, is hidden, and **Target Links** is
hidden for Doom, whose sectors are linked by tag instead. The projection, grid
size, Snap, and the Show items are kept from one session to the next, as
Radiant and TrenchBroom keep them. For a WAD, the
document bar's map box lists every map in it in
directory order, read once per WAD with `levelMapNamesInWad()`; choosing one
opens it, asking about unsaved edits first, and the box hides for a `.map`
path, where there is nothing to choose.

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
separately, and so does opening another package while changes are staged against
the open one (**Discard Staged Changes**, default Cancel), from the Open
dialogs, a drop, Open Recent, Go to File, or a loose asset's folder; those paths
used to replace the staged plan without a word. Unsaved map edits are asked
about before anything replaces or closes the map (opening or dropping another
map, reloading, closing the window) with **Save As…**, **Discard Edits**, or
Cancel: the map editor never overwrites its source, so keeping the edits means
writing a new file.

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
drives `DocumentWatcher::poll()` over the open map, the open package, every
file open in a code editor tab, and the project manifest — one path per role
for the others, so opening a second map stops watching the first. A code file whose read was truncated at
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
the watcher and remembers that change, as the file looked then, so the same
change is never asked about twice while a later one is, and saving the file
clears it. A file open as more than one thing, such as a `.map` in Levels and in
a code tab, is watched once and every holder hears of a change: the map asks,
the tab reloads or asks, and closing the tab leaves the map's watch in place. A
file deleted outside the studio leaves its code tab holding the only copy of the
text, so the tab turns unsaved: closing it asks first, and Save writes the file
back. The project manifest reloads without asking, because nothing the user
typed lives only in those panels, and a palette it names takes effect at once.
Every substantive change also records an activity entry, "File changed outside
the studio", in the warning state.

**A crashed session is offered back, not reopened.** An interactive start
arms crash capture before the shell exists (`installCrashHandling()` in
`src/main.cpp`), so a session that ends without shutting down leaves its marker
and, when the handler could write one, a report beside the session log. The
next start's `beginSession()` then does not reopen that session, since one of
its files may be what brought the studio down; the notice bar says **The studio
closed unexpectedly last time**, whether a report was kept, and that the files
were not reopened, with **Reopen Last Session** (the files that were open, in
one click) and **View Report**. Until the offer is answered, the crashed
session stays recorded, so a second crash or a close before then still has it
to offer; and since the offer can be taken long after start, Reopen asks about
unsaved map edits and staged package changes first, as any open does, leaving
that part closed when the question is cancelled. A run with its own settings
file keeps its crash markers and reports beside that file, never the user's. The report opens in a **Crash Reports** dialog
with a summary, the full text in a monospace view, **Copy Report**, and **Show
in Folder**; **Help > Crash Reports** lists every kept report the same way, and
the diagnostic bundle counts them. Nothing is ever sent: a report stays on the
machine unless the user copies it. The marker is consumed when capture starts,
so one crash is offered once, and an activity entry in the failed state keeps
it in the history. **Startup and Recovery > Keep a crash report on this
machine** turns capture off at once (and back on at once when it was armed at
start); the self-test and snapshot runs never arm it, so they neither leave a
marker nor consume a real one.

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
- [x] AI provider, model, and token usage for each Assistant answer, in the panel and its Activity task; the exact request, before it is sent.
- [ ] Connector capability detail, cost in currency, and generated asset provenance.
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
