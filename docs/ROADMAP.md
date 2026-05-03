# VibeStudio Roadmap

This roadmap turns the product vision into measurable, task-oriented work. It
uses a gradual improvement philosophy: ship thin vertical slices early, validate
them with real idTech projects, then widen and deepen each surface.

## Ultimate Goal

VibeStudio should become the definitive open-source development studio for
idTech1, idTech2, and idTech3 games: one seamless, modern, cross-platform,
user-friendly, adaptable, and efficient environment where a creator can manage
game installations, edit assets and maps, run compilers, package content, test
in-game, and maintain a complete mod or standalone project without constantly
switching tools.

In practical terms, the long-term goal is:

- [ ] A new user can point VibeStudio at their games and projects, then start working within minutes.
- [ ] A mapper can build, inspect, fix, package, and launch a playable map from one workspace.
- [ ] An artist can create or edit textures, sprites, models, audio, and shaders with game-aware previews.
- [ ] A programmer or scripter can edit code/scripts/configs with project-aware search and diagnostics.
- [ ] A release maintainer can validate packages, preserve credits, generate manifests, and ship reproducible builds.
- [ ] Users can choose familiar level-editor profiles inspired by GtkRadiant 1.6.0, NetRadiant Custom, TrenchBroom, and QuArK.
- [ ] Users always know what VibeStudio is doing, what is queued, what succeeded, what failed, and where output went.
- [ ] Users can start simple and delve into deeper metadata, logs, graphs, manifests, and raw format details when needed.
- [ ] Users can tailor language, accessibility, theme, editor profile, game installs, projects, compilers, AI, CLI, and automation through a complete modern setup flow.
- [ ] Users can work comfortably with high-visibility themes, UI/text scaling, keyboard navigation, assistive tools, OS-backed TTS, and localized UI.
- [ ] Advanced users can adapt the studio through profiles, toolchain settings, plugins, AI-assisted automation, and external compiler wrappers.
- [ ] Power users and CI systems can drive the same project workflows through a full-featured CLI.

## North-Star Metrics

These metrics define whether VibeStudio is moving toward the goal. When a task
changes behavior, update or add a metric-backed test where practical.

### Seamless Workflow
- [ ] Time from first launch to a usable project workspace: target under 5 minutes for a common Steam/GOG/manual install.
- [ ] Open project/package/install context is shared across package, preview, compiler, and launch surfaces.
- [ ] A compile failure links to the relevant map/script/package context when the compiler provides enough information.
- [ ] Common workflows avoid duplicate file picking after a project is configured.

### Modern UI
- [ ] Primary shell supports dense studio workflows without modal-first navigation.
- [ ] Core actions have consistent icons, labels, tooltips, shortcuts, disabled states, and status feedback.
- [ ] Text does not clip or overlap at 100%, 125%, 150%, and 200% scale on Windows, macOS, and Linux.
- [ ] Dark theme is readable for long sessions, with accessible contrast for primary text and controls.
- [ ] Every main workflow has summary-first UI with detail-on-demand panels.
- [ ] Graphical status elements communicate real project/package/compiler/asset state rather than decoration.

### Cross-Platform Quality
- [ ] CI builds and tests pass on Windows, macOS, and Linux.
- [ ] Installer/portable artifacts are produced for all three platforms before public MVP.
- [ ] Project, package, and compiler paths work with spaces, Unicode, long paths where supported, and platform path separators.
- [ ] Platform-specific integration is optional and guarded: file associations, launchers, crash handling, updater, and shell-open.

### User Friendliness
- [ ] First-run flow supports auto-detect, manual setup, and skip/later paths.
- [ ] First-run flow exposes language, scale, high-visibility, reduced motion, TTS, role, editor profile, installs, projects, compilers, AI, and CLI settings.
- [ ] Dangerous actions use staging, previews, undoable state, backups, or explicit confirmation.
- [ ] Error messages explain what failed, where, and the next practical action.
- [ ] Built-in help can be generated from user-facing docs before public MVP.
- [ ] Any operation over roughly 250 ms gives visible feedback if it blocks an active pane.
- [ ] Any background task over roughly 1 second appears in an activity surface with status.
- [ ] Completed write/export/compiler operations report exact output paths.
- [ ] Cancelable operations expose cancellation and report cleanup state.

### Accessibility And Localization
- [ ] UI follows OS font/scaling defaults and supports 100%, 125%, 150%, 175%, and 200% app scale checks.
- [ ] High-contrast dark and high-contrast light themes are available from setup and preferences.
- [ ] Color-blind-aware status palette and non-color-only state indicators are used across project, package, compiler, AI, and validation surfaces.
- [ ] Core shell, setup, package tree, activity center, compiler log, preferences, and editor profile controls support keyboard-only navigation.
- [ ] Custom widgets expose accessible names, roles, descriptions, focus, values, and state changes.
- [ ] OS-backed TTS can read selected summaries, diagnostics, setup guidance, and task completion/failure events.
- [ ] Reduced motion setting affects transitions, loading visuals, and timeline effects.
- [x] Translation pipeline supports pseudo-localization, right-to-left checks, pluralization, locale formatting, and stale-string reporting.
- [x] Initial localization target set covers the 20 languages documented in `docs/ACCESSIBILITY_LOCALIZATION.md`.

