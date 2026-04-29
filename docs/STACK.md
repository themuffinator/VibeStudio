# Technology Stack

This document defines the preferred implementation stack for VibeStudio. The
goal is a seamless, modern, cross-platform, user-friendly, adaptable, and
efficient development studio for idTech1, idTech2, and idTech3 projects.

The stack should stay boring where boring is valuable: stable desktop UI,
predictable builds, explicit data formats, testable services, and clear
licensing. Specialized libraries are introduced where they materially improve
rendering portability, source editing, media handling, search, or automation.

## Stack Summary

| Area | Choice | Status | Rationale |
| --- | --- | --- | --- |
| Primary language | C++20 | Active | Fits idTech-era native tooling, Qt, compilers, binary formats, and high-performance editors. |
| Application framework | [Qt 6](https://doc.qt.io/qt-6/) | Active | Mature cross-platform desktop framework with UI, networking, settings, processes, models, threading, and deployment support. |
| Primary UI | [Qt Widgets](https://doc.qt.io/qt-6/qtwidgets-index.html) | Active | Best fit for dense production tools, dockable panes, model/view data, custom inspectors, and native desktop behavior. |
| Shell UI primitives | Reusable Qt Widgets loading panes and detail drawers | Active | Shared shell components now cover operation state, progress, reduced-motion loading placeholders, and collapsible details for logs, metadata, manifests, and raw diagnostics. |
| Rich animated surfaces | [Qt Quick/QML](https://doc.qt.io/qt-6/qtquick-index.html) | Planned, bounded | Use for contained high-value surfaces only, such as onboarding, visual status views, or graph-like experiences. Do not rewrite the shell around QML without a migration plan. |
| Build system | [Meson](https://mesonbuild.com/) + [Ninja](https://ninja-build.org/) | Active | Fast, readable, cross-platform, and suitable for CI. |
| Automation | Python scripts + GitHub Actions | Active | Good fit for validation, release helpers, documentation checks, and CI orchestration. |
| Project manifests | JSON | Planned | Human-readable, easy to diff, scriptable, and already natural for CLI output. |
| User settings | Qt `QSettings` plus project overrides | Active/planned | Application shell settings, recent projects, and accessibility/language preferences use `QSettings`; project-local override support is planned. |
| Accessibility | [Qt Accessibility](https://doc.qt.io/qt-6/accessible.html), OS accessibility settings, accessible custom widgets | Active/planned | Shell preference storage and accessible control metadata are active; deeper workflow audits and custom-widget coverage are planned. |
| Scaling | [Qt High DPI](https://doc.qt.io/qt-6/highdpi.html), layout-driven UI, app text scale preferences | Active/planned | Shell text scale presets are active; broader high-DPI and layout smoke coverage is planned. |
| Text to speech | [Qt TextToSpeech](https://doc.qt.io/qt-6/qttexttospeech-index.html) | Planned optional module | TTS enablement preference is active; native OS speech playback for task summaries, diagnostics, setup guidance, and warnings is planned. |
| Localization | [Qt internationalization](https://doc.qt.io/qt-6/internationalization.html), Qt Linguist, `QTranslator`, `QLocale` | Active/planned | Locale preference storage and `QLocale` formatting are active; translation catalogs, pseudo-localization, and RTL validation are planned. |
| Asset index/search | [SQLite](https://sqlite.org/) through [Qt SQL](https://doc.qt.io/qt-6/qtsql-index.html), with [FTS5](https://sqlite.org/fts5.html) where available | Planned | Lightweight local database for project metadata, dependencies, search, diagnostics, and recent activity. |
| CLI parser | [CLI11](https://github.com/CLIUtils/CLI11) | Planned | Modern C++ parser with subcommands, validation, help output, and JSON-friendly command design. |
| Task execution | Qt `QProcess`, thread pools, futures, signals, and a VibeStudio task model | Active/planned | The reusable operation-state model and shell activity center are active; process execution, futures, and long-running service integration are planned. |
| Package/archive layer | PakFu-derived C++ services plus focused format readers | Planned | Preserves the strongest existing project lineage while moving logic into shared GUI/CLI services. |
| 2D editor rendering | Custom Qt Widgets, `QPainter`, and Qt Graphics View where useful | Planned | Fast path to responsive map views, sprite/texture surfaces, overlays, and inspectable editor state. |
| Early 3D preview | Qt `QOpenGLWidget` behind a renderer interface | Planned | Acceptable for MVP preview work while keeping the future backend replaceable. |
| Long-term 3D rendering | [bgfx](https://bkaradzic.github.io/bgfx/overview.html) behind a renderer abstraction | Planned | Cross-platform renderer backend for durable editor viewports across Direct3D, Metal, Vulkan, and OpenGL-style platforms. |
| Text editing | Qt text widgets first, then [KSyntaxHighlighting](https://api.kde.org/frameworks/syntax-highlighting/html/index.html) and [Tree-sitter](https://tree-sitter.github.io/tree-sitter/) | Planned | Starts simple, then adds high-quality highlighting and incremental parsing for scripts, shaders, configs, and code. |
| Language services | LSP client architecture | Planned | Allows QuakeC, C/C++, shader/config helpers, and future language tools without hardwiring one parser model. |
| Audio | Qt Multimedia first, [miniaudio](https://miniaud.io/) for low-level decode/playback/waveform gaps | Planned | Keeps basic playback simple while retaining a small portable fallback for editing-oriented audio tasks. |
| Model formats | Native idTech loaders, optional [Assimp](https://www.assimp.org/) for adjacent import/export | Planned | Native loaders preserve game-format fidelity; Assimp expands interchange support without becoming the source of truth. |
| AI connector layer | Provider-neutral HTTP/streaming connectors via Qt Network | Planned, optional core layer | Lets users route reasoning, coding, image, audio, voice, 3D, and agentic workflows through OpenAI, Claude, Gemini, ElevenLabs, Meshy, local/offline models, or future connectors. |
| First AI provider | OpenAI API, using [Responses](https://platform.openai.com/docs/api-reference/responses) and [tools/function calling](https://developers.openai.com/api/docs/guides/tools) patterns | Planned, optional | Strong first path for reviewable tool calls, structured outputs, and agentic workflow experiments. |
| Tests | Meson tests, Qt Test, focused executable tests, parser fixtures, and later fuzzing | Active/planned | Scales from smoke tests to parser safety, CLI parity, package safety, and editor regression coverage. |
| Packaging | PakFu-style scripts, Qt deployment tools, GitHub Actions artifacts | Planned | Produces portable packages while keeping license bundles and credits explicit. |

## Core Application Stack

Use C++20 and Qt 6 as the durable foundation. VibeStudio is a native desktop
tool with large projects, binary formats, compiler orchestration, custom
viewports, and low-latency editor interactions; C++ and Qt are the right center
of gravity for that work.

Qt Widgets remains the primary UI toolkit. It is the best fit for a dense
studio shell with dockable panels, inspectors, tree views, lists, logs, tables,
property editors, and long-lived desktop workflows. Qt Quick/QML is allowed for
contained surfaces where animation, transitions, or scene-graph composition
clearly improve the user experience.

Meson and Ninja remain canonical. Do not add CMake as a parallel first-class
build system for VibeStudio-owned code. External compiler projects may keep
their upstream build systems until a wrapper or import strategy is documented.

## UI And UX Stack

The shell should use Qt Widgets, model/view classes, custom widgets, icons,
dockable panes, persistent layouts, and reusable status/detail components. The
active shell includes a reusable `LoadingPane` for pane and preview loading
states and a reusable `DetailDrawer` for logs, metadata, manifests, raw
diagnostics, and support-copy text.

The UX stack must directly support the project philosophies:

- Seamless workflows: GUI, CLI, compiler runs, package operations, and project
  state share the same services.
- Modern but detailed: clean summaries first, with raw logs, manifests, graphs,
  metadata, and diagnostics available on demand.
- User awareness: every noticeable operation exposes loading, progress,
  cancellation, success, warning, failure, and detail states.
- Accessibility: high-visibility themes, scalable text/UI, keyboard access,
  screen-reader metadata, reduced motion, OS-backed TTS, and non-color-only
  status are normal shell capabilities.
- Localization: all user-facing strings should be translatable and layouts must
  support right-to-left, non-Latin scripts, and translation expansion.
- First-run setup: users can tailor language, accessibility, theme, density,
  editor profile, installations, projects, compilers, AI, CLI, and automation
  before work begins.
- Adaptability: editor profiles change layout, controls, selection behavior,
  terminology, camera behavior, and keybinds without forking editor logic.
- Graphical communication: use diagrams, timelines, dependency graphs,
  overlays, and previews to show real project state rather than decoration.

## Rendering And Viewports

Use a renderer abstraction before committing editor logic to any graphics API.
The minimum boundary should cover viewport creation, frame lifecycle, camera
state, draw batches, texture/material handles, overlays, picking, and readback
for tests.

Recommended progression:

1. MVP 2D surfaces: custom Qt Widgets and `QPainter`.
2. Early 3D previews: `QOpenGLWidget` through a thin `RenderBackend` interface.
3. Production 3D/editor viewports: bgfx backend once map/model previews need
   durable cross-platform rendering, batching, materials, and GPU portability.

Do not let rendering details leak into map, model, package, or compiler
services. Those services should produce editor data; render backends should
visualize it.

## Data, Search, And Persistence

Use JSON for project manifests and command manifests. Keep schemas versioned
and validate them through both GUI and CLI paths.

Use Qt `QSettings` for application settings and store project-specific
overrides under the project root. The active settings slice persists shell
geometry/state, selected mode, recent project folders, first-run setup
progress, locale, theme, text scale, UI density, reduced motion, and OS-backed
TTS preference through shared GUI/CLI services. Settings must be exportable
enough for support and issue reports without exposing secrets.

Use SQLite for the project asset index once package browsing and installation
profiles move beyond in-memory MVP structures. The index should cover mounted
package entries, metadata, dependency edges, compiler outputs, diagnostics,
recent activity, and search. Use FTS5 for fast text search where available, with
a graceful fallback if a platform build lacks it.

Use the reusable operation-state model for activity reporting across GUI and
CLI-backed workflows. The active model defines stable state identifiers for
idle, queued, loading, running, warning, failed, cancelled, and completed
operations, tracks progress, warnings, result summaries, cancellation
eligibility, and timestamped logs. Future package, compiler, validation, AI,
and export services should emit through this model instead of inventing local
task status enums.

## Accessibility, Localization, And Setup Stack

Use Qt's accessibility APIs and normal Qt widgets wherever possible so platform
assistive tools can understand the UI. Custom viewports, inspectors, graph
views, timeline views, map views, shader graphs, and package trees must add
explicit accessible names, roles, descriptions, focus handling, values, and
state changes.

Use layout-driven UI, OS font/scaling defaults, app-level text scale settings,
and high-visibility themes. Scaling and localization must be treated as layout
requirements, not post-release bug categories.

Use Qt TextToSpeech for optional OS-backed TTS. TTS should read selected
summaries, compiler errors, task outcomes, AI proposals, and setup guidance
without becoming the only way to receive that information.

Use Qt Linguist, `QTranslator`, and `QLocale` from the beginning. Initial
localization work should prove extraction, pseudo-localization, right-to-left
layout, and a 20-language target set documented in
[`docs/ACCESSIBILITY_LOCALIZATION.md`](ACCESSIBILITY_LOCALIZATION.md).

Build the first-run setup flow as a real settings workbench. It should configure
accessibility, language, theme, density, editor profile, game installations,
projects, compilers, AI connectors, CLI behavior, and automation preferences,
then leave those choices editable.

## CLI Stack

Use CLI11 for the full command-line interface. The CLI is a first-class product
surface, not a debug afterthought.

CLI commands must call the same services as GUI actions. Output should support
human text by default and `--json` for automation. Long-running commands should
emit task state, progress, warnings, and reproducible manifests.

## Text, Script, And IDE Stack

Start with Qt text widgets for MVP editors. Add KSyntaxHighlighting for
high-quality highlighting once packaging is understood. Add Tree-sitter for
incremental parsing where it materially improves diagnostics, outlines,
refactoring, shader/script structure, or AI context extraction.

Use an LSP client boundary for external language tooling. Avoid hard-coding one
language server into the editor core.

## Assets, Media, And Formats

Prefer native idTech parsers for core formats. VibeStudio must understand WAD,
PAK, PK3, BSP, MAP, shader scripts, palettes, sprites, textures, model formats,
and compiler outputs on their own terms.

Use optional helper libraries where they expand workflows without weakening
format fidelity:

- Qt image APIs for standard image loading and conversion.
- Qt Multimedia for simple playback and device integration.
- miniaudio for small portable audio playback/decoding/waveform tasks where Qt
  backends are insufficient.
- Assimp for adjacent model import/export, while keeping native MDL, MD2, MD3,
  MDC, MDR, IQM, and BSP-related loaders authoritative for game workflows.

## AI Automation Stack

AI features are optional, disabled by default, and safe-by-design, but the
product architecture should be AI-native. VibeStudio should treat generative and
agentic AI as a core acceleration layer that can plan, draft, explain, generate,
validate, and repeat workflows through explicit VibeStudio tools.

Use Qt Network and provider-specific HTTP/streaming adapters before introducing
heavy SDK dependencies. The connector model should support OpenAI, Claude,
Gemini, ElevenLabs, Meshy, local/offline models, and future community or studio
connectors. Each connector should declare capabilities such as text reasoning,
tool calls, vision, embeddings, image generation, audio generation,
speech-to-text, voice, 3D asset generation, streaming, local execution, cost
reporting, and privacy notes.

OpenAI should be the first general-purpose connector. The initial model should
follow the Responses API and tool-calling pattern so VibeStudio can expose a
small, reviewable set of actions.

AI tools must route through normal services: project summary, package search,
compiler profile listing, command proposal, staged text edits, diagnostics
explanation, and manifest generation. AI must not write directly to project
files, packages, compiler outputs, settings, or source trees without a preview
and explicit user approval.

AI-free mode must remain complete. Core editing, package management, compiler
orchestration, validation, CLI use, and launching must work without API keys or
cloud services.

## External Compiler Stack

Keep imported compiler sources under `external/compilers` as submodules or
explicitly credited forks. Invoke compilers through wrapper services that
capture command manifests, environment, progress, stdout/stderr, diagnostics,
outputs, and exit state.

VibeStudio-owned compiler orchestration should not depend on a GUI. Every
compiler run must be reproducible from the CLI.

## Testing And Quality Stack

Use Meson tests as the test runner entry point. Keep fast C++ executable tests
for core services and parsers. Use Qt Test for Qt-specific behavior. Add fixture
corpora for packages, maps, scripts, textures, audio, and models as support
lands. Add fuzzing for untrusted binary/text parsers before write-back features
become broad.

Every new dependency must have:

- A documented role.
- License and attribution notes.
- Platform and packaging notes.
- A reason it beats a smaller local implementation.
- A validation path in CI or local scripts.

## Packaging And Release Stack

Use GitHub Actions and PakFu-style release scripting as the automation model.
Package with Qt deployment tools and platform-specific wrappers as needed.

Every release artifact must include VibeStudio license text, third-party
license files, compiler/toolchain attribution, and a generated credits bundle.

## Explicit Non-Goals

- Do not rewrite the application shell in a web stack.
- Do not make Electron, Chromium, or a webview the primary UI framework.
- Do not make AI services required for local editing, building, packaging, or
  launching.
- Do not assume a single AI provider, model family, or capability set.
- Do not make bgfx, Assimp, Tree-sitter, or KSyntaxHighlighting mandatory before
  the MVP needs them.
- Do not bypass shared services for quick GUI-only or CLI-only behavior.
- Do not import third-party code without updating README credits and
  `docs/CREDITS.md`.

## Reference Links

- [Qt Widgets](https://doc.qt.io/qt-6/qtwidgets-index.html)
- [Qt Quick](https://doc.qt.io/qt-6/qtquick-index.html)
- [Qt SQL](https://doc.qt.io/qt-6/qtsql-index.html)
- [Qt OpenGL / QOpenGLWidget](https://doc.qt.io/qt-6/qopenglwidget.html)
- [Qt Multimedia](https://doc.qt.io/qt-6/qtmultimedia-index.html)
- [Qt Network](https://doc.qt.io/qt-6/qtnetwork-index.html)
- [Qt Accessibility](https://doc.qt.io/qt-6/accessible.html)
- [Qt High DPI](https://doc.qt.io/qt-6/highdpi.html)
- [Qt TextToSpeech](https://doc.qt.io/qt-6/qttexttospeech-index.html)
- [Qt internationalization](https://doc.qt.io/qt-6/internationalization.html)
- [Meson](https://mesonbuild.com/)
- [Ninja](https://ninja-build.org/)
- [bgfx](https://bkaradzic.github.io/bgfx/overview.html)
- [SQLite FTS5](https://sqlite.org/fts5.html)
- [CLI11](https://github.com/CLIUtils/CLI11)
- [KSyntaxHighlighting](https://api.kde.org/frameworks/syntax-highlighting/html/index.html)
- [Tree-sitter](https://tree-sitter.github.io/tree-sitter/)
- [miniaudio](https://miniaud.io/)
- [Assimp](https://www.assimp.org/)
- [OpenAI Responses API](https://platform.openai.com/docs/api-reference/responses)
- [OpenAI tool calling](https://developers.openai.com/api/docs/guides/tools)
- [Claude API documentation](https://platform.claude.com/docs/en/home)
- [Gemini API reference](https://ai.google.dev/api)
- [ElevenLabs documentation](https://elevenlabs.io/docs/overview/intro)
- [Meshy API documentation](https://docs.meshy.ai/en)
