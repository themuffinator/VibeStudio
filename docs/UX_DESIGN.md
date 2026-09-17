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

**Mode rail over a stacked work surface.** A left-hand rail lists ten modes —
Workspace, Levels, Models, Textures, Audio, Packages, Code, Shaders, Build,
Settings — and drives a `QStackedWidget`. Each mode is a separate page that
shows only the panels belonging to it. Previously the rail only recorded a
preference and posted a status message while every panel lived in one long
scrolling column; that column is gone. The selected mode is persisted and
restored.

**One command registry behind menus, toolbar, palette, and shortcuts.**
`src/app/studio_actions.*` owns the `QAction` instances. `populateMenuBar()`
builds the menu bar and `populateToolBar()` builds a text-beside-icon toolbar
from the same registration list; default shortcut sequences come from
`core/studio_semantics.h`. A duplicate sequence is recorded as a conflict and
skipped rather than silently shadowing its first owner, and the conflicts are
exposed to the diagnostics surface.

**Type-to-filter command palette.** `CommandPaletteDialog` lists every enabled
registry command plus the documented palette-only entries, filtered as the user
types, with each row showing its category, summary, and shortcut.

**Status chips.** Permanent status-bar chips report project, package,
installation, compiler, and AI health. Each renders a bracketed text cue before
its label, mirrors that text into an accessible description, and carries its
operation state as a style property.

**Real pictures instead of text stand-ins.** `src/app/studio_charts.*` provides
painted composition, pipeline, and activity-timeline widgets, and
`src/app/map_viewport.*` provides an interactive 2D map view that draws real
geometry through the shared solver in `core/map_geometry` — replacing the
`[##########........]` ASCII readiness bar as the primary display and the
text-only "preview lines" map tab entirely. A text readiness bar still appears
in the compiler tool list rows and the copyable inspector dump, where plain text
is the point. `src/app/asset_views.*` adds image, palette-swatch, and waveform
views so textures, palettes, and audio are shown rather than described.

**Direct manipulation.** The window accepts dropped files and routes each one by
type: maps to the Levels surface, a folder holding a project manifest to
Workspace, packages to Packages, anything else to the Code editor. The package
entry tree has a context menu for extract, stage-replace, rename, and delete.

**Confirmation before damage.** `confirmDestructiveAction()` shows a warning box
whose default button is Cancel. It guards discarding staged package changes,
launching an external game executable, and project-wide find/replace — which
runs as a dry run first and reports the match and file counts inside the prompt.
Closing the window with unsaved code or unwritten staged changes prompts
separately.

## Accessibility Baseline
Accessibility behavior should be designed into every surface:
- [x] High-contrast dark and high-contrast light themes.
- [x] Text/UI scale support at 100%, 125%, 150%, 175%, and 200%.
- [x] Comfortable, standard, and compact density presets.
- [ ] Reduced-motion setting for transitions and loading visuals — stored and
  honored by the loading pane and the map viewport selection ring, but the shell
  has no animations or transitions for it to suppress yet.
- [ ] Keyboard-visible focus and no keyboard traps.
- [x] Accessible names and self-updating accessible descriptions for the charts,
  the map viewport, the asset preview views, the loading pane, and the detail
  drawer.
- [ ] Accessible names, roles, descriptions, and status changes for every
  remaining custom widget.
- [ ] Screen-reader-readable task states, compiler diagnostics, validation results, and setup warnings.
- [ ] OS-backed TTS for selected summaries, errors, task outcomes, and setup guidance.
- [x] No color-only status in the charts, status chips, and map viewport; colour
  is always paired with a glyph, hatch, stroke, or text cue.
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
  outlines, things, labels, hover, selection, and orthographic projections.

## Detail Surfaces
Advanced users should be able to inspect:
- [x] Raw package metadata.
- [x] Virtual paths and physical source paths.
- [x] Parsed format structures.
- [x] Compiler command manifests.
- [x] Compiler stdout/stderr.
- [x] Hashes and reproducibility manifests.
- [x] Map entity properties, texture/material references, validation, preview lines, and undo history.
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