### User-Visible Progress And Detail
- [ ] Global activity center shows queued, running, completed, warning, failed, and cancelled tasks.
- [ ] Package open, extraction, indexing, preview generation, compiler runs, validation, AI requests, and saves have explicit state transitions.
- [ ] Loading panes use skeletons/placeholders with context-specific labels.
- [ ] Task logs are available from both GUI and CLI-backed workflows.
- [ ] Users can copy or export details for support, bug reports, and reproducibility.

### Adaptability
- [ ] Game profiles are data-driven and can be edited without code changes.
- [ ] Compiler profiles are data-driven and can invoke bundled, system, or project-local tools.
- [ ] Level-editor layout/control profiles can switch without changing map data or forking editor logic.
- [ ] GtkRadiant 1.6.0, NetRadiant Custom, TrenchBroom, and QuArK-style profiles each cover layout, camera, selection, grid, and shortcut expectations.
- [ ] Package and format support is modular, with fixture-backed tests for each reader/writer.
- [ ] External tools/plugins can declare inputs, outputs, capabilities, and trust boundaries.

### AI-Assisted Workflows
- [x] AI connector integration is optional, clearly configured, and disabled by default.
- [x] OpenAI, Claude, Gemini, ElevenLabs, Meshy, local/offline, and custom connector paths can be represented by one provider-neutral capability model.
- [x] Users can select preferred providers per capability: reasoning, coding, vision, image, audio, voice, 3D, embeddings, and local/offline.
- [ ] Prompt-based workflows produce reviewable plans, staged changes, command manifests, or diffs before writing.
- [ ] Agentic workflows expose plan, context, tool calls, staged changes, validation, cancellation, and final summary.
- [ ] AI actions call explicit VibeStudio tools for package scans, compiler runs, text edits, and project changes.
- [ ] AI activity logs redact secrets and show what project context was used.
- [ ] AI-free mode is complete for core editing, packaging, compiling, validation, launch/testing, and CLI automation.
- [ ] At least one workflow explains compiler logs and proposes a reproducible next command.

### Full-Featured CLI
- [ ] GUI and CLI commands share the same service layer for projects, packages, compilers, validation, and automation.
- [x] CLI supports human-readable and JSON output.
- [ ] CLI commands support dry-run or staged behavior for destructive operations.
- [x] CLI help is generated or validated against documented command coverage.
- [x] CLI is capable enough for CI package validation and compiler smoke tests.

### Efficiency And Robustness
- [ ] Startup target for MVP: under 2 seconds to shell on a warm desktop machine without indexing.
- [ ] UI remains responsive during archive loading, previews, indexing, and compiler runs.
- [ ] Common post-setup development loop target: changed asset/map/script to compiler run in under 30 seconds of user interaction for common workflows.
- [ ] Common compiler failure loop target: failure to actionable summary in under 10 seconds after process exit where logs are available.
- [ ] Avoid repeated file picking after project setup; project, install, package, compiler, and output paths should be remembered and reusable.
- [ ] Batch operations and presets exist for repeated package, conversion, compiler, and validation workflows.
- [ ] Large packages are loaded incrementally; avoid full extraction unless a workflow requires it.
- [ ] Parser and package code is fuzzable or fixture-tested before supporting write-back.
- [ ] Compiler runs capture command, environment, duration, exit code, output files, and hashes.

## MVP Definition

The fastest valuable MVP is not the full editor suite. The MVP is an integrated
idTech project workbench that proves the end-to-end loop:

1. Detect or configure a game installation.
2. Tailor language, accessibility, theme, editor profile, AI mode, CLI, and core preferences.
3. Open a project folder or package.
4. Browse package/project contents.
5. Preview common assets.
6. Edit project text assets safely.
7. Run at least one compiler/toolchain profile.
8. Capture diagnostics and produced files.
9. Package or stage the result.
10. Launch or reveal the output for in-game testing.
11. Run the same core workflow from the CLI for automation.
12. See loading/progress/results for every long-running step.
13. Open detailed logs/manifests/metadata when the summary is not enough.

MVP exit criteria:

