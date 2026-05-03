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
- Durable recent activity history for terminal package, compiler, setup, and
  shell tasks shown in the workspace timeline after restart.
- Notification and status system for inline feedback, toasts, task cards, warnings, and recoverable errors.

### Project Core
- Project manifest and settings with `.vibestudio/project.json` metadata,
  current-project persistence, and dashboard health checks.
- Game installation profiles with manual settings persistence, stable IDs,
  engine-family defaults, and read-only validation.
- Asset database and dependency graph.
- Automation graph for reusable workflows, presets, command manifests, and batch operations.
- Package mount graph for folders, WADs, PAKs, PK3s, and nested containers.
- Task runner for compilers, converters, validators, and external tools.
- Compiler registry descriptors and executable discovery for imported external
  tools, user-configured executable paths, project-local overrides, version
  probes, and capability flags.
- Compiler wrapper profiles and command planning for ericw-tools `qbsp`, `vis`,
  and `light`, Doom-family node-builder stages, and q3map2 probe/BSP stages.
- Compiler runner services for `QProcess` execution, cancellation, stdout/stderr
  capture, diagnostic parsing, output registration, and re-running manifests.
- Schema-versioned compiler command manifests with structured task-log entries,
  environment subsets, inputs/outputs, hashes, duration, exit code, and raw
  process output.
- Level-map document services for Doom WAD map lumps, Quake-family `.map`
  text, Quake III `.map` text, entity/brush/thing inspection, texture/material
  references, validation health, safe entity and movement edits, undo/redo, and
  non-destructive save-as.
- Advanced Studio services for idTech3 shader script models, shader stage
  edits, sprite workflow planning, source tree indexing, extension manifests,
  and deterministic AI creation proposals.
- Shared command services used by both GUI actions and CLI commands.
- Operation state model for loading, scanning, indexing, compiling, extracting, saving, cancelling, and failure recovery.

### Format And Package Layer
- PakFu-derived archive interfaces, virtual path safety, read-only readers,
  extraction reports, staging manifests, deterministic writers, and format
  parsers.
- Shared package entry metadata, read-only listing readers, and mount-layer
  session state for folder, PAK, WAD, ZIP, and PK3 workflows.
- Shared package preview service for text/script samples, image/texture
  metadata, model metadata, audio metadata/waveform summaries, and binary
  hex/metadata summaries used by both GUI and CLI surfaces.
- Shared asset tooling service for image conversion queues, WAV export helpers,
  native idTech model metadata boundaries, and project text find/replace.
- Shared level-map parsing and save-as service for direct WAD map lump access
  and text-map round-tripping, keeping map edits independent from package entry
  sorting or archive browser presentation.
- Shared Advanced Studio format helpers for shader text round-tripping, mounted
  texture-reference validation, Doom/Quake sprite package paths, and
  extension-generated staged-file declarations.
- Shared safe extraction service for selected/all entries, dry-run output-path
  reporting, no-overwrite defaults, progress callbacks, and cancellation.
- Fixture-backed support matrix.
- Safe write-back model with staging, diffing, conflict handling, deterministic
  save-as writers, and reproducible manifests.
- Palette, material, shader, model, and map metadata services.

### Tool Surfaces
- Level editor for Doom-family and Quake-family workflows.
- Interaction profile registry with routed MVP presets for GtkRadiant 1.6.0-style, NetRadiant Custom-style, TrenchBroom-style, and QuArK-style layouts/controls.
- Texture, sprite, model, audio, and cinematic editors.
- Code/script IDE, with an active source index, language-hook descriptors,
  diagnostics, symbol search, build task hints, and launch-profile summaries.
- idTech3 shader graph with text round-tripping, stage previews, mounted
  package-reference validation, and safe stage directive edits.
- Sprite creator workflows for Doom lump names, Quake sprite sequencing,
  palette previews, frame rotations, and package staging paths.
- Extension surface for manifest inspection, trust/sandbox metadata, reviewed
  command plans, dry-run execution, and staged generated files.
- Compiler pipeline editor.
- Detail-on-demand inspectors for raw metadata, dependency graphs, manifests, logs, and format-specific internals.

### UX Feedback Layer
- Loading displays and progress surfaces for every noticeable background operation.
- Skeleton/placeholder views while project, package, preview, or compiler data is arriving.
- Shared status chip, keyboard shortcut, and command-palette semantics for
  project, package, compiler, installation, AI, validation, QA, and support
  workflows.
- Visual summaries for project health, package composition, map health,
  compiler pipeline readiness, and Advanced Studio shader/sprite/code/extension
  status, with broader run graphs planned.
- Expandable detail drawers that expose raw logs, manifests, metadata, and diagnostic traces.
- Consistent success/failure/retry/cancel affordances.

### Accessibility And Localization Layer
- High-visibility themes, scalable text/UI, density presets, reduced motion, and OS-backed TTS.
- Accessible metadata for shell widgets, custom editors, package trees, compiler logs, graph views, and setup screens.
- Keyboard navigation, command palette coverage, focus order, and no keyboard traps.
- Locale target metadata, locale formatting, pseudo-localization, pluralization
  samples, expansion stress samples, representative layout-budget checks,
  Arabic/Urdu right-to-left smoke validation, and stale/untranslated
  translation status reporting.
- Initial 20-language localization target set, seed Qt TS catalog scaffold,
  dry-run Qt Linguist extraction validation, and planned runtime translator
  loading and translated release bundles.

### Initial Setup Layer
- First-run flow for language, accessibility, theme, density, role, editor profile, game installs, projects, toolchains, AI connectors, CLI, and automation.
- Auto-detection with manual add, skip, later, and review paths.
- Exportable/importable setup profile and editable preferences after setup.
- Smoke-check summary for installs, compilers, AI connectors, TTS, and workspace readiness.

