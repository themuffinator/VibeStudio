# Hinge: upper routes between rooms

Hinge is the predeclared eight-room, six-player seed 607 example from the normal
six-candidate Quake II generator. Its explicit wrapped-center request now permits
one upper connection between existing raised motifs. The selected connection
continues from Crossing 4 into Crossing 6 at floor 128, while preserving the lower
passages, room envelopes and all existing stairs. This is original procedural
construction informed by reference measurements; no archive geometry is copied.

The change adds a second route layer between rooms that were already adjacent.
It does not create a new unique room adjacency or reduce maximum room hops.
That distinction matters: a useful upper passage needs supported destinations
and real access routes, not an invented improvement to a room-graph score.

## Construction contract

The helper in [upper_links.cpp](../src/upper_links.cpp) runs after the current
motifs and barriers and before allocation. Its scope is explicit
`wrapped_center` Q2 deathmatch/duel with 5–8 rooms and planned verticality at
least 0.3. Other modes, unspecified intent and lower verticality retain their
existing construction. An unsuccessful search preserves the candidate; it does
not reroll a seed or substitute a hand-authored level.

The search considers adjacent rooms separated by 48-unit face seams and existing
24-thick decks at matching 128/192 elevations. A candidate adds two 160-wide
room-local branches and an explicit 24-thick support slab across the portal.
The actual passage starts at the deck floor and has 192 units of height.

Both chosen endpoints must lie in exact original deck navigation fragments
that have ordinary paths to and from their own room floors. Another accessible
fragment elsewhere on the same deck is insufficient. The entire new upper strip
must have standing clearance. Every old tread retains its full footprint and a
conservative envelope through its top+56+18, including the step attempt's rise
before moving forward. Existing raised-platform representation, materials,
ownership, room containment and common floors are guarded at the public API.

Source-clear strips receive a stable seed-based ordering. At most 64 are lowered
through the real in-memory MapBuilder, with counted shell-clearance rejections;
the first successful lowering is committed privately. Other lowering errors
propagate without changing the caller. This bounded construction search does
not alter ranker weights, candidate count or selection policy.

## Keep support and passage identity separate

The free-volume union can remove an implicit upper floor when a lower passage
overlaps its footprint. Source navigation still treats passage minima as floor
sources, so a connected graph alone can conceal the missing floor. Explicit
slabs and emitted/compiled full-footprint checks are therefore required.

Likewise, deleting only an upper connection record is not always a physical
closure. A 128-height slab can still fit a standing hull beneath an overlapping
192-high original lower doorway. The upper-closure control physically blocks
the upper opening while separately retaining lower-route clearance.

The spawn helper's reviewed integration permits passage minima at or above the
common room floor, preserving its in-room floor assumption. It still requires
explicit full-footprint support for a raised spawn and rejects lower free
volumes. Its certificate does not establish an elevated passage floor. New
passage lights use passage minimum Z+144; the old ground lights are unchanged.

## The first portfolio remains evidence

The nine primary 4/6/8-room requests, six separately declared 6/8-room holdouts
and separate seed 607 example retain their exact prompts and six candidates.
Thirteen requests explicitly ask for wrapped construction; the three four-room
controls do not. All holdout seeds are odd. The matrix is a small reproducible
sample, not a general quality or diversity estimate.

The first 16 exports are retained under
`generated` (`out/hinge-portfolio/generated/record.json`, written by `python -B -m tools.mp_portfolio.hinge_compare`). They offered upper
connections in 67 of 96 candidate observations and selected one in 10 of 16
outputs. Hinge's pool offered four links, but its selected candidate had none;
its MAP remained identical to the baseline. That result was diagnosed instead
of changing the hero seed.

The cause was a coordinate-phase bug. Recentered room geometry can sit at 8
modulo 16. An exact 160-wide overlapY 600..760 has only one legal portal center,
680; globally rounding to 688 discards it. The helper's first exact-hero
diagnostic recorded zero considered strips. The general correction starts its
16-unit sequence at each legal interval's lower bound. Translated±8 fixtures,
both seam axes, common floors 0/64 and a rejected 159-wide interval cover it.