- [ ] Runs on Windows, macOS, and Linux from CI-built artifacts.
- [ ] Supports at least one complete idTech2/Quake-family compile loop using ericw-tools.
- [ ] Supports at least one Doom-family node-building loop using ZDBSP or ZokumBSP.
- [ ] Supports at least one idTech3/q3map2 diagnostic or compile loop.
- [ ] Supports package browsing for folders, PAK, WAD, and PK3/ZIP-family archives.
- [ ] Supports previews for core text, image, audio metadata/playback where available, and model/map metadata.
- [ ] Provides project-level logs, diagnostics, and reproducible command manifests.
- [ ] Provides global activity/task feedback with output paths and expandable logs.
- [ ] Provides progressive disclosure for package metadata, compiler logs, project validation, and asset details.
- [ ] Provides at least one useful graphical project/package/compiler summary.
- [ ] Provides CLI coverage for project info, package inspection, validation, compiler runs, and command manifests.
- [x] Provides at least a documented first pass of editor profile architecture, even if full profile fidelity lands after MVP.
- [ ] Provides first-run setup with language, accessibility, high-visibility, scaling, editor profile, install/project/compiler, AI-free, and CLI choices.
- [ ] Provides high-visibility themes, app scaling settings, keyboard-accessible MVP flows, and OS-backed TTS architecture.
- [x] Provides localization pipeline proof with pseudo-localization, right-to-left smoke checks, and initial translation catalog structure.
- [ ] Provides opt-in AI documentation and a safe architecture path; AI implementation may remain experimental after MVP.
- [x] Preserves credits and third-party license visibility in README, docs, About, and release bundles.

## Gradual Improvement Rules

- [ ] Prefer vertical slices over isolated subsystems.
- [ ] Each milestone should leave the app more usable than before.
- [ ] Build read-only support before write support.
- [ ] Build CLI and tests alongside GUI behavior.
- [ ] Port PakFu functionality in small credited modules with tests.
- [ ] Integrate external compilers by process execution first; source-level integration comes only after license and maintenance review.
- [ ] Implement editor profile behavior as configuration over shared commands before adding profile-specific code.
- [ ] Treat AI-assisted workflows as proposals over deterministic tools, not as hidden direct mutation.
- [ ] Treat agentic AI as supervised workflow acceleration: plan, review, stage, validate, summarize.
- [ ] Preserve an AI-free/manual path for every core workflow.
- [ ] Prefer reusable presets, manifests, templates, and batch actions when users repeat a workflow.
- [ ] Add CLI coverage alongside every workflow that can reasonably run headless.
- [ ] Treat user-visible state as part of the feature, not polish.
- [ ] Treat accessibility and localization as part of every UI feature.
- [ ] Make setup choices editable later; first-run setup should never be the only place to configure a core behavior.
- [ ] Build summary-first/detail-on-demand UI before adding advanced-only panels.
- [ ] Prefer graphical communication when it clarifies real project state, relationships, or workflow progress.
- [ ] Mark rough edges in docs and issues rather than blocking useful slices.
- [ ] Measure performance before large refactors.

## Milestone 0: Foundation Hardening

Goal: make the repository easy to build, test, credit, and extend.

### Build And CI
- [x] Create Meson/Qt6 C++20 scaffold.
- [x] Add minimal Qt Widgets shell.
- [x] Add CLI diagnostics.
- [x] Add cross-platform CI workflow.
- [x] Add compiler-submodule verification workflow.
- [x] Add release/nightly workflow skeleton adapted from PakFu.
- [x] Add `scripts/validate_docs.py` for link, credits, localization catalog, and submodule checks.
- [x] Add `scripts/validate_source_layout.py` for required directories and generated-file exclusions.
- [x] Add CI artifact retention for app binaries from PR builds.

### Documentation
- [x] Add AGENTS rules.
- [x] Add README with PakFu lineage and compiler imports.
- [x] Add credits document.
- [x] Add architecture, dependencies, support matrix, compiler integration, and installation docs.
- [x] Replace broad roadmap with metric-driven roadmap.
- [x] Add editor profile, AI automation, and CLI strategy docs.
- [x] Add stack decision record.
- [x] Add efficiency philosophy.
- [x] Add accessibility/localization philosophy.
- [x] Add initial setup flow philosophy.
- [x] Add contribution guide for task sizing, attribution, and fixture expectations.
- [x] Add issue templates for feature, bug, format support, compiler integration, and attribution updates.

### Product Shell
- [x] Add mode rail and placeholder studio shell.
- [x] Add persistent settings storage.
- [x] Add recent projects list.
- [x] Add global status/log panel.
- [x] Add global activity center with task list, progress, result, warnings, failures, and cancellation.
- [x] Add reusable operation-state model for idle/loading/running/warning/failed/cancelled/completed states.
- [x] Add reusable loading/skeleton components for panes and previews.
- [x] Add reusable detail drawer pattern for logs, metadata, manifests, and raw diagnostics.
- [x] Add status chips for project, package, compiler, install, and AI states.
- [x] Add command palette shell.
- [x] Add keyboard shortcut registry.
- [x] Add interaction profile registry placeholder.
- [x] Add accessibility settings placeholder: theme, scale, density, reduced motion, and TTS.
- [x] Add language/locale settings placeholder.
- [x] Add first-run setup shell with skip/resume behavior.
- [x] Add AI integration disabled/experimental settings placeholder.

