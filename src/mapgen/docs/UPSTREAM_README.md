# Quake-MapGen

Quake-MapGen is an early-stage, prompt-conditioned generator for editable Quake, Quake II, and Quake III Arena level blockouts. Its long-term target is professional single-player and multiplayer generation learned from properly licensed maps and improved by measured validation and repair.

The v0.4.0/M3 pipeline adds bounded, deterministic navigation, visibility, analytical-lighting, and quality analysis to the runnable M2 generator. After structural validation and `MapBuilder` succeed, it derives player navigation, Q1/Q2 single-player monster navigation, spawn/route visibility, lighting readability, travel timing, and a lexicographic quality score. Navigation, visibility, lighting, quality, and optional diagnosed-leak evidence are SHA-256-bound to the canonical typed `Layout`; quality analysis also independently reruns each supplied analyzer under the exact `QualityOptions` and content-compares the result. Findings and the quality score are advisory, but analyzer execution, bounds, and exceptions are mandatory fail-closed gates: a failure aborts generation before map emission.

Separate source-level APIs ingest compiler pointfiles, diagnose a unique exposed local boundary, and evaluate typed repair candidates without mutating the baseline. `local_seal_proposal_preconditions_hold` proves only local applicability. Accepting a seal requires caller-configured compiler policy, a complete canonical integrity envelope over the exact candidate/lowered source/invocation/BSP/diagnostics/postcompile leak result, and a non-null host `CompilerRecheckVerifier` that authenticates the runner through a signature, trusted IPC token, or equivalent out-of-process trust mechanism. The unkeyed envelope hash is not authentication.

Generation turns a normalized prompt and a versioned, coordinate-free semantic plan into a validated integer `Layout`, lowers the union of its rooms and explicit passages into a sealed `MapDocument`, and writes one of four canonical map dialects. Omitting `--format` selects `q1-valve220`, `q2-valve220`, or `q3-legacy` from the requested game.

```powershell
qmg generate `
  --prompt "Quake 2 deathmatch, 8 players, 8-room industrial loop with strong height variation, seed 42" `
  --format q2-valve220 `
  --output out/foundry.map
```

The four CLI format names are `q1-valve220`, `q2-valve220`, `q3-legacy`, and `q3-brush-primitives`. A bounded source parser can also canonicalize an existing map when the dialect is supplied explicitly:

```powershell
qmg canonicalize `
  --input maps/source.map `
  --output out/source-canonical.map `
  --format q3-brush-primitives
```

GGUF planning remains explicit and local. Quake-MapGen never discovers or downloads weights. Supplying a single-file decoder-only GGUF with a supported chat template opts into llama.cpp planning with a request-specific grammar, strict parsing, bounded full-plan revisions, and model/tokenizer provenance. Omitting `--model` uses the deterministic rules/v1 planner.

```powershell
qmg generate `
  --prompt "Quake 1 single-player, six-room low-key castle loop, seed 42" `
  --model E:/models/local-instruct.gguf `
  --output out/castle.map
