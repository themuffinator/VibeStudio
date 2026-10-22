# Braid: explicit wrapped construction and local spawn spacing

Braid is an ordinary six-candidate default selection for the predeclared
eight-room, six-player seed 503 request. The prompt explicitly asks for a
“wrapped layout around a playable center.” Its display title changes only the
analytical viewer label. The generator now carries that request as typed
construction intent, separately from ranking preferences and local placement.

The delivered shape has an unequal outer circuit around a playable center,
three raised crosswalks, two horseshoe galleries and three offset barriers.
Local floors 0/128 connect through ten stair runs with 80 treads. The room graph
has eight rooms and eleven passages. These are original generated structures;
reference research informs the checks without supplying copied architecture.

## Two separately retained interventions

The same 16 request texts and seeds are retained across three stages, using the
ordinary rules/v1 planner, six candidates, ranker and repair pipeline:

| Stage | Retained behavior | What the comparison isolates |
| --- | --- | --- |
| A, baseline | Previous parser/construction; wrapped phrase had no typed effect | Exact prompts before either change |
| B, intent only | Typed `wrapped_center`; previous spawn placement | A→B construction-intent effect |
| C, final | Same intent plus local spawn spacing after previous visibility refinement | B→C placement effect |

Nine primary requests use 4/6/8 rooms at seeds 17/42/99. Six separately declared
holdouts use 6/8 rooms at seeds 7/101/307; all these holdout seeds are odd. The
four-room controls retain their earlier text. The other 13 requests, including
the separate example, append the wrapped phrase. The example is seed 503,
declared before either intervention. This is a small reproducibility matrix,
not a population-wide diversity or quality estimate.

Baseline source, executable, prompt and output pins are historical snapshots.
Every stage retains its own candidate trace and final canonical identity.
Compiler artifacts may be reused only after exact MAP-byte equality, followed
by fresh stage-specific analysis. Equal geometry does not make changed reports
or intent metadata interchangeable. The three stages are not 96 unchanged
candidate geometries: that earlier claim belongs to the Lattice ranking study.

## Construction intent is explicit

`PromptSpec::layout_intent` distinguishes `unspecified` and `wrapped_center`.
The wrapped contract is limited to Q2 deathmatch/duel requests with 5–8 rooms;
unsupported game/mode/size combinations and invalid enum values fail explicitly.
The rules planner and constrained semantic-plan validation require no symmetry
for that intent. Requested intent propagates through reports, canonical identity,
planner provenance, candidate traces and analytical exports.

Wrapped requests construct the existing pinwheel family for every candidate
seed. An outer ordinary bypass and a playable center are construction choices,
not hidden bonuses in a quality score. Ranker weights and the established
exact-score tie policy remain unchanged. Unspecified intent preserves the
earlier family-selection behavior at stageB; it does not acquire a wrapped
meaning merely from generic words such as “loops.”

The intent-only compatibility record retains **42 exact historical files**:
MAP/report pairs for 16 unspecified requests and five legacy controls. These
are stageB evidence, before the new spawn pass. Final compatibility is a
different **12-file record**: Braid's ordinary CLI MAP/report plus five legacy
MAP/report pairs. The 42-file result must not be presented as current final
parity for all previous compact requests, because placement can now change.

## Spawn spacing preserves each blocked pair

The [spawn study](Q2_SPAWN_SEPARATION_STUDY.md) explains the previous blind spot:
zero visible peers caused an immediate skip, even when an occluded same-floor
pair remained below the existing 256-unit separation warning. Its first
in-memory experiment found one supported local shift in each of five historical
Lattice cases. Those inputs precede Braid's intent intervention and remain a
separate feasibility population.

The new helper runs **after** the prior visibility refinement and only for
marked 2–8-room Q2 DM/duel layouts. Each spawn keeps its initial room and floor;
counts, facing, resources and geometry stay unchanged. Candidate stations must
have a clear supported 32×32×56 standing hull and at least 64 units from other
non-light point anchors. The public API rejects unsupported materials,
mechanisms, ownership, convex solids and differing room/passage base floors.

