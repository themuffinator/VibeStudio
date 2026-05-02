# Compiler Integration

VibeStudio imports level compiler and node-builder sources as Git submodules.
The first integration step is orchestration: build or locate compiler
executables, run them through a structured wrapper, capture diagnostics, and
feed outputs back into the project/package graph.

## Imported Sources

| Tool | Local path | Main role |
|---|---|---|
| ericw-tools | `external/compilers/ericw-tools` | Quake/idTech2 `qbsp`, `vis`, `light`, `bspinfo`, `bsputil`. |
| q3map2-nrc | `external/compilers/q3map2-nrc/tools/quake3/q3map2` | q3map2 from NetRadiant Custom for Quake III/idTech3 BSP compile, light, conversion, and package helpers. |
| ZDBSP | `external/compilers/zdbsp` | Doom-family node building, including GL and extended node formats. |
| ZokumBSP | `external/compilers/zokumbsp` | Doom-family node, blockmap, and reject building with vanilla-focused output. |

Initialize imports:
```sh
git submodule update --init --recursive
```

## Integration Model
1. Discover compiler availability from bundled builds, user paths, source-port toolchains, and project-local overrides.
2. Normalize compiler profiles by engine family, map format, source file, target game, output package, and quality preset.
3. Generate a command manifest before every run.
4. Execute compilers in a task sandbox with captured stdout/stderr, exit code, duration, environment, and file outputs.
5. Parse diagnostics into clickable editor markers where possible.
6. Register produced BSP/WAD/node/lightmap/assets with the package manager.

Current implementation:
- `src/core/compiler_registry.*` defines descriptors for ericw-tools `qbsp`,
  `vis`, and `light`, NetRadiant Custom `q3map2`, ZDBSP, and ZokumBSP.
- Discovery checks imported source directories, known build-output locations,
  optional extra search paths, user-configured executable overrides,
  project-local executable overrides, and PATH. The registry records static
  capability flags for ericw-tools, Doom node builders, and q3map2, and can run
  short version/help probes when listing tools.
- GUI inspector details and CLI `--compiler-registry` expose source/executable
  availability without running external tools.
- `src/core/compiler_profiles.*` defines ericw-tools `qbsp`, `vis`, and
  `light`, ZDBSP and ZokumBSP node-builder profiles, and q3map2 probe/BSP
  wrapper profiles. The planner produces reviewable command plans with program,
  arguments, working directory, expected output path, warnings, and readiness.
- Compiler command manifests are schema-versioned JSON documents generated from
  command plans and runs. They include command line, working directory,
  environment subset, inputs, expected and registered outputs, file hashes,
  duration, exit code, stdout/stderr, parsed diagnostics, warnings, errors, and
  structured task-log entries. They can be saved and loaded through the shared
  core service.
- `src/core/compiler_runner.*` executes compiler profiles through `QProcess`,
  captures stdout/stderr, parses warning/error lines opportunistically, links
  diagnostics to file paths and line numbers where present, supports timeout and
  cancellation callbacks, saves manifests, and registers produced outputs.
- CLI `compiler profiles`, `compiler plan`, `compiler manifest`,
  `compiler run`, `compiler rerun`, `compiler copy-command`,
  `compiler set-path`, and `compiler clear-path` expose the same services used
  by the GUI. `compiler run` can save a manifest and register outputs into a
  project manifest with `--register-output`, stream task-log entries with
  `--watch`, and add machine-readable task-state JSON with `--task-state`.
- The Qt Widgets shell shows a pipeline graphic, per-profile readiness, run and
  cancellation actions, activity-center summaries/logs, and copy actions for a
  CLI equivalent and JSON manifest.

## CLI Examples

```sh
vibestudio --cli compiler set-path ericw-qbsp --executable C:\tools\qbsp.exe
vibestudio --cli compiler plan ericw-qbsp --input maps/start.map --workspace-root E:\Projects\QuakeMod
vibestudio --cli compiler run ericw-qbsp --input maps/start.map --workspace-root E:\Projects\QuakeMod --manifest build/qbsp-run.json --register-output --watch
vibestudio --cli compiler rerun build/qbsp-run.json --manifest build/qbsp-rerun.json
vibestudio --cli compiler copy-command build/qbsp-run.json
```

## License Boundary
External compilers are kept as submodules and treated as separate tools until a
specific source-level merge is reviewed. This matters because the imported
tools use GPL-2.0-era licensing, mixed GPL/LGPL/BSD file licensing, or
dependency-specific terms.

Rules:
- Keep upstream license files in place.
- Credit upstream in `README.md` and `docs/CREDITS.md`.
- Prefer process execution over static linking for the first integration.
- If a VibeStudio fork is needed, document the fork URL, branch, revision, and reason here.
- If compiler code is copied or modified in-tree, preserve headers and add nearby comments for derived code.

## Planned Build Strategy
- Keep VibeStudio's main build independent from compiler builds by default.
- Add optional Meson features only after each tool's native build requirements are documented.
- Prefer a `tools/compiler-build` helper that can produce platform-specific tool bundles in CI.
- Store built tools in release artifacts with their corresponding source revision and license bundle.

## Engine Mapping
- idTech1 Doom-family maps: WAD map lumps, UDMF/TextMap variants, ZDBSP and ZokumBSP node/blockmap/reject profiles.
- idTech2 Quake-family maps: `.map` to BSP through ericw-tools `qbsp`, `vis`, and `light`.
- idTech3 Quake III-family maps: `.map` to BSP through q3map2-nrc, the q3map2 compiler imported from NetRadiant Custom, including shader-aware light and packaging flows.

## Diagnostics Contract
Compiler wrappers should emit:
- Start/end timestamps.
- Working directory.
- Full command line with redacted secrets if any are ever introduced.
- Source files and package mounts.
- Output files and hashes.
- Parsed warnings/errors with source locations where available.
- Re-run recipe.
