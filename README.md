# VibeStudio

<p align="center">
  <a href="VERSION"><img alt="Version" src="https://img.shields.io/badge/version-0.1.0-rc1-0A66C2?style=for-the-badge"></a>
  <img alt="Development Stage" src="https://img.shields.io/badge/stage-extremely%20early%20pre--alpha-B85C00?style=for-the-badge">
  <a href="#tech-stack"><img alt="Tech Stack" src="https://img.shields.io/badge/stack-C%2B%2B20%20%7C%20Qt6%20Widgets-00599C?style=for-the-badge"></a>
  <a href="#build-and-run"><img alt="Build" src="https://img.shields.io/badge/build-Meson%20%2B%20Ninja-4C8EDA?style=for-the-badge"></a>
  <a href="#overview"><img alt="Platforms" src="https://img.shields.io/badge/platforms-Windows%20%7C%20macOS%20%7C%20Linux-444444?style=for-the-badge"></a>
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/license-GPLv3-2EA44F?style=for-the-badge"></a>
  <a href="https://github.com/themuffinator/VibeStudio/actions/workflows/pr-ci.yml"><img alt="PR CI" src="https://img.shields.io/github/actions/workflow/status/themuffinator/VibeStudio/pr-ci.yml?label=PR%20CI&style=for-the-badge"></a>
  <a href="https://github.com/themuffinator/VibeStudio/actions/workflows/compiler-submodules.yml"><img alt="Compiler Imports" src="https://img.shields.io/github/actions/workflow/status/themuffinator/VibeStudio/compiler-submodules.yml?label=compiler%20imports&style=for-the-badge"></a>
</p>

## Introduction

VibeStudio is an open-source, cross-platform development studio for idTech1,
idTech2, and idTech3 game projects. Its goal is to unify level editing,
modelling, texture and audio workflows, scripting and shader tooling, package
management, asset inspection, compiler orchestration, diagnostics, automation,
and optional AI-assisted workflows in one integrated environment for creating,
validating, packaging, and launching classic game content.

> [!WARNING]
> VibeStudio is at an extremely early pre-alpha stage. The repository is
> currently a foundation scaffold with planning documentation, CI, imported
> compiler submodules, a Qt shell, shared core services, and CLI workflows. Many
> studio features described below remain product goals and roadmap targets; the
> implemented surfaces are listed in Current Development State. It is not ready
> for production modding, mapping, packaging, or asset-authoring work.

The product direction borrows the clear, always-in-context workflow of modern
idStudio-style tools while staying grounded in the constraints and file formats
of Doom, Quake, Quake II, and Quake III-era games.

<details>
  <summary><strong>Table of Contents</strong></summary>

