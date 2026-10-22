# Fold — compact rooms with internal traversal

Fold is an original Quake II deathmatch blockout for an intended 4–6 players.
Five unequal adjoining spaces wrap around an offset indoor atrium, with nine
short passage connections. Main floors are 0 and 128; a stepped 64-unit Quad
crate is a local prop landing. An optional teleporter and shallow wash basin
offer further commitments while all required gameplay journeys retain dry
walk/step/door routes.

The [recipe](../tools/map_recipes/fold.cpp), MAP/BSP package (`out/fold/qmg_fold.zip`, written by `tools/map_recipes/build.ps1 -Recipe fold`)
and [experiment record](../knowledge/q2/experiments/fold-v1.json) retain the
construction and validation evidence. No archive geometry or game assets are
bundled. This explicit recipe expands the vocabulary; automatic motif selection,
native traversal and competitive balance remain separate work.

## Spatial roles

| Space | Intended decisions |
| --- | --- |
| Fold Atrium | Rocket on the open lower floor; Mega beneath the north gallery; armor above. Separate lower and upper movement occupies the same encounter volume. |
| Horseshoe | A west-attached full-height service core creates two arms and an eastern crossbar. Southern, northern and atrium connections give onward choices. The teleporter leaves from the lower northern arm. |
| Lintel | An elevated return around the atrium, divided by offset screens. Rail and grenade approaches are distinct from the lower rocket encounter. |
| Wash Bay | A wrapping gallery over a dry bypass and small water pocket. The southern gallery carries the hyperblaster, while the teleporter arrives farther north facing south. |
| Press | Lower recovery space with crate barriers, a broad upper return and an explicitly stepped Quad crate. The 64-unit prop does not justify a whole third floor. |

Four eight-tread stair runs join main floors; one four-tread run reaches the
crate. All use 36-unit runs and 16-unit rises. Gallery undersides at 104 leave a
separate lower route; a pair of exact compiled sweeps checks the same atrium XY
segment at feet 0 and 128. The full U-shaped lower horseshoe is represented by
three joined flat segments, totaling 1,520 units. Each segment has full-footprint
support on a common convex floor brush and continuous standing-hull clearance.
These selected routes do not certify every possible lateral path or native turn.

The layout has no articulation rooms. All nine short 144/160-unit-wide openings
are nevertheless classified as local chokepoints by the visibility analyzer.
Only one center-to-center connection meets its visual-readability descriptor;
all nine meet the lighting-readability descriptor. These are different metrics,
not interchangeable claims that every route is easy to recognize.

## Less exterior waste, with remaining tradeoffs

| Same authored-volume descriptor | Undercut | Fold |
| --- | ---: | ---: |
| XY envelope | 2,032 × 1,840 | 1,792 × 1,664 |
| Envelope area | 3,738,880 | 2,981,888 |
| Room/passage projected union coverage | 61.2% | 94.8% |
| Edge-connected uncovered envelope area | 984,320 | 16,896 |
| Enclosed uncovered envelope area | 466,432 | 139,008 |

The envelope is 20.2% smaller. The exact packing report (`out/fold/packing-comparison.json`, written by `python -B -m tools.map_recipes.packing_audit`)
projects authored room/passage boxes and partitions their uncovered areas. It
does not subtract internal cores or cover, establish usable floor area or assign
a quality score. Small internal seam regions remain useful structure. The Quake
Live atlas's rendered-surface coverage uses different geometry and a different
denominator; do not compare those fractions as if they measured the same thing.

Fold's sampled sightline median/p90 are 268.9/737.4, versus Undercut's
172.7/585.9. Its final straight-line spawn-to-nearest-weapon median/p90 are
329.5/423.2, versus 268.2/340.2. Packing therefore improved while these descriptors
remain longer. No uniformly tighter flow or improved spawn recovery is claimed.
The long surrounding rooms and local chokepoints need actual encounter review.