### Visual Communication Foundation
- [ ] Add graph/diagram widget decision record: Qt Graphics View, custom widgets, or future scene graph.
- [ ] Add renderer abstraction decision record covering QPainter, QOpenGLWidget MVP previews, and bgfx production viewport goals.
- [x] Add project health summary placeholder.
- [x] Add package composition summary placeholder.
- [x] Add compiler pipeline summary placeholder.
- [ ] Add task timeline placeholder.
- [x] Define icon/color semantics for success, warning, failure, running, paused, cancelled, local, cloud, staged, and read-only.

Exit criteria:
- [ ] New checkout builds with one documented command per platform.
- [x] CI proves build/test/docs/submodule/sample checks.
- [ ] Contributors can identify current MVP tasks from this roadmap.

## Milestone 1: PakFu Core Migration MVP

Goal: make VibeStudio useful as a safe package/project browser before deeper
editors arrive.

### Core Package Abstractions
- [x] Port or adapt PakFu archive interfaces with attribution.
- [x] Add path safety and normalized virtual paths.
- [x] Add folder package session.
- [x] Add PAK reader.
- [x] Add WAD reader.
- [x] Add ZIP/PK3 reader.
- [x] Add package entry metadata model: path, size, modified time, type hints, source package.
- [x] Add nested package detection as metadata, with mounting deferred if needed.

### Read-Only GUI
- [x] Add package/project tree view.
- [x] Add entry list/details view.
- [x] Add search/filter for paths and type hints.
- [x] Add package loading state with progress when entry count is known.
- [x] Add package scan task card in activity center.
- [x] Add preview pane for text.
- [x] Add preview pane for basic images.
- [x] Add metadata preview for unknown/binary entries.
- [x] Add summary/detail split for entries: friendly overview first, raw metadata on demand.
- [x] Add package composition graphic by type and size.
- [x] Add extract selected/all workflow.
- [x] Add extraction progress, cancellation, completion summary, and exact output paths.

### CLI
- [x] Add `--list`.
- [x] Add `--info`.
- [x] Add `--extract`.
- [x] Add `--validate-package`.
- [x] Add JSON output mode for automation.
- [x] Add stable exit code definitions.
- [x] Add dry-run/staged write conventions before write support lands.

### Tests
- [x] Add tiny fixture PAK.
- [x] Add tiny fixture WAD.
- [x] Add tiny fixture PK3.
- [x] Add path traversal and duplicate-path tests.
- [x] Add CLI validation for package commands.
- [x] Add package loading/progress state tests.
- [x] Add extract output-path reporting tests.

Exit criteria:
- [x] User can open folder/PAK/WAD/PK3, inspect entries, preview simple content, and extract safely.
- [x] No write-back support exists until fixture tests and staging model are ready.

## Milestone 2: Project And Installation Workbench

Goal: make VibeStudio understand games and projects, not just individual files.

### Project Model
- [x] Define `.vibestudio/project.json` schema.
- [x] Add project create/open/save.
- [x] Add project root, source folders, package folders, output folders, and temp folders.
- [x] Add project-local settings override layer.
- [x] Add migration/version field for project schema.
- [x] Add project validation command.

### Game Installations
- [x] Port/adapt PakFu game profile model with attribution.
- [x] Add manual installation profile creation.
- [x] Add Steam detection.
- [x] Add GOG detection.
- [x] Add profile validation against expected base packages/executables.
- [x] Add per-profile palette and engine-family defaults.
- [x] Add first-run installation flow with skip/later path.

### Workspace UX
- [x] Add workspace dashboard for active project, install, packages, recent files, and tasks.
- [x] Add project problems panel.
- [x] Add global search across mounted packages and project files.
- [x] Add changed/staged files panel.
- [x] Add reveal-in-folder and copy-virtual-path actions.
- [x] Add project health summary with install/package/compiler status.
- [x] Add project dependency graph placeholder.
- [x] Add recent activity timeline.
- [x] Add empty states for no project, no install, no packages, no compiler, and no recent tasks.
- [x] Add detail drawers for project manifest, install validation, and mounted package roots.

Exit criteria:
- [x] User can configure one game install, open one project, mount packages, and see project/package context, health, loading state, and next actions everywhere.

## Milestone 3: Compiler Orchestration MVP

Goal: prove the end-to-end build/test loop without waiting for full native
editors.

