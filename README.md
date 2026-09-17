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
> VibeStudio is at an extremely early pre-alpha stage. Real format work now
> exists — idTech image decoding, DEFLATE, brush and sector geometry, BSP
> inspection, chained compiles — but it sits behind inspect-and-save-as
> surfaces, not production editors. Many studio features described below remain
> product goals and roadmap targets; the implemented surfaces are listed in
> Current Development State. It is not ready for production modding, mapping,
> packaging, or asset-authoring work.

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
- Repository state: pre-alpha, with documentation, CI, compiler submodules, dependency-free idTech format readers, a painted Qt Widgets shell, and a 78-command CLI.

## Current Development State
VibeStudio is still pre-alpha, but the format layer is no longer a placeholder:
packages, images, maps, and compiled BSPs are parsed by in-tree readers, drawn
with real QPainter code, and driven end to end by a chained build pipeline. What
is still missing is depth — editors, 3D, and audio playback — not honesty about
what the file readers do.

What exists today:
- Documentation for product goals, stack, roadmap, UX, accessibility,
  localization, AI connectors, setup, compiler integration, and credits.
- Cross-platform Meson/Qt6 C++20 build with moc-generated `Q_OBJECT` widgets and
  33 Meson tests: 28 C++ smoke-test binaries plus Python validators for docs,
  samples, packaging, source layout, and build configuration. CI also runs an
  offscreen `--self-test` GUI pass and the CLI/credits validators.
- A Qt Widgets shell built around a ten-entry mode rail
  (Workspace, Levels, Models, Textures, Audio, Packages, Code, Shaders, Build,
  Settings) over a `QStackedWidget`, with a generated menu bar, a toolbar, a
  fuzzy command palette, keyboard shortcuts taken from the shared semantics
  registry, non-color-only status chips, drag-and-drop file/package opening,
  confirmation prompts before destructive actions, a persistent window/mode
  state, and a date-stamped session log that captures Qt warnings and above.
- A dependency-free DEFLATE codec (RFC 1951/1950 inflate plus a deterministic
  fixed-Huffman encoder, CRC-32, Adler-32), so compressed ZIP/PK3 entries are
  actually read and written rather than skipped.
- Package browsing for folders, PAK, WAD, ZIP, and PK3, including ZIP64 central
  directories, nested-archive detection, layered mounting where a second archive
  overrides a base one, normalized virtual paths, traversal and symlink-safe
  extraction, entry filtering, detail drawers, and activity-center scan/extract
  tasks.
- Package staging and save-as with add/import, replace, rename, delete, conflict
  reporting, before/after composition, schema-versioned manifests, and
  deterministic PAK, ZIP/PK3, PWAD, and WAD2/WAD3 writers.
- idTech image decoding for Doom patches, flats, `PLAYPAL` and `COLORMAP`,
  Quake `.lmp`, WAD2/WAD3 miptextures with mip chains, Quake II `.wal` with
  surface/content flags, PCX, Targa, and Quake `.spr` frames — with the palette
  resolved at run time from the package the user opened, a clearly-labelled
  generated fallback when no game palette is present, palette quantization, and
  a swatch view that marks the transparent index.
- Graphical asset surfaces painted with QPainter: a zoomable, pannable image
  preview with checkerboard alpha, nearest-neighbour magnification, and mip/frame
  stepping; a palette swatch grid; a per-channel audio waveform; and composition,
  pipeline, and timeline charts that each carry a non-color cue and a text
  summary.
- An interactive 2D map viewport: real Doom vertices, linedefs, traced sector
  fills and things, and solved Quake-family brush footprints and Quake III patch
  outlines, with click-to-select, drag-to-pan, wheel zoom, Tab cycling, three
  orthographic projections, grid and label toggles, and a high-contrast mode.
