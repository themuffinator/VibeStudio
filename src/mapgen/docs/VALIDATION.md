# Validation and repair

The v0.4.0/M3 pipeline retains three generation trust boundaries—model text before intent, typed integer geometry before map syntax, and `MapDocument` before writing/compilation—and adds independent analysis, authenticated compiler-evidence, and candidate-acceptance boundaries. A pass at one boundary does not waive the next. Navigation, visibility, lighting, and quality analysis run only after structural validation and `MapBuilder` succeed. Their findings are advisory, but execution, limits, and exceptions fail closed and can abort generation before map/report emission; schema v4 records that distinction as `analysis.advisory: true` and `analysis.execution_required: true`.

## Semantic-plan boundary

The request-specific GBNF fixes canonical JSON shape, integer syntax, exact room-role count, route feasibility, normalized-request drift bounds, and directly expressible SP/arena/CTF role contracts. A separate dependency-free parser accepts fields in any order but rejects duplicate, unknown, or missing fields; invalid UTF-8/strings/escapes; floats in integer fields; unsupported schemas; trailing data; and input above 65,536 bytes. Typed semantic validation independently rechecks request drift, route feasibility, native Q1/Q2 SP endpoints/combat beats, Q3 tournament roles, and mirrored CTF intent.

A model receives one initial attempt plus the configured number of full-plan revisions. Each revision repeats the authoritative normalized request and supplies bounded, JSON-escaped issue evidence. A complete schema-valid object at the exact token limit is accepted as `ok_at_token_limit`; incomplete output, changing identity/configuration, byte/context overflow, or exhausted revisions fails closed. Omitting `--model` selects rules/v1 rather than presenting deterministic fallback as model output.

## Layout validation

`LayoutValidator` checks:

- nonzero unique IDs and signed-integer geometry within the conservative ±32768 map bound;
- room volume/clearance and overlap constraints;
- unique room-to-room connections and explicit, face-connected passage chains anchored at both endpoint rooms;
- corridor width/height and centering constraints plus whole-graph reachability;
- requested loops, verticality, spawn counts, weapon coverage, SP encounters/exits, and CTF objectives;
- bounded closed convex solids, material/purpose/ownership/contents contracts, and structural-only liquid semantics;
- placement variants, teams, angles, lights, volumes, typed links, and containment; and
- placement-owned brush containment and point-placement separation from solid volume.

Convex validation derives all common half-space intersections, proves the normals bound space, rejects degenerate/inactive planes, computes conservative aggregate bounds, and returns an interior witness used later to orient map faces. It does not accept a polyhedron merely because each individual plane is nondegenerate.

The repair loop can remove malformed connections, regenerate explicit passage chains, bridge aligned disconnected components, add a feasible loop, restore vertical features, enlarge undersized rooms, normalize IDs, and repopulate invalid placement sets. Every actual change is followed by full validation. Repeated state or an exhausted iteration budget stops the loop. Remaining errors prevent map lowering/writing and produce exit status 2; warnings remain in the schema-v4 report but do not by themselves make `ValidationResult::ok()` false.

## Construction validation

The advanced construction layer sorts unique request keys before allocating stable IDs and rejects stale/colliding application. Every stair, ramp, arch, curved-wall segment, liquid, and mover-owned brush passes the same bounded convex validator used by `Layout`. It additionally enforces dimension/segment/control/solid caps, checked integer arithmetic, axis-aligned mover travel, vertical-only lifts, positive speed, valid teleporter angles, structural-only liquids, and odd Q3 patch dimensions no larger than 31.

Patch veneers always carry explicit hidden collision/backing solids. Mover directives refer to a generated typed door or lift and retain travel delta, speed, and wait independently of native entity keys.

## Free-space and lowering validation

`MapBuilder` treats room and passage boxes as a union of free space. It rejects invalid or over-limit volumes, detached/edge-only contact, shell-thickness intrusion, incompatible game/dialect choices, excessive compressed face cells, and shell/total brush caps. Tests cover partial and multiple openings, bends, T/cross junctions, duplicate and nested volumes, convex/placement-owned brushes, every title placement profile, collection reordering, and manifest stability.

The builder then validates liquid/detail/owner/link contracts, lowers through the selected title profile, assigns deterministic map-node IDs, and runs the strict generated-document profile. No `.map` is written if lowering or strict validation fails.

## Map-document profiles

The bounded parser enforces source/token/entity/property/primitive/face/patch/node/numeric limits and rejects malformed syntax without executing includes or other directives. It preserves ordered duplicate epairs, optional Q2/Q3 metadata, patches, and pristine source because source tooling must not rewrite distinctions it merely reads.

The `syntax_preserving` validation profile checks parseable geometry and dialect consistency without imposing project-generation policy. The `generated_document` profile additionally requires:

- one first and only `worldspawn` with at least one brush;
- one `classname` and unique generated keys per entity;
- exactly one Valve `mapversion 220` and no Q3 `mapversion`;
- explicit Q3 contents/surface/value on every face;
- consistent detail contents across a Q2/Q3 brush; and
- safe finite projections, nonsingular brush-primitive matrices, closed convex brushes, portable patches, and lexer-safe material tokens.