### Compiler Discovery
- [x] Add compiler registry model.
- [x] Add bundled submodule source metadata.
- [x] Add user-configured executable paths.
- [x] Add project-local compiler overrides.
- [x] Add compiler version probing.
- [x] Add capability flags for Doom node builders, ericw-tools, and q3map2.

### Command Manifests
- [x] Define compiler run manifest schema.
- [x] Record command, working directory, environment subset, inputs, outputs, duration, exit code, and hashes.
- [x] Add manifest save/load.
- [x] Add re-run previous command.
- [x] Add copy command line action.

### Toolchain Slices
- [x] Add ericw-tools profile: `qbsp`.
- [x] Add ericw-tools profile: `vis`.
- [x] Add ericw-tools profile: `light`.
- [x] Add ZDBSP profile for a selected WAD/map.
- [x] Add ZokumBSP profile for a selected WAD/map.
- [x] Add q3map2 info/help/probe profile.
- [x] Add q3map2 compile profile for a simple `.map`.

### Diagnostics
- [x] Capture stdout/stderr in task log.
- [x] Parse warnings/errors opportunistically.
- [x] Link diagnostics to files when paths are present.
- [x] Add task cancellation.
- [x] Add output file registration in project tree.
- [x] Show compiler run as activity-center task with stage, duration, result, and output paths.
- [x] Add compiler pipeline graphic: source map -> compile stages -> output artifacts.
- [x] Add summary-first compiler result with expandable raw stdout/stderr.
- [x] Add "copy CLI equivalent" and "copy manifest" actions.

Exit criteria:
- [x] User can run at least one compile/build command for each engine family from VibeStudio, watch progress, inspect summaries/logs, locate outputs, and keep a reproducible record.

## Milestone 3A: CLI Parity Backbone

Goal: make the CLI elegant, scriptable, and backed by the same services as the
GUI before workflows sprawl.

### Command Architecture
- [x] Define command router and subcommand hierarchy.
- [x] Evaluate CLI11 and adopt a testable in-process command registration layer now; keep full CLI11 parser/completion adoption deferred until the broader command surface justifies the dependency.
- [x] Define shared output writer for text and JSON.
- [x] Define shared error and exit-code contract.
- [x] Define command manifest writer.
- [x] Add service-layer tests that run through CLI commands.

### MVP Command Families
- [x] `project info`
- [x] `project validate`
- [x] `install list`
- [x] `package info`
- [x] `package list`
- [x] `package validate`
- [x] `compiler list`
- [x] `compiler run`
- [x] `compiler manifest`
- [x] `credits validate`

### UX And Automation
- [x] Add `--json`.
- [x] Add `--quiet`.
- [x] Add `--verbose`.
- [x] Add `--dry-run`.
- [x] Add `--manifest <path>`.
- [x] Add `--watch` or equivalent streaming output mode for long-running tasks.
- [x] Add machine-readable task state output for automation.
- [x] Add examples for PowerShell and POSIX shells.

Exit criteria:
- [x] MVP package/project/compiler workflows can be demonstrated without opening the GUI.

## Milestone 3B: AI Automation Experiments

Goal: embrace generative and agentic workflows safely and early without making
MVP depend on cloud AI.

### Provider And Configuration
- [x] Add opt-in AI settings model.
- [x] Add provider-neutral AI connector abstraction.
- [x] Add connector capability model for reasoning, code, vision, image, audio, voice, 3D, embeddings, tool calls, streaming, local/offline execution, cost/usage, and privacy notes.
- [x] Implement OpenAI as the first general-purpose connector.
- [x] Add design stubs for Claude, Gemini, ElevenLabs, Meshy, local/offline, and custom HTTP/MCP-style connectors.
- [x] Add provider routing preferences by capability.
- [x] Read API credentials from user settings or environment without logging secrets.
- [x] Keep model selection configurable rather than hard-coded.
- [x] Add global AI-free mode.
- [x] Add project-level disablement.

### Safe Tooling Surface
- [x] Define AI-callable tools for project summary.
- [x] Define AI-callable tools for package metadata search.
- [x] Define AI-callable tools for compiler profile listing.
- [x] Define AI-callable tools for proposing compiler commands.
- [x] Define AI-callable tools for staged text edits.
- [x] Define AI-callable tools for asset generation requests that always stage generated outputs before import.
- [x] Define AI workflow manifests: provider, model, prompt, context, tool calls, staged outputs, approval state, validation, cost/usage where available.
- [x] Add consent and preview UI before applying actions.
- [x] Add cancellation and retry state for long-running agentic workflows.

### First Experiments
- [x] Explain selected compiler log.
- [x] Propose next compiler command from a natural-language prompt.
- [x] Generate a project manifest draft.
- [x] Suggest missing package dependencies.
- [x] Generate a CLI command for a requested workflow.
- [x] Generate a supervised fix-and-retry plan for a compiler failure.
- [x] Generate placeholder sound/voice content through ElevenLabs when configured.
- [x] Generate placeholder model or texture concepts through Meshy when configured.
- [x] Compare output from two configured reasoning providers for the same prompt.