### CLI Layer
- Full command-line surface over projects, packages, previews, compilers, validation, automation, and release workflows.
- Human-readable output by default, structured JSON output for automation.
- Stable exit codes, command manifests, and scriptable operations.
- Active lightweight router and command registry for `project`, `package`,
  `install`, `asset`, `map`, `shader`, `sprite`, `code`, `extension`,
  `compiler`, `ai`, `credits`, and `cli` subcommands, with flat legacy options
  kept compatible.
- Global `--json`, `--quiet`, `--verbose`, `--dry-run`, `--watch`, and
  `--task-state` behavior for automation-friendly workflows where supported.

### AI Automation Layer
- Optional provider-neutral AI connector abstraction with active connector,
  model, credential-status, and workflow-manifest registries.
- Connector capability registry for reasoning, coding, vision, image, audio, voice, 3D generation, embeddings, tool calling, streaming, cost/usage, and local/offline execution.
- OpenAI implemented as the first general-purpose connector scaffold; design-stub connector targets remain for Claude, Gemini, ElevenLabs, Meshy, local/offline models, and custom HTTP/MCP-style integrations.
- Safe AI-callable tools for project summaries, package metadata search,
  compiler profile listing, compiler command proposals, staged text edits,
  shader scaffolds, entity snippets, package validation plans, batch conversion
  recipes, and staged asset-generation requests.
- Prompt-to-plan, prompt-to-asset, prompt-to-command, and prompt-to-action workflows that call explicit VibeStudio tools.
- Supervised agentic loops for gather context, plan, review, stage, validate, and summarize.
- Reviewable proposals before file/package/compiler changes are applied.
- Local project context redaction and consent controls.
- Active global AI-free mode and project-level disablement.

### External Toolchain Layer
- Wrapper model around external compiler submodules.
- Structured command manifests.
- Captured stdout/stderr and parsed diagnostics.
- Per-game build profiles.
- Reproducible output locations and package integration.

## Initial Code Shape
The current scaffold contains:
- `src/core`: manifest and future non-UI studio logic.
- `src/core/game_installation.*`: PakFu-inspired game installation profile
  model, known idTech game keys, engine-family defaults, read-only validation,
  and confirmable Steam/GOG candidate detection.
- `src/core/compiler_registry.*`: compiler tool descriptors and discovery
  results for imported ericw-tools, q3map2, ZDBSP, and ZokumBSP executables.
- `src/core/compiler_profiles.*`: wrapper profile descriptors and command-plan
  generation for ericw-tools, Doom-family node builders, and q3map2, plus
  schema-versioned compiler command manifests.
- `src/core/compiler_runner.*`: compiler execution, re-run, stdout/stderr
  capture, diagnostic parsing, cancellation callbacks, manifest saving, and
  output registration data.
- `src/core/project_manifest.*`: project manifest schema, project-local
  settings/compiler overrides, registered compiler outputs, JSON load/save, and
  health summary checks for the workspace dashboard and CLI.
- `src/core/studio_settings.*`: Qt settings facade for shell state, recent
  projects, recent activity history, accessibility/localization preferences,
  setup progress, editor profile selection, game installations, compiler
  executable overrides, and AI automation preferences.
- `src/core/package_archive.*`: PakFu-derived package/archive interface
  descriptors, read-only folder/PAK/WAD/ZIP/PK3 readers, package entry
  metadata, mount-layer session state, safe normalized virtual paths, and
  selected/all extraction reporting.
- `src/core/package_staging.*`: staged package add/import, replace, rename,
  delete, conflict reporting, before/after composition, manifest export,
  save-as guards, deterministic PAK/ZIP/PK3 writers, and a map-lump-tested PWAD
  writer.
- `src/core/package_preview.*`: shared read-only preview model for package
  entry text/script samples, image dimensions/format/palette metadata, native
  model metadata, audio metadata/waveform summaries, and binary samples.
- `src/core/asset_tools.*`: image conversion, texture metadata, MDL/MD2/MD3
  metadata, audio metadata/WAV export, local text highlighting/diagnostics, and
  project-wide find/replace services shared by GUI and CLI surfaces.
- `src/core/level_map.*`: Doom WAD map lump reader/writer, Quake-family and
  Quake III `.map` parser, map statistics, texture/material references,
  validation/preflight health, selection/property views, undo/redo edit stack,
  safe movement/entity edits, save-as, and compiler-request handoff.
- `src/core/advanced_studio.*`: idTech3 shader script parser/editor model,
  mounted package-reference validation, Doom/Quake sprite workflow planning,
  source workspace indexing, extension manifest/trust/sandbox command plans,
  and staged AI creation proposal helpers.
- `src/core/localization.*`: shared 20-language target registry, locale
  normalization, pseudo-localization, right-to-left smoke metadata, `QLocale`
  formatting samples, translation expansion stress data, and Qt TS catalog
  status reports.
- `src/core/studio_semantics.*`: shared non-color-only status chip semantics,
  default keyboard shortcuts, conflict checks, and command-palette metadata for
  shell and CLI surfaces.
- `src/cli`: diagnostics and automation entry points, including the active
  subcommand router, localization reports, diagnostic bundle export, JSON
  output envelopes, and stable exit-code contract.
- `src/app`: Qt Widgets studio shell.
- `src/tests`: smoke tests for core services, package flows, compiler flows,
  asset tooling, level maps, Advanced Studio services, and UI primitives.
- `scripts/package_portable.py`, `scripts/generate_offline_guide.py`,
  `scripts/validate_packaging.py`, and `scripts/validate_release_assets.py`:
  portable package staging, generated offline docs, license/checksum bundling,
  fixture smoke tests, timing checks, and release asset gates.
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
