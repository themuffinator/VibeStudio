# Roadmap and requirement matrix

The project is developed as vertical milestones. “Complete” means the stated evidence exists; it does not promote an editable blockout into a finished, balanced level.

## Current limits and release-candidate programme

Status on 2026-10-15:

- **Modes.** Each game has four generator modes: single-player, deathmatch, duel and CTF. Co-op, team deathmatch, Quake Threewave CTF and Quake III bot arena are not supported today. “tourney” and “tournament” are not recognised as mode words; Quake III tournament maps exist only as `duel`. Prompts that ask for co-op, TDM, bot arena or a “tourney” get one of the four modes with no warning or finding.
- **Playtesting.** No human playtest of any generated map has been done. The [human playtest protocol](PLAYTEST_PROTOCOL.md) defines how sessions would be run and recorded; no session has been run under it.
- **Draft benchmark.** 70 of 138 draft benchmark cases pass every hard gate (G1–G7). Evidence level: compiler output. Every failing case targets a mode the generator cannot represent (56) or uses a mode word it does not recognise (12). See the [release-candidate Phase 1 audit and plan](RC_PHASE1_AUDIT_AND_PLAN.md), section 5.
- **Release-candidate programme.** The [Phase 1 audit and plan](RC_PHASE1_AUDIT_AND_PLAN.md) holds the gap audit, area scores, path readiness and the phased plan toward `v1.0.0-rc1`. Its phase order was approved on 2026-10-15. No game × mode path meets the release-candidate conditions yet. The audit also records where milestone claims below rest on uncommitted code (section 2 and section 3, row B1).

## Milestones

### M0 — Buildable foundation (completed in v0.1.0)

- C++20/CMake CLI with pinned llama.cpp/ggml dependency.
- Prompt normalization for Q1/Q2/Q3 and SP/DM/duel/CTF semantics.
- Multiple deterministic layouts scored through a real ggml graph.
- Title-aware placements, axial shells, iterative validation/repair, `.map`, and JSON report.
- Unit/determinism matrix plus initial optional Q1/Q2 qbsp leak fixtures.

### M1 — Constrained GGUF semantic planner (completed in v0.2.0)

- Authoritative normalized request separated from a coordinate-free semantic plan.
- Versioned JSON schema, checked-in canonical GBNF, and request-specific runtime grammars.
- Model-neutral `TextModel`, scripted model, and CPU-only llama.cpp GGUF implementation.
- Prompt-to-plan and issue-to-full-plan-revision inference with bounded attempts and hash-chained provenance.
- Opt-in tokenizer and hash-pinned real-model tests; deterministic rules/v1 fallback.

M1 evidence covers every game/mode and normalized 2–32-room rule-plan contract, strict parse/semantic rejection, scripted revision/fail-closed behavior, runtime grammar cardinality in a real tokenizer, and exact local-model provenance. Its frozen M0 map-body compatibility hashes were a historical migration gate and were intentionally retired when M2 changed map syntax and integer snapping.

### M2 — Geometry and format fidelity (completed in v0.3.0)

- Dedicated integer `Layout` and syntax-preserving `MapDocument` representations.
- Strong stable IDs, explicit passage chains, bounded convex half-space validation, typed solids/placements/links, deterministic manifests, and configurable coordinate/cell/brush/control caps.
- Free-space-union shell construction with partial/multiple openings, bends, T/cross junctions, duplicates, nested volumes, and no giant outer seal.
- Exact stairs, convex ramps, segmented arches, curved brush approximations, Q3 patch veneers with collision backing, structural liquids, and typed door/lift/teleporter construction.
- Valve 220 Q1/Q2, optional Q2 contents/surface/value preservation, Q3 legacy, and distinct Q3 brush-primitive parsing/canonical writing.
- Bounded source parser, pristine-source preservation, strict syntax-preserving/generated validation profiles, four golden canonical fixed points, and the explicit `canonicalize` CLI.
- Reported map dialect/statistics/manifest counts and collection-order byte determinism.
- Strengthened hash-pinned Q1 SP/DM and Q2 SP/CTF BSP/leak/lump matrices plus both Q3 dialects through hash-pinned q3map2 IBSP46 and BSPC EAAS5/AAS metrics, with an independent negative leak control.