```

M2 added format-neutral construction primitives for exact stairs, convex ramps, segmented arches, Q1/Q2 curved-wall approximations, Q3 patch-veneer control grids with collision backing, structural liquids, and typed door/lift/teleporter assemblies. A dedicated `MapBuilder` overload verifies that construction solids/placements were applied unchanged, emits veneers for both Q3 dialects, and translates validated mover directives. The default Q2 deathmatch/duel path now selects a bounded vocabulary of compact unequal rooms, offset openings, raised crosswalks, horseshoe galleries and ordinary stairs. Other modes/titles retain their existing construction path; automatic liquids, movers, teleporters and continuous ramps remain separate work. Q3 “single-player” output is an arena with deathmatch spawns, not a bot-arena ladder entry or a Q1/Q2-style monster campaign, and there is no invented stock Q3 changelevel contract. Licensed-map learning remains M4, while finished balance, art, performance, and playtest telemetry remain M5.

## Modes and current limits

Each game has four generator modes: single-player, deathmatch, duel and capture the flag (`--mode sp|dm|duel|ctf`). The following are **not supported today**. Prompts that ask for co-op, TDM, bot arena or a “tourney” get one of the four modes with no warning or finding.

- **Co-op** (Quake, Quake II). There is no co-op mode. Co-op prompts produce single-player maps (15 of 16 draft benchmark cases) with one `info_player_start` and no `info_player_coop`.
- **Team deathmatch** (all three games). There is no TDM mode. TDM prompts produce deathmatch or single-player maps.
- **Quake Threewave CTF.** Quake CTF output has the `item_flag_team1`/`item_flag_team2` flags but no `info_player_team1`/`info_player_team2` starts, names no game module, and has never been loaded under a Threewave runtime. Its only module-related diagnostic is a generic `q1_ctf_mod_profile` warning that does not name Threewave.
- **Quake III bot arena.** Quake III “single-player” output is an arena with deathmatch spawns. No `.arena` script or `info_player_intermission` is written, so it is not a bot-arena ladder entry.
- **“Tourney” as a mode word.** Quake III tournament maps exist only as `duel` (`--mode duel`, or the words duel, 1v1 or one versus one). “tourney” and “tournament” are not recognised; prompts that rely on them become deathmatch or single-player maps.

**No human playtest of any generated map has been done.** In every game, evidence for generated maps reaches compiler output (qbsp/VIS/light, q3map2, and global BSPC AAS counts for Quake III). Only Quake II deathmatch has more: native movement trials, and dedicated-server initialization with no connected player, on recipe maps and on six default-generator studies built before the current code. That engine smoke cannot yet be rebuilt from its documented root. The [human playtest protocol](PLAYTEST_PROTOCOL.md) defines how sessions would be run and recorded; no session has been run under it.

**Draft benchmark.** A draft benchmark ran 138 prompts (17 game × mode targets × 8 prompts, plus 2 Quake II showcase probes) through the normal CLI, then compiled each map with full VIS and final light, plus BSPC AAS for Quake III. **70 of 138 cases pass every hard gate** (G1–G7: generated, intended game and mode, leak-free compile, full VIS, final light, Quake III AAS, mode entities). The 68 failures are 56 cases whose target mode the generator cannot represent (co-op, TDM, bot arena, and Threewave under the draft policy) and 12 prompts whose mode words are not recognised. Quake maps were compiled with `-notex`: two of their three visible texture names (WALL1_1, FLOOR0_1) are not stock names and no `wad` key is written, so a Quake compile pass says nothing about textures. Evidence level: compiler output. Cases, gates, per-target results and the full gap audit are in the [release-candidate Phase 1 audit and plan](RC_PHASE1_AUDIT_AND_PLAN.md) (sections 3 and 5).

## Release-candidate programme

The project is working toward a `v1.0.0-rc1` release candidate. [Phase 1](RC_PHASE1_AUDIT_AND_PLAN.md) audited all 17 game × mode paths, ran the draft benchmark above, scored each area 0–5 and set out a phased plan. No path meets the release-candidate conditions yet. The phase order was approved on 2026-10-15. Phase 2 builds the measuring stick first (fixed benchmark sets, gate fixes, baselines and a results ledger), then fixes entity-safety defects one cause at a time, each behind a switch that is off by default. A path is claimed only when it passes every hard gate on its benchmark set, reaches the parity bar, and has a hero map and runtime evidence; paths that do not get there are dropped and named in this README with their reasons.

## Build

Requirements are CMake 3.28+, a C++20 compiler, Git, and Ninja or another CMake generator. The executable links the exact pinned llama.cpp release and its synchronized ggml implementation. An existing source checkout can avoid the fetch, but it must resolve to the pinned commit with no modified or untracked files unless `QMG_ALLOW_UNPINNED_LLAMA=ON` is selected intentionally.

On x86, the pinned ggml defaults build an AVX2/FMA/F16C/BMI2 CPU backend. Older processors require configuring the corresponding `GGML_AVX2`, `GGML_FMA`, `GGML_F16C`, and `GGML_BMI2` options off.

```powershell
cmake --preset windows-clang `
  -DQMG_LLAMA_SOURCE_DIR=E:/_SOURCE/_CODE/llama.cpp-v0.3.0
cmake --build --preset windows-clang
ctest --preset windows-clang
```

On Linux or with a compiler already selected by the environment:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Optional Q1/Q2 compiler gates use separately installed, caller-hash-pinned ericw-tools 2.0.0-alpha11 tools. They exercise representative Q1 SP/32-room DM and Q2 SP/32-room CTF outputs, reject leak/invalid-brush diagnostics, require nonempty portal files with neither `.pts` nor `.lin` leak output, and inspect required BSP29/IBSP38 lumps. Project-authored open-ceiling negative controls must produce the expected leak diagnostics and nonempty `.pts`. When the VIS/light quartet is supplied, every fixture also runs single-threaded quick and final VIS/light; the gate requires nonempty visibility and lighting data and validates the Q1 RGB `.lit` sidecar. This is controlled compiler evidence, not a claim that compiler artifact bytes are deterministic.