- Map parsing with a real tokenizer covering classic, Valve 220, `brushDef`,
  `brushDef3`, `patchDef2`, and `patchDef3` primitives, Doom and Hexen lump
  strides, brush-plane solving by half-space intersection, sector outline
  tracing, undo/redo, and save-fidelity-checked non-destructive save-as. UDMF
  (`TEXTMAP`) maps are detected and reported, not edited.
- Deterministic headless SVG map rendering that shares the same geometry solver
  as the viewport, so a `map render` picture matches the on-screen one.
- Read-only inspection of compiled artifacts: Quake BSP29/BSP2/2PSB, Quake II
  IBSP38, and Quake III IBSP46/RBSP lumps, entities, and textures, plus the
  `.pts`/`.lin` leak point files and `.prt` portal files the compilers leave
  beside them. Every offset and count is bounds-checked against the real file.
- Chained build pipelines for Quake (`qbsp` → `vis` → `light`), Quake III
  (q3map2 BSP → vis → light), and Doom node building (ZDBSP, ZokumBSP), running
  stage by stage through the shared compiler runner with per-stage logs,
  diagnostics, leak detection, hashes, manifests, optional stages, per-stage
  extra arguments, and output registration.
- Compiler registry and executable discovery for imported ericw-tools, q3map2,
  ZDBSP, and ZokumBSP, with reviewable command plans, flag-aware expected output
  paths, streamed stdout/stderr, parsed diagnostics, schema-versioned command
  manifests, run/rerun, copy-command, and user/project executable overrides.
- Reviewable game launch plans built from a saved installation profile, plus
  optional execution of the planned command line.
- Asset analysis for MDL/MD2/MD3 model metadata with skin and material
  dependencies, WAV metadata with decoded peaks, real Ogg/Vorbis, MP3, and FLAC
  header parsing, and CFG/shader/QuakeC text diagnostics.
- A text surface with a data-driven `QSyntaxHighlighter` for config, idTech3
  shader, QuakeC, and adjacent languages, theme-aware colours, open/save of
  project files, diagnostics, and project-wide find/replace.
- idTech3 shader script parsing into stage graphs with package-reference
  validation and round-tripped stage edits written to a save-as path.
- Manual game installation profiles with stable IDs, game keys, engine-family
  defaults, read-only validation, GUI management, and confirmable Steam/GOG
  candidate detection.
- Project manifests at `.vibestudio/project.json` with current-project
  persistence, project-local overrides, schema migration for the settings store,
  a `--settings-file` override that keeps automation out of the user's real
  preference store, and a workspace dashboard health summary.
- Workspace workbench panels for project problems, search across project files
  and mounted package entries, Git changed/staged files, recent activity,
  reveal-in-folder, and copy-virtual-path actions.
- A CLI subcommand router with 78 registered commands across the `cli`, `ui`,
  `project`, `package`, `asset`, `map`, `bsp`, `build`, `launch`, `texture`,
  `shader`, `sprite`, `code`, `localization`, `diagnostics`, `extension`,
  `compiler`, `install`, `editor`, `about`, `ai`, and `credits` families, with
  JSON output for automation and a documented stable exit-code contract.
- Runtime translation loading: `lrelease` compiles the checked-in `.ts` catalogs
  into `.qm` files, the application resolves and installs the catalog for the
  selected locale at start-up, and applies right-to-left layout direction where
  the locale requires it.
- Localization target metadata and Qt TS catalogs for 20 languages plus
  pseudo-localization, with CLI reports for RTL smoke coverage, locale
  formatting, pluralization samples, expansion layout checks, catalog status,
  and dry-run Qt Linguist extraction validation.
- Portable release packaging scripts for Windows, macOS, and Linux target
  bundles with generated offline guide, platform smoke notes, checksums,
  samples, package manifests, and VibeStudio/imported-compiler license bundle.
- Imported compiler source submodules and CI that builds, tests, validates
  samples and docs, and runs the offscreen GUI smoke pass on every PR.

