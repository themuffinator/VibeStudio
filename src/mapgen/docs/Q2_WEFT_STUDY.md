# Weft: compact motifs in the default generator

The default Quake II deathmatch and duel generator now constructs compact groups
of unequal rooms with short offset openings, raised crosswalks, horseshoe
galleries and structural sightline barriers. Each elevated room has two ordinary
stair approaches, with separate upper and lower navigation. This changes the
actual prompt-to-layout path; the preceding Fold, Lockyard and Ravel studies
remain original recipe experiments with their own historical evidence.

Open the Weft layout (`out/weft/index.html`, upstream study evidence, not imported) or compare all 11 generated
blockouts in the portfolio (`out/mp-portfolio/index.html`, upstream study evidence, not imported). The
Weft package (`out/weft/qmg_weft.zip`, upstream study evidence, not imported) and
portfolio package (`out/mp-portfolio/qmg_compact_portfolio.zip`, upstream study evidence, not imported) retain the
original outputs and analytical diagrams for local review.

The change is a bounded procedural policy informed by reference research.
Model weights and the reference profile are unchanged. It does not add automatic
open-sky courts, liquids, teleporters, movers, continuous ramps or authored drop commitments.
Quake II single-player and CTF, and Quake I/III generation, retain the legacy
construction path.

## A fixed comparison before choosing an example

The case list (`out/mp-portfolio/cases.json`, written by `python -B -m tools.mp_portfolio.compare`) fixes nine requests: 4, 6 and 8
rooms crossed with seeds 17, 42 and 99. Four-room requests are two-player duel;
the others are six-player deathmatch. Each request goes through the real prompt
parser, rules/v1 semantic planner and default six-candidate generation/ranking
pipeline. The baseline was captured before integrating the motifs. Every
baseline and revised candidate is compiled and inspected on the same model-0
collision basis.

**Weft was declared in advance as rooms6-seed42.** Its display name does not
select another candidate or alter worldspawn, geometry, entities or materials.
The [exporter](../tools/mp_portfolio/README.md) serializes the exact selected
`GenerationPipeline` MAP and report, including the ggml selection trace. Its
separate layout export exposes the actual typed rooms, passages, solids,
placements, floor heights and geometrically verified stair runs. Unsupported
convex solids fail export rather than being flattened into boxes.

The retained parity control (`out/mp-portfolio/export-parity/parity.json`, upstream study evidence, not imported)
compares the initial Weft MAP/report bytes with ordinary CLI output from the
same request and default options. The export's optional display title affects
only `layout.json`. Changed existing outputs, path escapes and reserved map
names are rejected; an identical rerun leaves existing file contents and times
unchanged.

A separate final CLI parity run (`out/mp-portfolio/final-cli-parity/parity.json`, upstream study evidence, not imported)
again reproduces the delivered Weft MAP and report byte-for-byte after the
refinement. Eight published recipe MAP controls (`out/mp-portfolio/recipe-parity/parity.json`, upstream study evidence, not imported)
also remain byte-identical when rebuilt against the current core. Those controls
preserve historical authored geometry; they do not claim equal native movement
or validate the new default policy in every title and mode.

Two separately declared semantic controls (`out/mp-portfolio/semantic-controls.json`, upstream study evidence, not imported)
use the same six-room seed42 request with low and very high verticality. They
exercise prompt interpretation and default selection; they are not extra
baseline pairs and do not enter the nine-case comparison range.

The compiled low control selects floor0 with no stair runs and all 108 required
ordinary pairs. The high control selects local floors0/128/192 with ten stair
runs and all 138 pairs. Both pass local compiled hull/support checks, retain
zero visible spawn pairs, and lose no required pair under individual passage
node removals. The low control retains seven analytical lighting warnings; the
high control has none. This verifies the stated controls, not every wording,
seed or continuous verticality value.

## Construction contract

The [builder](../src/mp_motifs.cpp) varies room proportions, row populations,
passage positions and local motif orientations under the selected candidate seed. Shared room faces
receive short openings with sufficient overlap, rather than long corridors
around an empty perimeter. Rotation and unequal dimensions provide reproducible
variation; they are not evidence that all seeds produce distinct tactical play.