Canonical writer tests reparse all four golden dialects and require a byte-identical second canonical write. Mutation tests prove that pristine-source preservation is invalidated before canonical output.

## Navigation evidence

Stock Q2 mover and teleporter contracts, including canonical disc bounds, native lift station lowering and limits on static activation evidence, are documented in [Q2 mechanism contracts](Q2_MECHANISMS.md).

The [ramp contract](Q2_RAMPS.md) adds a continuous height function for supported axial six-plane wedges. Hull-aware feet follow the uphill footprint edge; both landings must admit ordinary end transitions. Unsupported convex tops do not become flat surfaces at their maximum Z. Navigation reports now retain the agent profile, complete node areas and optional slope functions, transition edges and placement bindings, allowing independent route checks from the serialized graph. Talus checks all 264 spawn-to-gameplay-anchor routes with jump, drop, lift and teleporter edges excluded.

`NavigationAnalyzer` derives a deterministic graph from the exact union of room/passage boxes and validated solid bounds. It applies a title/agent hull to legal origin space and represents walk, step, jump, drop, door, lift, and teleporter transitions with stable source references. Default player profiles use the official Q1/Q2 `[-16,-16,-24]..[16,16,32]` and Q3 `[-15,-15,-24]..[15,15,32]` standing hulls, an 18-unit step, conservative planned jump/drop bounds, and typed transition penalties. Q1/Q2 expose small and representative large ground-monster profiles; Q3 deliberately has no invented monster profile. Each result carries the lowercase SHA-256 of the canonical typed `Layout` so downstream consumers can reject evidence from a different layout.

The analysis records placement bindings, bounded shortest paths, and unreachable spawn/objective/monster findings. Invalid source geometry, insufficient hull clearance, missing passages, unresolved movers/teleporters, unbound required placements, and unreachable required gameplay nodes are errors and make `NavigationAnalysis::ok()` false. Invalid limits, coordinate/arithmetic overflow, excessive source/grid/node/edge/pair/path work, or malformed graph queries fail closed with `NavigationError`. The analyzer does not repair or mutate the layout.

`shortest_navigation_path` deliberately measures node-to-node cost. Placement timing uses `navigation_path_between_positions`: two endpoints on one free rectangular area pay their direct XY Manhattan distance; endpoints on different areas pay the graph's anchor-to-anchor cost plus their local XY approaches to those anchors. Volume placements use the same volume-center XY position as placement binding. Authored spawn/item height offsets are projected onto the bound walking floor rather than charged as travel. The helper retains graph transition penalties, path provenance, reachability and expansion limits; a finite cost may not collide with the unreachable sentinel.

These position-aware costs fix the previous zero-cost result for separated spawn/pickup positions on one node, but remain a planner estimate. Paths through area centers can detour, Manhattan distance depends on axis alignment, and the metric does not find shortest continuous-space routes. Converting cost to milliseconds at a fixed rate does not simulate acceleration, strafing, jump arcs, mover waits, pickup touch radius or players blocking one another. Use engine traversal and playtest measurements before treating timing differences as evidence of better flow or balance. Node-only costs saved by older study tools are not comparable to these position-aware timing metrics.

## Visibility and analytical lighting evidence

`VisibilityAnalyzer` distinguishes visible, occluded, and outside-free-space segments. Room/passage coverage and box occlusion are exact; convex-solid aggregate bounds are marked conservative and treated as potentially visible. A conservative aggregate box therefore cannot prove hostile occlusion, cover, or safety. The analyzer produces stable placement LOS, room exposure, exact cover, integer crossfire, topology/articulation, connection readability/chokepoint, and spawn-safety metrics. Missing mode-required spawns are errors. Hostile LOS, insufficient separation, wide crossfire, and missing cover are warnings by default; when missing-cover reporting is enabled, its requirement also participates in the spawn `safe` verdict. Invalid inputs or coordinate/collection/test/record/finding cap exhaustion fail closed with `VisibilityError`.

`LightingAnalyzer` deduplicates repeated passage boxes, samples room centers, unique passages, and gameplay placements, and traces all bounded sample/light pairs in one visibility batch. Brightness is a deterministic project metric—typed intensity times perceptual color luminance with inverse-square attenuation and a near-distance clamp—not physical lux or compiled lightmap energy. It cross-validates lighting caps against the nested visibility limits. No lights and unreadable critical placements are errors; ordinary dark samples/connections and excessive contrast are warnings. Invalid limits/thresholds, overflow, or sample/light/visibility/finding cap exhaustion fail closed with `LightingError`.

Visibility and lighting findings are advisory observations unless a caller explicitly promotes them into an acceptance policy. Their errors feed `QualityScore`, but neither analyzer edits geometry and neither substitutes for the external VIS/light gates. Advisory does not make pipeline execution optional: invalid configuration, overflow, cap exhaustion, or an analyzer exception aborts generation.

