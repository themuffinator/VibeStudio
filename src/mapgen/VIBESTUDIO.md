# VibeStudio Map Generator (Quake-MapGen)

This directory holds the Quake-MapGen level generator. It turns a prompt into an editable,
validated blockout for Quake, Quake II and Quake III Arena. It was imported from
[Quake-MapGen](https://github.com/themuffinator/Quake-MapGen) (MIT, copyright 2026 Quake-MapGen
contributors; see `LICENSE`) and is developed here as part of VibeStudio from now on.

Status: **Partial**, pre-alpha. No generated map has had a human playtest. See the Quake-MapGen
README of the imported revision for the evidence each game and mode has, and for the modes it does
not support (co-op, team deathmatch, Threewave CTF starts, Quake III bot arenas).

## Provenance

- Source: the local Quake-MapGen working tree on 2026-10-22, at commit
  `1252548ad68f44df63d56f1c6c068c4fc6f6330c` (2026-08-26, "complete M3 analysis and localized
  repair") plus its uncommitted work up to 2026-10-16 (stage switches, compact multiplayer motifs,
  upper links, spawn refinement, the `data/profiles` game profiles and the R1-s causes).
- `UPSTREAM.json` lists every imported file with the SHA-256 of its upstream bytes.
- Imported: `include/qmg`, `src` (without `main.cpp` and `llama_text_model.cpp`),
  `data/profiles`, `schemas/semantic-plan-v1.schema.json`, `grammars/semantic-plan-v1.gbnf`, the
  C++ unit tests in `tests` with their golden maps, `LICENSE` and `.clang-format`.
- Imported later the same day, from the same working tree: the research tooling (see
  [Research tools](#research-tools)). That is `tools` (the map recipes, the portfolio tools, the
  Python research tools and the native movement and teleport harnesses, as source only), the
  knowledge base in `knowledge`, the study and design documents in `docs`, the upstream README as
  `docs/UPSTREAM_README.md`, `THIRD_PARTY.md`, `tests/test_recipe_environment.cpp`, the Python tests
  `tests/test_*.py`, `tests/recipe_preview_check.cjs` and `tests/fixtures`.
- Not imported, and still in the Quake-MapGen repository: the `qmg` command-line program, the
  llama.cpp/GGUF backend and its licence notices (`licenses/`), the top-level CMake build
  (`CMakeLists.txt`, `CMakePresets.json`, `cmake/`), `CONTRIBUTING.md`, `.github`, and the
  untracked `build/`, `out/` and `.agents/` trees. Those trees hold the study outputs, the private
  native builds and the helper scripts that recorded much of the study evidence.

## What VibeStudio changed

Every change is marked `VibeStudio:` in a comment.

- **Ranker without ggml.** `GgmlRanker` (`ggml_ranker.*`) became `LayoutRanker`
  (`layout_ranker.*`). It computes the same linear model with plain float arithmetic in ggml's
  accumulation order, so scores, ties and selections match the ggml build bit for bit, and builds
  need neither ggml nor llama.cpp. The report keeps its schema v4 keys and records
  `vibestudio/scalar-linear-layout-ranker` and `not-linked` in them. In builds with
  `-Dggml=enabled`, `RankerBackend::ggml` (`PipelineOptions::ranker`, `--ranker ggml`) runs the
  upstream graph again, with the same scores and ggml's backend, version and commit in the report;
  `QMG_HAVE_GGML` marks that code.
- **Planning through VibeStudio's AI connectors.** `ModelIdentity::provenance` and
  `TextModel::provenance()` add `named_service`: a model reached through a connector, named by
  connector and model, with no weight digests and the provider's own token counts. The planner
  records it as the `connector` backend, and the report adds `"provenance": "named_service"` to the
  model object only in that case. `build_semantic_plan_json_schema` states the grammar's contract as
  a JSON Schema for structured output, and `semantic_plan_initial_request` returns the first request
  so the studio can preview it and ask before it leaves the machine. Through the Local GGUF
  connector a GGUF file runs as `local_weights`, under the planner's own GBNF grammar, and the report
  names it by SHA-256 as upstream's llama.cpp backend did (`src/core/ai_gguf_runtime.cpp` is derived
  from that backend).