All 16 requests were then rerun into distinct
`generated-refined` (`out/hinge-portfolio/generated-refined/record.json`, written by `python -B -m tools.mp_portfolio.hinge_compare`)
outputs. No seed, prompt, candidate budget or ranker changed. For the exact
Hinge candidate 8709371129873691315, the corrected helper finds three feasible
strips and the stable key selects Crossing 4→Crossing 6. It does not force the
earlier Gallery 3→Crossing 4 feasibility witness.

## The selected example

Hinge has eight rooms, twelve passages, three raised crossings, two horseshoe
galleries and three offset barriers. Main floors remain 0/128, with ten stair
runs and 80 treads. Its compiled world envelope remains 5,234,944 square units;
the original room, stair and passage geometry is retained. The new slabs and
opening change the compiled world brush count from 228 to 239. All original
placement fields and origins are unchanged for this example; its new portal
adds a light.

The new passage is`[344,600,128]..[392,760,320]`. Its branches extend 64 units
from Crossing 4's deck to the west face and 208 units from the east face to
Crossing 6's deck. The 32-unit-hull endpoint centerline is 352 units long. These
dimensions describe the supported connection; they are not movement-time or
gameplay-quality scores.

Sealed QBSP, full VIS, extra 4 one-bounce lighting and compiled local checks pass
for Hinge. Its 162 ordinary spawn-to-anchor graph pairs have no missing routes.
Four analytical lighting findings remain, as before; navigation, visibility
and quality-owned findings are zero.

## Final fixed portfolio and retained tradeoffs

The comparison (`out/hinge-portfolio/comparison.json`, written by `python -B -m tools.mp_portfolio.hinge_compare`) binds558 inputs and
rederives16 selected outputs with unchanged candidate seeds. All original
rooms, solids, stairs and lower passage geometry are preserved; all compiled
XY envelopes remain unchanged. Twelve selected outputs gain one connection and
three slabs. The scoped rooms8-seed7 holdout retains no link despite five of its
six candidates offering one. Three unrequested four-room controls also remain
unchanged.

| Stratum | Refined candidate offers | Selected links | Lighting findings before→after | Ordinary graph pairs |
| --- | ---: | ---: | ---: | ---: |
| Nine primary requests | 36/54 | 6/9 | 40→45 | 960 |
| Six holdouts | 35/36 | 5/6 | 29→35 | 882 |
| Separate seed607 example | 6/6 | 1/1 | 4→4 | 162 |

All16 selected maps pass sealed QBSP, full VIS, extra4 one-bounce lighting and
local compiled node, tread and gameplay-anchor checks. Ordinary graph pairs
have no missing routes or losses after single passage-node removal. Visible
and close spawn pairs remain zero; navigation, visibility and quality-owned
findings remain zero. Lighting worsens in both comparison strata and is retained
as a cost of the intervention. Local graph and pose checks are not continuous
native journeys through every new upper route.

Seven existing placement origins move across five maps: six spawns and one
weapon. The weapon in rooms8-seed17 moves from`[−936,16,152]` to`[−840,64,152]`.
All other existing typed placement fields remain unchanged; each linked map
adds one portal light. Hinge's existing placement fields and origins are
unchanged. Because allocation follows construction, this is a geometry-and-
placement intervention, not a globally position-neutral experiment.

All twelve upper links are parallel to existing room adjacencies. Distinct
selected multigraphs change from1/1/1 to1/3/2 in primary4/6/8-room strata and
from1/1 to2/3 in holdout6/8-room strata. Unique adjacency and room-hop values
remain unchanged in every map. These small-sample parallel-route variants are
not evidence of shorter travel or general map-quality gains. The96 trace rows
retain room IDs and passage endpoints, not every candidate's complete solid
geometry; only the selected16 exports support full structural comparisons.

## Layer controls and first native journeys