What does not exist yet:
- No 3D viewport and no model geometry rendering. Models are read for metadata
  and their first resolvable skin is decoded and shown; vertices, triangles, and
  tags are counted and listed, never drawn.
- No audio playback. Audio entries are analysed and drawn as a waveform; nothing
  is decoded to a sound device, and compressed codecs are read for headers only.
- No in-place package editing. Save-as to a different path is the only write
  path, and an in-place overwrite is explicitly blocked. There is no package
  compare tooling and no binary format editor.
- No text-to-speech engine. The TTS preference is stored and reported, but no
  speech backend is wired up.
- No AI provider network calls. Every AI command produces a local, reviewable,
  no-write proposal; the application makes no outbound HTTP requests at all.
- No full-production level, model, texture, audio, sprite, shader, code, or
  script editors. Map editing is limited to entity key/value edits and single
  object moves written to a new file; shader editing is limited to one stage
  directive at a time; UDMF maps cannot be edited.
- No translated user interface. The catalogs are seeds: the loading path works,
  but only the pseudo-locale carries translated text, so the UI still renders in
  the source language.
- No full guided first-run setup, no completed accessibility audit, and no
  production-ready workflow of any kind.

Treat every feature list below as roadmap intent until the roadmap and support
matrix mark it implemented.

## Studio Goals
These are product targets, not a feature list. Some already have a working slice
behind them — package management, compiler orchestration, installation
management, 2D level viewing, texture decoding — and the rest are unbuilt. Read
Current Development State for what the code does today.

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
The repository currently contains:
- A Meson/Qt6 C++20 app target named `vibestudio`, with moc wiring for the
  shell's `Q_OBJECT` widgets and an `i18n` subproject that compiles the
  translation catalogs.
- A Qt Widgets studio shell with a mode rail over a `QStackedWidget`, a
  generated menu bar and toolbar, a command palette, keyboard shortcuts, status
  chips, drag-and-drop, and an activity center with task state, progress,
  warnings, cancellation, and per-task logs.
- A CLI surface for version/platform diagnostics, project/package/installation
  management, asset and texture inspection, map inspection and rendering, BSP
  inspection, chained builds, game launch plans, shader, sprite, code,
  extension, localization, diagnostics, and AI workflows, plus credits
  validation, JSON output, quiet/verbose modes, watch streaming, and task-state
  automation.
- Reusable shell UI primitives for loading/progress placeholders and
  detail-on-demand logs or metadata.
- A shared package/archive layer adapted from PakFu's archive surface, with safe
  normalized virtual paths, traversal checks, and a symlink-safe extractor.
- Folder, PAK, WAD, ZIP, and PK3 mounting through shared core services used by
  both GUI and CLI, with a dependency-free DEFLATE decoder so compressed PK3
  entries read, ZIP64 support, and nested-archive detection.
- Package staging with save-as writers for PAK, ZIP/PK3, PWAD, and WAD2/WAD3
  outputs plus schema-versioned staging manifests; in-place overwrite is
  blocked.
- Painted graphical shell views for project health, package composition by
  type/size, build pipeline stages, the activity timeline, level-map statistics,
  decoded textures, palette swatches, and audio waveforms.
- Level-map services and UI/CLI surfaces for Doom WAD map lump inspection,
  Quake-family and Quake III `.map` parsing, brush and sector geometry solving,
  an interactive 2D viewport, deterministic SVG rendering, entity/property
  lists, texture/material references, validation, safe entity/movement edits,
  undo/redo state, non-destructive save-as, and compiler profile handoff.
- Compiled-artifact inspection for Quake, Quake II, and Quake III BSP files plus
  the leak point and portal files emitted alongside them.
- Chained build pipelines for Quake, Quake III, and Doom node building, and
  reviewable game launch plans built from installation profiles.
