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
  duration, exit code, stdout/stderr, parsed diagnostics, known-issue warnings,
  preflight warnings, combined warnings, errors, and structured task-log
  entries. They can be saved and loaded through the shared core service.
- `src/core/compiler_known_issues.*` defines the high-value ericw-tools known
  issue catalog used by compiler plans. Warnings are scoped by profile/tool,
  include upstream issue IDs and suggested local actions, and are also kept in
  the manifest `knownIssueWarnings` field.
- `src/core/ericw_map_preflight.*` performs conservative Quake `.map`
  preflight checks for ericw-tools profiles. It reports path privacy, long
  values, escape sequences, external-map/prefab hazards, light-group conflicts,
  region risks, brush-entity origin keys, non-integer brush coordinates, Phong
  risks, `_minlight`, `_sunlight2`, and per-entity `world_units_per_luxel`
  warnings. Compiler plans and manifests keep these separately in
  `preflightWarnings` while preserving the combined warning list.
- `src/core/compiler_runner.*` executes compiler profiles through `QProcess`,
  captures stdout/stderr, parses warning/error lines opportunistically, links
  diagnostics to file paths and line numbers where present, supports timeout and
  cancellation callbacks, surfaces plan/preflight warnings before launch,
  refuses missing working directories, applies isolated `TMP`, `TEMP`, and
  `TMPDIR` paths to compiler processes, saves manifests, and registers produced
  outputs.
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

## ericw-tools Known-Issue Mitigation Model
VibeStudio does not patch ericw-tools in the first integration pass. High-value
and remaining-pass open upstream issues are handled through wrapper behavior,
preflight map validation, manifest provenance, artifact gates where local
services can inspect the output, and known-issue diagnostics. When a behavior
requires compiler internals or output changes, VibeStudio should identify the
risk, recommend a known-good compiler version or workflow, and keep the fix
tracked upstream.

The full remaining-pass acceptance matrix is maintained in
`docs/plans/ericw-tools-remaining-bugs-resolution.md`. That document groups
issue IDs by VibeStudio-owned status rather than duplicating the upstream audit
rows: implemented catalog warnings, implemented map preflight warnings,
registry/helper readiness, artifact validation, and upstream-only tracked
limitations. If a source/test lane has only targeted a mitigation in the
current Ralph pass, docs should say so until the code exists.

Wrapper and preflight mitigations VibeStudio owns in the current implementation:
- Run compiler processes with isolated temporary directories and register only
  expected outputs, mitigating temp-file and overwrite risks in the wrapper
  layer. `lightpreview` is discoverable as an optional helper, but native launch
  and OpenGL/Qt behavior remain upstream-owned until VibeStudio adds a
  smoke-tested preview workflow.
- Generate command manifests with resolved executable path, command line,
  inputs, outputs, hashes, warnings, and diagnostics, mitigating provenance
  gaps from #167 and #483 even when BSP/BSPX metadata is unavailable. Registry
  version/help probes remain discovery data rather than manifest fields.
- Sanitize or warn about absolute WAD paths, long entity values, escape
  sequences, dotted filenames, and hardcoded asset roots before invoking the
  compiler, covering the wrapper side of #87, #201, #230, #245, #288, #293,
  #450, and #451.
- Validate light entities for grouped/toggled light style conflicts, mismatched
  `START_OFF` flags, ambiguous `_minlight` scale, risky Phong/bmodel settings,
  and unsupported sun/surface-light combinations, covering the product-facing
  side of #122, #173, #310, #351, #377, #405, #470, and #475.
- Detect risky region and prefab compile setups, including multiple region
  brushes, region brushes with areaportals or origin brushes, external-map
  missing classnames, grouped external maps, and map-relative path ambiguity,
  covering #193, #194, #199, #207, #231, #327, #333, #390, #417, #422, and
  #444 as diagnostics or workflow constraints.
- Catalog helper-tool risks for `bspinfo` and `bsputil`, expose first-class
  helper descriptors, and smoke-test the core registry path so packaging,
  binary-name, argument-parsing, and dependency issues can become clear setup
  diagnostics. Operation-level `bspinfo`/`bsputil` probes are still planned.

Planned or diagnostic-only mitigations that are not compiler fixes:
- Post-compile BSP/BSPX validation now checks missing outputs, wrong BSP
  family, truncated/corrupt headers, lump bounds, selected face-reference
  risks, conversion-output mismatches, and missing profile-requested metadata.
  It should continue to expand for deeper semantic checks. These checks may
  block promotion or packaging, but they do not repair compiler output.
- Visual regression fixtures should track lighting, shadow, VIS, and debug
  output changes by ericw-tools version, but wrapper checks cannot guarantee
  visual parity.

Upstream-only items VibeStudio should not claim to resolve:
- Compiler output correctness regressions such as lighting artifacts, BSPX
  lump generation bugs, VIS behavior changes, corrupt BSP output, and geometry
  compile bugs. VibeStudio can flag known affected versions and maintain
  regression fixtures, but fixes belong in ericw-tools.
- New compiler features such as `world_units_per_luxel` command overrides,
  `func_viscluster`, `func_detail_null`, embedded lightmaps, custom hull sizes,
  conditional entities, model shadow casting, translucent lighting, and new
  image-format support. VibeStudio can expose options after upstream support is
  released and documented.
- Native ericw-tools build, packaging, logging, or launcher changes unless
  VibeStudio intentionally creates and documents a fork.
