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

## Accessibility Baseline
Accessibility behavior should be designed into every surface:
- [ ] High-contrast dark and high-contrast light themes.
- [ ] Text/UI scale support at 100%, 125%, 150%, 175%, and 200%.
- [ ] Comfortable, standard, and compact density presets.
- [ ] Reduced-motion setting for transitions and loading visuals.
- [ ] Keyboard-visible focus and no keyboard traps.
- [ ] Accessible names, roles, descriptions, and status changes for custom widgets.
- [ ] Screen-reader-readable task states, compiler diagnostics, validation results, and setup warnings.
- [ ] OS-backed TTS for selected summaries, errors, task outcomes, and setup guidance.
- [ ] No color-only status communication.

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
- [ ] Indeterminate activity indicators only when progress cannot be measured.
- [x] Task cards for background operations.
- [x] Activity center for queued/running/completed work.
- [x] Status chips for package, project, compiler, and install health.
- [ ] Toasts for short-lived confirmations.
- [x] Persistent problem panels for actionable warnings/errors.
- [x] Expandable logs for compiler, package, AI, and CLI-backed operations.
- [x] Cancellation controls when an operation can safely stop.

## Progressive Disclosure
VibeStudio should present a clear summary first, then let users delve into
detail:
- [x] Package summary before raw entry tables.
- [ ] Compiler status before raw stdout/stderr.
- [x] Project health before validation traces.
- [ ] Asset preview before byte-level metadata.
- [ ] Shader stage graph before raw shader text, while preserving round-trip access.
- [ ] AI proposal summary before prompts, context, and generated commands.
- [ ] Agentic workflow plan before tool calls, writes, generated assets, or validation loops.
- [x] Friendly error summary before stack traces or diagnostic dumps.

## Creative Graphical Communication
Graphical elements should help users decide and act:
- [x] Package composition charts for file types and sizes.
- [ ] Asset dependency graphs for textures, shaders, models, maps, and packages.
- [ ] Compiler pipeline diagrams for source map to output artifacts.
- [ ] Map health overlays for leaks, missing textures, entity problems, and compile warnings.
- [ ] Shader stage diagrams for idTech3 material flow.
- [x] Timeline views for task history.
- [ ] Visual diff/staging views for package writes.

## Detail Surfaces
Advanced users should be able to inspect:
- [x] Raw package metadata.
- [x] Virtual paths and physical source paths.
- [ ] Parsed format structures.
- [x] Compiler command manifests.
- [x] Compiler stdout/stderr.
- [x] Hashes and reproducibility manifests.
- [ ] AI prompts, selected context, responses, and proposed tool calls.
- [ ] AI provider, model, connector capability, cost/usage metadata where available, and generated asset provenance.
- [x] CLI-equivalent command for GUI compiler actions.

## MVP UX Requirements
- [x] Every package open shows loading feedback.
- [x] Every compiler run creates a visible task with progress, logs, result, and output paths.
- [x] Every validation run has a summary and expandable details.
- [x] Every extract/package operation reports exact output paths.
- [ ] Every destructive or write operation is staged, previewed, or confirmed.
- [x] Empty states say what the user can do next.
- [x] Errors avoid dead ends.

Milestone 4 adds durable recent task history for completed package/compiler
operations and a release validation script that checks loading/progress,
summary/detail, graphical summary, high-visibility, keyboard, localization, and
TTS smoke coverage before a release-candidate package can pass.

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