M2’s completion claim is format and compiler fidelity for representative editable blockouts. The default planner does not yet choose every advanced motif. Q3 “SP” remains arena generation with deathmatch spawns, not a bot-arena ladder entry or a monster campaign, and the project does not invent a stock Q3 changelevel contract. Engine playtests, balance, final VIS/light, and production art are not M2 evidence.

### M3 — Navigation, visibility, lighting, and localized repair (completed in v0.4.0)

Completed API, pipeline/report, and compiler evidence:

- bounded title-specific hull-expanded walk/step/jump/drop/door/lift/teleporter navigation, including Q1/Q2 small/large ground-monster profiles;
- deterministic LOS, exposure, exact cover, crossfire, articulation, route-readability, chokepoint, spawn-safety, and batched analytical-lighting/readability metrics with conservative-occlusion safety rules;
- strict Q1/Q2/Q3 `.pts`/`.lin` ingestion, fixed-point direction-invariant witness normalization, a coordinate-compressed exterior flood, unique local-boundary diagnosis, and a typed bounded seal proposal without global wrapping;
- timing and Q1/Q2 monster reachability analysis plus typed light/placement/passage/cover/seal edits, canonical layout SHA-256 evidence binding, exact preconditions, regression gates, and lexicographic score acceptance;
- pipeline analysis only after structural validation/lowering plus a schema-v4 report with canonical and repeated per-analysis layout identity, independent quality-content verification, `advisory: true` findings, `execution_required: true` analyzer gates, availability, summaries, scores, and typed findings;
- seal acceptance bound to exact dialect, canonical source map, compiler invocation, BSP, diagnostics, pointfile witness/absence, explicit leak freedom, an unkeyed canonical integrity envelope, and positive host-verifier authentication;
- single-threaded quick and final classic VIS/light on every Q1 SP/DM and Q2 SP/CTF fixture when configured, plus project-authored `.pts` leak controls; and
- single-threaded quick Q3 VIS/light at duel/CTF/32-room scales, final VIS/light on the duel representative with positive illuminated luxels/lightmaps, deeper global EAAS topology/category checks, and dialect-native `.lin` leak controls.

M3 analysis findings are advisory unless a caller promotes them. Analyzer execution, caps, and exceptions nevertheless fail closed and can abort generation before emission, a distinction exposed as `analysis.advisory: true` and `analysis.execution_required: true`. Quality independently reruns supplied analyzer evidence before use. Repair acceptance is hard-gated. `local_seal_proposal_preconditions_hold` proves only local applicability, while seal acceptance requires configured runner policy, a complete unkeyed integrity envelope, and a host `CompilerRecheckVerifier` that authenticates it; core manages no keys and launches no compiler. Static/compiler evidence is not an engine playtest or a balance claim. Q3 AAS proves global compiled topology/category content; entity-to-entity reachability comes from the project navigation analysis.

### M4 — Licensed map learning system

Initial local-study foundation implemented: bounded Q2 BSP/AAS ingestion, content-hashed provenance, static descriptor serialization, an interactive atlas, 26-map/12-lesson curriculum, 60 Q2 multiplayer rule cards (15 initial rule hypotheses), provisional author/family splits and a fixed generator/compiler comparison matrix. See [the study regimen](MAP_STUDY.md). Rights-reviewed admission, labels and actual training remain outstanding; this does not mark M4 complete.

- Provenance/licence-gated source registry and safe parser.
- Topology/gameplay/construction feature extractor.
- Retrieval index, ranking dataset, semantic-plan and repair corpora.
- Author/family-held-out evaluation, LoRA training, GGUF export, model cards, and reproducible manifests.

### M5 — Professional blockouts and production loop

- Q1/Q2 SP campaigns and MP/CTF layouts; Q3 duel/FFA/team/CTF/tournament arenas.
- Encounter/resource/item timing simulation, bot/monster regression suites, theme/material/lighting passes, complex motifs, performance budgets, and automated playtest telemetry import.
- Multi-pass self-critique that proposes, tests, measures, and keeps only verified improvements.