Raised decks are 24 units thick, with top heights 128 or 192 where requested.
Stairs use 36-unit treads, 16-unit rises and 128-unit width. Every elevated room
has two physically distinct runs. Crosswalks retain a clear lower route beneath
the deck. Gallery arms and connecting sections surround a usable lower space;
offset barriers interrupt selected views without becoming mandatory jumps.

The nine primary selected layouts use actual floors 0/128. Direct generator
tests exercise 192-unit decks; the separate semantic controls examine the
prompt-to-height response. Intermediate stair tops are not counted as whole-map
floors. Low verticality must omit raised decks. The
[construction tests](../tests/test_mp_motifs.cpp) also cover 2, 3, 31 and 32 rooms
and keep the non-Q2-DM/duel paths outside this policy.

The existing allocator still assigns semantic item roles, variants and rooms.
The motif policy places these on supported ground or deck stations with a
clear standing 32×32×56 hull. Spawn quantity remains the allocator's requested
player count: two for these duel requests, six for these deathmatch requests.
Position checks do not establish sufficient spawn variety, actual spawn
selection or balanced equipment recovery.

The subsequent spawn refinement stays in the selected room and floor and
examines at most 24 nearby supported alternatives per spawn, over two
deterministic passes. It accepts a move only when it strictly reduces visible
source spawn-eye rays, retaining separation from other point placements. Ties
prefer the nearer station. This bounded local objective does not optimize
equipment travel, match balance or the compiled ray result; those must be
retained and checked independently after selection.

## Initial results and the retained tradeoff

The initial revision (`out/mp-portfolio/revisions/initial`, written by `python -B -m tools.mp_portfolio.compare`) is retained before
the spawn-location refinement. All nine revised BSPs are sealed. Every required
ordinary spawn-to-gameplay-anchor pair is reachable in the serialized directed
walk/step/door graph, and removing each passage's nodes in turn loses no required
pairs. Compiled node-center, tread-center and authored anchor checks have no
failures. These are local hull/support checks and graph interventions, not
continuous native execution of every route or physical doorway-closure tests.

| Request | Model-0 XY envelope reduction from baseline | Initial visible spawn pairs |
| --- | ---: | ---: |
| 4 rooms / seed17 | 35.0% | 0/1 |
| 4 rooms / seed42 | 36.3% | 0/1 |
| 4 rooms / seed99 | 33.4% | 0/1 |
| 6 rooms / seed17 | 38.9% | 0/15 |
| 6 rooms / seed42 — Weft | 37.0% | 0/15 |
| 6 rooms / seed99 | 41.7% | 2/15 |
| 8 rooms / seed17 | 33.7% | 0/15 |
| 8 rooms / seed42 | 31.7% | 1/15 |
| 8 rooms / seed99 | 34.7% | 0/15 |

Every baseline case has zero visible spawn pairs under the same compiled
world-ray measurement. The two new positive cases therefore contradict a claim
that compactness automatically improves spawn protection. Source visibility,
lighting and quality findings remain separate report fields: zero
quality-owned findings does not mean zero findings overall. In particular,
initial rooms6-seed99 has four directed spawn-LOS findings and a wide-crossfire
finding in its visibility analysis.

The 31.7–41.7% reduction measures the XY bounding rectangle of compiled model-0
brushes, including the shell. It is neither playable floor area nor a quality
score. For initial Weft that rectangle is 1,712×2,656, compared with the
baseline's 3,432×2,104. The revised authored room/passage union has a different
denominator: its 1,648×2,592 envelope is 94.75% covered before subtracting
barriers or internal cores. Do not compare that percentage with baseline
compiled bounds or with the Quake Live atlas's rendered-surface area.

Three baseline seed99 outputs fail the conservative standing-hull test centered
at one health entity each. This checks an exact source origin and a bounded
settling candidate. Item touch bounds and game-module settling can differ, so
these findings do not prove that a native player cannot collect those pickups.
The initial revised outputs have zero failures under the same test.

## Final evidence and remaining work

The revised nine-case portfolio (`out/mp-portfolio/generated`, written by `python -B -m tools.mp_portfolio.compare`) retains the
initial envelope dimensions and has zero compiled visible spawn pairs in every
case. Every ordinary pair remains reachable; individual passage-node closures
still lose none. Local compiled node/tread support and authored anchor checks
have zero failures. There are 960 required ordinary pairs across the nine
outputs, with the per-case identities and closure results kept in their
`verification.json` records by the [compiled audit](../tools/mp_portfolio/inspect.py).