- **Rules fallback.** `PipelineOptions::rules_fallback` plans by the rules when a model answers but
  its plans fail (`PipelineResult::planner_fallback`, the report's `planner.fallback`), while
  `TextModelUnavailable` (no connection, a refusal, a cancel) still stops generation.
- **Overrides.** `PromptOverrides` gains `verticality` and `intensity` for the Generate Level
  dialog's and CLI's choices.
- **Build.** Meson builds the library (`meson.build`). `generate_profile_tables.py` ports
  `cmake/GenerateGameProfileTables.cmake` and writes the same header apart from its first comment
  line. `QMG_VERSION` is `0.4.0+vibestudio`; `QMG_LLAMA_REVISION` is the pinned llama.cpp revision
  in builds with ggml, else `not-linked`.
- **Tests.** The unit tests run as the `mapgen` Meson suite. Only the ranker expectations changed.
  `test_grammar.cpp` (needs llama.cpp) was not imported. `test_recipe_environment.cpp` came with the
  recipes and runs in the `mapgen-research` suite (see [Research tools](#research-tools)).

## Style

The code keeps Quake-MapGen's style: four-space indents, 120 columns, `.clang-format`, and the
nested `.editorconfig`. Keep that style here so changes stay easy to compare with the imported
revision.

## How the studio uses it

`src/core/level_mapgen.*` runs the pipeline for the Generate Level dialog and `vibestudio --cli map
generate --generator quake-mapgen`. It converts the result into the studio's level generation result:
the map text, a preview layout, the plan, the analysis findings, and the schema v4 report.

## Research tools

Quake-MapGen's research tooling measures and studies the generator: hand-built map recipes,
multiplayer portfolio exports and comparisons, reference-map studies, the Benchmark v1 runner and
its ledgers, game profile checks, and native Quake II movement trials. It is not part of the studio.
Nothing in `src/core`, `src/app` or `src/cli` uses it, and none of it is installed.

Status: **Partial**. The tools run as they ran in Quake-MapGen. Most of them need maps, game data,
compilers or engines that the user supplies; nothing is bundled or downloaded, and several defaults
still name the original author's folders (`E:/_SOURCE/...`). The evidence in `knowledge/` and
`docs/` records Quake-MapGen's own studies and has not been repeated here.

### What each package does

| Package | What it does |
| --- | --- |
| `tools/asset_registry` | Builds texture and shader registries from Quake II and Quake III trees the user supplies, checks the texture admission rules, and checks that `data/` holds no asset bytes. |
| `tools/benchmark` | Benchmark v1: fixed prompt sets, a runner that generates, compiles with hash-pinned tools and compares them, and the release-candidate ledger, parity and docs-claims checks. |
| `tools/map_recipes` | Hand-built Quake II deathmatch recipes in C++ (Switchback, Spillway, Counterweight, Talus, Undercut, Fold, Lockyard, Ravel, Relay) with their inspection, audits, layout viewer and engine smoke check. |
| `tools/map_study` | Reads Quake II maps the user owns into a local atlas, registry, curriculum and profile (`ingest`), compares a compiled candidate with them (`compare`), and holds the focused reference studies. |
| `tools/mp_portfolio` | Exports the default pipeline's maps for fixed multiplayer portfolios and compares portfolios, with C++ candidate tracers. |
| `tools/native_movement` | Runs synthetic movement through stock Quake II 3.19 `Pmove` against compiled recipe maps, through a private GPL harness. |
| `tools/native_teleport` | Runs stock game-module teleporter trials through a second private GPL host. |
| `tools/profiles` | Checks `data/profiles`: schema, citations, the facts fixture, generator cross-checks and the freshness of the generated header. |
| `tools/ql_atlas` | Builds a local orthographic atlas of Quake Live maps the user owns. Needs the Shapely package. |

### Running them

Run the Python tools from `src/mapgen`, which they treat as their root, with Python 3.10 or newer
and `-B` so that no bytecode lands in the tree:

```sh
cd src/mapgen
python -B -m tools.map_study ingest --archive <folder of maps you own> --output out/map-study
python -B -m tools.map_study compare --bsp <compiled.bsp> --profile out/map-study/profile.json --cohort dm_small --output out/compare.json
python -B tools/benchmark/run.py --help
```

Outputs go to `out/` below `src/mapgen`, which Git ignores. Everything derived from maps or game
data (atlases, registries, profiles, compiled maps, packages, trajectories) stays there, on the
user's machine; do not commit or distribute it.

The C++ programs build with VibeStudio, into `<build>/src/mapgen/tools/`:
`mapgen_<recipe>_recipe <output folder>` for each recipe (`talus_stairs` is the stair variant of
Talus), `mapgen_mp_portfolio_export <prompt file> <output folder> <map name> [title]`, and
`mapgen_mp_candidate_trace` and `mapgen_span_trace`, which each take a prompt file. Quake-MapGen
called them `qmg_*`. `tools/map_recipes/build.ps1`, which chains a recipe with the compilers and
the inspection, still builds and runs the recipe through Quake-MapGen's CMake preset and its
`build/windows-clang` folder, so in VibeStudio run the recipe program yourself and then the later
stages. `tools/mp_portfolio/compile.ps1` only compiles an exported map and works as it is.

### The GPL boundary

`tools/native_movement/harness.c`, `tools/native_teleport/host.c` and
`tools/native_teleport/server_bridge.c` are original MIT code, but the programs they make link GPL
Quake II 3.19 source from a local checkout outside this tree. They are here as source only: Meson
never builds them, nothing in VibeStudio links them, and their `CMakeLists.txt` stay Quake-MapGen's
separate private build (`python -B -m tools.native_movement.build` and
`python -B -m tools.native_teleport.build`), whose outputs belong under `.agents/tmp/` and `out/`.
Never ship those programs with VibeStudio. Their READMEs say the same.

### Tests

`meson test -C <build> --suite mapgen-research` runs `tools/meson.build`'s registrations, which
mirror Quake-MapGen's CMake:

- `mapgen-recipe-<recipe>`: each recipe program writes its layout into
  `<build>/src/mapgen/tools/recipe-test/<recipe>` (Quake-MapGen's `qmg_<recipe>_layout`).
- `mapgen-recipe-environment`: `tests/test_recipe_environment.cpp`.
- `mapgen-py-<group>`, when Python 3.10 or newer is found, run from `src/mapgen`: the unittest
  groups `arrival-audit`, `arrival-bindings`, `atrium-routes`, `braid-compare`, `fold-audit`,
  `gate-audit`, `hinge-compare`, `lockyard-audit`, `mp-portfolio`, `mp-ranking`, `mp-topology`,
  `native-ascent`, `native-motif-circuits`, `native-movement`, `native-ravel`, `native-recovery`,
  `native-teleport`, `native-upper-link`, `packing-audit`, `ravel-audit`, `recipe-native-display`,
  `span-compare`, `spawn-separation-study`, `teleporter-arrivals`, `upper-link-controls`, and
  `ql-atlas` when Shapely imports; the test scripts `map-study`, `map-study-q1q3`,
  `recipe-geometry` and `drop-audit`; and `profile-schema`, `data-no-asset-bytes`,
  `benchmark-compare-selftest`, `rc-ledger`, `rc-ledger-parity`, `docs-claims` and
  `rc-benchmark-selftest`.

The four native groups (`native-motif-circuits`, `native-movement`, `native-ravel`,
`native-recovery`) run their contract cases and report their integration cases as skipped unless
the private native build and the retained `out/` trajectories are present. `rc-ledger` warns about
the local `out/` evidence it cannot see; that is expected here.

Not registered:

| Test | What it needs |
| --- | --- |
| `tests/test_atrium_audit.py` | `out/span-study/native_routes.py` and `verify_native.py`, helper scripts that never left Quake-MapGen's untracked `out/`. |
| `tests/test_native_motif_turning.py` | `out/native-motif-circuits/inspect_observations.py`, likewise; `tools/native_movement/motif_turning.py` imports it when it loads. |
| `tools/asset_registry/test_asset_admission.py` | Extracted Quake II and Quake III trees (`--q2`, `--q3`). Without them its synthetic checks pass and it exits 77, not configured. |
| `tools/benchmark/run.py --controls` and `tools/benchmark/rc_benchmark.py` | The `qmg` program, hash-pinned ericw-tools, q3map2 and mbspc, and stock Quake, Quake II and Quake III assets. |
| `tests/recipe_preview_check.cjs` and `tools/ql_atlas/preview_check.cjs` | Node with Playwright, Microsoft Edge (or another channel in `QMG_BROWSER_CHANNEL`), and a wrapped preview page built under `out/`. Quake-MapGen does not register them either. |
| `tools/profiles/check_profiles.py --resolve` | Local id Software source roots and a 1996 `progs.dat`. It runs by hand in Quake-MapGen too. |

Quake-MapGen's compiler, VIS and light, determinism, CLI and GGUF gates test the `qmg` program
and were not imported.

### Changes from Quake-MapGen

Each change is marked `VibeStudio:` where the file format allows a note.

- `tools/mp_portfolio/span_trace.cpp` and `trace_candidates.cpp` include `qmg/layout_ranker.hpp`
  and use `qmg::LayoutRanker`. Their output text still says "ggml" where Quake-MapGen's did. No
  recipe or test expected the old backend string.
- Links in the imported Markdown: repository-root paths point to their place under `src/mapgen`
  (the README now sits in `docs/`). Links to generated `out/` artefacts are plain text that names
  the artefact and the command that writes it, or says that it is upstream study evidence that was
  not imported; most of it was recorded by helper scripts in Quake-MapGen's untracked `out/`.
  `THIRD_PARTY.md` names the llama.cpp and ggml notices instead of linking them.
- `tools/benchmark/docs-claims.json` reads its README claims from `docs/UPSTREAM_README.md` and
  expects the repointed links. JSON takes no comments, so its `vibestudio` key carries the note.
- `tools/native_movement/README.md` and `tools/native_teleport/README.md` open with the
  source-only note.

The evidence records in `knowledge/` hash Quake-MapGen's files as they were when each study ran.
They describe that history, not these copies; `UPSTREAM.json` holds the hashes of the imported
bytes.