Each eye endpoint is validated separately. Exact shell occlusion between valid
endpoints is distinguished from explicit-solid occlusion, and both are
preserved per pair. A move cannot swap one hidden pair for a newly exposed one
while keeping the same total visibility count. Eligible moves strictly reduce
the lexicographic tuple of visible pairs, pairs below 256 and their capped
squared-distance deficit. The search uses the nearest 24 eligible existing
stations, at most two passes, and minimum-displacement/XYZ ties. Already
acceptable pairs receive no reward for greater distance.

The 9–32-room path keeps its previous visibility policy. The requested-player
spawn-count policy and separation threshold are unchanged. Static pair
occlusion and a cleared warning do not prove safe respawns, good recovery,
weapon access or multiplayer balance.

## The full fixed portfolio retains tradeoffs

The comparison (`out/braid-portfolio/comparison.json`, written by `python -B -m tools.mp_portfolio.braid_compare`) binds 604 inputs and
independently checks all 16 selected outputs in each stage. All 96 B→C
spawn-neutral candidate hashes match. Selected seed, rank scores and candidate
features remain unchanged across that placement intervention; independent
compiled checks cover selected world solids, nonspawn entities, full standing
stations and every blocked spawn-eye pair. This does not claim that all 96
candidates were compiled.

| Stratum | Close unordered pairs A→B→C | Lighting findings A→B→C | Final ordinary graph pairs |
| --- | ---: | ---: | ---: |
| Nine primary requests | 2→1→0 | 41→40→40 | 960 |
| Six holdouts | 3→2→0 | 28→29→29 | 882 |
| Separate seed 503 example | 1→0→0 | 6→3→3 | 162 |

Visible spawn-eye pairs are zero at every stage. Each close unordered pair
produces two directed insufficient-separation findings in the source report;
those findings are 4→2→0 primary, 6→4→0 holdout and 2→0→0 example. B→C clears
the remaining three pairs with one moved station in each affected map:

| Request | Spawn ID | Authored X before→after | Movement |
| --- | ---: | ---: | ---: |
| rooms8-seed17 | 25 | −456→−520 | 64 |
| rooms6-seed7 | 20 | −248→−120 | 128 |
| rooms6-seed101 | 21 | 248→184 | 64 |

Their Y, Z=24, initial room and facing remain unchanged. The other 13 selected
MAPs remain byte-identical B→C. These are the final intent-conditioned moves,
not the five earlier Lattice feasibility moves.

A→B does not uniformly reduce map size. Six-room primary seeds 42/99 grow
21.12%/7.16%; holdout seeds 7/101 grow 28.47%/8.81%, while their maximum room
hops fall 3→2. Eight-room primary seeds 42/99 shrink 22.80%/27.00%, with hops
unchanged at three. Eight-room holdout seeds 7/101 shrink 11.74%/22.21%; only
seed 7 reduces maximum hops, 4→3. Other primary/holdout envelopes are unchanged.
The example's separate change is shown below. Enforcing one family also
reduces distinct selected room multigraphs: primary six/eight-room strata and
six-room holdouts go 2→1, and eight-room holdouts go 3→1. The three four-room
controls keep one row graph. B→C changes none of these descriptors.

All 13 explicit requests offer six pinwheel candidates and select pinwheel;
the three unmarked controls remain rows. Their wrapped room arrangements have
no full-span room-only slicing strips, but that descriptor excludes passage
space and is not a full supported-route certificate. Final navigation and
quality-owned findings are zero. Ordinary graph pairs have no missing routes
or losses after any single passage-node removal. Local compiled hull checks
and graph redundancy remain distinct from continuous native movement.

## The predeclared example

| Seed 503 descriptor | A: previous rows | B/C: Braid wrapped |
| --- | ---: | ---: |
| Rooms / passages | 8 / 10 | 8 / 11 |
| Compiled model 0 XY envelope area | 5,825,280 | 5,233,920 |
| Maximum unweighted room hops | 4 | 3 |
| Mean unweighted room hops | 2 | 1.785714 |
| Analytical lighting findings | 6 | 3 |
| Close unordered source spawn pairs | 1 | 0 |

Braid's compiled envelope is about 10.15% smaller and has no room-only slicing
strip. Those changes already occur in A→B. The final spawn pass leaves Braid's
MAP/report unchanged from B, so its removed close pair is not credited to that
pass. The smaller envelope and shorter unweighted room paths describe this
particular selection; they are not native travel times or evidence of tighter
combat flow. Gross authored box coverage is likewise not walkable density.

