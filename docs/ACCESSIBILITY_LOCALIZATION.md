# Accessibility And Localization

Accessibility and localization are core product design requirements for
VibeStudio. They should be built into the shell, setup flow, editor surfaces,
CLI output, task feedback, AI workflows, and documentation from the beginning.

The goal is simple: VibeStudio should be usable by as many creators as possible
without asking them to fight the interface before they can make something.

## Accessibility Philosophy

- Accessibility is not polish. It is part of the definition of a usable studio.
- Follow [WCAG 2.2](https://www.w3.org/TR/WCAG22/) AA principles where they
  apply to desktop software, then go beyond them when the workflow needs it.
- Respect operating-system accessibility settings whenever possible.
- Make every core workflow possible without relying on color, pointer-only
  gestures, sound-only feedback, tiny text, or hidden status.
- Keep accessibility settings visible during first-run setup and reachable from
  preferences, the command palette, and CLI diagnostics.
- Validate accessibility continuously with keyboard, screen reader, scaling,
  high-visibility, and localization checks.

## Platform Support

VibeStudio should use Qt's accessibility and internationalization stack:

- [Qt Accessibility](https://doc.qt.io/qt-6/accessible.html) for assistive
  technology metadata, scalable UI, keyboard navigation, contrast, sound/speech,
  and screen-reader-friendly widgets.
- [Qt High DPI](https://doc.qt.io/qt-6/highdpi.html) behavior for platform-aware
  scaling and multi-monitor support.
- [Qt TextToSpeech](https://doc.qt.io/qt-6/qttexttospeech-index.html) for
  OS-backed text-to-speech engines. **Not yet a dependency of this build**: see
  [OS-Backed Text To Speech](#os-backed-text-to-speech).
- [Qt internationalization](https://doc.qt.io/qt-6/internationalization.html),
  `QTranslator`, `QLocale`, and Qt Linguist tooling for translation, locale
  formatting, pluralization, Unicode, bidirectional text, and writing-system
  support.

High-DPI behavior is configured before the `QApplication` is constructed:
`configureHighDpiBehavior()` in `src/app/studio_runtime.*` sets the
`PassThrough` rounding policy so fractional scale factors stay exact for the
painted map viewport, charts, and texture previews.

## Visual Accessibility

Required settings and behavior:

- [ ] Follow OS font and scaling defaults on first launch.
- [x] Support application text scale presets: 100%, 125%, 150%, 175%, and 200%,
  and a custom scale path where practical.
- [x] Support high-contrast dark and high-contrast light themes.
- [ ] Support a dedicated color-blind-aware status palette and a
  reduced-saturation option as separate, selectable settings.
- [x] Support UI density presets: comfortable, standard, compact.
- [ ] Ensure toolbars, tabs, cards, inspectors, dialogs, and status chips do not
  clip text at 100%, 125%, 150%, 175%, and 200% scale. Labels that hold paths or
  live readouts now elide instead of widening the window, icons and the mode rail
  grow with the text scale, and every work surface has been rendered at 200%
  through `--ui-snapshot`; narrow side-panel tab strips still elide their labels
  at that size.
- [x] Use icons plus accessible labels/tooltips for key actions: the global tool
  bar, page headers, page tool bars, and the mode rail pair a theme-aware glyph
  with an accessible name and a tooltip, and icon-only tool buttons name their
  shortcut in the tooltip.
- [x] Pair color with a glyph, hatch, stroke, or text cue for status in the
  charts, status chips, and map viewport.
- [ ] Audit every remaining surface for color-only state.
- [ ] Provide visible focus states for keyboard and assistive-technology users.
  Every focusable control now draws a focus ring in a colour reserved for focus
  (2px in the high-visibility themes), distinct from the selection fill; the
  audit of every surface is still outstanding.
- [x] Provide a reduced-motion preference that is stored, applied at start-up,
  and settable from preferences and the CLI.
- [ ] Apply reduced motion to animations, transitions, and timeline effects.
- [ ] Provide preview checks for maps, textures, sprites, shaders, and package
  summaries under high-visibility themes.

### High-Visibility Themes

`StudioTheme` in `src/core/studio_settings.h` carries `HighContrastDark` and
`HighContrastLight` beside `System`, `Dark`, and `Light`. Every theme resolves
to one token set in `src/app/studio_theme.*`, which becomes both the
application `QPalette` and the generated application stylesheet, so dialogs,
menus, tooltips, and docks follow the theme as well as the main window. The
high-visibility themes use pure black and white with a single saturated accent
(yellow on black, blue on white), full-strength outlines on every panel, 2px
borders and focus rings, and a focus colour distinct from the accent so focus
never hides inside a selection. The `highContrast` flag also reaches every
painted widget: the map viewport, the model viewport, the image/palette/waveform
asset views, the composition, pipeline, and timeline charts, and the code
syntax highlighter.

`studio-theme-smoke` checks all four explicit themes against WCAG 2.2 contrast
ratios on every run: body and secondary text against the frame, page, panel,
and input backgrounds, selection text against the selection fill, and accent
text against the accent fill at 4.5:1; focus rings and success, warning, and
danger colours at 3:1. The default dark theme's list selection is a deeper
burnt orange than its accent for that reason: white text on the bright accent
would fall below 4.5:1. `System` follows the platform colour scheme through
`QStyleHints::colorScheme()`.

Check boxes and radio buttons are drawn by a proxy style over Fusion from the
same tokens: an outlined box that is clearly visible on every panel, an accent
fill when checked, and a contrasting mark, so a checked state reads from the
mark itself.

`studioStateColor()` in `src/app/studio_charts.*` keeps four tuned ramps — two
high-contrast and two standard — and separates the eight operation states by
both hue and lightness. That is not the same thing as a color-blind-aware
palette the user can choose, and there is no reduced-saturation setting, so
both remain unticked above.

### Non-Color Status

Status no longer depends on hue:

- Charts draw each slice with one of eight hatch patterns
  (`patternForIndex()`), and every pipeline stage and timeline row is prefixed
  with a state glyph from `studioStateGlyph()` — a check mark, a cross, a
  warning triangle, and so on, written as explicit code points.
- Status-bar chips and page status strips show the state glyph from
  `studioStateGlyph()` followed by words (a check mark before the package name,
  a warning triangle before "Compilers 7/9", a middle dot before "No project"),
  carry the same text in an accessible description, and expose the state as a
  style property rather than as a raw color.
- Activity, problem, timeline, and build-stage rows lead with the same glyph,
  and their accessible text spells the state out ("Package Scan, Completed.
  ..."), so a screen reader hears the word rather than the symbol.
- The map viewport marks selection with a ring plus a crosshair and a text
  label, draws unsolved brushes with a dashed pen and a cross, and separates
  patches with a dash-dot stroke, so shape and stroke carry the meaning.
- The entity inspector lists each declared spawnflag as a check box row in its
  **Spawnflags** group, named for the flag and valued with its bit. The set
  state is the check mark the proxy style draws, and it is exposed to assistive
  technology as the row's checked state, so set and unset survive a color-blind
  or high-contrast reading. Toggling the box rewrites the entity's summed
  `spawnflags` value.
- The entity section of the Levels **Health** tab is headed
  `ENTITIES [<state>]`, with the state word from
  `localizedOperationStateName()`, and prints each finding as its
  `EntityValidationIssue::code` followed by the message
  (`entity-unknown-class`, `entity-required-key-missing`, and so on).
  `m_levelMapValidation` is not one of the lists `applyStateColor()` tints, so
  no row in it depends on hue. Per-row severity is kept only as a data role: it
  is neither painted nor written into the row text, so a reader learns which
  check fired but not how severe that particular finding was.
- The shader stage tree marks a texture missing from the open package with a
  warning glyph and the words "missing from the open package", and one that was
  found with the package it was found in. The model inspector marks the skin
  it drew with a check glyph beside the path, and model warnings sit in their
  own group with a warning glyph on every row.
- The first-run setup stepper marks each step with a glyph (a check for done,
  a chevron for the current step, and a dot for pending, whose text is also
  muted), and each row's accessible text spells the state out: "Step 2 of 8,
  current step: Workspace Profile".

### Reduced Motion

The reduced-motion preference is stored, settable from preferences and from
`--set-reduced-motion`, and read back at start-up. The shell still has no
declarative animation framework — there is no `QPropertyAnimation`,
`QVariantAnimation`, `QTimeLine`, or `QMovie` anywhere in the source tree — so
the setting acts on the three surfaces that move on their own:

- `LoadingPane` swaps its indeterminate (marquee) progress bar for a static
  determinate bar while a busy state is in progress.
- The map viewport draws the selection ring solid instead of dashed, because
  fine dashes shimmer while panning.
- The model viewport refuses to animate. `ModelViewport::setReducedMotion(true)`
  drops out of playback if it was running and emits `playbackChanged`, `play()`
  returns without starting, `advanceFrame()` returns early, and
  `updatePlaybackTimer()` stops the `QTimer` that drives frame stepping.
  Stepping by hand with Page Up and Page Down keeps working, because that is a
  deliberate user action rather than motion the widget starts on its own, and
  `statusLines()` says so: "Playback: disabled by reduced motion; step frames
  with Page Up and Page Down". `accessibleSummary()` reports the same state as
  "playback disabled by reduced motion".

The shell pushes the preference into the model viewport wherever it pushes it
into the map viewport, both when a model is shown and when preferences change.
The model workbench's Play/Pause button, though, is enabled purely on
`frameCount() > 1` in `refreshModelPlaybackControls()`, so under reduced motion
it stays clickable and pressing it simply leaves the viewport paused; the
readout under the viewport is the only place that explains why.

Everything else in the shell is static, so there is nothing further for the
setting to suppress yet.

## Keyboard, Screen Reader, And Assistive Tool Support

Required behavior:

- [x] Every command in the shell command registry is reachable from the menu
  bar, the command palette, and — where `core/studio_semantics.h` declares one —
  a keyboard shortcut, all generated from that one registry.
- [ ] Bring the remaining per-surface controls into the same command registry.
- [ ] No keyboard traps in modals, dock widgets, editors, setup flow, or preview
  panes.
- [ ] Consistent tab order in setup, preferences, inspectors, and task details.
- [x] The charts, the map viewport, the model viewport, and the asset preview
  views expose accessible names and keep an accessible description in sync with
  their contents, so their data is readable as text.
- [ ] Every other custom widget exposes accessible names, descriptions, roles,
  values, and state changes.
- [ ] Task progress, warnings, failures, prompts, and completion states are
  available to assistive tools, not just visually rendered.
- [ ] The CLI provides accessible plain-text output and machine-readable JSON.
- [ ] Editor profile controls document keyboard/mouse changes clearly and expose
  reset/revert actions.

`src/app/studio_actions.*` owns the `QAction` instances. Menus, the toolbar, the
command palette, and shortcuts are all generated from one registration list, and
the default shortcut for a command comes from `core/studio_semantics.h`, so the
documented registry and the running application cannot drift apart. Duplicate
sequences are recorded as conflicts and skipped rather than silently shadowing
an earlier owner. The command palette is a type-to-filter dialog that lists
every enabled registry command plus the documented palette-only entries.

The active editor profile registry exposes routed keyboard and mouse binding
descriptions with stable command IDs in preferences, inspector details, CLI
text, and JSON. Whether a binding routes anywhere is now computed rather than
asserted — see [`docs/EDITOR_PROFILES.md`](EDITOR_PROFILES.md). Reset/revert
actions and full keyboard/mouse audits remain planned.

Current shell custom widgets include a reusable loading pane and detail drawer.
They expose accessible names and descriptions for their title, state,
progress, placeholder, section-list, copy, and detail-content controls; broader
screen-reader and keyboard audits remain required before MVP.

The Level Editor MVP adds accessible names/descriptions for the map path,
Doom map marker, engine hint, compiler profile, object list, statistics,
preview, validation, save-as, edit, move, run-profile, and CLI-copy controls.
All visible map workbench strings route through Qt translation APIs, while map
format identifiers and CLI flags remain stable technical identifiers.

The Advanced Studio MVP adds accessible names/descriptions for shader script
paths, stage inspection, sprite engine/name/frame/rotation controls, code index
paths, AI proposal kind/prompt controls, extension discovery roots, summary
cards, and detail tabs. Visible shader, sprite, code, AI, and extension strings
route through Qt translation APIs; shader directives, CLI flags, extension IDs,
file paths, and format identifiers remain stable technical identifiers.

The studio shell's navigation is keyboard-first. The mode rail is a single tab
stop whose arrow keys, Home, and End move between modes; tool buttons, page
buttons, and the rail take focus from Tab but not from a mouse click, so focus
rings appear for keyboard users without lingering after a click; and the entity
property grid edits the selected value on Enter. Keyboard focus starts on the
work surface rather than on the first tool button. The model and audio
property grids and the shader stage tree are ordinary tree views, so the arrow
keys walk their rows and expand or collapse groups, and every row's tooltip
holds its full, unelided value. The float and close buttons on each dock's
title bar are named for their panel ("Float the Activity panel", "Close the
Activity panel") and take focus from Tab.

The format and UI round adds accessible names for every control it introduces:
the Levels page **Entity** tab and its inspector list, the entity definition
path field, its Browse and Load buttons and the summary label beneath them, the
**Snap** checkbox on the map viewport control row, the package **Compare**
button, and the model workbench's render-mode combo, animation combo,
Play/Pause button, Frame Model button, Export OBJ button, viewport, and hover
readout. The entity inspector, the entity definition path field, the model
viewport, the model skin preview, and the model details list also carry
accessible descriptions. All of these strings route through `tr()`; entity issue
codes, spawnflag names read out of a definition file, format identifiers, and
CLI flags stay untranslated as stable technical identifiers.

`package.compare`, `model.export`, and `entity.definitions` are registered in
the same shell command registry as everything else, so they appear in the menu
bar and in the command palette. `core/studio_semantics.h` declares no shortcut
for any of the three, so today they are menu- or palette-driven only.

Two viewport details matter for keyboard users. The map viewport binds `Tab` and
`Shift+Tab` to cycling the selection through object tiers, so `Tab` no longer
leaves the widget; `Escape` is the documented way out, and it is staged — it
first cancels an in-progress drag or rubber band and discards the preview, then
clears the selection, and only then calls `focusNextChild()`. The model viewport
takes `Qt::StrongFocus` and does not intercept `Tab`, so focus moves through it
normally.

`m_levelMapViewport`'s accessible description now covers every gesture:
Shift-click to add, Ctrl-click to toggle, dragging from empty space to
box-select, dragging a selected object to move it, arrow keys to nudge by one
grid step, the wheel to zoom, Tab to cycle objects, and Escape to cancel and
leave.

## MVP Release Audit Status

Milestone 4 adds a release asset gate in
`scripts/validate_release_assets.py`. The gate does not replace manual assistive
technology testing, but it prevents release-candidate packages from omitting
the documented smoke paths for:

- keyboard-only setup completion, skip, and resume checks;
- high-contrast dark and high-contrast light preference coverage;
- text scale checks at 100%, 125%, 150%, 175%, and 200%;
- pseudo-localization and right-to-left Arabic/Urdu smoke checks;
- pluralization and translation expansion layout smoke checks;
- OS-backed TTS test phrase and task-result smoke coverage where the platform
  exposes an engine;
- non-color-only state names for project, package, compiler, AI, setup, and
  validation surfaces;
- accessible custom-widget metadata for loading panes, detail drawers, package
  trees, activity tasks, setup controls, preferences, and editor profile
  selectors. Level-map path, object, validation, preview, and compiler-profile
  controls are now part of the manual screen-reader spot-check scope. Advanced
  Studio shader, sprite, code, AI proposal, and extension controls are included
  in the same manual spot-check scope, as are the entity inspector and entity
  definition controls, the model viewport and its playback controls, and the
  package Compare button.

CI additionally runs the shell itself under an offscreen platform plugin with
`--self-test`, which builds every work surface once and pumps the event loop, so
a crash in widget construction or painting fails the build rather than waiting
for a manual pass.

## OS-Backed Text To Speech

**Status: preference only. No speech engine is wired up.**

`textToSpeechEnabled` is stored in `src/core/studio_settings.*`, shown as a
checkbox in preferences, reported by the CLI, and settable with `--set-tts`.
Nothing reads it back to speak. The build does not link `Qt TextToSpeech`, and
no `QTextToSpeech` instance exists anywhere in `src/`. Every target below is
therefore still outstanding, including the preferences test phrase.

VibeStudio should use OS-backed TTS through Qt TextToSpeech where available.
TTS should be useful without becoming noisy or mandatory.

Initial TTS targets:

- [ ] Read selected task summaries, compiler errors, package validation issues,
  AI proposals, and setup guidance.
- [ ] Announce long-running task completion or failure when enabled.
- [ ] Let users select voice, rate, pitch, volume, and enabled event categories
  where the OS engine exposes them.
- [ ] Provide a test phrase in setup and preferences.
- [ ] Keep visual/log equivalents for every spoken message.
- [ ] Never send private project content to cloud voice services unless the user
  explicitly chooses a cloud connector for that task.

## Localization Goals

VibeStudio should be localizable from the beginning, even while translations are
incomplete. Strings should be written so translators can succeed without code
changes.

Engineering requirements:

- [ ] All user-visible UI strings go through translation APIs.
- [ ] Avoid string concatenation that breaks grammar in translated languages.
- [ ] Support pluralization, gender-neutral phrasing where possible, and
  translator comments for technical terms.
- [x] Use `QLocale` for dates, numbers, sizes, durations, currencies, and
  collation.
- [x] Compile `.qm` catalogs during the build and install them with
  `QTranslator` at start-up.
- [x] Set the application layout direction from the selected locale, so Arabic
  and Urdu start the shell right-to-left.
- [ ] Audit each surface — including the painted charts and map viewport, which
  do not mirror automatically — under a right-to-left locale.
- [ ] Leave expansion room in layouts for longer translated text.
- [ ] Keep file formats, technical identifiers, paths, compiler flags, and code
  snippets untranslated unless they are explanatory prose.
- [ ] Allow language selection in setup and preferences, with a restart prompt
  only if live switching is not yet implemented.
- [x] Provide pseudo-localization and right-to-left test modes.

### Translation Context

`ApplicationShell` now declares `Q_OBJECT` and is run through `moc`
(`app_moc_headers` in `src/meson.build`), as are the map viewport, the model
viewport, asset views, charts, syntax highlighter, and command palette. Before
that the shell inherited `tr()` from `QMainWindow`, so its strings were
extracted and looked up under the `QMainWindow` context and could never match a
catalog entry written for the shell. Shell strings now resolve under their own
class context. Core modules that are not `QObject`s use explicit
`QCoreApplication::translate("VibeStudio…", …)` contexts instead —
`VibeStudioEditorProfiles`, `VibeStudioRuntime`, and siblings — so every string
has a stable, intentional context. The modules added this round follow the same
rule: `VibeStudioEntityDefinitions`, `VibeStudioModelMesh`,
`VibeStudioPackageCompare`, and `VibeStudioDocumentWatch`.

`ModelViewport` is listed in `app_moc_headers` alongside `MapViewport`, so the
strings it paints and reports — status lines, the accessible summary, and its
empty and no-geometry states — are extracted under their own class context
rather than a base class's.

Newer shell strings use Qt plural forms where a count is involved, for example
`tr("%n difference(s) between the two packages.", nullptr, differences)` and the
`ENTITIES [%1]` header on the Health tab. There are still no `//:` translator
comments anywhere in `src/app` or `src/core`, so the pluralization checklist
item above, which also covers translator comments for technical terms, stays
unticked.

This fixes the lookup, not the coverage. Strings are still added throughout the
shell, so the first checklist item above stays unticked until an extraction run
is clean.

### Catalog Loading

`installStudioTranslations()` in `src/app/studio_runtime.*` is called from
`main()` before the shell is constructed. It removes any previously installed
VibeStudio translator, then searches these directories in order:

1. `$VIBESTUDIO_I18N_DIR`, when set;
2. `<application dir>/i18n`;
3. `<application dir>/../i18n` (the development build layout);
4. `<application dir>/../share/vibestudio/i18n` (the installed layout);
5. `<application dir>/../../i18n` (the portable package layout);
6. `<working directory>/i18n`.

Within each directory it tries `vibestudio_<requested>.qm`, then the normalized
target id, then the base language, so `pt_BR` falls back to `pt`. The source
language (`en` or an empty locale) installs `vibestudio_en.qm`, which holds only
English plural forms (see below), and skips Qt's own catalogs. For any other
language, Qt's `qtbase_<locale>.qm` is installed alongside the studio catalog so
standard dialogs and buttons are translated too. Failures are collected as warnings and
the application continues in the source language rather than refusing to start.

`i18n/meson.build` compiles every checked-in `.ts` file with `lrelease` into
`<builddir>/i18n` and installs the results to
`<datadir>/vibestudio/i18n`. `lrelease` is optional: when it is not found, Meson
prints a message and the application runs in the source language.

`applyLayoutDirectionForLocale()` sets `Qt::RightToLeft` for right-to-left
locales — including ones outside the shipped target set — and is likewise called
from `main()`.

Both of these run once, at start-up. Changing the language in preferences
updates `QLocale::setDefault()` immediately, which changes number, date, and
size formatting, but it does **not** reinstall the translator or flip the layout
direction, and the shell does not yet show a restart prompt. That is why the
language-selection item above is still unticked.

### Catalog Contents

**English plural forms.** Source strings mark plurals the Qt way, for example
`tr("%n item(s)", nullptr, count)`, and Qt only resolves `%n` into a singular or
plural form through a translator. Without an English catalog the studio showed
"1 item(s)". `scripts/english_plurals.py --write` asks `lupdate -pluralonly` for
every plural message and writes English singular and plural forms into
`i18n/vibestudio_en.ts`, derived from the markup between `%n` and the next
`%1`..`%9` placeholder (`entr(y)(ies)`, `item(s)`, `match(es)`, and bare plurals
such as "%n entries") plus a short list of sentences whose verb also changes.
Without `--write` it checks that every plural message has finished forms, and
that check runs in the gate as `english-plurals-validation`.

**The other shipped catalogs are stubs, not translations.** Each other
`i18n/vibestudio_*.ts` file currently holds a single message in the
`VibeStudioLocalization` context, marked `type="unfinished"`. A compiled `.qm`
built from one of them therefore resolves almost nothing, and the UI renders in
the source language even when a translator is successfully installed and the
layout direction has flipped. Treat right-to-left runs as layout smoke tests,
not as localized builds.

The active localization scaffold lives in `src/core/localization.*` and is
shared by preferences, tests, CLI reports, and diagnostic bundles. It defines
the 20-language target registry, normalizes locale IDs, identifies Arabic and
Urdu as right-to-left smoke targets, generates pseudo-localized and expansion
stress samples, emits `QLocale` formatting and pluralization samples, checks
expanded text against representative shell layout budgets, resolves the catalog
root and the ordered `.qm` candidates for a locale, reports which `.ts`/`.qm`
pairs are actually present, and inspects Qt `.ts` catalogs for missing,
unfinished, obsolete, or vanished translations. Its pluralization report
distinguishes "the count was substituted" from "translated plural forms came
from a translator", so a stub catalog cannot read as a pass.
`scripts/extract_translations.py` dry-runs Qt `lupdate` against the source tree
and catalogs so extraction drift is visible before release.

## Initial Localization Set

The initial target set covers 20 predominant world languages by total speaker
reach, global distribution, development relevance, and writing-system coverage.
The list should be reviewed periodically against sources such as the
[Ethnologue 200](https://www.ethnologue.com/insights/ethnologue200/) and real
user demand.

| Code | Language | Notes |
| --- | --- | --- |
| en | English | Source language and fallback. |
| zh-Hans | Chinese, Simplified | Mandarin-focused Simplified Chinese UI. |
| hi | Hindi | Devanagari script coverage. |
| es | Spanish | Global Spanish localization. |
| fr | French | Global French localization. |
| ar | Arabic | Right-to-left layout and Arabic-script validation. |
| bn | Bengali | Bengali script coverage. |
| pt-BR | Portuguese, Brazil | Largest Portuguese localization target. |
| ru | Russian | Cyrillic script coverage. |
| ur | Urdu | Right-to-left Arabic-script validation. |
| id | Indonesian | Southeast Asia coverage. |
| de | German | Long-string layout stress case. |
| ja | Japanese | CJK layout and line-break validation. |
| pcm | Nigerian Pidgin | Broad West African reach; fallback strategy may begin with English-adjacent terminology. |
| mr | Marathi | Devanagari and Indic shaping coverage. |
| te | Telugu | Telugu script coverage. |
| tr | Turkish | Locale casing and terminology validation. |
| ta | Tamil | Tamil script coverage. |
| vi | Vietnamese | Diacritics and text rendering validation. |
| ko | Korean | Hangul and CJK-adjacent layout validation. |

A pseudo-localization catalog ships beside these 20 targets. The pipeline —
extraction, compilation, installation, locale formatting, and layout direction —
is proven end to end; the translations themselves are not written yet.

## Testing And Acceptance

- [ ] Run layout smoke tests at 100%, 125%, 150%, 175%, and 200% scale.
- [ ] Run high-contrast dark and high-contrast light smoke tests.
- [ ] Run keyboard-only setup and package/compiler workflow smoke tests.
- [ ] Run screen-reader metadata spot checks for shell, setup, preferences,
  package tree, activity center, compiler log, and editor profile controls.
- [ ] Run TTS smoke tests for enabled event categories.
- [x] Run pseudo-localization and right-to-left layout checks in CI or release
  validation.
- [x] Run pluralization and translation expansion layout smoke checks in CI or
  release validation.
- [x] Run a Qt Linguist extraction dry-run in local and CI validation.
- [ ] Track untranslated strings and stale translations as release blockers once
  a language is marked supported.