Exit criteria:
- [x] AI can explain a compiler log and propose a reviewable command without writing files.
- [x] AI can be globally disabled and core workflows remain usable.
- [x] Connector configuration can represent at least OpenAI plus one non-OpenAI provider without changing the workflow model.

## Milestone 4: MVP Release

Goal: ship the smallest public version that proves the complete loop.

### Packaging And Distribution
- [x] Add Windows portable package.
- [x] Add macOS portable package.
- [x] Add Linux portable package.
- [x] Add license bundle for VibeStudio and compiler/toolchain sources.
- [x] Add generated offline user guide.
- [x] Add release asset validation.

### MVP UX Completion
- [x] Add About/Credits/license surface with credits and license links.
- [x] Add Preferences for paths, theme, compilers, and installations.
- [x] Add Preferences for language, scale, density, high-visibility themes, reduced motion, and TTS.
- [x] Add project recent list and reopen-last-project option.
- [x] Add first-run setup checklist and guided flow.
- [x] Add setup summary with warnings, skipped steps, detected installs, toolchain probes, AI mode, and CLI details.
- [x] Add failure-friendly empty states.
- [x] Add loading/progress coverage audit for MVP workflows.
- [x] Add summary/detail coverage audit for MVP workflows.
- [x] Add graphical project/package/compiler summary views.
- [x] Add task history persistence for recent compiler/package operations.
- [x] Add basic keyboard navigation audit.
- [x] Add high-visibility theme audit.
- [x] Add localization/pseudo-localization audit.
- [x] Add OS-backed TTS smoke path.

### MVP Validation
- [x] Smoke-test Windows clean machine launch.
- [x] Smoke-test macOS clean machine launch.
- [x] Smoke-test Linux clean machine launch.
- [x] Smoke-test opening fixture PAK/WAD/PK3.
- [x] Smoke-test each compiler family with tiny sample project.
- [x] Measure startup time.
- [x] Measure package open time on small, medium, and large archives.
- [x] Verify visible feedback during package open, extraction, validation, compiler run, and AI request.
- [x] Verify first-run setup can be completed with keyboard-only navigation.
- [x] Verify first-run setup offers high-visibility, scaling, language, TTS, AI-free, and skip/later paths.
- [x] Verify MVP shell at 100%, 125%, 150%, 175%, and 200% scale.
- [x] Verify high-contrast dark and high-contrast light smoke flows.
- [x] Verify pseudo-localization and right-to-left smoke flows.
- [x] Verify TTS reads a test phrase and one task result where OS support exists.
- [x] Verify every MVP write/export operation reports output path.
- [x] Verify raw details/logs/manifests are reachable from summary views.
- [x] Verify credits and license bundle skeleton.

MVP release exit criteria:
- [x] A user can install/open VibeStudio, configure a game/project, browse assets, run a compiler profile, inspect diagnostics, and locate or launch output.
- [x] The app is honest about what editing surfaces are still previews or planned.

## Milestone 5: Safe Write-Back And Packaging

Goal: make package edits practical without sacrificing trust.

### Staging Model
- [x] Add staged package changes model.
- [x] Add add/import file.
- [x] Add rename.
- [x] Add delete.
- [x] Add replace.
- [x] Add conflict resolution.
- [x] Add diff/preview for staged changes.
- [x] Add graphical staging summary by operation type and package location.
- [x] Add before/after package composition view.
- [x] Add "why cannot save" blocked-state messages.
- [x] Add save-as before overwrite.

### Package Writers
- [x] Add PAK writer from PakFu lineage.
- [x] Add ZIP/PK3 writer from PakFu lineage.
- [x] Add WAD writer only after map-lump tests exist.
- [x] Add package manifest export.
- [x] Add reproducibility checks for deterministic outputs.

Exit criteria:
- [x] User can safely create and rebuild simple PAK/PK3 packages with visible staged changes.

## Milestone 6: Asset Preview And Editing Depth

Goal: widen the workbench into a real asset studio.

### Texture And Image
- [x] Port/adapt image loader workflow concepts from PakFu with attribution.
- [x] Add palette-aware preview.
- [x] Add conversion workflow.
- [x] Add texture metadata inspector.
- [x] Add image loading and conversion progress feedback.
- [x] Add before/after preview for edits/conversions.
- [x] Add basic crop/resize/palette operations.
- [x] Add batch conversion queue.

### Model
- [x] Port/adapt model metadata loader workflow concepts from PakFu.
- [x] Define native idTech model loader boundary before adding optional Assimp import/export.
- [x] Add model preview viewport.
- [x] Add skin/material dependency panel.
- [x] Add model loading state and fallback metadata view.
- [x] Add animation list where format supports it.
- [x] Add export/conversion hooks.