## Compiler pointfiles and local seal diagnosis

Classic ericw-tools leak trails are normally `.pts`; q3map2 leak trails are `.lin`. The compiler gates reject both `.pts` and `.lin` after successful Q1/Q2 compilation so stale or tool-variant output cannot pass accidentally. Project-authored open-ceiling Q1/Q2 negative controls must produce a leak diagnostic and nonempty `.pts`. Q3 positive fixtures reject `.lin`, and a dialect-native deliberately open Q3 negative fixture must emit a nonempty `.lin` plus a leak diagnostic.

The pointfile API accepts an explicit Q1/Q2/Q3 dialect but treats the extension as caller metadata: content must be one finite `x y z` triplet per bounded line. Defaults cap input at 4 MiB, 65,536 points, 256 bytes per line, and absolute coordinates at 1,048,576 units. Coordinates normalize to signed fixed-point values at 1/1024 world-unit precision, consecutive duplicates are removed, and forward/reverse representations canonicalize to one witness/fingerprint. Empty, malformed, non-finite, out-of-range, overlong, oversized, over-count, or fewer-than-two-distinct-point input fails closed.

Leak diagnosis coordinate-compresses the room/passage union into bounded interior, enclosed-void, and exterior cells, then verifies the first witness transition across an exposed boundary. It succeeds only when one room or connection, one axis, and one minimum/maximum face own the crossing and an adjacent exterior-cell proof agrees. No crossing, edge/corner/coincident ambiguity, invalid geometry, arithmetic overflow, and plane/cell/fill/segment-volume caps are typed non-success results.

A successful `LocalSealProposal` is deliberately narrow: an outward structural wall slab around the diagnosed face, plus canonical layout SHA-256, exact layout/witness/owner/face/bounds/crossing/exterior-proof applicability metadata, and baseline regression metadata. `local_seal_proposal_preconditions_hold` can recheck those local preconditions, but applicability is not acceptance and cannot prove a compiled artifact is sealed. A global outer wrapper is never proposed.

## Quality analysis and hard repair acceptance

`QualityAnalyzer::analyze` combines structural validation, optional diagnosed-leak evidence, player navigation, visibility, lighting, spawn-to-weapon/health/armor/monster/flag/exit travel timing, and Q1/Q2 small/large-monster reachability. Every supplied result must carry the exact lowercase `canonical_layout_sha256(layout)`. The analyzer then independently reruns navigation, visibility, lighting, and any supplied leak diagnosis under the exact nested `QualityOptions` and requires full typed-content equality, so copied layout hashes cannot legitimize a mutated payload; returned `QualityAnalysis` repeats the canonical layout SHA. Null leak evidence means leak status was not evaluated; a supplied ambiguous, invalid, or otherwise non-diagnosed result rejects. The 64-bit `quality_layout_fingerprint` is a domain-separated derivative of that bounded canonical SHA and only a lightweight edit-precondition token, not artifact identity. Inputs must also match the expected agent profiles; Q1/Q2 monsters require both monster analyses, while Q3 rejects monster evidence and omits monster timing. `QualityLimits` bounds rooms, connections, solids, placements, spawns, targets, navigation nodes/edges, upstream findings, timing/monster pairs, per-query expansions, and local findings; violations fail closed with `QualityError`. Metrics and findings are advisory and do not mutate the layout. When quality is present, report rendering recomputes it independently and rejects any complete-content mismatch before serialization.

`QualityScore` orders structural, leak, navigation, visibility, lighting, timing-error, then warning counts lexicographically. The order is an explicit acceptance policy, not a learned notion of overall fun. `QualitySummary::leak_diagnosis_evaluated` distinguishes absent local pointfile evidence from a diagnosed result; schema-v4 generation reports it as false because the pipeline receives no compiler pointfile, so `leak_errors: 0` is not a leak-free proof. The field does not describe later authenticated compiler evidence. `QualityRepairEngine` accepts typed add/adjust-light, move-placement, replace-passage, add-cover, or local-seal proposals only after these hard gates:

- proposal ID, baseline layout fingerprint, and edit preconditions match exactly;
- evaluation occurs on a copy and introduces no new structural error or placement-identity loss;
- no established player/monster path is lost and no navigation error is added or replaced by a different error identity at an equal count;
- the candidate still lowers through `MapBuilder`, preserving supplied `ConstructionResult` patch veneers and mover directives when requested;
- the optional `QualityRepairOptions::dialect` is compatible with the title and binds lowering to that exact dialect (otherwise the title default is used; Q3 brush primitives are supported);
- a seal has configured `CompilerRecheckRequirements`, matching complete `CompilerRecheckEvidence`, and a non-null `CompilerRecheckVerifier` that authenticates it; and
- the candidate score is strictly lexicographically better.

