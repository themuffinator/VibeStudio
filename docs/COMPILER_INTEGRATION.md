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
  `vis`, `light`, `bspinfo`, `bsputil`, and `lightpreview`, NetRadiant Custom
  `q3map2`, ZDBSP, and ZokumBSP.
- Discovery checks imported source directories, known build-output locations,
  optional extra search paths, user-configured executable overrides,
  project-local executable overrides, and PATH. The registry records static
  capability flags for ericw-tools, Doom node builders, and q3map2, and can run
  short version/help probes when listing tools. `lightpreview` is discovered
  presence-only and is never probed, because it is a GUI helper.
- GUI inspector details and CLI `--compiler-registry` expose source/executable
  availability without running external tools.
- `src/core/compiler_profiles.*` defines fifteen wrapper profiles across the
  four toolchains (see [Wrapper Profiles](#wrapper-profiles)). The planner
  produces reviewable command plans with program, arguments, working directory,
  expected output path, further required outputs, optional sibling outputs,
  warnings, and readiness.
- Compiler command manifests are schema-versioned JSON documents (currently
  `CompilerCommandManifest::kSchemaVersion` 4) generated from command plans and
  runs. They include command line, working directory, environment subset,
  inputs, expected and registered outputs, an `expectedOutputKnown` flag for
  stages whose destination cannot be predicted, optional output paths, file
  hashes, duration, exit code, stdout/stderr, parsed diagnostics, known-issue
  warnings, preflight warnings, combined warnings, errors, and structured
  task-log entries. They can be saved and loaded through the shared core
  service.
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
  streams stdout/stderr line by line while the process runs, parses diagnostics
  incrementally, links them to file paths and line numbers where present,
  supports timeout and cancellation callbacks, surfaces plan/preflight warnings
  before launch, refuses missing working directories, applies isolated `TMP`,
  `TEMP`, and `TMPDIR` paths to compiler processes, detects leaks, validates
  produced artifacts, saves manifests, and registers produced outputs.
- `src/core/build_pipeline.*` chains those single-profile wrappers into
  multi-stage builds and turns a finished build into a reviewable game launch
  command line. See [Chained Build Pipelines](#chained-build-pipelines).
- CLI `compiler profiles`, `compiler plan`, `compiler manifest`,
  `compiler run`, `compiler rerun`, `compiler copy-command`,
  `compiler set-path`, and `compiler clear-path` expose the same services used
  by the GUI. `compiler run` can save a manifest and register outputs into a
  project manifest with `--register-output`, stream task-log entries with
  `--watch`, and add machine-readable task-state JSON with `--task-state`.
  CLI `build list`, `build plan`, `build run`, `launch plan`, `launch run`, and
  `bsp inspect` cover pipelines, launching, and compiled-artifact inspection.
- The Qt Widgets shell shows a pipeline graphic, per-profile readiness, run and
  cancellation actions, activity-center summaries/logs, and copy actions for a
  CLI equivalent and JSON manifest. The Build surface adds a pipeline chooser,
  a stage list, and Run / Cancel / Copy Commands actions; pipelines run on a
  worker thread and report stage start/finish and every log line back into the
  activity center.
- `scripts/validate_credits.py` and CLI `credits validate` compare imported
  compiler pins across `src/core/studio_manifest.cpp`, `.gitmodules`,
  `README.md`, [`docs/CREDITS.md`](CREDITS.md), and the checked-out submodule
  revisions.

## Wrapper Profiles
A profile is the wrapper layer's unit of work: one tool, one stage, one command
shape. `CompilerProfileDescriptor` in `src/core/compiler_profiles.h` carries the
stage token, the input extensions, how the tool accepts an output path, where it
writes by default, which sibling files it produces or needs, and its named
argument presets.

| Profile id | Tool | Stage | Input | Output behavior |
|---|---|---|---|---|
| `ericw-qbsp` | ericw-tools `qbsp` | `qbsp` | `.map` | Trailing positional destination; defaults to `<input base>.bsp` |
| `ericw-vis` | ericw-tools `vis` | `vis` | `.bsp` | Rewrites its input in place; requires a sibling `.prt` |
| `ericw-light` | ericw-tools `light` | `light` | `.bsp` | Rewrites its input in place; `-lit` adds a required `<base>.lit` |
| `ericw-bspinfo` | ericw-tools `bspinfo` | `inspect` | `.bsp` | Writes `<base>.bsp.json` beside the BSP |
| `ericw-bsputil-check` | ericw-tools `bsputil` | `inspect` | `.bsp` | `--check`; console output only, no artifact |
| `ericw-bsputil-extract-entities` | ericw-tools `bsputil` | `extract` | `.bsp` | `--extract-entities`; writes a sibling `.ent` |
| `ericw-bsputil-extract-textures` | ericw-tools `bsputil` | `extract` | `.bsp` | `--extract-textures`; writes a sibling `.wad` |
| `zdbsp-nodes` | ZDBSP | `nodes` | `.wad` | `-o <path>` before the input; without it ZDBSP writes `tmp.wad` into the working directory |
| `zokumbsp-nodes` | ZokumBSP | `nodes` | `.wad` | `-o <path>` after the input; without it the input is rewritten in place |
| `q3map2-probe` | q3map2 | `probe` | none | `-help`; console output only, no artifact |
| `q3map2-bsp` | q3map2 | `bsp` | `.map` | No stage token (BSP is q3map2's fall-through); default arguments are `-meta`; writes `<input base>.bsp` with `.prt`, `.srf`, and `.lin` as optional siblings |
| `q3map2-vis` | q3map2 | `vis` | `.bsp` | `-vis`, in place; requires a sibling `.prt` |
| `q3map2-light` | q3map2 | `light` | `.bsp` | `-light`, in place |
| `q3map2-convert` | q3map2 | `convert` | `.bsp` or `.map` | `-convert`; destination depends on `-format`, so it is treated as unknown |
| `q3map2-pk3` | q3map2 | `package` | `.bsp` | `-pk3`; writes into the engine path, so the destination is treated as unknown |

### q3map2 Stage Dispatch
q3map2 parses its general options first, then dispatches on the *front* of what
is left: `main.cpp` in NetRadiant Custom uses `args.takeFront("-vis")`,
`takeFront("-light")`, `takeFront("-pk3")`, `takeFront("-convert")` and so on,
and falls through to `BSPMain` when nothing matches. A stage token pushed behind
user-supplied extras is therefore not a stage token at all - q3map2 silently
runs a BSP compile instead.

The planner handles this with `CompilerProfileDescriptor::leadingStageArgument`,
which `buildCompilerCommandPlan()` always emits as argument 0, before the
profile's default arguments and before any caller extras. `q3map2-bsp` carries
no leading token on purpose, because `BSPMain` is the fall-through case. The
same mechanism carries `bsputil`'s `--check`, `--extract-entities`, and
`--extract-textures` sub-commands.

### Output Paths
`CompilerOutputArgumentStyle` records how a tool accepts a destination, and
`CompilerDefaultOutputMode` records where it writes when VibeStudio does not
supply one:
- `Positional` - the path is a trailing argument. ericw-tools `qbsp` accepts
  `sourcefile.map [destfile.bsp]`.
- `Flag` - the path follows a flag. Both Doom node builders take `-o`, but the
  ordering differs: ZDBSP parses options before the input, so VibeStudio emits
  `-o <output> <input>`, while ZokumBSP reads `-o` after the input file and its
  level list, so VibeStudio emits `<input> -o <output>`. This is what
  `outputArgumentAfterInput` selects.
- `None` - the tool has no destination argument and rewrites its input
  (ericw-tools `vis`/`light`, q3map2 `-vis`/`-light`). Requesting an output path
  for such a profile produces a plan warning rather than a silently ignored
  argument.
- Defaults: `DerivedFromInput`, `InPlace`, `WorkingDirectoryFile` (ZDBSP's
  `tmp.wad`, which also raises a plan warning), `Unknown` (q3map2 `-convert` and
  `-pk3`, which clears `expectedOutputKnown` so artifact validation skips the
  file), and `NoArtifact` (`bsputil --check`, `q3map2 -help`).

A derived output that collapses onto the input path is rejected and downgraded
to "unknown", so validation can never pass by inspecting an untouched input.

### Named Argument Presets
`CompilerArgumentPreset` gives each profile a small catalog of reviewed,
translatable switches instead of free-form strings: an id, a display name, a
description, the literal arguments, and - for switches that take a value - a
`requiresValue` flag with a placeholder the caller fills in.
- ericw-tools `qbsp`: `bsp2`, `hlbsp`, `q2bsp`, `qbism`, `hexen2`, `notex`,
  `leaktest`, and `wadpath` (takes a directory).
- ericw-tools `vis`: `level4`, `fast`.
- ericw-tools `light`: `extra4`, `bounce`, `lit`, `soft`.
- q3map2 (shared by the BSP, vis, light, convert, and pk3 profiles): `meta`,
  `fast`, `fs-basepath` and `fs-game` (take a directory and a mod name),
  `threads` (takes a count), and `verbose`.

Presets are resolved through `compilerArgumentPresetsForProfile()` and
`compilerArgumentPresetForId()` and are covered by
`src/tests/compiler_profiles_smoke_test.cpp`. They are a core-layer catalog
today: neither the CLI nor the shell has a preset picker yet, so callers still
pass the resolved arguments as extras.

## Diagnostics And Leak Detection

### Live Output Streaming
`runCompilerCommand()` polls the process on a 100 ms cadence, drains both
channels, and splits them into complete lines. Each line becomes a task-log
entry as it arrives - `stderr` lines are prefixed `[stderr]` - so `--watch`, the
activity center, and the pipeline log all show progress while the compiler is
still running rather than after it exits. Any trailing partial line is flushed
once the process finishes. Cancellation and the timeout are checked on the same
loop and kill the process.

### Diagnostic Parsing
`CompilerDiagnosticParser` consumes one line at a time and keeps per-channel
state, which is what makes the two-line error form work:
- **Fatal-error banners.** ericw-tools (`common/log.cc`) and q3map2
  (`tools/quake3/common/inout.cpp`) both print a `*** ERROR ***`-style banner
  and put the actual message on the *next* line. A line matching the banner is
  held, and the following line is emitted as a single error diagnostic whose
  `rawLine` contains both. A banner with nothing after it is still flushed as an
  error when the run ends.
- **ericw source locations.** ericw-tools report positions as
  `<source>[line N]` (from the `parser_source_location` formatter in
  `include/common/parser.hh`), not as `path:N`. That form is matched first, and
  the file name is only taken when it carries a known content extension.
- **Bare line numbers.** Many qbsp warnings are `WARNING: <line>: message` with
  no file name; the line number is still captured.
- **Generic fallback.** Otherwise a `path[:line[:column]]` pattern is tried,
  restricted to a fixed extension list so ordinary prose is not mistaken for a
  path.
- **Status lines are not diagnostics.** `0 errors`, `no warnings`, and
  `Error count: 0` are recognised as summaries and ignored, so a clean compile
  does not report itself as failing.

Diagnostics carry their originating channel (`stdout` or `stderr`), and are
folded into the manifest's `errors` or `warnings` list by level.

### Leak Detection
A leaked Quake compile still exits 0, so the exit code cannot be trusted.
`detectLeak()` runs for qbsp-stage profiles and reports a leak when either
signal appears:
- a `.pts` leak point file exists among the run's optional outputs (qbsp writes
  `<bsp>.pts` and `<bsp>.leak.prt` from `qbsp/outside.cc`; both, plus
  `<bsp>.prt`, are declared as related outputs of the `ericw-qbsp` profile), or
- the captured output contains `Reached occupant "<classname>" at (<x y z>)`,
  which gives the entity and position that escaped into the void.

The resulting warning names the entity, the position, and the leak point file to
load in the editor. Independently, `inspectCompiledMapArtifacts()` in
`src/core/bsp_inspect.*` scans the output folder for `.pts`, `.lin`, and
`.leak*` files beside a BSP, parses their coordinate triples into a leak line
with bounds, and warns that visibility and lighting results cannot be trusted.
CLI `bsp inspect` exposes that report.

### The `.prt` Requirement For Visibility
Both `ericw-vis` and `q3map2-vis` declare `.prt` in
`requiredCompanionInputExtensions`, because `vis.cc` and `vis.cpp` load
`<bsp base>.prt` written by the preceding BSP stage. When that file is missing,
the plan warns before anything is launched and names the classic cause: qbsp
found a leak, so the portal file was never kept and visibility cannot run.

## Chained Build Pipelines
`src/core/build_pipeline.*` composes profiles into the loop a mapper actually
runs. Every stage still goes through the same runner, so logs, diagnostics,
hashes, and manifests are identical to a single-profile run.

| Pipeline id | Engine | Stages (default state) |
|---|---|---|
| `quake-full` | idTech2 | `ericw-qbsp` -> `ericw-vis` -> `ericw-light` |
| `quake-fast` | idTech2 | `ericw-qbsp` -> `ericw-vis` (optional, off by default) -> `ericw-light` |
| `quake-bsp-only` | idTech2 | `ericw-qbsp` |
| `quake3-full` | idTech3 | `q3map2-bsp` -> `q3map2-vis` -> `q3map2-light` |
| `quake3-bsp-only` | idTech3 | `q3map2-bsp` |
| `doom-zdbsp` | idTech1 | `zdbsp-nodes` |
| `doom-zokumbsp` | idTech1 | `zokumbsp-nodes` |

Behavior:
- Each stage names the earlier stage whose expected output feeds it, so the BSP
  a stage writes is the BSP the next stage reads.
- The pipeline's explicit output path belongs to the last surviving stage that
  can actually be told where to write; later in-place stages operate on that
  artifact. If no stage accepts a destination, the requested path is ignored
  with a warning rather than silently dropped.
- Stages can be skipped per run (`--disable-stage`), or are skipped because they
  are optional and off by default, or because their profile is not registered in
  this build. Skips are reported with a reason.
- `planBuildPipeline()` resolves every stage, input, and output without running
  anything; `runBuildPipeline()` reuses that resolution.
- With `stopOnFailure` (true by default, and not yet switchable from the CLI), a
  failed stage marks the remaining stages as skipped with the reason recorded;
  cancellation does the same.
- On a dry run, a chained stage whose input does not exist yet - because the
  stage that would have produced it did not actually run - has that
  missing-input error downgraded to a warning, so planning a full chain does not
  report its later stages as broken.
- With a manifest directory, each stage writes
  `<pipeline id>.<stage id>.json` into it.
- Per-stage extra arguments are passed as `--stage-args <stageId>=<args>`.

`gameLaunchProfiles()` then turns a finished build into a launch command line
from argument templates using `{map}`, `{mod}`, `{basedir}`, and `{bsp}`, with
profiles for Quake, Quake II, Quake III, and Doom source ports plus a `custom`
profile that adds nothing of its own. `buildGameLaunchPlan()` only produces a
reviewable plan; `startGameLaunch()` refuses to run one that is not marked
runnable.

## CLI Examples

```sh
vibestudio --cli compiler set-path ericw-qbsp --executable C:\tools\qbsp.exe
vibestudio --cli compiler plan ericw-qbsp --input maps/start.map --workspace-root E:\Projects\QuakeMod
vibestudio --cli compiler run ericw-qbsp --input maps/start.map --workspace-root E:\Projects\QuakeMod --manifest build/qbsp-run.json --register-output --watch
vibestudio --cli compiler rerun build/qbsp-run.json --manifest build/qbsp-rerun.json
vibestudio --cli compiler copy-command build/qbsp-run.json
vibestudio --cli compiler plan q3map2-light --input maps/q3dm1.bsp --workspace-root E:\Projects\Q3Mod
vibestudio --cli build list --json
vibestudio --cli build plan quake-full --input maps/start.map --json
vibestudio --cli build run quake-full --input maps/start.map --manifest build/manifests --stage-args light=-extra4 --watch
vibestudio --cli build run quake-fast --input maps/start.map --disable-stage light
vibestudio --cli bsp inspect build/start.bsp --json
vibestudio --cli launch plan --map start --json
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
- idTech1 Doom-family maps: WAD map lumps, UDMF/TextMap variants, and the
  ZDBSP and ZokumBSP node/blockmap/reject profiles. Both node builders take
  their destination behind `-o`, on opposite sides of the input path, and both
  have a single-stage pipeline (`doom-zdbsp`, `doom-zokumbsp`).
- idTech2 Quake-family maps: `.map` to BSP through ericw-tools `qbsp`, `vis`,
  and `light`, chained by the `quake-full`, `quake-fast`, and `quake-bsp-only`
  pipelines, with `bspinfo` and `bsputil` available as inspection and extraction
  profiles.
- idTech3 Quake III-family maps: `.map` to BSP through q3map2-nrc, the q3map2
  compiler imported from NetRadiant Custom, including shader-aware light and
  packaging flows. The BSP, `-vis`, `-light`, `-convert`, and `-pk3` stages each
  have their own profile, and `quake3-full` / `quake3-bsp-only` chain them.

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
  diagnostics. Runnable wrapper profiles now exist for `bspinfo` and for
  `bsputil --check`, `--extract-entities`, and `--extract-textures`. The
  sub-command is emitted as argument 0, ahead of any caller extras, because
  `bsputil` parses its options before the single positional BSP path; that keeps
  VibeStudio's own command shape stable but does not resolve the upstream
  argument-parsing risk tracked as #435. Registry discovery itself is still
  presence and help/version output only, and operation-level `bspinfo`/`bsputil`
  probes remain without smoke-test coverage.

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