The compiled control audit (`out/native-upper-link/geometry-audit.json`, written by `python -B -m tools.native_movement.upper_link`)
checks the full upper strip and both selected stair runs in normal Hinge, a
physically closed upper opening and a missing-slab world. Normal support and
clearance pass. The closed opening blocks the transfer. Removing the explicit
portal slab leaves the center supported but loses1,536 square units of edge
support, demonstrating why a center-only check is inadequate. All57 point
entities remain identical across these control worlds.

The separate lower-route audit (`out/hinge-portal-controls/lower-route-audit.json`, upstream study evidence, not imported)
certifies the original160-wide ground doorway in all three worlds. Its center
rectangle is`[328,744]..[408,872]` at floor0, with full standing support and
56-unit clearance. This is a static lower-route check, not a native lower
journey or complete ground-return loop.

The [native study](Q2_NATIVE_UPPER_LINK.md) freezes12 first command profiles:
eight ground→stair→upper link→other stair→ground rotations and four straight
upper transfers. Seven rotations and all four transfers pass. The fast forward
rolling rotation remains failed: it reaches a supported point0.25 units short
of the declared Y gate while already outside the turn rectangle in X. Its
later stair descent and ground-pocket dwell are retained observations, not a
retroactive pass or evidence of an unsupported fall.

Six of seven accepted rotations finish0.125–0.5 units from the outer wall;
the remaining accepted rotation finishes16.875 units from it. These are
wall-adjacent stopping pockets, not open-space braking evidence. Native frame
states and reconstructed supported chords do not expose internal movement
substeps. No game module, pickup contact, damage, opponent or match behavior
was tested. All5,375 raw frame states remain available.

## Validation and delivery

All55 configured CTest groups pass, with affected upper-link/wrapped-intent and
final evidence controls rerun separately after their revisions. The owner
construction tests have11 groups; the frozen comparison has15 Python controls,
and the native study has20. Analytical previews pass288 states:270 portfolio
and18 example states. Hinge passes16 dedicated initialization checks. Final
ordinary CLI/legacy parity covers12 exact files; the separate32-file guard-only
parity snapshot belongs to the earlier spawn-guard intervention.

Open Hinge (`out/hinge/index.html`, upstream study evidence, not imported), the
15-map portfolio (`out/hinge-portfolio/index.html`, upstream study evidence, not imported), or download
Hinge (`out/hinge/qmg_hinge.zip`, upstream study evidence, not imported) and the
portfolio (`out/hinge-portfolio/qmg_hinge_portfolio.zip`, upstream study evidence, not imported). The final artifact
gate is[hinge-v1](../knowledge/q2/experiments/hinge-v1.json). Analytical images
are geometry diagrams, not engine-rendered scenes. Automatic approval review
rejected the single cleanup attempt for the three new task-local directories;
the exact paths and retained status are in
cleanup-status.json (`out/hinge/cleanup-status.json`, upstream study evidence, not imported). No blocked historical
path was retried.

## Reference transfer and limits

The [Powertrip study](Q2_UPPER_CONNECTION_STUDY.md) measures a 1,568-unit upper
route and a 992-unit lower route through two spaces. Both include the same
80-unit connector footprint at floors separated by 256 units. They share
endpoint XY but differ in Z, so their lengths are not interchangeable costs
between identical endpoints. A direct vertical sweep hits the deck, and
projecting the lower shortcut onto the upper floor fails support/clearance.

The fixed lower west↔bridge eye pair is visible; the corresponding upper pair
is blocked. This supports distinct local route exposure, not a claim that the
entire upper path is safer. The study binds 24 files and does not certify native
ascent, return, pickup contact or multiplayer outcomes.

The generator's refined 48-case source sweep produces 44 links and four unchanged
observations, with 51 lowering attempts, seven shell rejections and no cap hits.
Those fixtures differ from the 16-request pipeline portfolio. Reproducible rule
cards and procedural construction do not train model weights. Resource control,
opposing traffic, spawn recovery and human matches remain separate evaluation
work.