The original [Counterweight experiment](Q2_COUNTERWEIGHT_STUDY.md) extends the study sequence with native lift/teleport contracts, exact compiled chamfer and swept-volume checks, and a pinned dedicated-engine initialization smoke gate. It has no connected-player traversal or match evidence yet. The [iteration roadmap](../knowledge/q2/iteration-roadmap.json) tracks mechanism ablations, continuous ascent, drop/recovery and integration into the default generator; explicit experimental recipes do not establish automatic motif selection or trained weights.

[Talus](Q2_TALUS_STUDY.md) extends that sequence with two continuous ramps, exact compiled slope and landing-turn sweeps, complete serialized navigation graphs, and 264 routes requiring no jump, drop or mechanism. The [continuous movement study](Q2_CONTINUOUS_MOVEMENT_STUDY.md) adds three archive references and documents the difference between rendered ramp width and usable hull clearance. Material physics flags and Q2 spawn-eye sampling now have explicit gates. Its original static record is supplemented by the native ramp/stair comparison below; landing damage and automatic continuous-ramp selection remain outstanding.

[Undercut](Q2_UNDERCUT_STUDY.md) adds three local floors and two separately audited
drop/return candidates. The [reference return study](Q2_DROP_RETURN_STUDY.md)
demonstrates a complete ordinary geometric `gladdm1` return with explicit support,
steps and exposure phases. The [Talus comparison](Q2_RAMP_STAIR_COMPARISON.md)
isolates one ramp/stair intervention while checking unchanged compiled geometry,
entities and materials. These are stronger construction and measurement
contracts. The later native Talus pair below supplies bounded world-movement
evidence; Undercut's native falls, damage and player outcomes remain open.

[Fold](Q2_FOLD_STUDY.md) adds a compact adjoining-room example with interior
walkways, a real horseshoe, a stepped crate and explicit water/teleport contracts.
Its selected compiled routes, dry ordinary graph and dedicated initialization
pass. The six-map local QL atlas (`out/ql-atlas/index.html`, written by `python -B -m tools.ql_atlas.package`) and
[spatial-pattern research](MP_SPATIAL_PATTERNS.md) extend the vocabulary while
preserving reference revisions and cross-game limits. The
[compact MP regimen](MP_COMPACT_LAYOUT_REGIMEN.md) separates projected packing,
native movement and encounter outcomes. Fold's historical record remains a
static/initialization result; the subsequent movement experiments below add
separate evidence for its shallow crossing.

[Lockyard](Q2_LOCKYARD_STUDY.md) supplies the outdoor-court and deeper-water
fixtures, informed by [Manhunt and gladdm1](Q2_COURTYARD_LIQUID_STUDY.md). It adds
viewpoint-specific sky checks, full wet-route/emergence geometry, and passage-node
dependency tests that caught an ordinary return hidden by the room abstraction.
The final example passes 17 affected CTest gates, 24 preview states, 42 prior-map
preview controls and 16 dedicated initialization gates; the full configured test
suite was not rerun. Its larger envelope and mixed sightline/resource-distance
descriptors remain explicit tradeoffs.

The optional [native CM/Pmove harness](../tools/native_movement/README.md) uses
unmodified external Quake II 3.19 code on the compiled worlds. Across Fold and
Lockyard, 16 retained trials contain 14 completions and two level-view failures
at Lockyard's low roof; dive profiles complete the culvert. Matched drained
controls isolate water on the same route. These results do not time Lockyard's
actual dry perimeter or simulate oxygen, damage, dynamic entities or matches.
The knowledge base retains those observations and counterexamples. Profile
admission and learned weights remain outstanding; later default motif
integration is recorded below.

[Ravel](Q2_RAVEL_STUDY.md) adds a more tightly packed interleaved composition:
six unequal rooms, nine passages, three stairs, one ramp and one optional drop
with local floors 0/128/256. The [Reclamation study](Q2_INTERLEAVED_ROUTE_STUDY.md)
certifies actual nested paths and preserves rejected corner/vertical shortcuts.
Ravel's XY envelope area is 18.1% smaller than Fold's and 29.9% smaller than Lockyard's;
its 94.0% gross authored volume coverage sits between their 94.8% and 91.0%.
These comparisons do not assign a map-quality score. The final example passes
23 selected CTest gates, 24 preview states, 72 prior-map controls and 16 dedicated
initialization gates. Its floorplan comparison retains a common XY envelope,
scale bar and other-floor context. The core library and reference profile were
unchanged for that iteration; the full configured test suite was not rerun.