```powershell
cmake --preset windows-clang `
  -DQMG_Q1_QBSP_EXECUTABLE=E:/path/to/ericw-tools/qbsp.exe `
  -DQMG_Q1_QBSP_SHA256=fad5a4dc4daf42773ad9a812349b2667221bec47a920cad10cb821ae442c8eb8 `
  -DQMG_Q2_QBSP_EXECUTABLE=E:/path/to/ericw-tools/qbsp.exe `
  -DQMG_Q2_QBSP_SHA256=fad5a4dc4daf42773ad9a812349b2667221bec47a920cad10cb821ae442c8eb8 `
  -DQMG_VIS_EXECUTABLE=E:/path/to/ericw-tools/vis.exe `
  -DQMG_VIS_SHA256=01be572845e05c87b01a0b667e590ac49bdaba23369af8529fe82fcfbc4fe6cf `
  -DQMG_LIGHT_EXECUTABLE=E:/path/to/ericw-tools/light.exe `
  -DQMG_LIGHT_SHA256=8e4fe9821f73c2d5c8a13655be20d3a71f357e76171de37c02413f4da3131a2f
ctest --preset windows-clang
```

The optional Q3 gate requires both hash-pinned NetRadiant Custom tools and their runtime-library directory. Each Q3 dialect is tested independently with duel, CTF, and 32-room DM fixtures through single-threaded q3map2 IBSP46 compilation and BSPC EAAS5 generation. All three scales receive quick q3map2 VIS/light; the duel fixture also receives final VIS/light, and every lit stage must report positive illuminated luxels and lightmaps. BSPC `aasinfo` must account for every reported reachability category and provide representative walk, barrier-jump, and walk-off-ledge evidence. Those AAS checks prove global compiled topology/category content; entity-to-entity reachability comes from the project navigation analysis. A deliberately open, dialect-native room proves that leak diagnostics and `.lin` checks are live in both Q3 source dialects.

```powershell
cmake --preset windows-clang `
  -DQMG_Q3MAP2_EXECUTABLE=E:/path/to/netradiant-custom/q3map2.exe `
  -DQMG_Q3MAP2_SHA256=7cc3963b21fa937880935e818624eb28c5424f7ff312148b13468e7afb1a71e7 `
  -DQMG_BSPC_EXECUTABLE=E:/path/to/netradiant-custom/mbspc.exe `
  -DQMG_BSPC_SHA256=77c9b9cac946e7d770bce9d934404b421e51ebe01963cd864c9d0e245243b003 `
  -DQMG_Q3_TOOL_RUNTIME_DIR=C:/path/to/runtime-dlls
ctest --preset windows-clang
```

Two model-related tests are also opt-in. `QMG_TEST_TOKENIZER_GGUF` parse-checks every generated runtime grammar with a local tokenizer GGUF. `QMG_TEST_GGUF_MODEL` runs real inference and requires the expected hash so the test cannot silently switch weights:

```powershell
cmake --preset windows-clang `
  -DQMG_TEST_TOKENIZER_GGUF=E:/path/to/tokenizer.gguf `
  -DQMG_TEST_GGUF_MODEL=E:/path/to/instruct.gguf `
  -DQMG_TEST_GGUF_SHA256=<64-hex-digest>
```

Quake-MapGen does not bundle compilers, game data, maps, textures, model weights, or the external tools’ runtime libraries.

## Generation pipeline

