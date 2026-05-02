# Initial Setup Flow

VibeStudio should include a modern first-run setup flow that lets users tailor
the overall application ecosystem to their needs before serious work begins.
The setup flow should be fast, skippable, resumable, accessible, and honest
about what has been detected automatically.

The setup flow is not a marketing tour. It is a practical configuration
workbench for accessibility, language, game installations, projects, editor
profiles, compilers, AI connectors, CLI use, and workflow preferences.

## Principles

- Start with accessibility and language so users can comfortably complete the
  rest of setup.
- Detect what can be detected, but never make destructive or private choices
  without confirmation.
- Keep every step skippable and resumable.
- Explain what was found, what was not found, and what can be configured later.
- Offer role-based presets without hiding advanced configuration.
- Save setup as editable preferences, not one-time decisions.
- Make AI opt-in and AI-free mode obvious.
- Provide a final review screen with tests, warnings, and next actions.

## Setup Steps

### 1. Welcome And Access

- [ ] Choose language.
- [ ] Choose UI scale and font size.
- [ ] Choose theme: system, dark, light, high-contrast dark, high-contrast
  light.
- [ ] Choose reduced motion.
- [ ] Enable or skip OS-backed TTS.
- [ ] Test TTS output if enabled.
- [ ] Show keyboard navigation help and allow keyboard-only completion.

### 2. Workflow Role

- [ ] Choose primary role presets: mapper, package maintainer, artist,
  programmer/scripter, shader author, audio creator, release maintainer,
  all-in-one, or custom.
- [ ] Choose experience level: guided, standard, expert.
- [ ] Choose default detail level for logs, metadata, and inspector panes.

### 3. Editor Familiarity

- [x] Choose level-editor profile: VibeStudio default, GtkRadiant 1.6.0-style,
  NetRadiant Custom-style, TrenchBroom-style, QuArK-style, or decide later.
- [x] Preview key differences: layout, camera, grid, selection, clipping,
  shortcuts, and terminology.
- [ ] Import or skip shortcut/profile files where supported later.

### 4. Game Installations

- [x] Auto-detect Steam and GOG install candidates.
- [x] Let users skip game installation setup and resume it later.
- [ ] Detect source ports where possible.
- [x] Let users add installs manually.
- [x] Validate expected packages/executables.
- [x] Let users import detected candidates only after confirmation.
- [x] Let users mark installs as read-only, active, hidden, or later in stored
  profile data.

### 5. Projects And Packages

- [ ] Open existing project.
- [ ] Create project from folder.
- [ ] Create empty project.
- [ ] Mount folders, WADs, PAKs, PK3s, and package roots.
- [ ] Choose output, temp, backup, and export directories.
- [x] Explain and persist project-local settings overrides in the manifest.

### 6. Toolchains And Build

- [ ] Detect bundled and system compiler tools.
- [ ] Configure ericw-tools, q3map2, ZDBSP, ZokumBSP, and source-port launchers.
- [ ] Choose default compiler profiles by engine family.
- [ ] Run a harmless toolchain probe and show results.
- [ ] Enable command manifests and output-path reporting.

### 7. AI And Automation

- [x] Choose AI-free mode, configure later, or configure now.
- [x] Configure provider connectors: OpenAI, Claude, Gemini, ElevenLabs, Meshy,
  local/offline, or custom connector.
- [x] Choose preferred provider per capability when configured.
- [ ] Explain what project context may be sent to providers.
- [ ] Configure consent, redaction, cost/usage display, and generated-asset
  provenance.
- [x] Enable or skip agentic workflows.

### 8. CLI And Integration

- [ ] Offer CLI path setup instructions.
- [ ] Show current CLI executable path.
- [ ] Enable shell integration instructions where appropriate.
- [ ] Configure default JSON/text output preference for copied commands.
- [ ] Offer file association and shell-open integration where supported.

### 9. Review And Finish

- [ ] Show summary of accessibility, language, theme, editor profile, installs,
  projects, compilers, AI mode, CLI, and warnings.
- [ ] Run optional smoke checks.
- [ ] Save settings.
- [ ] Offer export of setup profile.
- [x] Open workspace dashboard with detected next actions.

## Setup Profiles

Setup should produce an editable profile that can be exported, imported, or
project-overridden.

Profile areas:

- Accessibility and language.
- Theme, scale, density, motion, and TTS.
- Editor profile and shortcuts.
- Game installations and source ports.
- Project defaults and package mount preferences.
- Compiler and launch profiles.
- AI connector configuration and AI-free mode.
- CLI and automation defaults.

## MVP Acceptance

- [x] First-run setup appears on a clean profile and can be skipped.
- [x] Accessibility/language choices are available before visual-heavy setup.
- [x] High-contrast and scaling choices apply immediately through the shared
  preferences path.
- [x] Game installation setup supports auto-detect, manual add, skip, and later.
- [x] Editor profile selection is present even if only placeholders exist.
- [x] AI-free mode is visible and selectable.
- [x] OpenAI-first AI connector settings are represented without blocking
  non-OpenAI connector design.
- [x] Setup summary explains incomplete, skipped, failed, successful, warning,
  install, toolchain, AI, and CLI states.
- [x] Setup choices are editable in preferences after completion.

## Current Scaffold Slice

The current shell includes a persistent first-run setup panel backed by
`QSettings`. It tracks not-started, in-progress, skipped, and completed states;
stores the current setup step; exposes skip, resume, next, finish, and reset
actions; and shows a summary with completed, pending, and warning items. The
same state is available from the CLI through `--setup-report`,
`--setup-start`, `--setup-step`, `--setup-next`, `--setup-skip`,
`--setup-complete`, and `--setup-reset`.

The full guided setup flow remains planned. Steam/GOG game installation
detection, manual install profiles, editor profile selection, language/theme/
scale/density/reduced-motion/TTS preferences, AI connector preference storage,
CLI setup reports, and setup summaries are active scaffold slices. Source-port
detection, live TTS playback, deeper project/package mounting, and richer
toolchain probes remain represented as setup steps, preferences, release smoke
checks, or warnings until their dedicated roadmap slices land.
