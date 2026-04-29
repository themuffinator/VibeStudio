# VibeStudio Agent Rules

These rules guide automated and human contributors working on VibeStudio.
Keep them aligned with `README.md`, `docs/ARCHITECTURE.md`, and the current
credits list.

## Core Requirements
- Use modern C++ for production code.
- Use Qt6 Widgets as the primary UI surface; QML may be introduced only where it improves a contained workflow.
- Keep Meson + Ninja as the canonical build system.
- Treat `docs/STACK.md` as the stack decision record; update it whenever major dependencies, rendering strategy, AI integration strategy, or CLI architecture changes.
- Preserve cross-platform support for Windows, macOS, and Linux.
- Maintain a documented CLI for diagnostics, automation, compiler orchestration, and package operations.
- Treat VibeStudio as an integrated development environment for idTech1, idTech2, and idTech3 games first.
- Treat accessibility, localization, and first-run setup as product requirements, not optional polish.

## Product Direction
- Build toward an all-in-one studio: level editor, modeller, texture editor, audio editor, package manager, coding IDE, script editor, sprite creator, shader graph, and compiler pipeline.
- Borrow workflow lessons from modern idStudio-style production tooling while staying appropriate for Quake, Doom, and Quake III-era assets.
- Treat editor adaptability as a core feature: the level editor must support interaction/layout profiles inspired by GtkRadiant 1.6.0, NetRadiant Custom, TrenchBroom, and QuArK so users can work from familiar controls.
- Treat efficiency as a core product feature: streamline setup, editing, compiling, packaging, validation, and launch/testing so common development loops are as quick and easy as possible.
- Make VibeStudio accessible by design: high-visibility themes, scalable text/UI, keyboard navigation, screen-reader metadata, OS-backed TTS, reduced motion, and non-color-only status.
- Localize from the start. Keep UI strings translatable, support right-to-left layouts, preserve room for translated text, and target the initial language set in `docs/ACCESSIBILITY_LOCALIZATION.md`.
- Provide a full modern first-run setup flow so users can tailor language, accessibility, theme, editor profile, game installs, projects, compilers, AI connectors, CLI, and automation preferences.
- Embrace the AI era through optional, transparent, provider-neutral generative and agentic AI workflows. Plan for connectors such as OpenAI, Claude, Gemini, ElevenLabs, Meshy, local/offline models, and custom integrations, but never make cloud AI mandatory for core editing, building, packaging, validation, CLI use, or launching.
- Treat the CLI as a first-class product surface. GUI and CLI behavior should share services, validation, project state, diagnostics, and tests.
- Think like the user: the user should always know what is happening, what is blocked, what is safe to do next, and where to inspect more detail.
- Embrace modern UX design with progressive disclosure. Keep common workflows clean, but let users delve into detailed logs, manifests, metadata, dependency graphs, and compiler output whenever possible.
- Use graphical elements creatively and purposefully to explain project state, asset relationships, package structure, compiler pipelines, map health, and long-running operations.
- Prefer workflows that keep context visible: project tree, asset dependencies, compiler output, map state, and game installation state should reinforce each other.
- Use PakFu as the structural and technical reference for archive management, filetype parsing, installation detection, release automation, documentation style, and durable CLI behavior.