### Audio
- [x] Add audio metadata preview.
- [x] Add playback where Qt backend supports codec.
- [x] Evaluate miniaudio for portable playback/decoding/waveform gaps.
- [x] Add loading/buffering/playback state display.
- [x] Add waveform preview.
- [x] Add convert-to-WAV helper for supported sources.

### Text And Scripts
- [x] Start with Qt text widgets and a local syntax-highlighting boundary.
- [x] Evaluate KSyntaxHighlighting packaging and theme integration.
- [x] Evaluate Tree-sitter for shader/script/config incremental parsing.
- [x] Add syntax highlighting for CFG.
- [x] Add syntax highlighting for shader scripts.
- [x] Add syntax highlighting for QuakeC.
- [x] Add project-wide find/replace.
- [x] Add diagnostics markers from compiler output.
- [x] Add save state indicators: clean, modified, saving, saved, failed.

Exit criteria:
- [x] VibeStudio replaces PakFu for core browse/preview/package workflows and adds project/compiler context.

## Milestone 7: Level Editing MVP

Goal: introduce native editing through focused slices instead of attempting a
full Radiant/Doom Builder replacement in one step.

### Read And Inspect Maps
- [x] Load Doom map lump structure.
- [x] Load Quake-family `.map` text structure.
- [x] Load Quake III `.map` text structure.
- [x] Show entity list.
- [x] Show texture/material references.
- [x] Show map statistics.
- [x] Show validation problems.

### Visual MVP
- [x] Add 2D map view for Doom-family maps.
- [x] Add simple 3D/orthographic brush preview for Quake-family maps.
- [x] Add selection model.
- [x] Add property inspector.
- [x] Add save-as for non-destructive map edits.
- [x] Add map loading state and parse/validation progress.
- [x] Add map health overlay for missing textures, leaks, entity issues, and compiler warnings when data is available.
- [x] Add map statistics summary with detail drawer.

### Editor Profile MVP
- [x] Define editor profile schema.
- [x] Add profile selector in preferences.
- [x] Add GtkRadiant 1.6.0-style layout/control preset.
- [x] Add NetRadiant Custom-style layout/control preset.
- [x] Add TrenchBroom-style layout/control preset.
- [x] Add QuArK-style layout/control preset.
- [x] Add profile-specific keybinding tests.
- [x] Add profile-specific camera/selection smoke tests.

### Editing MVP
- [x] Edit entity key/value pairs.
- [x] Move selected Doom vertices/linedefs in 2D.
- [x] Move selected Quake entities.
- [x] Add undo/redo command stack.
- [x] Add visible edit state and undo/redo history summary.
- [x] Run compiler/profile from map editor.

### CLI And Tests
- [x] Add `map inspect`, `map edit`, `map move`, and `map compile-plan`.
- [x] Add fixture-backed tests for Doom WAD and Quake-family map parse/edit/save-as.
- [x] Add release validation coverage for map CLI inspect, edit, move, and compile-plan.

Exit criteria:
- [x] User can inspect a map, choose a familiar editor profile, make a small safe edit, compile/build, and test the result.

## Milestone 8: Advanced Studio Surfaces

Goal: evolve from workbench into the full all-encompassing studio.

### idTech3 Shader Graph
- [x] Parse shader scripts into an editable model.
- [x] Render stage list and texture dependencies.
- [x] Add graph editor for stages and blend modes.
- [x] Add shader stage visual preview with raw text detail.
- [x] Round-trip graph edits back to text.
- [x] Validate shader references against mounted packages.

### Sprite Creator
- [x] Add Doom sprite naming workflow.
- [x] Add Quake sprite workflow.
- [x] Add palette preview and conversion.
- [x] Add frame sequencing.
- [x] Add package staging integration.

### Code IDE
- [x] Add project source tree.
- [x] Add language service hooks.
- [x] Add build task integration.
- [x] Add symbol search where feasible.
- [x] Add run/debug launch profiles for source ports.

### AI-Assisted Creation
- [x] Prompt-to-shader scaffold.
- [x] Prompt-to-entity-definition snippet.
- [x] Prompt-to-package-validation plan.
- [x] Prompt-to-batch-conversion recipe.
- [x] Prompt-to-CLI command generation.
- [x] AI proposal review surface with summary, context used, generated actions, and detailed prompt/response log.

### Plugin/Extension System
- [x] Define extension manifest.
- [x] Define trust and sandbox model.
- [x] Add extension discovery.
- [x] Add extension command execution.
- [x] Add extension-generated file staging.

Exit criteria:
- [x] Common mod development no longer requires VibeStudio users to leave for routine shader, sprite, script, package, and build operations.