The [native Talus ascent study](Q2_NATIVE_ASCENT_STUDY.md) completes 24 matched
journeys and eight deterministic repeats on the existing ramp/stair pair.
Uphill times tie across all three lanes and both fixed inputs. Downhill results
separate station crossing from usable grounded recovery, so the evidence does
not justify a universal stair penalty or ramp advantage. Five interpretation
controls pass. These are world-only Quake II 3.19 trajectories, without game
damage, items, camera, player steering or combat; historical study records stay
unchanged.

Six native Ravel probes (`out/ravel/native-routes/README.md`, upstream study evidence, not imported) now complete its
center ramp in both directions and the ledge descent with two fixed inputs.
The retained landing positions and velocities remain separate from the authored
geometric drop candidate. Three frame-analysis controls protect goal bounds and
event ordering. This proves those bounded world-only journeys, without safe-fall,
braking, turning, complete-return or multiplayer claims.

[Weft](Q2_WEFT_STUDY.md) integrates reusable compact-room construction into the
actual default Q2 deathmatch/duel generator: unequal adjoining rooms, short
offset passages, raised crosswalks, horseshoe galleries, structural barriers
and two ordinary stair approaches per elevated room. Other title/mode paths
remain unchanged. The fixed nine-case 4/6/8-room by seed17/42/99 portfolio uses
normal six-candidate selection and retains the pre-change CLI baseline. Initial
compiled model-0 XY envelopes shrink 31.7–41.7%, while two cases gain visible
spawn pairs; the original failures remain in the revision record. Separate low
and very-high verticality prompt controls do not enter that comparison range.
The revised nine outputs have zero compiled visible spawn pairs and no local
hull/support or ordinary graph failures, retaining the same envelope reduction.
Thirty-nine analytical lighting warnings remain, including two in Weft.
All nine primary outputs, two separate semantic controls and the duplicate
Weft hero compile pass full VIS and extra4 one-bounce lighting. The semantic
controls select floor0 alone and local floors0/128/192.
At Weft delivery, all 42 then-configured CTest gates passed and the knowledge base contained 42 cards.
Analytical previews pass 216 width/theme/floor states: 162 primary, 36 semantic
control and 18 Weft states. Final CLI MAP/report parity and eight prior-recipe
MAP controls pass. Weft passes 16 dedicated initialization gates on the final
lit BSP, without a connected player or engine-rendered evidence. This is
procedural integration, with no trained weights, changed reference profile or
claim that every advanced recipe mechanism is selected.

The [native recovery study](Q2_NATIVE_RECOVERY_STUDY.md) adds eight fixed
post-drop profiles on unchanged Ravel geometry: five successful low-speed
grounded pauses and three retained failures. Releasing input succeeds at both
approach inputs; north-turn recovery depends on the declared pause clearance.
Six analysis controls protect these interpretations. Turn/pause success does
not establish onward doorway traversal, full-game damage or Weft movement.

Next evidence includes complete ordinary journeys with turns and passing, a
non-hijacking full-game rendering/traversal path, first-time dive discovery and
2/4/6-player route/control observations. A core placement limitation also needs a
separate fix: point lights inside liquid bounds are rejected as
`placement_in_solid`; Lockyard uses dry/above-water lights.

[Splice](Q2_SPLICE_STUDY.md) adds a second default family with an unequal wrapped perimeter, a playable center and an outer ordinary bypass. Nine fixed requests and a separately predeclared example pass full QBSP/VIS/light and local compiled checks; 180 analytical preview states and 16 Splice dedicated initialization checks pass. At Splice delivery the knowledge base contained 45 cards. Six selected topologies change, but eight-room selected graph variety falls from two to one despite richer candidate pools. The next ranking experiment must hold those candidate pools fixed and distinguish structural intent from universal quality.

The [native Weft circuits](Q2_NATIVE_MOTIF_CIRCUITS.md) retain eight strict protocol failures. Four stay in the actual full-hull stair band while missing a narrower checkpoint; four use partial edge support. Two later goal-pocket dwells remain observations, not circuit passes. Next isolate geometry-bound gate validity and turn timing before changing architecture; Splice itself has no native circuit or multiplayer playtest claim.