## Architecture And Code Practices
- Keep core parsing, package management, game detection, compiler orchestration, and UI code modular.
- Prefer moving PakFu-derived reusable logic into clear VibeStudio modules instead of copy-pasting large opaque blocks.
- Keep heavy file, preview, compiler, and asset indexing work asynchronous so the UI stays responsive.
- Keep editor interaction profiles data-driven where practical: keybinds, mouse gestures, camera movement, panel layout, grid behavior, selection behavior, and terminology should be configurable without forking editor logic.
- All custom widgets must expose accessible names, roles, descriptions, focus behavior, and state changes where applicable.
- New UI must be tested against scaling, high-visibility themes, keyboard-only navigation, and translation expansion expectations.
- User-visible strings must be localizable; avoid hard-coded English in UI code except stable technical identifiers.
- AI integrations must be opt-in, connector-neutral where practical, redact sensitive paths/secrets where possible, show proposed actions before applying changes, and route all file/package/compiler operations through normal VibeStudio services.
- AI-free mode must remain complete and tested for core workflows.
- Agentic workflows must expose plan, context, tool calls, staged changes, validation results, cancellation, and final summaries.
- Avoid OS-specific APIs unless guarded, documented, and covered by a portable fallback.
- Update docs and help text whenever behavior, CLI options, supported formats, dependencies, or build requirements change.
- Update `docs/DEPENDENCIES.md` when adding or changing libraries.
- Keep stack, dependency, roadmap, and credits documents aligned when introducing or removing a library.
- Keep `docs/ACCESSIBILITY_LOCALIZATION.md` and `docs/INITIAL_SETUP.md` aligned when changing UI, setup, settings, accessibility, TTS, or localization behavior.
- Keep `.editorconfig` formatting: tabs for C/C++ files, spaces for Markdown/YAML/JSON/Meson.

## Credits, Licensing, And Borrowed Code
- Keep the README Credits section and `docs/CREDITS.md` up to date in the same change that imports, ports, translates, or derives code, formats, assets, algorithms, documentation, or UI patterns from another project.
- Every borrowed item must be neatly credited with a link to the upstream project, the relevant file or directory when practical, the license, and a revision/tag/date.
- Preserve upstream license headers and copyright notices.
- Add nearby source comments when a function or data layout is directly derived from another codebase or public specification.
- Do not link GPL-incompatible code into VibeStudio. External compiler tools may remain separate executables/submodules until their integration model has a documented license review.
- When using PakFu code, credit [PakFu](https://github.com/themuffinator/PakFu) and name the source module or commit when practical.
- When using idTech community references, prefer public source/specification links over vague attribution.

## External Compiler Toolchains
- Imported compilers live under `external/compilers` as Git submodules unless there is a strong reason to vendor a specific fork.
- Preserve upstream history for ericw-tools, q3map2 from NetRadiant Custom, ZDBSP, ZokumBSP, and future compilers.
- VibeStudio-owned changes to external compilers should be made in explicit forks or patch directories, with upstream links and rationale documented in `docs/COMPILER_INTEGRATION.md`.
- The studio should invoke external compilers through a wrapper/orchestration layer with structured logs, progress, inputs, outputs, and reproducible command manifests.

## Supported Formats And Game Installations
- Keep archive and format support aligned with the support matrix.
- Treat game installation detection as user-confirmable, never destructive.
- Never hardcode a single storefront, OS path, source port, or install layout as the only supported route.
- Prefer fixture-backed tests for parser behavior and smoke tests for installation detection heuristics.

## UI Principles
- Keep the interface sleek, dense, and production-focused.
- Use a modern dark studio shell, but prioritize legibility, predictable navigation, and fast repeated workflows over decoration.
- Every operation that may take noticeable time must expose a loading, progress, queued, cancelled, success, or failure state as appropriate.
- Prefer visible task cards, progress bars, inline skeletons, spinners, timelines, status chips, previews, and logs over silent waits.
- Use progressive disclosure: summaries first, details on demand, raw data/logs available when useful.
- Graphical elements should clarify real project information; avoid decorative visuals that do not help the user decide or act.
- Avoid one-off UI metaphors that do not scale across level, asset, package, and code workflows.
- Let users choose familiar editor profiles without making any profile a second-class path.
- Respect OS scaling and accessibility settings by default, while providing app-level high-visibility, text scale, density, motion, and TTS controls.
- Do not add visible tutorial text where a direct control, tooltip, status message, or contextual inspector would be better.

## Contribution Boundaries
- Do not replace Qt/Meson without an explicit migration plan.
- Do not remove CLI coverage for key behaviors without a replacement automation path.
- Do not import assets, SDKs, game data, or proprietary content from commercial games.
- Do not bury borrowed-code attribution in commit messages only; credits must live in the repository documentation.