Seal evidence binds the envelope schema and runner ID; proposal ID; baseline and candidate fingerprints and canonical layout SHA-256 values; exact map dialect; canonical lowered map hash and byte count; compiler executable, options, and invocation hashes; BSP hash and byte count; diagnostics hash; pointfile dialect and witness; compiler success; postcompile pointfile absence; and an explicit leak-free result. `compiler_recheck_envelope_sha256` is an unkeyed integrity digest over those fields in a canonical encoding capped at 4,096 bytes; it is not authentication. Runner IDs are capped at 256 bytes and opaque `verifier_authentication` at 16,384 bytes. The host verifier must authenticate that envelope through a signature, trusted IPC token, or equivalent out-of-process mechanism and return true; missing/false/throwing verification fails closed. Core manages no keys and launches no compiler. Failure retains the baseline and returns a typed rejection reason. Passing does not require every pre-existing warning to disappear and is not a gameplay-quality claim.

## Compiler evidence

Optional external gates provide evidence beyond in-memory validation:

- Q1: six-room SP and 32-room DM through hash-pinned strict BSP29 qbsp, then quick and final ericw-tools VIS/light on both fixtures when the complete optional tool quartet is configured;
- Q2: six-room SP and 32-room CTF through hash-pinned strict IBSP38 qbsp, then quick and final ericw-tools VIS/light on both fixtures when configured; and
- Q3 legacy and brush primitives independently: duel-4, CTF-8, and DM-32 through hash-pinned q3map2 IBSP46 and BSPC EAAS5, quick VIS/light at every scale, and final VIS/light on the duel representative.

Positive fixtures lowercase-scan combined diagnostics, reject leak/invalid-brush markers even when a compiler reports false success, require nonempty portal files, forbid leak pointfiles, and bounds-check required nonempty BSP lumps. Dialect-native open-ceiling negative controls prove the leak checks are live. Classic quick VIS uses `-fast`; final light uses `-extra4 -bounce 1 -soft`. VIS/light run single-threaded where supported, but the project does not claim compiler artifact bytes are deterministic. Both stages must report completion metrics. Q1 final artifacts require BSP29 lighting plus a QLIT version-1 `.lit` whose RGB payload exactly corresponds to the BSP lighting sample count; Q2 requires nonempty IBSP38 VIS and lighting lumps.

Q3 quick mode applies `-fast` to VIS and light, while the duel representative also runs their final forms. Every lit stage must report positive illuminated luxels and lightmaps; the resulting IBSP46 must retain nonempty draw surfaces, lightmaps, light-grid, and visibility data. BSPC `aasinfo` must report positive and internally consistent areas/settings, nodes, portals, clusters, and reachability size. Counts for all reported reachability kinds—walk, crouch, barrier jump, jump, ladder, walk off ledge, swim, water jump, teleport, elevator, rocket jump, BFG jump, grapple hook, double jump, ramp jump, strafe jump, jump pad, and func bob—must sum to `reachabilitysize - 1` because index zero is the sentinel; representative walk and barrier-jump counts must cover at least the fixture room count, and walk-off-ledge must be positive. These AAS checks are global compiled topology/category evidence; entity-to-entity reachability is established by the project navigation analysis. A deliberately open-ceiling map in each native Q3 source dialect must be diagnosed as leaked and emit a nonempty `.lin`, proving the test is capable of failing.

These compiler gates run external GPL tools as subprocesses; none of their code or binaries is linked into or copied into this MIT repository.

## Remaining validation boundary

Original recipes now add exact compiled convex-polyhedron versus standing-hull checks, including chamfers, axial ramps, separate world/mover submodels and Counterweight's full platform/passenger sweep. The legacy vertical-prism API remains available. Talus adds three standing-hull lanes per ramp with continuous swept checks in both directions, explicit entry steps, actual compiled support knots, and supported lateral translations on both landings. Surface-matching tolerances identify source correspondence; collision checks preserve exact stored-plane fractions with zero collision epsilon. `qmg_recipe_geometry_tests` covers actual overlap/contact, separating axes, mid-segment blockers, sloped support, playerclip/window contents, compiler bevels and landing turns. These geometric translations do not simulate native stepping or steering.

The core now samples Q2 player-spawn LOS at entity origin plus 22 units, matching the compiled study's eye policy. Reports label the offset; other placement targets and distance descriptors retain entity origins. Q1/Q3 retain their previous origin policy pending a separate title review. Historical reports retain their original sampling and are not retroactively relabelled. Conservative convex bounds remain potentially visible in core safety metrics, so typed and compiled eye checks remain separate evidence.

Talus also checks stock WAL contents and surface flags, rejects mixed-content compiler warnings, and requires every architectural world brush to compile as SOLID. A candidate mud texture carried WATER contents and made 26 authored architectural brushes non-solid; compiled support checks caught this before delivery. Texture existence or visual naming alone cannot establish architectural collision.

Undercut adds a bounded drop audit: full-footprint compiled takeoff/landing support,
literal axial departure and vertical descent sweeps against every player-mask
world brush, and exact-height endpoint binding followed by an independent
walk/step/door-only return search on the serialized graph. The supporting ledge
remains in collision checks. Native ballistic motion, fall damage, occupied
landings and a complete compiled sweep of the ordinary return remain untested.
`qmg_drop_audit_tests` includes blocked intermediate space, missing support,
forbidden-only/reversed returns, all player collision contents and identity drift.