- Advanced Studio services and UI/CLI surfaces for shader script parsing and
  stage edits, sprite naming/sequencing/package plans, source indexing with
  syntax highlighting, extension discovery/command planning, and staged AI
  creation proposals.
- A data-driven editor profile registry with routed presets for VibeStudio
  default, GtkRadiant 1.6.0-style, NetRadiant Custom-style, TrenchBroom-style,
  and QuArK-style workflows.
- AI-free-by-default settings, provider-neutral connector/model metadata,
  redacted credential discovery, safe AI-callable tool descriptors, and
  local-only proposal workflows for OpenAI, Claude, Gemini, ElevenLabs, Meshy,
  local/offline, and custom connector paths. No provider is ever contacted.
- Tiny license-clean sample projects for Doom, Quake, and Quake III-family
  smoke checks.
- A portable packaging skeleton that stages the built binary, docs, credits,
  licenses scaffold, samples, and a package manifest.
- An About/Credits/license surface shared by the GUI inspector and CLI.
- CI workflows for cross-platform build/test, an offscreen GUI self-test, sample
  validation, packaging skeleton validation, and submodule verification.
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
- Qt Linguist `lrelease`, optional: present it compiles the `.qm` translation
  catalogs, absent the build still succeeds and the application uses the source
  language
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

Global options that apply to any invocation:
- `--settings-file <path>`: resolve every persistent setting against an explicit
  INI file instead of the user's real preference store. Read before any other
  argument, so scripted runs, CI, and tests never mutate real preferences.
- `--json`: emit machine-readable JSON for supported commands.
- `--quiet`: suppress successful narration; errors still go to stderr.
- `--verbose`: print timing and extra diagnostics for supported text commands.
- `--dry-run`: report what a write-capable command would do without touching the
  file system.
- `--overwrite`: allow a command to replace an existing output file.
- `--watch`: stream task-log entries while a long-running command is active.
- `--task-state`: include machine-readable task state objects in JSON output.

The GUI binary also accepts `--self-test`, which builds the shell, visits every
mode so each page is laid out and painted at least once, reports shortcut
conflicts, and exits. CI runs it under an offscreen platform plugin as a GUI
smoke check:
```sh
QT_QPA_PLATFORM=offscreen ./builddir/src/vibestudio --self-test
```

Single-flag commands:
- `--version`: print the application version.
- `--help`: print CLI help.
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
- `--extract-all`: extract every readable package entry.
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

Command families (78 registered commands; `vibestudio --cli cli commands --json`
prints the authoritative list):

`cli` and `ui`
- `cli exit-codes`: print the stable exit-code contract.
- `cli commands`: print the registered command surface used for help and
  documentation validation.
- `ui semantics`: report non-color-only status chip semantics, default
  shortcuts, shortcut conflicts, and command palette entries.

`project`
- `project init <path> [--installation <id>] [--editor-profile <id>]`: create
  or refresh a project manifest and optional project-local overrides.
- `project info <path>`: print project manifest and health summary.
- `project validate <path>`: validate project health and return
  `validation-failed` for blocking issues.

`install`
- `install list`: list saved manual game installation profiles.
- `install detect [--root <path>]`: detect Steam/GOG candidates read-only
  without saving profiles.
- `install add <root> [--install-game <key>] [--install-engine <id>]
  [--install-name <name>] [--install-executable <path>]
  [--install-base-packages <paths>] [--install-mod-packages <paths>]
  [--install-palette <id>] [--install-compiler-profile <id>]
  [--install-read-only <on|off>] [--install-hidden <on|off>] [--dry-run]`:
  add or update a manual installation profile.
- `install select <id>`: mark a saved profile as the selected installation.
- `install validate <id>`: validate a saved profile read-only.
- `install remove <id> [--dry-run]`: remove a saved profile without touching
  game files.

`package`
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
  output paths, and optional manifest JSON. Writing back over the source path is
  refused.

`asset` and `texture`
- `asset inspect <package> <virtual-path>`: inspect image, model, audio, text,
  script, or binary metadata for one package entry.
