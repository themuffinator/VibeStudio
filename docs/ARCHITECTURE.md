# VibeStudio Architecture

VibeStudio is organized as a studio shell with shared project state underneath
specialized work surfaces. The goal is one environment where editing, package
management, game detection, asset preview, compilation, and diagnostics all
understand the same project graph.

Implementation choices should follow the stack decision record in
[`docs/STACK.md`](STACK.md). Architecture documents describe boundaries and data
flow; the stack document describes preferred libraries, rendering progression,
CLI infrastructure, AI integration, accessibility, localization, persistence,
and packaging strategy. Efficiency goals are documented in
[`docs/EFFICIENCY.md`](EFFICIENCY.md), and accessibility/localization goals are
documented in [`docs/ACCESSIBILITY_LOCALIZATION.md`](ACCESSIBILITY_LOCALIZATION.md).

## Layers

### Studio Shell
- Qt Widgets application frame.
- Mode rail for workspace, levels, models, textures, audio, packages, code, scripts, shaders, and build output.
- Shared inspector, status, search, diagnostics, and task surfaces.
- Dockable panes for asset context, compiler logs, map/object properties, and dependency information.
- Layout preset service for editor-profile workspaces and user-customized panes.
- Accessibility, language, theme, scale, density, motion, and TTS preferences.
- First-run setup/resume service for tailoring the application ecosystem.
- Global activity center for queued/running/completed tasks, progress, cancellation, and detailed logs.
- Notification and status system for inline feedback, toasts, task cards, warnings, and recoverable errors.

### Project Core
- Project manifest and settings.
- Game installation profiles.
- Asset database and dependency graph.
- Automation graph for reusable workflows, presets, command manifests, and batch operations.
- Package mount graph for folders, WADs, PAKs, PK3s, and nested containers.
- Task runner for compilers, converters, validators, and external tools.
- Shared command services used by both GUI actions and CLI commands.
- Operation state model for loading, scanning, indexing, compiling, extracting, saving, cancelling, and failure recovery.

### Format And Package Layer
- PakFu-derived archive readers/writers and format parsers.
- Fixture-backed support matrix.
- Safe write-back model with staging, diffing, conflict handling, and reproducible manifests.
- Palette, material, shader, model, and map metadata services.

### Tool Surfaces
- Level editor for Doom-family and Quake-family workflows.
- Interaction profiles for GtkRadiant 1.6.0-style, NetRadiant Custom-style, TrenchBroom-style, and QuArK-style layouts/controls.
- Texture, sprite, model, audio, and cinematic editors.
- Code/script IDE.
- idTech3 shader graph with text round-tripping.
- Compiler pipeline editor.
- Detail-on-demand inspectors for raw metadata, dependency graphs, manifests, logs, and format-specific internals.

### UX Feedback Layer
- Loading displays and progress surfaces for every noticeable background operation.
- Skeleton/placeholder views while project, package, preview, or compiler data is arriving.
- Visual summaries for project health, asset dependencies, package composition, shader stages, and compiler pipelines.
- Expandable detail drawers that expose raw logs, manifests, metadata, and diagnostic traces.
- Consistent success/failure/retry/cancel affordances.

### Accessibility And Localization Layer
- High-visibility themes, scalable text/UI, density presets, reduced motion, and OS-backed TTS.
- Accessible metadata for shell widgets, custom editors, package trees, compiler logs, graph views, and setup screens.
- Keyboard navigation, command palette coverage, focus order, and no keyboard traps.
- Translation catalog loading, locale formatting, pluralization, pseudo-localization, and right-to-left layout validation.
- Initial 20-language localization target set and translation status reporting.

### Initial Setup Layer
- First-run flow for language, accessibility, theme, density, role, editor profile, game installs, projects, toolchains, AI connectors, CLI, and automation.
- Auto-detection with manual add, skip, later, and review paths.
- Exportable/importable setup profile and editable preferences after setup.
- Smoke-check summary for installs, compilers, AI connectors, TTS, and workspace readiness.

### CLI Layer
- Full command-line surface over projects, packages, previews, compilers, validation, automation, and release workflows.
- Human-readable output by default, structured JSON output for automation.
- Stable exit codes, command manifests, and scriptable operations.

### AI Automation Layer
- Optional provider-neutral AI connector abstraction.
- Connector capability registry for reasoning, coding, vision, image, audio, voice, 3D generation, embeddings, tool calling, streaming, and local/offline execution.
- Planned connector targets for OpenAI, Claude, Gemini, ElevenLabs, Meshy, local/offline models, and custom HTTP/MCP-style integrations.
- Prompt-to-plan, prompt-to-asset, prompt-to-command, and prompt-to-action workflows that call explicit VibeStudio tools.
- Supervised agentic loops for gather context, plan, review, stage, validate, and summarize.
- Reviewable proposals before file/package/compiler changes are applied.
- Local project context redaction and consent controls.
- AI-free mode and per-project disablement.

### External Toolchain Layer
- Wrapper model around external compiler submodules.
- Structured command manifests.
- Captured stdout/stderr and parsed diagnostics.
- Per-game build profiles.
- Reproducible output locations and package integration.

## Initial Code Shape
The current scaffold contains:
- `src/core`: manifest and future non-UI studio logic.
- `src/cli`: diagnostics and automation entry points.
- `src/app`: Qt Widgets studio shell.
- `src/tests`: smoke tests for core manifests.
- `external/compilers`: imported compiler submodules.

## Design Principles
- Keep project state central and shared across tools.
- Keep editor surfaces specialized, but not isolated.
- Keep layout/control profiles adaptable without fragmenting the underlying editor model.
- Think from the user's current uncertainty: show what VibeStudio is doing, why it is waiting, and what the next safe action is.
- Keep modern UX clean at the surface while preserving detailed inspection paths for expert users.
- Treat efficiency as a product feature: reduce setup time, repeated work, context switching, uncertainty, and waiting.
- Treat accessibility as a product feature: the studio must support high visibility, scaling, keyboard use, screen readers, TTS, and non-color-only state.
- Treat localization as architecture: every user-visible string, layout, status, and setup step should be designed for translation and locale-aware formatting.
- Treat initial setup as a tailoring workbench, not a tour.
- Use graphical representations to communicate real structure and status, not as decoration.
- Treat packages as editable project containers, not just files to extract.
- Make compiler runs reproducible and inspectable.
- Make prompt-based and agentic AI actions auditable, reversible where practical, connector-neutral, and available through the same command services as manual workflows.
- Keep AI optional while designing the architecture so AI-assisted users can move much faster.
- Keep CLI parity in mind for every feature that can reasonably run headless.
- Keep borrowed code credited at the point of use and in repository-level credits.