1. Normalize game, mode, theme, size, intensity, verticality, player count, and seed into the authoritative `PromptSpec`.
2. Produce and strictly validate a coordinate-free `SemanticPlan` through rules/v1 or opt-in grammar-constrained llama.cpp/GGUF inference.
3. Generate multiple deterministic integer-grid `Layout` candidates with stable room, connection, solid, and placement IDs; score them through a real ggml graph.
4. Allocate typed title-neutral placements, then validate topology, explicit passage chains, bounded convex geometry, ownership, links, objectives, and containment; repair to a fixed point or budget.
5. Have `MapBuilder` derive shell slabs from the free-space union, lower typed solids and placements through a centralized game/dialect profile, assign deterministic map-node IDs, and retain a source-to-node manifest.
6. Strictly validate the generated `MapDocument`, then run SHA-bound player navigation, applicable Q1/Q2 monster navigation, visibility, analytical lighting, and quality analysis.
7. Write canonical map text only after all mandatory stages return, and emit the deterministic schema-v4 report. Its `analysis.advisory: true` marks finding policy, while `analysis.execution_required: true` records that analyzer execution/caps fail closed. The report includes the canonical layout SHA-256, repeated per-analysis SHA identities (including quality), availability, summaries, and typed findings alongside dialect, lowering statistics, manifest counts, planner provenance, ranking, structural repairs, and final validation issues. Before serializing an available quality artifact, `render_report` independently recomputes it and requires complete content equality. `analysis.quality.summary.leak_diagnosis_evaluated` distinguishes “no local pointfile diagnosis supplied” from diagnosed local-boundary evidence; normal pipeline output is `false`, and this field does not describe later authenticated compiler evidence. JSON numeric rendering is locale-stable, and a non-finite rank score is rejected instead of producing invalid JSON.

Collection reordering does not change canonical map/report bytes: semantic IDs, sorted lowering, explicit caps, and canonical number/property/primitive rendering define the boundary.

`NavigationAnalyzer`, `VisibilityAnalyzer`, and `LightingAnalyzer` return typed metrics and findings. `QualityAnalyzer` rejects a supplied result unless its lowercase `layout_sha256` matches `canonical_layout_sha256(layout)` and its full content equals an independent bounded rerun under the configured options; the resulting `QualityAnalysis` repeats that layout SHA. Null leak evidence means leak status was not evaluated; a supplied ambiguous, invalid, or otherwise non-diagnosed result rejects. `quality_layout_fingerprint` remains a lightweight edit-precondition token, not artifact identity. `parse_compiler_pointfile` plus `diagnose_layout_leak` normalize `.pts`/`.lin` witnesses and can propose only a bounded local seal. `QualityRepairEngine` evaluates a copy, preserves optional `ConstructionResult` veneers/directives during lowering, requires exact preconditions, no structural/identity/navigation regression (including no substitution of navigation error identities at equal counts), successful lowering, and a strictly better score. `QualityRepairOptions::dialect` optionally binds candidate lowering to an exact title-compatible dialect—including Q3 brush primitives—or defaults by title; incompatibility rejects. A seal additionally requires `CompilerRecheckRequirements`, matching `CompilerRecheckEvidence`, and a `CompilerRecheckVerifier` that returns true. `compiler_recheck_envelope_sha256` is an unkeyed field-integrity digest, while verifier authentication and key/trust management belong to the host; core performs no automatic compiler invocation.

## Documentation

- [Architecture](ARCHITECTURE.md)
- [Research and technical decisions](RESEARCH.md)
- [Map formats](MAP_FORMATS.md)
- [Validation and leak repair](VALIDATION.md)
- [Dataset and learning plan](DATASET.md)
- [Reference-map study regimen and local knowledge base](MAP_STUDY.md)
- [First Quake II study results and generator priorities](Q2_FIRST_STUDY.md)
- [Switchback: asymmetric routes and selective extra floors](Q2_SWITCHBACK_STUDY.md)
- [Route diversity research](Q2_ROUTE_DIVERSITY_STUDY.md)
- [Reusable map design playbook](MAP_DESIGN_PLAYBOOK.md)
- [Spillway: distributed encounters and crossing routes](Q2_SPILLWAY_STUDY.md)
- [Counterweight: faceted spaces and optional traversal shortcuts](Q2_COUNTERWEIGHT_STUDY.md)
- [Talus: continuous ramps and distributed recovery routes](Q2_TALUS_STUDY.md)
- [Undercut: committed descents and ordinary returns](Q2_UNDERCUT_STUDY.md)
- [Fold: compact adjoining rooms, interior walkways and optional water](Q2_FOLD_STUDY.md)
- [Lockyard: an outdoor spine, independent dry return and deliberate dive](Q2_LOCKYARD_STUDY.md)
- [Ravel: tighter interleaved routes and local vertical choices](Q2_RAVEL_STUDY.md)
- [Weft: compact motifs in the default generator](Q2_WEFT_STUDY.md)
- [Native Ravel recovery: landing, braking and turning outcomes](Q2_NATIVE_RECOVERY_STUDY.md)
- [Interleaved-route and elbow reference study](Q2_INTERLEAVED_ROUTE_STUDY.md)
- [Native Talus ramp/stair journeys](Q2_NATIVE_ASCENT_STUDY.md)
- [Courtyard and deep-water reference study](Q2_COURTYARD_LIQUID_STUDY.md) and [native movement experiments](../tools/native_movement/README.md)
- [Compact multiplayer study regimen](MP_COMPACT_LAYOUT_REGIMEN.md) and [reference spatial patterns](MP_SPATIAL_PATTERNS.md)
- Six-map Quake Live atlas: 64 orthographic diagrams (`out/ql-atlas/index.html`, written by `python -B -m tools.ql_atlas.package`) and [reproduction tools](../tools/ql_atlas/README.md)
- [Drop/return reference study](Q2_DROP_RETURN_STUDY.md) and [controlled ramp/stair comparison](Q2_RAMP_STAIR_COMPARISON.md)
- [Continuous movement reference study](Q2_CONTINUOUS_MOVEMENT_STUDY.md) and [ramp navigation contract](Q2_RAMPS.md)
- [Traversal reference study](Q2_TRAVERSAL_STUDY.md) and [native Quake II mechanism contracts](Q2_MECHANISMS.md)
- [Roadmap and requirement matrix](ROADMAP.md)
- [Release-candidate Phase 1 audit and plan](RC_PHASE1_AUDIT_AND_PLAN.md)
- [Human playtest protocol](PLAYTEST_PROTOCOL.md)
- [Third-party software and references](../THIRD_PARTY.md)