## Requirement status

The [Lattice iteration](Q2_LATTICE_STUDY.md) implements the fixed-pool ranking follow-up: matching marked 2–8-room Q2 deathmatch/duel pools retain all feature diagnostics while two unsupported descriptor weights become zero. Exact score ties use a deterministic seed permutation. The knowledge base now has 48 cards. All 96 candidate records retain geometry hashes and feature vectors; 16 selected outputs pass full compilation and 288 analytical preview states. Selected graph variety increases in the declared six/eight-room samples, while larger envelopes and new spawn-separation warnings remain. The [feature-semantics controls](Q2_RANKING_FEATURE_STUDY.md) do not identify learned replacement weights. Next specify and validate requested wrapping, outer bypass and spawn-separation contracts; native Lattice journeys and multiplayer quality remain open. The separate [Weft turning study](Q2_NATIVE_MOTIF_TURNING.md) adds 16 successful fixed trials, with wall-adjacent finishing dwells and no open-space stopping claim.

| Goal | Current evidence | Status / next proof |
| --- | --- | --- |
| Uses ggml | `GgmlRanker` executes a ggml tensor graph; optional planning uses pinned llama.cpp/ggml with local GGUFs | Achieved for deterministic ranking and constrained inference; learned layout/repair weights remain M4 |
| Prompt-based | Authoritative normalization plus rules/v1 or grammar-constrained GGUF produces a typed semantic plan affecting route, height, intensity, roles, and lighting | Achieved within the bounded vocabulary; richer learned intent remains M4 |
| Coherent Q1/Q2/Q3 formats | Four explicit dialects, syntax IR, typed title profiles, canonical round trips, and BSP/AAS plus compiled VIS/light matrices | Editable compiler-valid blockouts achieved; engine/runtime and gameplay-quality proof remains M5 |
| Sensible entities | Typed entities plus hull-aware placement binding/reachability, spawn safety, LOS/cover, timing, and Q1/Q2 monster evidence | Static M3 evidence implemented; encounter/difficulty simulation and playtests remain M5 |
| Complex geometry | Union shells, boxes/convex polyhedra, stairs, ramps, arches, curves/patches, liquids, detail and placement ownership | M2 construction/lowering boundary achieved; richer learned motif selection remains M4 and production performance tuning remains M5 |
| Learn from human maps | Local Q2 reference ingestion, provenance registry, static descriptors, study curriculum/rules, provisional author/family splits and generator comparison tools | Local study foundation implemented; reviewed rights, training labels, trained weights and audited held-out evaluation remain M4 |
| Recursive improvement | Semantic-plan revisions plus typed localized candidates evaluated on a copy under exact preconditions, regression gates, exact lowering dialect, and strict lexicographic score improvement | M3 source-level acceptance and advisory report evidence achieved; learned repair remains M4/M5 |
| Elegant leak repair | Bounded `.pts`/`.lin` ingestion, direction-invariant witness, exterior proof, unique source-face ownership, local slab proposal, exact compiler envelope, and mandatory host-verifier authentication | M3 diagnosis/acceptance contract achieved; runner execution, trust/key management, and authentication mechanism remain explicit host responsibilities, while runtime repair orchestration remains M5 |
| Navigation and bots/monsters | Hull-expanded player and Q1/Q2 monster graphs plus Q3 EAAS category/topology evidence at three scales | Strong static/compiler evidence; AAS is global compiled evidence, not entity reachability, engine physics, AI behavior, or a playtest |
| Professional full levels | Canonical compiler-valid blockouts with static navigation/visibility/lighting/quality evidence and compiled VIS/light | Not yet professional, balanced, art-complete, or runtime-proven; M5 |

## Braid construction and placement follow-up

[Braid](Q2_BRAID_STUDY.md) completes a bounded explicit-intent follow-up to
Lattice. Sixteen fixed requests retain baseline A, intent-only B and local
spawn-refined C outputs. Explicit `wrapped_center` controls construction for Q2
DM/duel with 5–8 rooms; unspecified selection and ranker policy remain separate.
A→B changes family/geometry and resulting placement. B→C retains all 96
spawn-neutral candidate hashes and changes three selected maps; the predeclared
hero is unchanged by that second intervention. Smaller eight-room envelopes
coexist with larger six-room envelopes and less selected graph variety.

