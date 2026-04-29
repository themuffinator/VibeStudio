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
  OS-backed text-to-speech engines.
- [Qt internationalization](https://doc.qt.io/qt-6/internationalization.html),
  `QTranslator`, `QLocale`, and Qt Linguist tooling for translation, locale
  formatting, pluralization, Unicode, bidirectional text, and writing-system
  support.

## Visual Accessibility

Required settings and behavior:

- [ ] Follow OS font and scaling defaults on first launch.
- [ ] Support application text scale presets: 100%, 125%, 150%, 175%, 200%, and
  a custom scale path where practical.
- [ ] Support high-visibility themes: high-contrast dark, high-contrast light,
  color-blind-aware status palette, and reduced-saturation option.
- [ ] Support UI density presets: comfortable, standard, compact.
- [ ] Ensure toolbars, tabs, cards, inspectors, dialogs, and status chips do not
  clip text at 100%, 125%, 150%, 175%, and 200% scale.
- [ ] Use icons plus accessible labels/tooltips for key actions.
- [ ] Never communicate state through color alone; pair color with text, icon,
  shape, pattern, or position.
- [ ] Provide visible focus states for keyboard and assistive-technology users.
- [ ] Provide reduced-motion settings for animations, transitions, loading
  visuals, and timeline effects.
- [ ] Provide preview checks for maps, textures, sprites, shaders, and package
  summaries under high-visibility themes.

## Keyboard, Screen Reader, And Assistive Tool Support

Required behavior:

- [ ] Every command reachable from menus, command palette, keyboard shortcuts,
  or focusable controls.
- [ ] No keyboard traps in modals, dock widgets, editors, setup flow, or preview
  panes.
- [ ] Consistent tab order in setup, preferences, inspectors, and task details.
- [ ] Custom widgets expose accessible names, descriptions, roles, values, and
  state changes.
- [ ] Task progress, warnings, failures, prompts, and completion states are
  available to assistive tools, not just visually rendered.
- [ ] The CLI provides accessible plain-text output and machine-readable JSON.
- [ ] Editor profile controls document keyboard/mouse changes clearly and expose
  reset/revert actions.

## OS-Backed Text To Speech

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
- [ ] Use `QLocale` for dates, numbers, sizes, durations, currencies, and
  collation.
- [ ] Support bidirectional layouts and text for Arabic and Urdu.
- [ ] Leave expansion room in layouts for longer translated text.
- [ ] Keep file formats, technical identifiers, paths, compiler flags, and code
  snippets untranslated unless they are explanatory prose.
- [ ] Allow language selection in setup and preferences, with a restart prompt
  only if live switching is not yet implemented.
- [ ] Provide pseudo-localization and right-to-left test modes.

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

This list is a product target, not a claim that all translations are complete
at MVP. MVP should at minimum prove the localization pipeline, pseudo-localized
UI, and at least a small verified translation slice.

## Testing And Acceptance

- [ ] Run layout smoke tests at 100%, 125%, 150%, 175%, and 200% scale.
- [ ] Run high-contrast dark and high-contrast light smoke tests.
- [ ] Run keyboard-only setup and package/compiler workflow smoke tests.
- [ ] Run screen-reader metadata spot checks for shell, setup, preferences,
  package tree, activity center, compiler log, and editor profile controls.
- [ ] Run TTS smoke tests for enabled event categories.
- [ ] Run pseudo-localization and right-to-left layout checks in CI or release
  validation.
- [ ] Track untranslated strings and stale translations as release blockers once
  a language is marked supported.