The paired comparison (`out/mp-portfolio/comparison.json`, written by `python -B -m tools.mp_portfolio.compare`) verifies the same
selected candidate seed, equal compiled solid signatures and equal non-spawn
entities between each initial and refined case. That isolates the spawn-location
refinement within this set. The larger baseline-to-motif comparison changes
both construction and placement policy, so it does not isolate either one.

Variation also has a limit: the three four-room cases share one unlabelled room
graph, as do the three six-room cases; the eight-room set contains two graphs.
Adding motif identities distinguishes two, three and three graphs respectively.
These signatures omit within-room circulation, but show why a different seed or
geometry hash is insufficient evidence of a new topology or tactical experience.

Source navigation, visibility and quality-owned findings are zero in those
outputs. **Thirty-nine analytical lighting warnings remain**, two to eight per
case. Weft retains two `excessive_connection_contrast` warnings. Thus zero spawn
LOS and passed structural gates do not mean all advisory findings have been
resolved. These light estimates do not measure the final engine render.

The final hero editable MAP (`out/weft/qmg_weft.map`, upstream study evidence, not imported) and
compiled BSP (`out/weft/qmg_weft.bsp`, upstream study evidence, not imported) pass the
compiled verification (`out/weft/verification.json`, upstream study evidence, not imported), including full VIS and
extra4 one-bounce lighting. Its MAP SHA-256 is
`a00415003b3a72aaa206ad38d8ea02f5a12296441b70d746539bda41b31aa3c6`;
the lit BSP is
`980b22bf6aaedb032df1f04db5cc46d864d72f961a9852809486fbd64abf50f0`.
All nine primary BSPs, the two separate semantic-control BSPs and the duplicate
Weft hero BSP pass full VIS and extra4 one-bounce lighting. These are 11 unique
generated blockouts and one separately compiled delivery of the predeclared hero.

All **42 configured CTest gates pass**, across the four affected
core tests (`out/mp-portfolio/core-tests.log`, upstream test log, not imported) and the
remaining 38 tests (`out/mp-portfolio/remaining-tests.log`, upstream test log, not imported). This includes the
new compact motif contract and legacy generator/navigation/quality controls.
The [knowledge base](../knowledge/q2/rules.json) now contains 42 cards, retaining
the preceding studies and negative observations.

Analytical preview checks pass **216 states**: 162 across the nine primary
outputs, 36 across the low/high semantic controls and 18 for Weft. The matrix
uses three widths, both themes and every available floor choice. Weft's stacked
view and both floor orthographics also receive visual inspection. These are
geometry diagrams, not engine renders or observations of player orientation.

The dedicated initialization audit (`out/weft/engine-smoke/engine-smoke.json`, upstream study evidence, not imported)
passes all 16 checks on the final lit BSP with pinned Q2REPRO and its legacy
module. It loads native entities and completes 40 scheduled post-spawn server
frames with no matched initialization failures. No client, connected player,
engine image or input device is involved. An authored entity dump is not live
item-state evidence; this does not prove spawn selection, pickup contact or
traversal.

The complete [experiment record](../knowledge/q2/experiments/weft-v1.json) binds
the final artifacts, sources and checks. The retained initial revision remains
available after final selection.

Weft contains six rooms, seven passages, eight stair runs, four
elevated rooms and two rooms with offset barriers. Its six spawns and 22
gameplay anchors produce 132 required ordinary pairs. Those facts describe the
selected construction; they do not establish encounter pace, resource control,
safe spawn selection or multiplayer balance.

The separate [native Ravel recovery study](Q2_NATIVE_RECOVERY_STUDY.md) records
five successes and three failures across eight fixed profiles. Releasing after
landing succeeds at both approach inputs; a northward turn-and-release succeeds
at the slower input but misses the declared pause area at the faster one.
These results support measuring grounded arrival, velocity, turning space and
low-speed dwell separately. They concern the frozen Ravel BSP, and successful
turn/pause profiles do not certify onward doorway traversal or Weft movement.

Next tests need complete ordinary journeys with turns and passing, native pickup
and spawn behavior, rendered orientation/readability and 2/4/6-player resource
and route observations. The current policy is an experimental blockout
generator with construction and static regression gates; the generated maps
have no full multiplayer playtest claim.