## Reference-map studies

The first M4 study tool loads a local Quake II BSP archive into a provenance registry, a searchable analysis atlas, a 26-map/12-lesson curriculum and descriptive reference profiles. It measures static spaces, combat elevation, item placement, sightlines and material use; it separately identifies unverified navigation sidecars and unobserved runtime quality. Author/family groups stay together across reference, validation and test partitions. Map-derived outputs remain local under `out/`; training and raw-map retrieval are disabled until rights and provenance are reviewed.

```powershell
python -B -m tools.map_study ingest `
  --archive E:/Development/q2maparchive-wip/maps `
  --output out/map-study --strict
```

Open `out/map-study/index.html`. The study tools also compile and compare a fixed generator prompt/seed matrix against compatible reference cohorts. [The regimen](MAP_STUDY.md) explains the initial 15 rule cards, controlled experiments, human review and benchmark commands. This is an executable learning foundation; it does not train or replace model weights.

The [knowledge base](../knowledge/q2/rules.json) contains 60 cards today (48 at the Lattice iteration), retaining counterexamples and evidence limits. [Weft](Q2_WEFT_STUDY.md) takes the compact-room research into the actual default Q2 deathmatch/duel generator. A fixed nine-case portfolio compares 4/6/8 rooms across three seeds using normal six-candidate selection; Weft is the predeclared six-room seed42 example. Compiled XY envelopes are 31.7–41.7% smaller than the same-request baselines. Two initial spawn-sightline regressions are retained; a bounded placement refinement removes visible spawn pairs across the nine revised outputs while preserving those extents. Analytical lighting warnings remain. At Weft delivery, all 42 then-configured CTest gates passed. These are construction and static-analysis results, not multiplayer quality scores. Model weights and the reference profile were unchanged for that historical iteration.

[Splice](Q2_SPLICE_STUDY.md) adds an unequal wrapped-perimeter family around a playable center to eligible five- through eight-room Q2 multiplayer candidates. The outer loop bypasses the center. Six of nine fixed requests change room topology; the separate predeclared Splice example has nine passages and two local floor levels. Its footprint grows 8.5% against the matched previous request, while the eight-room samples shrink 18.3–25.1%. Candidate tracing exposes a ranking limit: all eligible sampled selections choose the new family, reducing eight-room selected graph variety from two to one. Open Splice (`out/splice/index.html`, upstream study evidence, not imported), inspect the nine-map portfolio (`out/splice-portfolio/index.html`, upstream study evidence, not imported), or download Splice (`out/splice/qmg_splice.zip`, upstream study evidence, not imported).