The knowledge base advances from 48 to 51 cards while preserving every previous
card and native record. New cards cover explicit construction intent, supported
pairwise spawn stations and [resource approach certification](Q2_RAISED_RESOURCE_CIRCUIT_STUDY.md).
The target-player-count spawn policy and 256-unit warning remain unchanged.
Next gates are complete native journeys, resource contact, respawn selection
and recovery, and human combat/navigation observations. Neither static warning
removal nor a wrapped silhouette closes those gates; the reference circuit's
blocked drop remains a failed edge. This iteration adds no trained weights and
does not promote the project to gameplay-validated or production-ready maps.

## Inter-room upper circulation

[Hinge](Q2_HINGE_STUDY.md) extends compatible existing 128/192-height decks
across room faces for explicit wrapped Q2 DM/duel requests with 5–8 rooms. The
bounded search preserves old rooms, stairs and lower passages, and rejects
unsafe emitted shell geometry. A retained first-stage grid-phase failure led
to a general interval-anchored search, with unchanged requests and selection
seeds. A no-compatible result remains part of the measured population.

Next distinguish a single useful upper connection from a connected upper
network. Declare exact resource journeys, crossings under the new route,
opposing traffic and alternative approaches before measuring them. Parallel
upper/lower room edges, small increases in projected coverage and successful
local movement do not establish route preference, combat balance or a reason
to add more height everywhere. [Powertrip](Q2_UPPER_CONNECTION_STUDY.md) supplies
bounded support and exposure evidence, not learned generator weights.

The knowledge base advances from 51 to 54 cards without changing earlier
cards or native experiment entries. Final selected layouts gain 12 upper links
from 77 offers among 96 candidate observations. The
[native Hinge study](Q2_NATIVE_UPPER_LINK.md) passes 7/8 rotations and 4/4
straight transfers; its supported fast-turn gate deviation remains failed.
Keep that protocol result when designing the next gate or movement experiment.

## Arrival spaces and native teleport behavior

[Relay](Q2_RELAY_STUDY.md) adds an original perimeter arrival pocket, two
departures into different rooms and a separate ordinary approach to the same
resource. The [reference study](Q2_TELEPORTER_ARRIVAL_STUDY.md) preserves a
failed Painkiller cap descent and Torn Glory's 336-unit shared route prefix.
Neither a clear facing ray nor two distant endpoints certifies two immediate
ordinary exits.

The new optional [stock game-module host](Q2_NATIVE_TELEPORT.md) exercises
actual trigger dispatch, post-teleport state, facing, hold and occupied exits.
It complements the existing world-only Pmove harness. Relay's four first
source-entry/hold/off-disc trials pass; full onward resource pickup, opponents
and human matches remain open. The original eight host-startup failures are
retained separately from the eight completed mechanism controls.

Next, test matched touch-on/off conditions with both solid pads retained,
then the partition's effect on exit denial and resource control. Only after
these spatial contracts should a typed opt-in shortcut policy enter a fixed
default-generator portfolio. No such policy or model training is delivered
by this recipe.

## Opposed stair bridges

[Span](Q2_SPAN_STUDY.md) expands default compact Q2 DM/duel construction with
opposed stair approaches, a connected upper bridge and a separately supported
lower aisle. Its 14 fixed requests retain 84 before/after candidate observations;
all 11 high-verticality selected maps offer the feature, while three flat
controls remain byte-identical. The source domain is 2–32 rooms; the compiled
portfolio covers 4–8. This distinction remains part of the transfer contract.

The knowledge base advances 57→60 cards without changing prior cards or native
experiments. Four first native upper journeys pass with paused turns and
wall-adjacent stops. Next execute the lower return, 192-height and other-rotation
journeys, continuous turns and interior stops before claiming full native loops
or combat flow. Match resources and starts when comparing the earlier crosswalk
with opposed stairs, then measure actual pickup, route choice and opponent
denial. Preserve the failed reference high bend and earlier native failures.