Fold adds exact liquid-volume identity, deliberate water surface metadata,
native teleporter target uniqueness and facing, full disc support and raised
arrival clearance. Eight selected flat routes have common convex support and
continuous hull sweeps, including the above/below atrium pair. The ordinary BFS
filters gameplay anchors separately from mechanism markers. Its historical
experiment did not verify optional liquid entry/exit or native motion; the
separate Quake II 3.19 trials below now cover selected wet/drained crossings.
`qmg_fold_audit_tests` retains
same-AABB chamfer, altered contents/flags, unsupported disc, wrong yaw, duplicate
target and intermediate obstruction controls.

Lockyard adds exact water bounds/metadata, 16 wet tread support checks, 19
piecewise route intervals, bounded full-footprint floor-union coverage, two clear
emergence columns and a blocked surfacing control beneath the low roof. The
compiled sky audit requires the intended solid sealing roof and three selected
sky-first rays. Recipe environment tests bind the sky face to its room's lowering
provenance and preserve earlier recipes when environment options are absent.
Fourteen synthetic Lockyard cases exercise these contracts. The analytical viewer
checks local bed height, wet stairs, low-roof projection and major-item labels;
24 Lockyard states and 42 prior-map compatibility states pass.

The independent [passage dependency audit](../tools/map_recipes/gate_audit.py)
removes a passage's nodes from the serialized directed walk/step/door graph,
retaining exact layout identity and gameplay bindings. Six tests cover its
contract. Closing Boiler–Winding in final Lockyard loses no required pair; also
disabling the new Court–west-station opening loses 57. This is a graph
intervention, not a physically recompiled closure. Optional recess dependencies
remain reportable rather than universally rejected. A room graph with no
articulation rooms cannot substitute for this test.

The optional [native movement harness](../tools/native_movement/README.md) links
externally pinned, unmodified Quake II 3.19 `Pmove`, `CM_BoxTrace` and
`CM_PointContents` in a private command-line dependency. It uses compiled world
collision, explicit stable settled starts and bounded synthetic command streams;
it has no client, game module or input-device access. Twelve tests pass with that
dependency and generated fixtures present; native integration tests explicitly
skip when their optional dependencies are absent. The 16 retained Fold/Lockyard
trials contain 14 completions and two level-view stalls. First accepted grounded,
standing, dry arrival is resolved to 16 ms; settling and post-goal commands are
excluded from goal time. Failure traces remain available.

Water-removed controls compare complete remaining compiled planes, texinfo and
contents plus unchanged nonworld source entities. This isolates water on the same
route, without asserting identical BSP indices or timing the actual dry
perimeter. Native waterlevel is observed, but oxygen, damage, items, mechanisms,
opponents and Q2REPRO movement equivalence are outside this harness. These trials
do not upgrade Undercut's fall tests to native evidence. The separate Talus
experiment below supplies its own ramp/stair evidence.
See the [Lockyard study](Q2_LOCKYARD_STUDY.md) for the final artifact and the 17
affected CTest gates; the full configured suite was not rerun.

The [native ascent study](Q2_NATIVE_ASCENT_STUDY.md) rechecks the existing Talus
source/compiled intervention and runs 24 complete ramp/stair journeys plus eight
deterministic repeats. Three lanes, both directions and literal forward inputs
400/200 retain stable starts, complete commands/frames and dry standing grounded
goals. Five analysis controls distinguish an airborne plane crossing from a
grounded completion, reject distant recovery or missing frames, separate feet
motion from stored velocity, ignore post-goal falls and isolate the output root.
Uphill times tie; the downhill difference partly or wholly occurs after crossing
the destination plane. This does not measure camera smoothing, damage or human
control, and deterministic repeats are not independent player observations.

[Ravel](Q2_RAVEL_STUDY.md) independently checks supported bent routes, its
above/below crossing, a three-lane continuous ramp and a drop's geometric
departure/descent/landing with an ordinary graph return. Its 264 required
walk/step/door journeys exclude the optional drop. The passage-dependency audit
retains exact graph identity and reports closures separately. The
[Reclamation reference study](Q2_INTERLEAVED_ROUTE_STUDY.md) supplies a negative
case where valid endpoints still produce a blocked diagonal corner cut, and
another where stacked floors cannot form a vertical drop.

The existing XY floorplan preview now preserves the same envelope, origin and
scale through floor changes and shows a world-unit scale bar; other-floor shapes
remain dashed context. Projection checks independently bind room coordinates,
floor geometry and scale-bar length, alongside existing labels and mechanism
arrows. Ravel passes 24 floor/width/theme states and 72 regenerated prior-map
controls. Twenty-three selected CTest gates and 16 dedicated initialization gates
pass on the final BSP; this is not a full configured-suite run, native whole-map
traversal or multiplayer balance claim. Core/profile hashes remained unchanged
for that historical iteration.