[Lattice](Q2_LATTICE_STUDY.md) changes selection over 96 unchanged candidate records. A scoped Q2 multiplayer policy stops weighting room-volume variation and preferred 1,280-unit center spacing, using deterministic seed-based ties. Primary selected graph counts for 4/6/8 rooms change from 1/1/1 to 1/2/2; separate 6/8-room holdouts change from 1/1 to 2/3. Larger envelopes and new spawn-separation warnings remain. Lattice itself grows 8.1% and has longer room-graph routes than its matched predecessor. The [feature study](Q2_RANKING_FEATURE_STUDY.md) supports abstaining from unsupported preferences, not learned quality or improved flow. Open Lattice (`out/lattice/index.html`, upstream study evidence, not imported), compare the 15-map portfolio (`out/lattice-portfolio/index.html`, upstream study evidence, not imported), or download Lattice (`out/lattice/qmg_lattice.zip`, upstream study evidence, not imported) and the portfolio (`out/lattice-portfolio/qmg_lattice_portfolio.zip`, upstream study evidence, not imported). The separate [16 successful native turning trials](Q2_NATIVE_MOTIF_TURNING.md) use frozen Weft with wall-adjacent finishing dwells; they do not test Lattice or open-space stopping.

Open Weft (`out/weft/index.html`, upstream study evidence, not imported), compare the 11-layout portfolio (`out/mp-portfolio/index.html`, upstream study evidence, not imported), or download the Weft package (`out/weft/qmg_weft.zip`, upstream study evidence, not imported) and portfolio package (`out/mp-portfolio/qmg_compact_portfolio.zip`, upstream study evidence, not imported). The portfolio passes 216 analytical preview states: 162 for the nine primary outputs, 36 for the separate low/high verticality controls and 18 for Weft. All nine primary outputs, two semantic controls and the duplicate Weft hero compile pass full VIS and extra4 one-bounce lighting; Weft also passes 16 dedicated initialization gates. Final CLI MAP/report parity and eight prior-recipe MAP controls pass. Diagrams, compiler checks and dedicated initialization do not establish engine appearance or player navigation in a match.

The earlier [Ravel experiment](Q2_RAVEL_STUDY.md) places six unequal rooms, nine passages, three stairs, one ramp and one optional drop inside a tighter envelope, with three floors only where their route roles warrant them. Its XY envelope area is 18.1% smaller than Fold's and 29.9% smaller than Lockyard's; gross authored room/passage coverage is 94.0%, versus 94.8% and 91.0%. These are packing descriptors, not usable floor area or quality scores. Reproduce it with `./tools/map_recipes/build.ps1 -Recipe ravel`.

Ravel passes 23 selected CTest gates, 24 analytical preview states, 72 prior-map preview controls and 16 dedicated initialization gates. Its existing XY floorplan mode now keeps one common envelope and scale when switching floors, with a map-unit bar and dashed context for other floors. The [Reclamation reference study](Q2_INTERLEAVED_ROUTE_STUDY.md) explains why an apparent corner shortcut or vertically overlapping plan still needs complete hull and return checks. Neither the preview nor initialization proves gameplay or rendered appearance.

The separate [native Talus comparison](Q2_NATIVE_ASCENT_STUDY.md) adds 24 matched ramp/stair journeys and eight deterministic repeats using pinned Quake II 3.19 world movement. All complete; uphill times tie, while downhill planar crossing and grounded recovery produce different timing interpretations. It does not establish a universal ramp advantage, camera behavior or multiplayer balance. Core/profile identities remained unchanged for that historical iteration; Weft's later default construction integration is recorded separately.

Six separate Ravel native probes (`out/ravel/native-routes/README.md`, upstream study evidence, not imported) complete the center ramp in both directions and the ledge descent under two fixed forward inputs. The moving player lands beyond the drawn geometric drop point and retains horizontal momentum. These bounded world-only results do not prove safe falls, braking, turning recovery, complete ordinary journeys or multiplayer quality.

The later [native recovery experiment](Q2_NATIVE_RECOVERY_STUDY.md) retains five successful pauses and three failures across eight fixed profiles on unchanged Ravel geometry. It distinguishes landing from braking and turning space. Successful turn/pause trials stop before onward doorway traversal; they do not test Weft movement or game-module damage.

The preceding [Lockyard experiment](Q2_LOCKYARD_STUDY.md) adds a partial gallery around an outdoor court, a dry recovery loop and a culvert with up to 112 units of water. Passage-node closure checks expose dependencies that a room graph can conceal. Reproduce it with `./tools/map_recipes/build.ps1 -Recipe lockyard`.

