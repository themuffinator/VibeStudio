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
| Project manifests | JSON | Active | Human-readable `.vibestudio/project.json` manifests store roots, folders, timestamps, selected install IDs, project-local overrides, compiler executable overrides, and registered compiler outputs. |
| User settings | Qt `QSettings` plus project overrides | Active | Application shell settings, recent projects, recent terminal activity, editor profile selection, accessibility/language preferences, installation profiles, and project-local overrides are active. |
| Accessibility | [Qt Accessibility](https://doc.qt.io/qt-6/accessible.html), OS accessibility settings, accessible custom widgets | Active/planned | Shell preference storage and accessible control metadata are active; deeper workflow audits and custom-widget coverage are planned. |
| Scaling | [Qt High DPI](https://doc.qt.io/qt-6/highdpi.html), layout-driven UI, app text scale preferences | Active/planned | Shell text scale presets are active; broader high-DPI and layout smoke coverage is planned. |
| Text to speech | [Qt TextToSpeech](https://doc.qt.io/qt-6/qttexttospeech-index.html) | Planned optional module | TTS enablement preference is active; native OS speech playback for task summaries, diagnostics, setup guidance, and warnings is planned. |
| Localization | [Qt internationalization](https://doc.qt.io/qt-6/internationalization.html), Qt Linguist, `QTranslator`, `QLocale` | Active/planned | Locale preference storage and `QLocale` formatting are active; translation catalogs, pseudo-localization, and RTL validation are planned. |
| Asset index/search | [SQLite](https://sqlite.org/) through [Qt SQL](https://doc.qt.io/qt-6/qtsql-index.html), with [FTS5](https://sqlite.org/fts5.html) where available | Planned | Lightweight local database for project metadata, dependencies, search, diagnostics, and recent activity. |
| CLI parser | Lightweight Qt `QStringList` router with in-process command registry; [CLI11](https://github.com/CLIUtils/CLI11) deferred | Active | Current router keeps project/package/install/asset/map/shader/sprite/code/extension/compiler/AI/credits subcommands dependency-free with JSON output, quiet/verbose/watch/task-state switches, stable exit codes, and testable command metadata through `cli commands`; CLI11 remains deferred until shell completion and broader validation justify the dependency. |
| Task execution | Qt `QProcess`, threads, signals, and a VibeStudio task model | Active/planned | The reusable operation-state model, shell activity center, compiler process runner, captured logs, cancellation plumbing, and run manifests are active; broader thread-pool/future integration is planned. |
| Package/archive layer | PakFu-derived C++ services plus focused format readers and deterministic writers | Active | Package/archive interfaces, virtual path safety, read-only folder/PAK/WAD/ZIP/PK3 entry readers, text/image/model/audio/script metadata previews, safe extraction reports, staged write-back, package manifests, and deterministic PAK/ZIP/PK3/tested PWAD save-as writers are active. |
| Level-map services | Native C++ parser/editor model over Doom WAD lumps and Quake-family `.map` text | Active | Provides shared GUI/CLI map inspection, entity/texture/statistics/validation surfaces, safe MVP edits, undo/redo, non-destructive save-as, and compiler profile handoff without adding a rendering dependency yet. |
| Advanced Studio services | Native C++ services for shader scripts, sprite workflow plans, code indexing, extension manifests, and staged AI creation proposals | Active | Provides shared GUI/CLI coverage for idTech3 shader graph data, stage edits, mounted texture validation, Doom/Quake sprite planning, source tree diagnostics, extension trust/sandbox command plans, and reviewable prompt-to-creation workflows without new dependencies. |
| 2D editor rendering | Custom Qt Widgets, `QPainter`, and Qt Graphics View where useful | Planned | Fast path to responsive map views, sprite/texture surfaces, overlays, and inspectable editor state. |
| Early 3D preview | Qt `QOpenGLWidget` behind a renderer interface | Planned | Acceptable for MVP preview work while keeping the future backend replaceable. |
| Long-term 3D rendering | [bgfx](https://bkaradzic.github.io/bgfx/overview.html) behind a renderer abstraction | Planned | Cross-platform renderer backend for durable editor viewports across Direct3D, Metal, Vulkan, and OpenGL-style platforms. |
| Text editing | Qt text widgets first, then [KSyntaxHighlighting](https://api.kde.org/frameworks/syntax-highlighting/html/index.html) and [Tree-sitter](https://tree-sitter.github.io/tree-sitter/) | Active/planned | Local CFG, shader, QuakeC, and idTech text highlighter/diagnostic boundaries plus project find/replace are active; KSyntaxHighlighting and Tree-sitter remain deferred until packaging and parser value justify dependencies. |
| Language services | LSP client architecture | Planned | Allows QuakeC, C/C++, shader/config helpers, and future language tools without hardwiring one parser model. |
| Audio | Qt metadata/playback candidates first, [miniaudio](https://miniaud.io/) for low-level decode/playback/waveform gaps | Active/planned | WAV metadata, platform-codec playback candidacy, waveform summaries, and WAV export are active; miniaudio remains the planned portable fallback for compressed decoding and richer editing. |
| Model formats | Native idTech loaders, optional [Assimp](https://www.assimp.org/) for adjacent import/export | Active/planned | Native MDL/MD2/MD3 metadata, skin/material dependency, animation-name, and fallback loader boundaries are active; Assimp remains optional for future adjacent import/export. |
| AI connector layer | Provider-neutral connector/model metadata plus manifest-backed workflow experiments | Active experimental | Lets users route reasoning, coding, image, audio, voice, 3D, and agentic workflows through OpenAI, Claude, Gemini, ElevenLabs, Meshy, local/offline models, or future connectors while keeping credentials redacted and outputs staged. |
| First AI provider | OpenAI connector scaffold, with future provider calls following [Responses](https://platform.openai.com/docs/api-reference/responses) and [tools/function calling](https://developers.openai.com/api/docs/guides/tools) patterns | Active experimental | OpenAI is implemented for configuration, credential discovery, model routing, safe tool descriptors, and no-write first experiments; network invocation remains opt-in future work. |
| Tests | Meson tests, Qt Test, focused executable tests, parser fixtures, and later fuzzing | Active/planned | Scales from smoke tests to parser safety, CLI parity, package safety, and editor regression coverage. |
| Packaging | PakFu-style scripts, Qt deployment tools, GitHub Actions artifacts | MVP package gate active, deployment planned | The active scripts stage Windows, macOS, and Linux portable release directories/zips with binary, docs, generated offline guide, checksums, samples, manifests, and license bundles; Qt deployment and signing remain planned. |

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
states, a reusable `DetailDrawer` for logs, metadata, manifests, raw
diagnostics, and support-copy text, and compact graphical summaries for project
health, package composition, map health/statistics, and compiler pipeline
readiness. The Advanced Studio workbench adds shader, sprite, code, AI, and
extension summaries with detail-on-demand tabs while staying in Qt Widgets.

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
geometry/state, selected mode, recent project folders, recent terminal activity
records, first-run setup progress, locale, theme, text scale, UI density,
reduced motion, OS-backed TTS, selected editor profile, manual game
installation profiles, and the current project path through shared GUI/CLI
services. Settings must be exportable enough for support and issue reports
without exposing secrets.

Project manifests use `.vibestudio/project.json` at the project root. The active
schema stores a version, project ID, display name, source folders, package
folders, output/temp folders, optional selected installation ID, project-local
settings overrides, compiler executable search paths/overrides, registered
compiler outputs, and timestamps. The workspace dashboard and CLI project
reports build a summary-first health view from this manifest.

Editor profiles are currently registry-backed placeholder presets with a
settings-backed global selection. The active schema records layout, camera,
selection, grid, terminology, default panels, workflow notes, and reserved
bindings for the VibeStudio default, GtkRadiant 1.6.0-style, NetRadiant
Custom-style, TrenchBroom-style, and QuArK-style workflows. Real editor command
routing, camera behavior, and per-project overrides remain planned.

Game installation profiles are currently settings-backed. They keep a stable
profile ID, game key, engine-family default, root path, optional executable,
package path hints, palette/compiler defaults, read-only state, and read-only
validation results. Steam/GOG detection produces unsaved candidates that the
GUI can import after confirmation and the CLI can report with `install detect`;
source-port detection remains planned.

Compiler registry data is now modeled as descriptors over imported compiler
submodules, expected executable names/build paths, capability flags, user
executable overrides, and project-local executable overrides. Discovery checks
source directories, known build-output paths, extra search paths, overrides,
and PATH, and registry reports can run short version/help probes. ericw-tools
`qbsp`, `vis`, and `light`, ZDBSP/ZokumBSP node-builder profiles, and q3map2
probe/BSP profiles can produce command plans with arguments, working directory,
expected output, warnings, and readiness. Plans and runs emit
schema-versioned JSON command manifests with command/environment details,
duration, exit code, hashes, stdout/stderr, diagnostics, task-log entries, and
registered outputs.

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

Use the package/archive layer for package metadata, read-only browsing, safe
extraction, and save-as write-back. The active slice defines stable descriptors
and readers for folder, PAK, WAD, ZIP, and PK3 package formats; shared package
entry metadata; mount-layer session state; safe normalized virtual paths;
traversal rejection; and output-path joining that proves extraction targets
remain under the chosen root. Package preview and extraction build on those
readers for text samples, basic image format/dimension metadata, binary hex
summaries, GUI composition buckets by entry type and byte size, selected/all
extraction, dry-run output reporting, cancellation-aware GUI progress, and CLI
validation.

The write-back slice adds a staged package model with add, replace, rename,
delete, conflict reporting, before/after composition, blocked-state messages,
manifest export, save-as guards, deterministic PAK and ZIP/PK3 writers, and a
PWAD writer covered by map-lump ordering tests. This layer is adapted from
PakFu's archive surface and credited in `docs/CREDITS.md`; future package
writers should build on it instead of duplicating path safety rules per format.

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

Use the active lightweight in-process router plus command registry for the
command-line interface. The CLI is a first-class product surface, not a debug
afterthought. It exposes project, package, installation, asset, map, shader,
sprite, code, extension, compiler, AI, credits, and registry commands with JSON
output, stable exit codes, quiet and verbose modes, dry-run behavior, compiler
watch streaming, machine-readable task state, and examples for PowerShell and
POSIX shells. CLI11 remains deferred for the fuller parser/completion layer
once that dependency earns its weight.

CLI commands must call the same services as GUI actions. Output should support
human text by default and `--json` for automation. The active router exposes
stable exit codes through `--exit-codes` and `cli exit-codes`. Long-running
commands should emit task state, progress, warnings, and reproducible manifests.

## Text, Script, And IDE Stack

Start with Qt text widgets for MVP editors. The active asset-tools slice uses
local syntax-highlighting and diagnostic boundaries for CFG, shader scripts,
QuakeC, and idTech text assets, plus project-wide find/replace with clean,
modified, saving, saved, and failed states. The Advanced Studio slice adds a
source tree index with language service hook descriptors, diagnostics, symbol
search, compiler task suggestions, and source-port launch profile summaries.
Add KSyntaxHighlighting for high-quality highlighting once packaging is
understood. Add Tree-sitter for incremental parsing where it materially
improves diagnostics, outlines, refactoring, shader/script structure, or AI
context extraction.

Use an LSP client boundary for external language tooling. Avoid hard-coding one
language server into the editor core.

## Assets, Media, And Formats

Prefer native idTech parsers for core formats. VibeStudio must understand WAD,
PAK, PK3, BSP, MAP, shader scripts, palettes, sprites, textures, model formats,
and compiler outputs on their own terms.

The active Advanced Studio slice parses idTech3 shader scripts into editable
stage models, validates texture references against mounted folders/packages,
round-trips selected stage directive edits back to text, and plans Doom/Quake
sprite names, rotations, palette actions, frame sequences, and package staging
paths. These are native C++/Qt services; no new graphics, parser, or media
library is required for the current milestone.

Use optional helper libraries where they expand workflows without weakening
format fidelity:

- Qt image APIs for standard image loading and conversion.
- Qt image APIs plus VibeStudio palette metadata for texture previews, crop,
  resize, grayscale/indexed conversion, and batch output queues.
- Qt Multimedia for simple playback and device integration; the active service
  records platform codec candidacy without making codec availability mandatory.
- miniaudio for small portable audio playback/decoding/waveform tasks where Qt
  backends are insufficient.
- Native MDL, MD2, and MD3 metadata loaders for package preview and dependency
  inspection before any generic model library is introduced.
- Assimp for adjacent model import/export, while keeping native MDL, MD2, MD3,
  MDC, MDR, IQM, and BSP-related loaders authoritative for game workflows.

## AI Automation Stack

AI features are optional, disabled by default, and safe-by-design, but the
product architecture should be AI-native. VibeStudio should treat generative and
agentic AI as a core acceleration layer that can plan, draft, explain, generate,
validate, and repeat workflows through explicit VibeStudio tools.

The active MVP slice stores global AI-free mode, cloud-connector opt-in,
agentic-workflow opt-in, provider preferences, configurable model preferences,
and redacted credential environment references without storing secrets. It
also exposes provider-neutral connectors, models, credential status, safe
AI-callable tools, and manifest-backed experiments through shared core
services, the GUI inspector/preferences surface, CLI text/JSON, and smoke
tests. OpenAI is the first implemented general-purpose connector scaffold;
provider network integration remains opt-in future work.

Use Qt Network and provider-specific HTTP/streaming adapters before introducing
heavy SDK dependencies. The connector model should support OpenAI, Claude,
Gemini, ElevenLabs, Meshy, local/offline models, and future community or studio
connectors. Each connector should declare capabilities such as text reasoning,
tool calls, vision, embeddings, image generation, audio generation,
speech-to-text, voice, 3D asset generation, streaming, local execution, cost
reporting, and privacy notes.

OpenAI is the first general-purpose connector scaffold. Future networked
provider calls should follow the Responses API and tool-calling pattern so
VibeStudio can expose a small, reviewable set of actions.

AI tools must route through normal services: project summary, package metadata
search, compiler profile listing, command proposal, staged text edits, staged
asset generation requests, diagnostics explanation, shader scaffold/entity
snippet/package validation/batch conversion proposals, proposal review
surfaces, and manifest generation.
AI workflow manifests capture provider, model, prompt, context summary, tool
calls, staged outputs, approval state, validation, cancellation/retry state, and
cost/usage placeholders. AI must not write directly to project files, packages,
compiler outputs, settings, or source trees without a preview and explicit user
approval.

Extension integration starts with `vibestudio.extension.json` manifests parsed
through the same C++/Qt service layer. The active model records schema version,
trust level, sandbox model, capabilities, command descriptors, reviewed command
plans, dry-run defaults, and extension-generated staged files before any command
can mutate user projects.

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
The active wrapper slices define ericw-tools, Doom-family node-builder, and
q3map2 command profiles with CLI command planning, manifest writing/loading,
run/rerun execution, copyable command lines, output registration, and GUI
readiness/run summaries by engine/stage. Process execution captures
stdout/stderr, parsed diagnostics, duration, exit code, file hashes, and
manifest records through the shared core runner.

## Testing And Quality Stack

Use Meson tests as the test runner entry point. Keep fast C++ executable tests
for core services and parsers. Use Qt Test for Qt-specific behavior. Add fixture
corpora for packages, maps, scripts, textures, audio, and models as support
lands. Add fuzzing for untrusted binary/text parsers before write-back features
become broad.

The active sample-project slice keeps tiny license-clean Doom, Quake, and
Quake III-family workspaces under `samples/projects`. `scripts/validate_samples.py`
checks their manifests, package folders, and compiler command plans locally and
in PR CI using the built CLI.

Every new dependency must have:

- A documented role.
- License and attribution notes.
- Platform and packaging notes.
- A reason it beats a smaller local implementation.
- A validation path in CI or local scripts.

## Packaging And Release Stack

Use GitHub Actions and PakFu-style release scripting as the automation model.
Package with Qt deployment tools and platform-specific wrappers as needed.

The active packaging scripts are `scripts/package_portable.py`,
`scripts/generate_offline_guide.py`, `scripts/validate_packaging.py`, and
`scripts/validate_release_assets.py`, validated locally and in PR CI. They
create versioned Windows, macOS, and Linux staging directories plus optional
zips with the built binary, documentation, generated offline guide, platform
smoke notes, samples, checksums, copied VibeStudio/imported-compiler license
files, and schema-versioned package manifests. They do not yet run Qt
deployment tools, bundle external compiler executables, sign artifacts, or
publish release downloads.

The active About/Credits/license surface is backed by structured metadata in
`src/core/studio_manifest.*`, exposed through the GUI inspector and CLI
`--about`/`about` commands, and points to `LICENSE`, `docs/CREDITS.md`,
`docs/DEPENDENCIES.md`, and `docs/PACKAGING.md`.

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