The separate Ravel native record (`out/ravel/native-routes/README.md`, upstream study evidence, not imported) retains
six successful fixed-input world-only profiles: four center-ramp journeys and
two ledge descents. Its three frame controls reject goals outside the original
recovery window, preserve landing-before-station semantics and reject a forged
earlier goal. Actual first landings differ from the diagram's geometric candidate
and retain horizontal velocity. The summary binds BSP, MAP and layout identities;
these results do not certify damage, braking, turning recovery, whole-map ordinary
returns, or equivalence to the dedicated Q2REPRO module.

An outstanding core limitation rejects point lights inside liquid aggregate
bounds as `placement_in_solid`, although native underwater point lights are
valid. Lockyard's lights remain dry or above the surface; this iteration does
not change that placement rule.

The [Weft/default motif portfolio](Q2_WEFT_STUDY.md) compares nine fixed real
prompt requests against retained pre-change CLI MAP/report/BSP outputs, with
normal six-candidate selection. The new exporter preserves selected MAP/report
bytes and exports actual typed geometry and all placements separately; it fails
on unsupported convex solids instead of replacing them with boxes. Its initial
parity control compares MAP/report bytes with the ordinary CLI using identical
options. Compiled audits separately retain local navigation-node and tread
centers, full standing anchor hulls and support candidates, ordinary directed
graph pairs, per-passage node-removal dependencies, model-0 bounds, texture
physics and each analyzer's findings/quality score.

All nine initial revised BSPs are sealed and pass those local hull/support and
ordinary graph gates. Their model-0 XY envelopes are 31.7–41.7% smaller than the
same-request baselines. Two initial outputs introduce visible spawn pairs;
zero quality-owned findings must not hide visibility or lighting findings.
Three baseline health-entity centers fail the standing-hull placement check;
this conservative exact-center test does not prove native pickup
unreachability. Graph closure tests do not physically compile shut doorways,
and local compiled checks do not execute complete native routes. After bounded
spawn-location refinement, all nine outputs have zero compiled visible spawn
pairs, unchanged envelope dimensions and no local hull/support or ordinary
graph failures. Thirty-nine analytical lighting warnings remain; Weft retains
two connection-contrast warnings. All nine primary outputs, two separate
semantic controls and the duplicate Weft hero compile pass full VIS and
extra4 one-bounce lighting. The semantic controls select floor0 alone and
local floors0/128/192 respectively. At Weft delivery, all 42 then-configured CTest gates passed across
the four core tests and remaining 38 tests. Analytical preview checks pass 216
states across three widths, two themes and every floor choice: 162 primary,
36 semantic control and 18 Weft states. The final ordinary CLI reproduces the
Weft MAP/report bytes, and eight published recipes reproduce their historical
MAP bytes against the current core. Weft passes all 16 dedicated initialization
checks on the final lit BSP, completing 40 scheduled post-spawn server frames
without matched initialization failures. There is no client, connected-player
traversal, live item-state dump or engine render in that result.

The separate [native recovery study](Q2_NATIVE_RECOVERY_STUDY.md) adds eight
fixed world-only profiles to frozen Ravel: five successes and three failures.
The goal requires a dry standing grounded hull, bounded position and velocity,
and nine qualifying 16ms frame-end samples spanning 128ms. Leaving the larger
recovery envelope or required floor state permanently fails an attempt.
Compiled full-footprint support and hull sweeps cover the declared pocket;
actual native trajectories decide individual outcomes. Six controls reject
weakened dwell, goal or frame interpretations. A successful turn/pause profile
does not certify onward doorway traversal, and these trials do not test Weft.

The optional Quake Live atlas reader has synthetic IBSP46/47 format, index,
patch, height-clipping and annotation controls. It projects actual upward draw
surfaces; its area metric is not navigation or player-accessible coverage.
`qmg_ql_atlas_tests` is registered when the optional Shapely dependency is present.
The separate `qmg_packing_audit_tests` checks exact authored rectangle unions and
exterior/enclosed uncovered regions without labeling that metric playable density.

The optional [dedicated smoke harness](../tools/map_recipes/ENGINE_SMOKE.md) runs a pinned local Q2REPRO legacy module against independent executable/DLL copies with read-only stock assets. A positive control and deliberately buried-item negative control establish the diagnostic gate. Counterweight passes native loading and initialization. Dedicated ticks without connected players do not establish spawn selection, pickup contact, mover activation, teleport use or rendering. Follow the study's explicit runtime gates before promoting those claims.

M3 provides the integrated SHA-bound static analyzers, advisory schema-v4 report evidence, pointfile/local-boundary diagnosis, typed repair evaluator, deeper Q3 AAS checks, and representative compiled VIS/light evidence described above. It does not run an engine, simulate full title physics/combat/item economies, prove bot tactics, or turn a blockout into a balanced/art-complete map. Licensed corpus learning stays in M4; runtime playtest telemetry, production balance/art/performance, and the wider self-improvement loop stay in M5.

## Splice topology iteration