The optional [native movement harness](../tools/native_movement/README.md) now measures Fold and Lockyard against the same compiled geometry with water removed. Its 16 retained Quake II 3.19 world-only trials include 14 completions and two level-view failures beneath Lockyard's low roof; deliberate dive profiles complete that route. This adds movement evidence without simulating oxygen, damage, item or mechanism activation, or opponents. Lockyard passes 17 affected CTest gates, 24 preview states, 42 prior-map preview controls and 16 dedicated initialization gates. Full-game traversal, rendering and multiplayer balance remain open; Lockyard is larger than Fold and is not a uniformly tighter result.

The [Switchback experiment](Q2_SWITCHBACK_STUDY.md) builds an original asymmetric Quake II layout with ten connections, six stair routes and three occupied combat heights. Its [tracked recipe and build instructions](../tools/map_recipes/README.md) reproduce the compiled map and static evidence under `out/switchback/`. It exercises the typed construction APIs; automatic CLI selection of these spatial motifs remains future work. Core validation and repair now preserve distinct passage routes between the same rooms, and ordinary generated stairs use 36-unit treads.

[Spillway](Q2_SPILLWAY_STUDY.md) tests six distributed spaces and a bridge crossing an independent lower route. Reproduce it with `./tools/map_recipes/build.ps1 -Recipe spillway`. Four additional reference studies feed the [design playbook](MAP_DESIGN_PLAYBOOK.md), with counterexamples, failure signals and controlled playtest questions. The core now accepts typed point lights inside sealed passages and includes endpoint approaches in placement travel costs. Those costs remain planner estimates, not measured engine time.

[Counterweight](Q2_COUNTERWEIGHT_STUDY.md) adds faceted and L shaped spaces, an automatic lift, and a directed teleporter. Reproduce it with `./tools/map_recipes/build.ps1 -Recipe counterweight`. All 264 spawn-to-gameplay-anchor routes pass with jumps, lifts and teleporters excluded. Native lift and teleport construction now follows stock Q2 behavior; an independent compiled convex-prism checker validates the chamfers, discs and lift sweep. The [dedicated engine smoke harness](../tools/map_recipes/ENGINE_SMOKE.md) checks map loading and item initialization using an isolated local server. It does not establish movement, rendering or multiplayer balance. These are original explicit recipes and reusable engineering improvements; the default planner does not yet select this full motif set automatically, and no model weights were trained.

## Credits