- `asset convert <package> --output <folder> [--entry <virtual-path>]`:
  batch-convert package images with optional `--format`, `--crop`, `--resize`,
  `--palette`, `--dry-run`, and `--overwrite`.
- `asset audio-wav <package> <virtual-path> --output <file.wav>`: export
  readable WAV/PCM package entries with dry-run and overwrite controls.
- `asset find` / `asset replace`: search or safely replace project text/script
  assets with file/line matches and save-state reporting.
- `texture decode <package> <virtual-path> [--palette <id>] [--output <file.png>] [--dry-run] [--overwrite]`:
  decode a Doom patch or flat, Quake `.lmp`, WAD2 or WAD3 miptexture, Quake II
  `.wal`, PCX, Targa, or Quake `.spr` entry, report dimensions, mip levels,
  frames, transparency, and which palette was used, and optionally write a PNG.
- `texture palette [<package>] [--palette <id>]`: resolve the palette used to
  decode indexed art and report whether it came from the package, which virtual
  path it was read from, which paths were searched, and whether a generated
  stand-in was substituted.

`map` and `bsp`
- `map inspect`, `map edit`, `map move`, and `map compile-plan`: inspect Doom
  WAD map lumps and Quake-family `.map` files, make safe non-destructive edits,
  and hand off to compiler profile plans.
- `map render <path> [--projection top-xy|front-xz|side-zy] [--width <px>] [--height <px>] [--grid <units>] [--no-grid] [--labels] [--high-contrast] [--highlight <object>] [--output <file.svg>] [--dry-run] [--overwrite]`:
  render a deterministic SVG picture of a map. Without `--output` the SVG goes
  to stdout.
- `bsp inspect <path.bsp>`: inspect a compiled Quake, Quake II, or Quake III
  BSP — magic, version, lump table, entities, and textures — plus any
  `.pts`/`.lin` leak point file and `.prt` portal file sitting beside it.
  Returns `validation-failed` when the BSP does not parse.

`build` and `launch`
- `build list`: list the chained pipelines and their stages. Current pipelines
  are `quake-full`, `quake-fast`, `quake-bsp-only`, `quake3-full`,
  `quake3-bsp-only`, `doom-zdbsp`, and `doom-zokumbsp`.
- `build plan <pipeline> --input <path>`: plan a pipeline without running
  anything. Optional flags include `--output <path>`, `--working-directory`,
  `--workspace-root`, `--disable-stage <id>`, `--stage-args <id>=<args>`,
  `--compiler-search-paths <paths>`, and `--timeout-ms <ms>`.
- `build run <pipeline> --input <path>`: run the pipeline stage by stage through
  the shared compiler runner, chaining each stage output into the next, with
  `--watch` streaming, per-stage logs, diagnostics, leak detection,
  `--manifest <folder>` records, and `--register-output`.
- `launch plan [--installation <id>] [--launch-profile <id>] [--map <name>] [--mod <dir>] [--basedir <dir>] [--bsp <path>] [--executable <path>] [--extra-args "<args>"]`:
  build a reviewable launch command line without starting anything.
- `launch run [same options]`: start the configured game installation with the
  planned command line and report the process id.

`compiler`
- `compiler list`: print compiler registry and executable discovery.
- `compiler profiles`: list wrapper profiles, currently ericw-tools
  `qbsp`/`vis`/`light`, ZDBSP nodes, ZokumBSP nodes, and q3map2 BSP, vis, and
  light stages.
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

`shader`, `sprite`, `code`, and `editor`
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
- `editor profiles` / `editor current` / `editor select <id>`: list, print, or
  select the routed editor interaction profile.

`localization` and `diagnostics`
- `localization targets` / `localization report`: list the target language set
  or report pseudo-localization, RTL, locale formatting, expansion, compiled
  `.qm` coverage, and TS catalog status.