[Splice](Q2_SPLICE_STUDY.md) retains nine matched before/after requests and a separately predeclared six-room seed73 example. All ten final outputs pass sealed QBSP, full VIS, extra4 one-bounce lighting and local compiled anchor/node/tread checks. They retain 180 analytical preview states. Splice passes 16 dedicated initialization checks. Final CLI MAP/report, three unchanged four-room MAPs and eight historical recipe MAPs match exactly. Thirty-four primary lighting findings and four hero lighting findings remain. No native Splice movement or gameplay is claimed.

Eight affected CTest groups pass across the motif, six-test core/tool wave and final two-test analysis wave; the topology group overlaps the latter two waves. Eight topology controls distinguish tied shortest paths, parallel passages, hub removal, disconnected graphs, slicing gaps and stale candidate traces. Ten native-circuit controls pass; all eight actual Weft circuit experiments retain their strict failed outcomes. [Full observations](Q2_NATIVE_MOTIF_CIRCUITS.md) separate four fully supported exit bands from four partial-support approaches and two later endpoint dwells.

Candidate traces observe the same six generator/ranker candidates and check selected seed, score and final canonical identity against an ordinary pipeline run. Both primary and hero traces bind to the final layout; comparison checks actual MAP/BSP bytes before using compiled metrics. Room hops, centrality and room-removal connectivity are unweighted graph descriptors, not movement times or combat quality. The candidate pool grows while the selected eight-room sample loses graph variety, which remains a recorded limitation.

The [Lattice ranking study](Q2_LATTICE_STUDY.md) binds 96 unchanged candidate records, 16 frozen traces and 32 unchanged diagnostic MAP/report file pairs; five out-of-scope CLI requests retain ten exact MAP/report files. All 16 selected outputs pass sealed full QBSP/VIS/light and local compiled checks, with 960 primary, 882 holdout and 162 example ordinary spawn-to-anchor graph pairs and no missing pairs or single-passage removal losses. Zero visible spawn-eye pairs coexist with 4 primary and 6 holdout insufficient-separation warnings. Lighting findings remain 41 primary, 28 holdout and 5 example; no navigation or quality-owned findings remain. Analytical preview checks pass 288 states: 270 comparison and 18 hero. All 47 configured CTest groups pass, followed by a successful targeted rerun of the revised ranking and native-turning groups: 13 arithmetic/provenance controls and 11 physical-contract controls. Lattice matches ordinary CLI MAP/report bytes and passes 16 dedicated initialization checks. The separate [Weft turning study](Q2_NATIVE_MOTIF_TURNING.md) retains historical strict0/8, protocol-only2/8 and new16/16; every finishing dwell is wall-adjacent, and five broad station diagnostics still fail. Reconstructed upper-chord support and native frame-end contact are separate evidence. None of those native movement trials runs Lattice.

## Braid three-stage validation

[Braid](Q2_BRAID_STUDY.md) retains 16 requests at baseline A, intent-only B and
final C. The comparison binds 604 inputs, rederives selected compiled descriptors
and preserves all 96 B→C spawn-neutral candidate hashes. All final selected
maps pass sealed full QBSP/VIS/light and local compiled node/tread/anchor
checks. Ordinary graph pairs remain 960 primary, 882 holdout and 162 example,
with no missing pairs or single-passage removal losses. Visible spawn-eye
pairs remain zero throughout. Close unordered pairs are 2→1→0 primary,
3→2→0 holdout and 1→0→0 example; final visibility findings are zero. Lighting
findings remain 40 primary, 29 holdout and 3 example, with no navigation or
quality-owned findings. Some six-room envelopes grow and selected graph variety
falls under explicit wrapped intent; those tradeoffs remain in the comparison.

The full configured 50 CTest groups pass in 210.03 seconds. One newly registered
comparison group then passes in 0.25 seconds, giving 51 configured groups with
50 full-suite plus one focused-run evidence; its 14 controls include malformed
provenance and placement invariants. The full run includes 15 C++ helper groups
and six Python spawn-study controls. The separate raised-resource study's nine
controls are not a registered CTest group and retain a failed circuit drop.
Analytical previews pass 288 states. Braid passes 16 dedicated initialization
checks and the final CLI/legacy parity record covers 12 exact files. The
42-file unspecified compatibility record is historical intent-only evidence
from before the appended placement pass.

All older native movement records remain frozen, and none runs Braid. Dedicated
initialization is not respawn selection, pickup contact, client rendering or
gameplay. One automatic-review rejection prevents cleanup of the two new
project-local Braid viewer/engine scratch directories; the exact paths and
preflight are retained in cleanup status (`out/braid/cleanup-status.json`, upstream study evidence, not imported)
without retries of historical blocked paths.

## Upper-connection construction gates

[Hinge](Q2_HINGE_STUDY.md) adds exact endpoint-fragment access, full upper-strip
support and old stair raise-before-advance clearance. The source graph is
insufficient at overlapping free volumes: a missing portal slab remains a
negative emitted-shell control even when navigation still reports a path.
Physical upper-opening masks are separate from deleting connection metadata,
and every lower passage retains its own clearance check.