The dependency pin uses [llama.cpp](https://github.com/ggml-org/llama.cpp) for optional local GGUF inference and its bundled [ggml](https://github.com/ggml-org/ggml) for inference plus deterministic layout ranking. Format and gameplay behavior were checked against the official [Quake](https://github.com/id-Software/Quake), [Quake II](https://github.com/id-Software/Quake-2), and [Quake III Arena](https://github.com/id-Software/Quake-III-Arena) source releases. Compiler validation uses separately installed [ericw-tools](https://github.com/ericwa/ericw-tools) and [NetRadiant Custom](https://github.com/Garux/netradiant-custom) tools as external processes. No id Software or community-map code, proprietary assets, compiler binaries, or model weights are included.

The separate optional movement harness links externally located Quake II 3.19 code into a private GPL test executable. Its original orchestration remains MIT; the combined executable is not part of the library or map packages. [Third-party notices](../THIRD_PARTY.md#optional-private-native-movement-harness) record the source and RSA MD4 attribution boundary.

Quake and related names are trademarks of their respective owners. This project is not affiliated with or endorsed by id Software.

## Licence

Quake-MapGen is available under the [MIT License](../LICENSE). Dependencies, tools, model weights, and training maps retain their own licences; see [THIRD_PARTY.md](../THIRD_PARTY.md).

## Braid: explicit wrapped construction

Braid (`out/braid/index.html`, upstream study evidence, not imported) is the predeclared eight-room seed 503 example from
the ordinary six-candidate pipeline. An explicit wrapped-center request now
constrains construction to the existing pinwheel family in Q2 DM/duel at 5–8
rooms. Its playable center, unequal perimeter, three crosswalks and two
horseshoe galleries connect through ten stair runs. The example's compiled
envelope falls about 10.15% and maximum room hops fall 4→3; some six-room requests
grow, so wrapped intent is not a general compactness or quality guarantee.

The [three-stage study](Q2_BRAID_STUDY.md) separates construction intent
from a local spawn-spacing pass that preserves every blocked pair and full
standing support. [Reference resource approaches](Q2_RAISED_RESOURCE_CIRCUIT_STUDY.md)
retain a failed drop despite matching circuit endpoints. The knowledge base now
contains 51 scoped rule cards; reference assets, model profiles and weights are
unchanged. Braid is a compiled blockout with analytical previews and dedicated
initialization evidence; full native journeys and multiplayer quality remain
untested.

## Hinge: upper routes between rooms

Hinge (`out/hinge/index.html`, upstream study evidence, not imported) is the predeclared eight-room seed 607 example.
A supported passage now joins two existing raised crossings at floor 128,
preserving its lower loop, room envelope and ten stair runs. This changes
circulation between floors of already adjacent rooms; it does not reduce room
hops or establish multiplayer balance.

The [study](Q2_HINGE_STUDY.md) retains the first no-link example, diagnoses
a coordinate-phase error and applies a general fix across the same fixed
requests. It separates actual upper support, physical doorway closures and
movement evidence. [Powertrip's measured upper/lower routes](Q2_UPPER_CONNECTION_STUDY.md)
inform the construction checks without supplying copied architecture. Open the
comparison portfolio (`out/hinge-portfolio/index.html`, upstream study evidence, not imported) or download
Hinge (`out/hinge/qmg_hinge.zip`, upstream study evidence, not imported) and the
portfolio (`out/hinge-portfolio/qmg_hinge_portfolio.zip`, upstream study evidence, not imported).

Twelve of sixteen selected layouts gain an upper link; all compiled envelopes
stay unchanged, while lighting findings increase in both comparison strata.
[First native trials](Q2_NATIVE_UPPER_LINK.md) pass seven of eight complete
room-to-room rotations and all four straight transfers. One fast turn remains
failed, and stopping pockets are beside walls. The knowledge base now contains
54 cards, preserving the previous 51. These are procedural rules; no model
weights were trained, and multiplayer quality remains untested.

## Relay: teleporter arrivals and onward choices

Relay (`out/relay/index.html`, upstream study evidence, not imported) is a new original six-room experiment with an
offset arrival pocket, two different walking departures, a chaingun beyond the
partition and an ordinary source-to-resource bypass. Its selective upper
crossing connects two rooms inside a compact adjoining footprint. This is a
fixed recipe; the default generator and ranker are unchanged.

The [study](Q2_RELAY_STUDY.md) connects new
[Painkiller/Torn Glory measurements](Q2_TELEPORTER_ARRIVAL_STUDY.md) with
compiled route checks and [stock game-module teleport trials](Q2_NATIVE_TELEPORT.md).
Four native Relay activation/hold/disc-exit trials pass, following eight
mechanism controls. The retained exposed spawns and arrival sightline still
need combat testing. Open the arrival route figure (`out/relay/arrival-routes.png`, upstream study evidence, not imported),
download the original map (`out/relay/qmg_relay.zip`, upstream study evidence, not imported), or inspect the existing
64-view Quake Live reference atlas (`out/ql-atlas/index.html`, written by `python -B -m tools.ql_atlas.package`).

Three new arrival and shortcut rules bring the knowledge base to 57 cards,
preserving all 54 preceding cards. Full resource journeys and multiplayer
balance remain untested.

## Span: opposed stairs and an independent lower aisle

Span (`out/span/index.html`, upstream study evidence, not imported) is a six-room example from the ordinary generator.
Opposite stair approaches now connect a raised bridge over a clear lower aisle;
horseshoe galleries and an inter-room upper passage surround its central room.
The [14-request comparison](Q2_SPAN_STUDY.md) retains all 84 candidates per
stage. All 11 high-verticality selections contain the new pattern; three flat
controls remain byte-identical. Room envelopes and lower passages are preserved,
while item placement and optional upper links can respond to the changed decks.

The [reference study](Q2_ATRIUM_ROUTE_STUDY.md) retains an obstructed high
bend despite floor support. [Compiled audits](Q2_SPAN_ROUTE_AUDIT.md) pass
all 14 selected bridge rooms and four first native upper journeys. Those journeys
pause at turns and stop beside walls; the lower return is only statically
checked. Six hero lighting warnings remain, and multiplayer quality is untested.
The knowledge base now has 60 scoped cards, preserving the earlier 57. Download
Span (`out/span/qmg_span.zip`, upstream study evidence, not imported) or the 14-map portfolio (`out/span-portfolio/qmg_span_portfolio.zip`, upstream study evidence, not imported).
