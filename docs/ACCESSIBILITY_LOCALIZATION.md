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
  clip text at 100%, 125%, 150%, 175%, and 200% scale.
- [ ] Use icons plus accessible labels/tooltips for key actions.
- [x] Pair color with a glyph, hatch, stroke, or text cue for status in the
  charts, status chips, and map viewport.
- [ ] Audit every remaining surface for color-only state.
- [ ] Provide visible focus states for keyboard and assistive-technology users.
- [x] Provide a reduced-motion preference that is stored, applied at start-up,
  and settable from preferences and the CLI.
- [ ] Apply reduced motion to animations, transitions, and timeline effects.
- [ ] Provide preview checks for maps, textures, sprites, shaders, and package
  summaries under high-visibility themes.

### High-Visibility Themes

`StudioTheme` in `src/core/studio_settings.h` now carries `HighContrastDark` and
`HighContrastLight` beside `System`, `Dark`, and `Light`. Selecting either one
rebuilds the shell stylesheet from a black-on-white or white-on-black token set
and propagates a `highContrast` flag into every painted widget: the map
viewport, the image/palette/waveform asset views, the composition, pipeline, and
timeline charts, and the code syntax highlighter.

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
- Status-bar chips render a bracketed text cue in front of the label
  (`[Completed] Package`, `[No project] Project`), carry the same text in an
  accessible description, and expose the state as a style property rather than
  as a raw color.
- The map viewport marks selection with a ring plus a crosshair and a text
  label, draws unsolved brushes with a dashed pen and a cross, and separates
  patches with a dash-dot stroke, so shape and stroke carry the meaning.

### Reduced Motion

The reduced-motion preference is stored, settable from preferences and from
`--set-reduced-motion`, and read back at start-up. Its effect today is narrow
because the shell has no animation framework at all — there is no
`QPropertyAnimation`, `QVariantAnimation`, `QTimeLine`, or `QMovie` anywhere in
the source tree. Two surfaces honor it:

- `LoadingPane` swaps its indeterminate (marquee) progress bar for a static
  determinate bar while a busy state is in progress.
- The map viewport draws the selection ring solid instead of dashed, because
  fine dashes shimmer while panning.

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
- [x] The charts, the map viewport, and the asset preview views expose
  accessible names and keep an accessible description in sync with their
  contents, so their data is readable as text.
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
  in the same manual spot-check scope.

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
(`app_moc_headers` in `src/meson.build`), as are the map viewport, asset views,
charts, syntax highlighter, and command palette. Before that the shell inherited
`tr()` from `QMainWindow`, so its strings were extracted and looked up under the
`QMainWindow` context and could never match a catalog entry written for the
shell. Shell strings now resolve under their own class context. Core modules
that are not `QObject`s use explicit
`QCoreApplication::translate("VibeStudio…", …)` contexts instead —
`VibeStudioEditorProfiles`, `VibeStudioRuntime`, and siblings — so every string
has a stable, intentional context.

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
language (`en` or an empty locale) installs no catalog at all. When a catalog
loads, Qt's own `qtbase_<locale>.qm` is installed alongside it so standard
dialogs and buttons are translated too. Failures are collected as warnings and
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

**The shipped catalogs are stubs, not translations.** Each
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