An initial resource pass left southern Wash Bay spawns far from weapons. Moving
only the hyperblaster and cells to its southern gallery changes the nearest-weapon
median/p90 from 393.5/714.7 to 329.5/423.2. The
paired source comparison (`out/fold/item-placement-comparison.json`, upstream study evidence, not imported) confirms
all 179 world brush blocks and 89 other nonworld entities are unchanged. The
first-pass source/BSP package remains in `out/fold/design-controls/resource-gap`.
This is a controlled item-origin change and a distance descriptor, not a native
recovery-time result.

## Water and teleport contracts

The basin is `[(512,-64,0),(752,160,24)]`. Four low apron/inner steps and curbs
describe a possible wet crossing, with a dry bypass at X432. Only the inner flat
wet segment is swept in this audit; complete entry/exit execution remains pending.
The upper gallery crosses above part of this space.

The compiled water brush must retain all eight corners of the authored box.
Its native base contents are WATER=32; ericw additionally marks the transparent
brush DETAIL|TRANSLUCENT, producing 402653216. The final `e3u2/water2` faces have
explicit WARP|TRANS33=24 and value 0. The existing WAL has flags25/value100; this
recipe deliberately omits its emissive flag rather than accidentally inheriting
a glowing basin. Ordinary architectural brushes remain SOLID.

At the basin's bed, standing sample heights 1 and 23 are submerged while 46 is
above water. This corresponds to waterlevel2 under the inspected native source,
which changes movement. Crouching changes the samples and can fully submerge the
view. These stationary content queries do not execute Pmove, prove swimming
speed, test air/damage or demonstrate a reliable wet exit. The ground graph
conservatively excludes water; all 264 required routes remain available.

The source disc is at entity origin `(-800,608,24)` and destination at
`(816,96,152)`, facing 270 degrees. Compiled checks establish full 64×64 pad support,
pad clearance, source standing clearance and the actual destination-plus-10
arrival hull. The target name must resolve uniquely across all entities, with
origins and finite yaw matching the authored contract. Activation, velocity
reset, telefrags and post-arrival survival still need a native trial.

## Validation and next experiments

The sealed IBSP38 passes full VIS and final extra4 lighting with one bounce,
soft shadows and no empty lightmaps. All 33 gameplay anchors pass 5,874
player-hull/solid-brush checks. All 36 declared stair treads have compiled support
and standing clearance. Eight selected flat segments pass 1,424 world-brush pair
checks with exact support; their swept bounds need no SAT fallback. All 28
spawn-eye pairs are occluded. These are limited static checks, not spawn safety
from every opponent position.

Twelve affected CTest gates pass, covering seven recipe layouts and the geometry,
drop, packing, Fold and QL-atlas suites. The new Fold audit has twelve synthetic
controls, including a chamfered water brush whose AABB and sample contents remain
unchanged, an insufficiently supported teleporter disc, duplicate targets, wrong
yaw and intermediate hull obstruction. All five earlier published recipe MAPs
remain byte-identical after rebuilding with the extended helper.

The final BSP passes all sixteen dedicated engine initialization gates. No client
or player input is used. Eighteen viewer states, eighteen independent water/prop
projection states and forty-eight prior-map compatibility states pass. These
diagrams are analytical views, not engine render-target images.

Use the [compact-layout testing regimen](MP_COMPACT_LAYOUT_REGIMEN.md) next:
compare basin wet/dry, teleporter enabled/disabled and one walkway screen at a
time; retain matched starts and the solid geometry where possible. Measure wet
entry/exit, first movement after teleport, crate pickup contact and complete
ordinary returns. At 2/4/6 players record crowding, repeated deaths, resource
ownership and whether both horseshoe arms and both atrium levels see useful play.
Open-sky courtyards and deeper liquid paths remain separate future fixtures.

Existing engineering limitations remain: conservative narrow-tread/short-passage
erosion, default Q2 liquid material/metadata needing explicit asset validation,
and normal Windows client startup requesting focus. Fold uses 48-unit passage
seams, explicit material metadata and a dedicated harness. The native movement,
rendering and automatic-planner work remains open in the roadmap.