The 11-group owner test covers both axes,128/192 elevations, offset/stacked
portals, translated grid phases, unsupported endpoint fragments, stair-step
interference and public-input nonmutation. A 16-unit shell gap rejects, an
exact 32-unit gap passes, and a redundant 81-pair fixture stops after 64 failed
lowerings. The same 48 source fixtures yield 44 links and four unchanged cases;
the pipeline portfolio is a separate population. The full 55 configured CTest
groups pass, with the affected upper-link and wrapped-intent groups also
passing after the phase correction. Dedicated initialization, native movement
and analytical browser previews retain their distinct evidence scopes.

The final comparison binds 558 inputs, 96 trace observations and 16 selected
compiled layouts. All original geometry and envelopes are preserved; local
compiled checks and ordinary graph pairs pass throughout. Lighting findings
change 40→45 primary, 29→35 holdout and 4→4 example. All 288 analytical preview
states pass; Hinge passes 16 dedicated checks and 12 CLI/legacy files match.
The separate 32-file guard-only parity belongs to its earlier stage.

Three compiled Hinge worlds retain the normal upper route, physically block
it, or remove its support slab. The missing-slab center passes while its edge
loses support, and the original full-width ground doorway remains supported in
all three. The [native study](Q2_NATIVE_UPPER_LINK.md) retains 11/12 successful
first trials, including one failed ordered turn despite later grounded arrival.
Its 20 controls, all 5,375 raw frames and wall-adjacent dwell limits remain
separate from static support and multiplayer behavior. The single cleanup
attempt for three new task directories was rejected by automatic approval
review; cleanup status (`out/hinge/cleanup-status.json`, upstream study evidence, not imported) retains the exact paths
without retries.

## Arrival-room and game-module boundaries

[Relay](Q2_RELAY_STUDY.md) independently checks full 64×64 disc support,
actual raised arrival headroom, exact destination/yaw and four continuous
ground polylines. The path collision world includes both solid native discs.
Each departure starts in the arrival room, crosses its named incident passage
and ends with full-footprint support inside a different opposite room. A
station inside the passage alone is insufficient. The standalone auditor also
rejects non-SOLID world contents before any collision-mask filtering.

All 264 ordinary spawn/anchor graph pairs and the four named compiled polylines
pass. Sixteen dedicated initialization checks and 18 analytical preview states
pass. Three compiled spawn pairs and one arrival-to-spawn ray remain visible;
nine visibility and three lighting warnings are preserved.

The [native teleport host](Q2_NATIVE_TELEPORT.md) uses the stock game module
and server entity-world queries, so its first four Relay journeys exercise
actual activation and disc collision. Immediate relink, next ClientThink and
game-frame states are recorded separately. Eight mechanism controls include
real telefrag damage, an unkillable obstruction and no-activation negatives.
The obstruction control transports the actor but leaves it embedded; an event
is not a successful usable arrival. No client, input-device control, rendering,
network transport or multiplayer match is involved.

The complete Relay regression passes all 60 configured CTest groups. Its first
run retained three pre-existing direct-file Python import failures; changing
those three CTest invocations to module discovery resolves them without changing
test assertions. Both first (`out/relay/ctest-final.log`, upstream test log, not imported) and
corrected (`out/relay/ctest-v2.log`, upstream test log, not imported) complete runs remain available. Automatic
approval review blocked the new task cleanup; the exact retained paths (`out/relay/cleanup-record.json`, upstream study evidence, not imported)
were not retried.

## Opposed bridge construction and evidence

[Span](Q2_SPAN_STUDY.md) adds constructor-domain and emitted-geometry controls
for opposite stairs and three joined deck sections. Minimum, long and aligned
rooms cover four rotations and 128/192 heights. The independent all-SOLID
compiled auditor checks full stair caps, 18-unit step constructions, 160-wide
upper joins/turns, a separate lower aisle and exact gameplay station bindings.
All 14 rooms in 11 high-verticality selections pass; the 3 flat controls separately
contain no new motif. All 28 before/after selected worlds pass generic compiled
checks; 3 unchanged BSPs are independently copied and freshly inspected.

The comparison validates all 6 rows per candidate population, exact selected
seed/export binding and original room/lower-passage geometry. Only independently
bound optional upper-link slabs are excluded from room-local conservation.
First checker failures are retained. The unchanged CLI hero plus four
outside-scope legacy controls match 10 MAP/report files; the older nine-room Q2
DM control changes inside the intervention scope and is recorded separately.

Four native open upper journeys retain 2,306 complete states. Acceptance requires
all ordered gates, upper support and a 9-state 128-ms low-speed dwell. Four
24-frame pauses and wall-adjacent stops limit interpretation; descent includes
airborne states, and the lower native return is untested. Sixteen dedicated
initialization checks and 18 analytical preview states pass. The final full
regression and cleanup records are linked by the
[Span experiment manifest](../knowledge/q2/experiments/span-v1.json).

The complete Span regression passes all 64 configured CTest groups. Automatic
approval review rejected the single cleanup request for its five new runtime
and cache targets; the paths remain recorded without retries.