- [Introduction](#introduction)
- [Overview](#overview)
- [Current Development State](#current-development-state)
- [Studio Goals](#studio-goals)
- [Initial Scaffold](#initial-scaffold)
- [Imported Level Compilers](#imported-level-compilers)
- [PakFu Lineage](#pakfu-lineage)
- [Build and Run](#build-and-run)
- [CLI Quick Reference](#cli-quick-reference)
- [Documentation](#documentation)
- [Credits](#credits)
- [Tech Stack](#tech-stack)
- [License](#license)

</details>

## Overview
- Current version: `0.1.0-rc1` (see `VERSION`).
- Cross-platform targets: Windows, macOS, Linux.
- Build system: Meson + Ninja.
- UI framework: Qt6 Widgets.
- Primary scope: end-to-end idTech1-3 game development.
- Product emphasis: efficient, AI-accelerated, AI-optional workflows that reduce setup friction, repeated work, context switching, and time-to-test.
- Accessibility emphasis: high-visibility themes, scalable UI, OS-backed TTS, keyboard/screen-reader support, and localization-first design.
- Repository state: early foundation scaffold with documentation, CI, compiler submodules, shared core services, a Qt shell, and CLI workflow coverage.

## Current Development State
VibeStudio is still pre-alpha, but it now has enough shared services to prove
the early project/package/compiler loop. The repository captures the product
direction, stack, architecture, roadmap, contributor rules, credits policy,
imported compiler sources, and a buildable application shell.

What exists today:
- Documentation for product goals, stack, roadmap, UX, accessibility,
  localization, AI connectors, setup, compiler integration, and credits.
- Cross-platform Meson/Qt scaffold.
- Minimal Qt Widgets shell with persistent window/mode settings, a recent
  projects panel, accessibility/language preference controls, and a first-run
  setup panel with skip/resume/progress summary.
- Global activity center scaffold with task state, progress, warnings,
  cancellation, and selected-task logs.
- Reusable Qt Widgets loading/skeleton panes and collapsible detail drawers for
  summary-first logs, metadata, manifests, and raw diagnostics.
- PakFu-derived package/archive interface scaffolding with normalized virtual
  paths, path traversal checks, safe output-path joining, and mount-layer
  session metadata.
- Basic read-only package browsing for folders, PAK, WAD, ZIP, and PK3:
  tree and entry-list browsing, filtering,
  text/image/model/audio/script/binary metadata previews,
  detail drawers, activity-center scan/extract tasks, safe selected/all
  extraction with exact output paths, and CLI `--info`/`--list`/preview/
  extract/validate coverage.
- Safe package staging and save-as workflows with add/import, replace, rename,
  delete, conflict reporting, before/after composition, package manifest export,
  deterministic PAK and ZIP/PK3 writers, a map-lump-tested PWAD writer, GUI
  staging summaries, and CLI `package stage`/`manifest`/`save-as` coverage.
- Asset preview and editing-depth services for texture/image metadata,
  palette-aware previews, crop/resize/palette conversion queues, MDL/MD2/MD3
  model metadata, skin/material dependencies, WAV metadata/waveform export, and
  CFG/shader/QuakeC text diagnostics with CLI `asset inspect`/`convert`/
  `audio-wav`/`find`/`replace` coverage.
- Advanced Studio MVP services for idTech3 shader script graphs, shader stage
  editing and package-reference validation, Doom/Quake sprite workflow plans,
  project source indexing, language hook descriptors, extension manifests, and
  reviewable AI creation proposals for shaders, entity definitions, package
  validation, batch conversion, and CLI commands.
- Manual game installation profiles with stable IDs, game keys, engine-family
  defaults, read-only validation, GUI management, confirmable Steam/GOG
  candidate detection/import, and CLI report/add/select/validate/remove/detect
  coverage.
- Project manifest scaffolding with `.vibestudio/project.json`, current-project
  persistence, project-local settings overrides, a workspace dashboard health
  summary, and CLI project init/info/validate coverage.
- Workspace workbench panels for project problems, search across project files
  and mounted package entries, Git changed/staged files, dependency graph
  placeholders, recent activity, reveal-in-folder, and copy-virtual-path
  actions.
- Compiler registry and executable discovery for imported ericw-tools, q3map2,
  ZDBSP, and ZokumBSP tool descriptors, plus ericw-tools `qbsp`/`vis`/`light`,
  Doom-family node-builder, and q3map2 probe/BSP wrapper profiles that produce
  reviewable command plans, structured task-log entries, expected output paths,
  schema-versioned command manifests, user/project executable overrides,
  captured stdout/stderr, parsed diagnostics, run/rerun CLI execution, and
  project output registration.
- CLI subcommand router for project, package, installation, asset, map,
  shader, sprite, code, extension, compiler, localization, diagnostics, AI, and
  credits command families, with JSON output for automation and a documented
  stable exit-code contract.
- Localization target metadata and seed Qt TS catalogs for 20 languages plus
  pseudo-localization, with CLI reports for right-to-left smoke coverage,
  locale formatting, pluralization samples, translation expansion layout
  smoke checks, stale/untranslated catalog status, and dry-run Qt Linguist
  extraction validation.
- Portable release packaging scripts for Windows, macOS, and Linux target
  bundles with generated offline guide, platform smoke notes, checksums,
  samples, package manifests, and VibeStudio/imported-compiler license bundle.
- Minimal CLI diagnostics for version, platform, planned modules, and compiler
  import metadata, plus settings, setup, preference, recent-project,
  operation-state, UI-primitive, package-interface, and package-browsing
  diagnostics.
- Imported compiler source submodules.
- Early CI/build validation.

What does not exist yet:
- Broad in-place package editing, package compare tooling, and advanced binary
  format editors beyond the current staged save-as workflow.
- Full-production level editing, model, texture, audio, sprite, shader, code,
  or script editors beyond the current MVP service/UI slices.
- Full CLI parity.
- Provider network execution for AI connectors.
- Full guided first-run setup, full accessibility audits, complete translated
  bundles, or localization runtime loading.
- Production-ready workflows of any kind.

Treat every feature list below as roadmap intent until the roadmap and support
matrix mark it implemented.

## Studio Goals
- Level editor for Doom-family sectors and Quake-family brush workflows.
- Modeller and model preview/editing workflows for MDL, MD2, MD3, MDC, MDR, IQM, and adjacent idTech formats.
- Texture editor for indexed, paletted, mipmapped, shader-linked, and package-contained art.
- Audio editor and preview surface for WAV, OGG, MP3, and idTech-specific audio variants.
- PakFu-derived package manager for WAD, PAK, PK3, ZIP-family archives, folders, nested packages, manifests, and safe write-back.
- Coding IDE for QuakeC, C/C++ helper code, QVM-oriented workflows, configs, scripts, and source-port project glue.
- Script editor for CFG, shader scripts, entity definitions, menu scripts, and game-specific text assets.
- Sprite creator for Doom sprites, Quake sprites, HUD graphics, palette-aware image flows, and batch conversions.
- Graphical idTech3 shader editor that round-trips with `.shader` text.
- Adaptable level editor interaction profiles inspired by [GtkRadiant 1.6.0](https://github.com/TTimo/GtkRadiant), [NetRadiant Custom](https://github.com/Garux/netradiant-custom), [TrenchBroom](https://trenchbroom.github.io/), and [QuArK](https://quark.sourceforge.io/).
- User-aware design that keeps people informed with clear loading states, progress displays, task logs, previews, cancellation, and visible outcomes.
- Modern UX with progressive disclosure: simple workflows stay clean, while advanced users can drill into detailed logs, metadata, manifests, dependency graphs, and compiler output.
- Creative graphical communication for asset relationships, package structure, compiler pipelines, map statistics, shader stages, and project health.
- Accessibility-first design with high-visibility themes, scaling controls, keyboard navigation, assistive-tool metadata, OS-backed text-to-speech, and non-color-only status.
- Localization architecture targeting 20 predominant world languages, including right-to-left and non-Latin script coverage.
- Modern initial setup flow for tailoring language, accessibility, theme, editor profile, game installs, projects, compilers, AI connectors, CLI, and automation preferences.
- Efficiency-first workflows that streamline setup, browsing, editing, compiling, packaging, validation, and launch/testing as much as possible.
- Optional generative and agentic AI-assisted workflows through a provider-neutral connector layer, with planned connector targets including [OpenAI](https://platform.openai.com/docs/quickstart), [Claude](https://platform.claude.com/docs/en/home), [Gemini](https://ai.google.dev/api), [ElevenLabs](https://elevenlabs.io/docs/overview/intro), [Meshy](https://docs.meshy.ai/en), local/offline models, and custom integrations.
- Complete AI-free workflows for users and projects that prefer deterministic local tooling.
- A full-featured CLI that shares project, package, compiler, validation, and automation services with the GUI.
- Compiler orchestration for imported map/node builders, with structured logs, reproducible command manifests, progress, and game-profile-aware output paths.
- Installation detection and management guided by PakFu's game profile work.

## Initial Scaffold
The current scaffold includes:
- A Meson/Qt6 C++20 app target named `vibestudio`.
- A small but working Qt Widgets studio shell.
- A CLI surface for version/platform diagnostics, project/package/compiler,
  map, shader, sprite, code, extension, localization, diagnostics, and AI
  workflows, credits validation, JSON output, quiet/verbose modes, watch
  streaming, and task-state automation.
- Reusable shell UI primitives for loading/progress placeholders and
  detail-on-demand logs or metadata.
- A small shared package/archive interface layer adapted from PakFu's archive
  surface, including safe normalized virtual paths and read-only reader behavior.
- Folder, PAK, WAD, ZIP, and PK3 entry listing through shared core services
  used by both GUI and CLI, plus path-safe extraction for selected entries or
  entire packages.
- Safe package staging with save-as writers for simple PAK, ZIP/PK3, and tested
  PWAD outputs plus schema-versioned staging manifests.
- Summary-first graphical shell views for project health, package composition
  by type/size, level-map health/statistics, and compiler pipeline profile
  readiness.
- Level-map MVP services and UI/CLI surfaces for Doom WAD map lump inspection,
  Quake-family and Quake III `.map` inspection, entity/property lists,
  texture/material references, validation, safe entity/movement edits,
  undo/redo state, non-destructive save-as, and compiler profile handoff.
- Advanced Studio MVP services and UI/CLI surfaces for shader script parsing
  and stage edits, sprite naming/sequencing/package plans, source indexing,
  extension discovery/command planning, and staged AI creation proposals.
- A data-driven editor profile registry with routed MVP presets for
  VibeStudio default, GtkRadiant 1.6.0-style, NetRadiant Custom-style,
  TrenchBroom-style, and QuArK-style workflows.
- AI-free-by-default settings, provider-neutral connector/model metadata,
  redacted credential discovery, safe AI-callable tool descriptors, and
  manifest-backed first experiments with OpenAI as the first implemented
  connector scaffold plus Claude, Gemini, ElevenLabs, Meshy, local/offline, and
  custom connector paths.
- Tiny license-clean sample projects for Doom, Quake, and Quake III-family
  smoke checks.
- A portable packaging skeleton that stages the built binary, docs, credits,
  licenses scaffold, samples, and a package manifest.
- An About/Credits/license surface shared by the GUI inspector and CLI.
- CI workflows for cross-platform build/test, sample validation, packaging
  skeleton validation, and submodule verification.
- Documentation for architecture, compiler integration, installation management, support scope, dependencies, roadmap, and credits.
- External compiler imports preserved as Git submodules.

## Imported Level Compilers
Compiler sources are imported as submodules under `external/compilers` to keep
upstream history, licensing, and update paths intact.

| Tool | Purpose | Upstream | Imported revision |
|---|---|---|---|
| ericw-tools | Quake/idTech2-style `qbsp`, `vis`, `light`, `bspinfo`, `bsputil` | [ericwa/ericw-tools](https://github.com/ericwa/ericw-tools) | `f80b1e216a415581aea7475cb52b16b8c4859084` |
| q3map2-nrc | q3map2 compiler from NetRadiant Custom for idTech3 BSP compile, light, conversion, and packaging flow | [Garux/netradiant-custom](https://github.com/Garux/netradiant-custom) | `68ecbed64b7be78741878c730279b5471d978c7c` |
| ZDBSP | Doom-family node building, including vanilla, Hexen, GL, and UDMF-aware modes | [rheit/zdbsp](https://github.com/rheit/zdbsp) | `bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659` |
| ZokumBSP | Doom-family node, blockmap, and reject building for vanilla-conscious maps | [zokum-no/zokumbsp](https://github.com/zokum-no/zokumbsp) | `22af6defeb84ce836e0b184d6be5e80f127d9451` |

Clone with compiler sources:
```sh
git clone --recursive https://github.com/themuffinator/VibeStudio.git
```

Initialize compiler sources after a normal clone:
```sh
git submodule update --init --recursive
```

See [`docs/COMPILER_INTEGRATION.md`](docs/COMPILER_INTEGRATION.md) for the
planned wrapper layer and licensing boundaries.

## PakFu Lineage
VibeStudio uses [PakFu](https://github.com/themuffinator/PakFu) as the working
reference for repository structure, Meson/Qt stack, GitHub Actions style,
README format, archive/package handling, game installation detection, parser
hardening, release discipline, and credits policy.

PakFu code should move into VibeStudio deliberately: credited, tested, and
reshaped into modules that serve the larger studio instead of remaining a
standalone archive app in disguise.

## Build and Run

### Prerequisites
- C++20 toolchain
- Meson + Ninja
- Qt6 Core, Gui, Widgets, and Network
- Git submodules for imported compiler source review

### Configure And Build
```sh
meson setup builddir --backend ninja
meson compile -C builddir
meson test -C builddir --print-errorlogs
```

Windows helper:
```powershell
pwsh -NoProfile -File scripts/meson_build.ps1
```

### Run
Unix-like systems:
```sh
./builddir/src/vibestudio
./builddir/src/vibestudio --cli --help
```

Windows:
```powershell
.\builddir\src\vibestudio.exe
.\builddir\src\vibestudio.exe --cli --help
```

## CLI Quick Reference
Usage:
```text
vibestudio --cli [options]
vibestudio --cli [--json] <family> <command> [arguments]
```

Current scaffold commands:
- `--version`: print the application version.
- `--help`: print CLI help.
- `--json`: emit machine-readable JSON for supported command families.
- `--exit-codes`: print stable exit-code identifiers and meanings.
- `--studio-report`: print planned studio modules.
- `--compiler-report`: print imported compiler metadata.
- `--compiler-registry`: print compiler registry and executable discovery.
- `--platform-report`: print platform and Qt runtime details.
- `--project-init <path>`: create or refresh `.vibestudio/project.json`.
- `--project-info <path>`: print project manifest and health summary.
- `--project-validate <path>`: validate project health and return a stable
  validation exit code for blocking issues.
- `--project-installation <id>` / `--project-editor-profile <id>` /
  `--project-palette <id>` / `--project-compiler-profile <id>` /
  `--project-compiler-search-paths <paths>` /
  `--project-compiler-tool <id> --project-compiler-executable <path>` /
  `--project-ai-free <on|off>`: set project-local overrides while refreshing a
  manifest.
- `--operation-states`: print reusable operation state identifiers.
- `--localization-report`: print localization targets, pseudo-localization,
  RTL smoke coverage, formatting samples, expansion stress status, and
  translation catalog status.
- `--ui-primitives`: print reusable UI primitive identifiers and use cases.
- `--ui-semantics`: print status chip, shortcut registry, and command palette
  metadata.
- `--package-formats`: print package/archive interface descriptors.
- `--check-package-path <path>`: normalize and validate a package virtual path.
- `--info <path>`: print read-only package summary for a folder, PAK, WAD, ZIP, or PK3.
- `--list <path>`: list package entries and metadata.
- `--preview-package <path> --preview-entry <virtual-path>`: print text,
  image, model, audio, script, or binary metadata for a package entry.
- `--extract <path> --output <folder> [--extract-entry <virtual-path>]`:
  extract selected entries, or every entry when no entry is passed.
- `--extract-all` / `--dry-run` / `--overwrite`: control package extraction
  scope, package save-as planning, staged output reporting, and explicit
  replacement behavior.
- `--validate-package <path>`: validate package loading and report warnings.
- `--settings-report`: print persistent settings storage and recent projects.
- `--setup-report`: print first-run setup status and summary.
- `--setup-start`: start or resume first-run setup.
- `--setup-step <id>`: resume setup at a specific step.
- `--setup-next`: advance setup to the next step.
- `--setup-skip`: skip setup for now without completing it.
- `--setup-complete`: mark setup complete.
- `--setup-reset`: reset setup progress.
- `--preferences-report`: print accessibility and language preferences.
- `--set-locale <code>`: set the preferred UI locale code.
- `--set-theme <id>`: set `system`, `dark`, `light`,
  `high-contrast-dark`, or `high-contrast-light`.
- `--set-text-scale <percent>`: set text scale from 100 to 200.
- `--set-density <id>`: set `comfortable`, `standard`, or `compact`.
- `--set-reduced-motion <on|off>`: store the reduced-motion preference.
- `--set-tts <on|off>`: store the OS-backed text-to-speech preference.
- `--installations-report`: print saved manual game installation profiles.
- `--add-installation <root>`: add or update a manual installation profile.
- `--install-game <key>` / `--install-engine <id>` / `--install-name <name>`:
  refine the profile created by `--add-installation`.
- `--select-installation <id>`: mark a saved installation profile as selected.
- `--validate-installation <id>`: validate a saved profile read-only.
- `--remove-installation <id>`: remove a saved profile without touching files.
- `--detect-installations [--detect-install-root <path>]`: scan common Steam
  and GOG roots plus optional explicit roots for confirmable candidates without
  saving profiles.
- `--recent-projects`: print remembered project folders.
- `--add-recent-project <path>`: remember a project folder.
- `--remove-recent-project <path>`: forget a project folder without touching
  files.
- `--clear-recent-projects`: clear remembered project folders without touching
  project files.

Current subcommands:
- `cli exit-codes`: print the exit-code contract.
- `project init <path> [--installation <id>] [--editor-profile <id>]`: create
  or refresh a project manifest and optional project-local overrides.
- `project info <path>`: print project manifest and health summary.
- `project validate <path>`: validate project health and return
  `validation-failed` for blocking issues.
- `install list`: list saved manual game installation profiles.
- `install detect [--root <path>]`: detect Steam/GOG candidates read-only
  without saving profiles.
- `package info <path>` / `package list <path>`: inspect a folder, PAK, WAD,
  ZIP, or PK3 package.
- `package preview <path> <virtual-path>`: preview one package entry.
- `package extract <path> --output <folder> [--entry <virtual-path>]`: extract
  one or more entries, or all entries when no entry is supplied.
- `package validate <path>`: validate package loading and return
  `validation-failed` when loader warnings are present.
- `package stage <path> [stage options] [--resolve block|replace-existing|skip]`:
  preview staged add, replace, rename, and delete operations with before/after
  entry and composition JSON.
- `package manifest <path> --output <manifest.json> [stage options]`: export a
  schema-versioned staged package manifest without writing an archive.
- `package save-as <path> <output> [--format pak|zip|pk3|wad] [stage options] [--dry-run]`:
  write or dry-run a staged package to a new path, report blockers, hashes,
  output paths, and optional manifest JSON.
- `asset inspect <package> <virtual-path>`: inspect image, model, audio, text,
  script, or binary metadata for one package entry.
- `asset convert <package> --output <folder> [--entry <virtual-path>]`:
  batch-convert package images with optional `--format`, `--crop`, `--resize`,
  `--palette`, `--dry-run`, and `--overwrite`.
- `asset audio-wav <package> <virtual-path> --output <file.wav>`: export
  readable WAV/PCM package entries with dry-run and overwrite controls.
- `asset find` / `asset replace`: search or safely replace project text/script
  assets with file/line matches and save-state reporting.
- `map inspect`, `map edit`, `map move`, and `map compile-plan`: inspect Doom
  WAD map lumps and Quake-family `.map` files, make safe non-destructive edits,
  and hand off to compiler profile plans.
- `shader inspect <shader-file> [--package <path>]`: parse idTech3 shader
  scripts into stage graphs, previews, raw detail, and package-reference
  validation reports.
- `shader set-stage`: round-trip a stage directive edit with `--shader`,
  `--stage`, `--directive`, `--value`, and `--output` without modifying the
  source in place.
- `sprite plan --engine doom|quake --name <name>`: create Doom lump naming or
  Quake `.spr` frame plans with palette, sequence, and package staging notes.
- `code index <project-root> [--find <symbol>]`: scan source trees, language
  hooks, diagnostics, symbols, build task hints, and source-port launch profiles.
- `ui semantics`: report non-color-only status chip semantics, default
  shortcuts, shortcut conflicts, and command palette entries.
- `localization targets` / `localization report`: list the target language set
  or report pseudo-localization, RTL, locale formatting, expansion, and TS
  catalog status.
- `diagnostics bundle [--output <folder>]`: print or write a redacted support
  bundle with version, platform, command, module, operation-state, and
  localization diagnostics.
- `extension discover`, `extension inspect`, and `extension run`: load
  `vibestudio.extension.json` manifests, report trust/sandbox metadata, and
  dry-run or execute reviewed extension command plans.
- `compiler list`: print compiler registry and executable discovery.
- `compiler profiles`: list wrapper profiles, currently ericw-tools
  `qbsp`/`vis`/`light`, ZDBSP nodes, ZokumBSP nodes, and q3map2 probe/BSP
  plans.
- `compiler set-path <tool> --executable <path>` / `compiler clear-path <tool>`:
  manage user-configured compiler executable overrides.
- `compiler plan <profile> --input <path>`: build a reviewable compiler command
  plan. Optional flags include `--output <path>`, `--extra-args "<args>"`, and
  `--workspace-root <path>`.
- `compiler manifest <profile> --input <path>`: print or write a
  schema-versioned command manifest. Add `--manifest <path>` to save JSON.
- `compiler run <profile> --input <path>`: execute a profile through the shared
  runner, capture stdout/stderr, diagnostics, hashes, duration, and exit code,
  stream `--watch` task logs, emit `--task-state` JSON, and optionally write
  `--manifest <path>` and `--register-output`.
- `compiler rerun <manifest-path>`: re-run a saved command manifest.
- `compiler copy-command <manifest-path|profile>`: print a reproducible command
  line for support, automation, or CI.
- `credits validate`: validate README and `docs/CREDITS.md` coverage plus
  compiler pins, `.gitmodules`, and checked-out submodule revisions.
- `ai tools`: list safe AI-callable VibeStudio tool descriptors.
- `ai explain-log --log <path>|--text <text>`: explain compiler output as a
  no-write workflow manifest.
- `ai propose-command --prompt <text>`: propose a reviewable compiler command
  from natural language.
- `ai propose-manifest <project-root>`: draft a project manifest preview without
  writing it.
- `ai package-deps <package>`: suggest likely missing package dependencies from
  metadata.
- `ai cli-command --prompt <text>`, `ai fix-plan`, `ai asset-request`, and
  `ai compare`: generate staged, reviewable automation proposals.
- `ai shader-scaffold`, `ai entity-snippet`, `ai package-plan`,
  `ai batch-recipe`, and `ai review`: generate and inspect staged Advanced
  Studio creation proposals with summary, context, actions, prompts, and
  response detail.

## Documentation
- [`AGENTS.md`](AGENTS.md): contributor and automation rules.
- [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md): task sizing, validation,
  accessibility/localization, and credits expectations.
- [`docs/ACCESSIBILITY_LOCALIZATION.md`](docs/ACCESSIBILITY_LOCALIZATION.md): accessibility, high-visibility, scaling, TTS, and localization goals.
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md): studio architecture and module boundaries.
- [`docs/AI_AUTOMATION.md`](docs/AI_AUTOMATION.md): provider-neutral generative and agentic AI connector strategy.
- [`docs/CLI_STRATEGY.md`](docs/CLI_STRATEGY.md): full-featured CLI strategy and command coverage goals.
- [`docs/COMPILER_INTEGRATION.md`](docs/COMPILER_INTEGRATION.md): compiler imports, wrapper plans, and licensing boundaries.
- [`docs/DEPENDENCIES.md`](docs/DEPENDENCIES.md): dependency baseline.
- [`docs/EFFICIENCY.md`](docs/EFFICIENCY.md): efficiency philosophy, acceleration techniques, AI-free mode, and speed metrics.
- [`docs/EDITOR_PROFILES.md`](docs/EDITOR_PROFILES.md): adaptable level-editor layout and control profiles.
- [`docs/GAME_INSTALLATIONS.md`](docs/GAME_INSTALLATIONS.md): installation detection and profile management plan.
- [`docs/INITIAL_SETUP.md`](docs/INITIAL_SETUP.md): first-run setup and ecosystem tailoring flow.
- [`docs/PACKAGING.md`](docs/PACKAGING.md): portable packaging skeleton and release packaging plan.
- [`docs/RELEASE_CANDIDATE.md`](docs/RELEASE_CANDIDATE.md): current MVP release-candidate scope, gaps, and validation gate.
- [`docs/ROADMAP.md`](docs/ROADMAP.md): metric-driven roadmap, MVP definition, and task checklist.
- [`docs/STACK.md`](docs/STACK.md): preferred technology stack and stack decision rationale.
- [`docs/SUPPORT_MATRIX.md`](docs/SUPPORT_MATRIX.md): initial format and workflow support target.
- [`docs/UX_DESIGN.md`](docs/UX_DESIGN.md): user-aware modern UX, progress feedback, progressive disclosure, and visual communication philosophy.
- [`docs/CREDITS.md`](docs/CREDITS.md): complete attribution list.

## Credits
- Creator: [themuffinator](https://github.com/themuffinator) (DarkMatter Productions)
- Structural, archive-tooling, and installation-profile reference: [PakFu](https://github.com/themuffinator/PakFu), with the current package interface, virtual-path safety, and staged package write-back concepts adapted from its archive direction at `c82dfb0ef0b5d7442e243ace8cd83bc45f82f257`; the game installation profile/detection model is a VibeStudio-owned adaptation of PakFu's profile-driven workflow ideas.
- Imported compiler/toolchain sources: [ericw-tools](https://github.com/ericwa/ericw-tools), q3map2 from [NetRadiant Custom](https://github.com/Garux/netradiant-custom), [ZDBSP](https://github.com/rheit/zdbsp), and [ZokumBSP](https://github.com/zokum-no/zokumbsp)
- Editor workflow inspirations: [GtkRadiant](https://github.com/TTimo/GtkRadiant), [NetRadiant Custom](https://github.com/Garux/netradiant-custom), [TrenchBroom](https://trenchbroom.github.io/), and [QuArK](https://quark.sourceforge.io/)
- Optional AI automation references: [OpenAI API documentation](https://platform.openai.com/docs/quickstart), [Claude API docs](https://platform.claude.com/docs/en/home), [Gemini API docs](https://ai.google.dev/api), [ElevenLabs docs](https://elevenlabs.io/docs/overview/intro), and [Meshy docs](https://docs.meshy.ai/en)
- Full attribution list: [`docs/CREDITS.md`](docs/CREDITS.md)

The credits above are a maintenance requirement, not a courtesy footer. When
code, assets, documentation, algorithms, file-format knowledge, UI patterns, or
compiler changes are borrowed or derived from another project, update this
section and `docs/CREDITS.md` in the same change.

## Tech Stack
- Language: C++20.
- Application framework: Qt 6.
- Primary UI: Qt Widgets, with bounded Qt Quick/QML use only where it clearly improves a contained workflow.
- Build: Meson + Ninja.
- Automation: Python validation scripts and GitHub Actions.
- Project data: JSON manifests, Qt settings, and planned SQLite/FTS indexing.
- CLI: active lightweight subcommand router with testable command registry,
  JSON output, stable exit codes, quiet/verbose modes, dry-run behavior, watch
  streaming, task-state output, Advanced Studio command families, and deferred
  CLI11 adoption.
- Rendering: Qt custom widgets/QPainter for 2D, thin QOpenGLWidget previews for MVP 3D, planned bgfx renderer backend for production viewports.
- Text/IDE: Qt text widgets first, planned KSyntaxHighlighting, Tree-sitter, and LSP integration.
- Media: native idTech parsers first, with planned Qt Multimedia, miniaudio, and optional Assimp support.
- Accessibility/localization: Qt accessibility APIs, Qt High DPI behavior, Qt
  TextToSpeech, Qt Linguist/QTranslator, QLocale, seed TS catalogs, and CLI
  localization diagnostics.
- AI automation: optional provider-neutral connector/model/workflow layer, with
  OpenAI implemented first for configuration and reviewable no-write
  experiments including shader/entity/package/batch proposals, and planned
  Claude, Gemini, ElevenLabs, Meshy, local/offline, and custom connector paths.
- External compiler imports: Git submodules.

See [`docs/STACK.md`](docs/STACK.md) for the full stack decision record.

## License
VibeStudio-owned code is distributed under GPLv3 (`LICENSE`).

External compiler submodules and future third-party components retain their own
licenses. Review the license files in each submodule before linking, modifying,
or redistributing compiled binaries as part of VibeStudio packages.