- `diagnostics bundle [--output <folder>]`: print or write a redacted support
  bundle with version, platform, command, module, operation-state, and
  localization diagnostics.

`extension`, `about`, and `credits`
- `extension discover`, `extension inspect`, and `extension run`: load
  `vibestudio.extension.json` manifests, report trust/sandbox metadata, and
  dry-run or execute reviewed extension command plans.
- `about show`: print version, platform, imported compiler, and credits
  metadata.
- `credits validate`: validate README and `docs/CREDITS.md` coverage plus
  compiler pins, `.gitmodules`, and checked-out submodule revisions.

`ai` (every command is local and no-write; no provider is ever contacted)
- `ai status` / `ai connectors` / `ai tools`: print AI-free mode and opt-in
  settings, provider-neutral connector descriptors, and safe AI-callable
  VibeStudio tool descriptors.
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
- Application framework: Qt 6 (Core, Gui, Widgets, Network), with `moc` run over
  the shell's `Q_OBJECT` widgets through Meson's `qt6.preprocess`.
- Primary UI: Qt Widgets, with bounded Qt Quick/QML use only where it clearly improves a contained workflow.
- Build: Meson + Ninja, with an `i18n` subdirectory that runs `lrelease` over the
  checked-in `.ts` catalogs to produce the `.qm` files `QTranslator` loads.
  `lrelease` is optional: without it the build succeeds and the application falls
  back to the source language.
- Automation: Python validation scripts and GitHub Actions.
- Project data: JSON manifests, Qt settings with schema migration and a
  `--settings-file` override, and planned SQLite/FTS indexing.
- CLI: active lightweight subcommand router with a testable command registry,
  JSON output, stable exit codes, quiet/verbose modes, dry-run behavior, watch
  streaming, task-state output, and deferred CLI11 adoption.
- Compression: an in-tree DEFLATE codec written from RFC 1951/1950 rather than
  linked against zlib, so ZIP/PK3 reading and writing stays dependency-free,
  deterministic, and fixture-testable, with CRC-32 and Adler-32 alongside it.
- Rendering: real QPainter work today — the map viewport, image and palette
  views, waveform, and charts are all custom-painted widgets, and headless map
  rendering emits SVG as plain text with no Qt paint device at all. 3D is not
  started; a thin QOpenGLWidget preview and a later bgfx backend remain planned.
- Text/IDE: Qt text widgets with a data-driven `QSyntaxHighlighter` for config,
  shader, and QuakeC sources; KSyntaxHighlighting, Tree-sitter, and LSP
  integration remain planned.
- Media: native idTech parsers only — images, models, and audio headers are read
  in-tree. Qt Multimedia, miniaudio, and optional Assimp support remain planned,
  and nothing currently plays audio or draws model geometry.
- Accessibility/localization: Qt accessibility APIs, Qt High DPI behavior, Qt
  Linguist with `QTranslator` installed at start-up, compiled `.qm` catalogs,
  `QLocale`, layout direction applied for RTL locales, seed TS catalogs, and CLI
  localization diagnostics. Qt TextToSpeech is a stack target, not a dependency
  yet; the TTS preference is stored but no speech backend is wired up.
- AI automation: optional provider-neutral connector/model/workflow layer, with
  OpenAI described first for configuration and reviewable no-write experiments
  including shader/entity/package/batch proposals, and planned Claude, Gemini,
  ElevenLabs, Meshy, local/offline, and custom connector paths. No HTTP client
  is wired up: every proposal is generated locally and no provider is contacted.
- External compiler imports: Git submodules.

See [`docs/STACK.md`](docs/STACK.md) for the full stack decision record.

## License
VibeStudio-owned code is distributed under GPLv3 (`LICENSE`).

External compiler submodules and future third-party components retain their own
licenses. Review the license files in each submodule before linking, modifying,
or redistributing compiled binaries as part of VibeStudio packages.