Six spawn positions and 27 typed gameplay anchors yield 162 ordinary directed
spawn-to-anchor graph pairs. Full compiled local node, tread and anchor checks
remain separate from graph reachability and individual passage-node removal.
None of those checks certifies complete native player journeys. Three
analytical lighting warnings remain; compiled light data does not establish
rendered readability.

## Resource approaches teach a different lesson

The new [raised-resource study](Q2_RAISED_RESOURCE_CIRCUIT_STUDY.md) examines
The Rage by Sten “ztn” Uusvali. It certifies a 688-unit XY flat east approach to
the jacket-armor station and a 400-unit XY center approach with 64 units of rise
through four static 16-unit step constructions. The approaches start at
different stations and share the last 80 XY units. Their lengths therefore do
not compare alternative travel between identical endpoints.

The proposed closed circuit **fails**: its 64-unit drop has supported takeoff
and landing and a clear vertical envelope, but the lateral hull intersects
brushes 404/690. All five endpoint joins match; that does not repair a blocked
edge. Rail and Mega stations separately have full support, while their tested
approaches fail. They are not counted as connected playable layers.

Twenty fixed point rays from supported stations yield E 0/5, C 3/5, armor J 3/5
and Mega 0/5 clear targets. C and E belong to the passing armor approaches;
their exposure differs. Visibility, route support and pickup contact remain
separate evidence. The resource graph is not a certified closed loop, a native
traversal or a balance result. Its retained readme/metadata disagreements about
player range and spawn count further discourage treating archive labels as
ground truth for allocation policy.

## Validation and inspection

All 16 final selected maps pass sealed QBSP, full VIS, `-extra4` one-bounce light
and local compiled node/tread/anchor checks. Analytical previews pass 288 states
(270 comparison plus 18 example). Braid matches the ordinary CLI MAP/report
and passes 16 dedicated initialization checks. Its three remaining analytical
lighting findings are retained.

The full configured 50 CTest groups pass in 210.03 seconds, followed by the
new comparison CTest group passing in 0.25 seconds. There are now 51 configured
groups; this evidence is 50 full-suite plus one focused run. The new comparison
group contains 14 controls. The full run includes 15 C++ spawn-helper groups
and six Python spawn-study controls. Nine separate original reference controls
also pass; they are not another registered CTest group. The first helper-test
fixture failure remains in
its original log (`out/braid-portfolio/spawn-helper-tests.log`, upstream test log, not imported); fixing the
fixture's exception expectation did not weaken helper rejection or nonmutation.

Inspect the analytical layout (`out/braid/index.html`, upstream study evidence, not imported),
full preview (`out/braid/overview.png`, upstream study evidence, not imported),
Braid map package (`out/braid/qmg_braid.zip`, upstream study evidence, not imported),
portfolio index (`out/braid-portfolio/index.html`, upstream study evidence, not imported),
15-map comparison package (`out/braid-portfolio/qmg_braid_portfolio.zip`, upstream study evidence, not imported),
three-stage comparison (`out/braid-portfolio/comparison.json`, written by `python -B -m tools.mp_portfolio.braid_compare`),
full CTest log (`out/braid-portfolio/ctest.log`, upstream test log, not imported) and
new comparison gate (`out/braid-portfolio/final-analysis-tests.log`, upstream test log, not imported).
The knowledge updater (`out/braid-portfolio/update_knowledge.py`, upstream helper script, not imported) derives
51 cards from the SHA-pinned 48-card baseline, preserving every prior card and
native record; its `--verify` mode is read-only. Native Weft turning records
remain unchanged; no native movement or multiplayer experiment runs Braid.

The local archive, learned model profile and weights remain unchanged. No client
render, game input, gameplay recording or OS capture is used. The previews are
analytical orthographic diagrams. Full player routes, spawn selection/contact,
opponents, item control and human navigation remain follow-up work.

One exact PowerShell cleanup attempt for the new Braid viewer/engine scratch
directories was rejected by automatic approval review with “blocked by policy.”
The two project-local directories are retained without a retry; the checked
paths and process preflight are recorded in
cleanup status (`out/braid/cleanup-status.json`, upstream study evidence, not imported). Historical blocked scratch
paths and their parents were not retried.