## Continuous Quality Backlog

These tasks never fully end. Pull them forward whenever a feature touches the
related area.

### Performance
- [x] Add startup timer.
- [x] Add package open timing.
- [x] Add preview timing.
- [x] Add compiler task timing.
- [ ] Add time-to-first-feedback measurement for long-running operations.
- [x] Add task-state transition timing.
- [ ] Add cache invalidation tests.
- [ ] Add memory checks for large package previews.

### Robustness
- [ ] Add fuzz target for PAK parser.
- [ ] Add fuzz target for WAD parser.
- [ ] Add fuzz target for ZIP/PK3 parser.
- [ ] Add fixture tests for every claimed format.
- [ ] Add crash/session log capture.
- [ ] Add corrupted-file fixture suite.

### Accessibility And Usability
- [ ] Audit keyboard navigation.
- [ ] Audit high-DPI scaling.
- [ ] Audit app text/UI scaling at 100%, 125%, 150%, 175%, and 200%.
- [ ] Audit color contrast for normal, high-contrast dark, and high-contrast light themes.
- [ ] Add configurable font size.
- [ ] Add high-visibility theme tests.
- [ ] Add color-blind-aware status palette tests.
- [ ] Add reduced motion preference if animations are introduced.
- [ ] Add OS-backed TTS smoke tests.
- [ ] Add tooltips for icon-only actions.
- [x] Audit each editor profile for discoverable controls and non-conflicting shortcuts.
- [ ] Audit loading/progress UI for screen-reader labels and non-color-only status.
- [ ] Audit detail drawers for keyboard access.

### Localization
- [x] Add Qt translation extraction workflow.
- [x] Add pseudo-localization target.
- [x] Add right-to-left layout smoke test.
- [x] Add initial translation catalog structure for the 20-language target set.
- [x] Add locale formatting tests for dates, numbers, sizes, durations, and sorting.
- [x] Add stale/untranslated string report.
- [x] Add translation expansion stress sample and ratio reporting.
- [x] Add translation expansion layout smoke checks.

### User Awareness And Progressive Disclosure
- [ ] Add UX checklist requiring state, progress, result, next action, and details for each workflow.
- [ ] Add snapshot tests or scripted QA for loading/empty/error/success states.
- [x] Add "copy diagnostic bundle" workflow.
- [ ] Add operation result summaries for package, compiler, validation, AI, and export tasks.
- [ ] Add graphical views only when backed by real data and actionable drill-down.

### Cross-Platform
- [x] Add path handling tests.
- [ ] Add shell-open tests.
- [x] Add portable package tests.
- [ ] Add file association documentation.
- [ ] Add platform-specific launcher docs.

### AI Safety And Privacy
- [ ] Add API key redaction tests.
- [ ] Add AI prompt/context preview tests.
- [ ] Add staged-application tests for AI-proposed edits.
- [ ] Add project-level AI disablement test.

### Credits And Licensing
- [x] Validate README Credits section against `docs/CREDITS.md`.
- [x] Validate external compiler submodule revisions against `src/core/studio_manifest.cpp`.
- [x] Bundle third-party license files in release artifacts.
- [x] Add About/Credits/license surface generated from docs and structured metadata.
- [x] Require credits updates in PR checklist.

## Near-Term Task Queue

Use this queue to get to MVP quickly.

1. [x] Add persistent settings and recent projects.
2. [x] Add accessibility and language preferences: scale, high-visibility, density, reduced motion, TTS, locale.
3. [x] Add first-run setup shell with skip/resume and setup summary.
4. [x] Add global activity center and reusable operation-state model.
5. [x] Add loading/skeleton/detail-drawer UI primitives.
6. [x] Port PakFu archive interfaces and path safety.
7. [x] Add read-only folder/PAK/WAD/PK3 package browsing with visible loading/progress.
8. [x] Add text/image/binary metadata preview pane with summary/detail split.
9. [x] Add manual game installation profiles.
10. [x] Add project manifest and workspace dashboard with project health summary.
11. [x] Add compiler registry and executable discovery.
12. [x] Add CLI subcommand router, JSON output, and exit-code contract.
13. [x] Add ericw-tools `qbsp/vis/light` wrapper profile.
14. [x] Add ZDBSP or ZokumBSP wrapper profile.
15. [x] Add q3map2 probe/compile wrapper profile.
16. [x] Add structured task logs, command manifests, and output-path reporting.
17. [x] Add package composition and compiler pipeline graphical summaries.
18. [x] Add editor profile registry and routed MVP presets.
19. [x] Add AI connector opt-in settings and provider-neutral automation design stub.
20. [x] Add MVP sample projects and CI smoke checks.
21. [x] Add portable packaging skeleton.
22. [x] Add About/Credits/license surface.
23. [x] Cut first MVP release candidate.
