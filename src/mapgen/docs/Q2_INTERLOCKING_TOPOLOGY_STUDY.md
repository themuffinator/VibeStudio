# Interlocking approaches: a bounded q2rdm2 route comparison

The Chastity Belt Duel (`q2rdm2`, German “JaLisK0” Garcia) provides a useful
alternative to inferring connectivity from a row of room rectangles. Two measured
lower corridors approach the same rocket station around opposite sides of an
irregular central mass. One is longer but outside every tested ray from a fixed
northern viewpoint. A tempting direct crossing hits solid geometry.

The analytical comparison (`out/interlocking-topology-study/q2rdm2-route-comparison.png`, upstream study evidence, not imported)
shows the actual lower draw surfaces as context and the checked paths separately.
The [evidence record](../knowledge/q2/interlocking-topology-study.json) pins the
archive revision, protocol, source context and results. This extends the earlier
[route-diversity study](Q2_ROUTE_DIVERSITY_STUDY.md), which inspected this map's
projections and item distribution without certifying these corridors. It does
not repeat Reclamation's previously checked lower circuit or infer movement from
the Quake Live atlas.

## Two approaches, two different geometry contracts

Both paths start at player-center XY **A(−160,560)** and finish at
**B(640,592)**, the authored rocket launcher's XY. The reference coordinate system
is retained. The standing clearance envelope has feet Z−288 and dimensions
32×32×56.

| Selected corridor | Complete XY stations | Length | Ground contract |
| --- | --- | ---: | --- |
| North | A → (−160,832) → (640,832) → B | 1,312 | Actual ground continuously covers the projected footprint within 2.015625 units below the clear envelope |
| South | A → (−160,256) → (0,256) → (0,128) → (672,128) → (672,592) → B | 1,760 | Constant full-footprint support at feet Z−288 |

The south path is 448 map units, or 34.1%, longer and uses more bends. These are
the lengths of the selected polylines, not shortest routes or native travel
times. They share destination stations; their different centerlines do not
establish independent tactical control or eliminate shared space near the ends.
Pickup contact, weapon settling and actual player use are untested.

All nine selected segments have continuous standing-envelope clearance against
**646 model-0 player-blocking brushes** using mask33619971
(`SOLID | WINDOW | PLAYERCLIP | MONSTER`). The geometry checker uses exact stored
plane coefficients, complete translated-box SAT and zero collision epsilon.
For floor coverage it partitions the complete swept XY footprint at admitted
cap-rectangle edges. Every cell must be wholly covered by actual convex roof
faces. Each roof rectangle's corners satisfy all solid planes and the declared
height band; affine planes then bound the entire rectangle. This can reject
valid curved caps conservatively, but never admits support from bounds alone.

**The north path is not certified as constant-floor movement.** Its recessed
caps are at Z−290. The adjoining stored slope, brush106, reaches
Z−290.000009545… at XY(432,816). Therefore both a flat-Z−288 assumption and an
exactly two-unit ground band fail. Those failures remain in the protocol. The
accepted band is explicitly 129/64 units, covering the measured ground; no BSP
plane was snapped and no collision epsilon was added. Its hull clearance above
the band and complete nearby ground are static facts, not a simulation of
ground contact, stepping, falling or steering. Only the south route has the
stronger constant-floor support certificate.

The map also contains two downward doors, two push triggers and two matching
trigger_multiple volumes. All **54 segment-versus-mechanism exclusions** pass
against conservative envelopes covering the known door travel and trigger
bounds. Stock downward-door code maps angle−2 to downward motion; a full model
height conservatively covers the travel, including the default lip8 applied
to authored lip0. Both door states are covered. The selected envelopes also
avoid all three world liquid brushes, including the north shallow-band depth.
These exclusions do not certify the map's mechanisms or its complete runtime
connectivity.

## Why the direct lines fail

The candidate **A → (640,560) → B**, length832, crosses brushes
**330,331,339,400** and lacks complete floor support. It cannot be introduced as
a graph edge merely because the endpoints are clear and close.

The eastern corridor also needs an actual hull position. The attempted segment
**(704,128) → (704,592)** at feet−288 clips brushes **379,380,430**. The accepted
south path uses X672. The exploratory and refined failures remain under
`out/interlocking-topology-study/`; the final protocol was refined using this
evidence and is not presented as a blind preregistered experiment.

## A viewpoint can favor one approach without seeing the other

Three observer stations pass independent strict flat hull/support checks.
Observer eyes are west A(−160,560,−242), rocket B(640,592,−242), and north
N(144,832,−244). N stands on the actual recessed floor at−290. Target points use
the same fixed Z−242 clearance-envelope eye plane on both paths; on the north
recess this can sit about two units above a grounded player's corresponding
eye. That sampling convention is retained explicitly.

Targets are sampled every32 units of cumulative path length, plus endpoints
and bends. The counts include coincident source/target samples where applicable.
The BSP world point-ray mask is `SOLID | WINDOW`, independent of playerclip.

| Observer | North visible / sampled | South visible / sampled |
| --- | ---: | ---: |
| West A | 13/44 | 23/60 |
| Rocket B | 15/44 | 18/60 |
| North N | 34/44 | 0/60 |

N sees many selected north positions and none of the selected south positions.
This supports a local hypothesis about a bypass outside one viewpoint's rays.
It does not prove that an opponent can hold the north, that the south is safe,
or that the extra length is worth taking. Rays do not measure facing, sound,
splash, interception, exposure duration or continuous visibility between samples.
The two sample counts differ because the paths differ in length and bends.

## Keep graph conclusions within the measured subset

The two polylines form a nine-node, nine-edge witness graph with one cycle.
Its shortest A–B witness is1312 when both geometry contracts are admitted,
and1760 when only constant-floor edges are admitted. Removing any single edge
leaves a witness under the broader geometric policy. Removing a south edge
breaks A–B under the strict flat policy, while the separately labelled north
corridor remains under the broader policy. Removing one middle segment from
each path breaks the measured witness graph.

This is an intervention on two measured polylines, not a physical doorway
closure, complete navigation extraction or map-wide centrality calculation.
It cannot establish whether another unmeasured route survives. A low
room-graph centrality score likewise cannot prove that real movement avoids a
shared station inside a room.

For original generation, retain the complete geometric connection behind each
graph edge and compare alternate approaches between the same destinations.
Then vary which side of a mass they follow, their bends, lengths and sampled
views. Keep motion contracts on the edges: a nearby floor band, ordinary flat
path, ramp, door and optional commitment must not silently become equivalent
reachability. These are provisional construction lessons, with no learned
ranker weight or guaranteed gameplay advantage assigned.

## Reproduction, version and rights

Run from the project root:

```powershell
python -B -m tools.map_study.interlocking_topology --protocol out/interlocking-topology-study/protocol.json --output out/interlocking-topology-study/route-audit.json
python -B out/interlocking-topology-study/render.py
```

The source is the existing reference-partition
`q2rmappack/pack/maps/q2rdm2.bsp`, SHA-256
`d6ee3b39d4e557e7ad5cd39d2bf955e8251edd7e491f2033eee80500791d7199`.
The original readme dates the map May2001, updated June2001, and specifies duel
for two players. Archive metadata labels this Q2R TMP Edition, August2001, with
duel/FFA/2v2 and 2–4 players. Those capacity and revision differences remain
unresolved; no version equivalence or capacity label is inferred.

The readme prohibits using this level as a base for additional levels and
restricts redistribution. This study reads the local archive and records
original analytical diagrams and abstract hypotheses; no reference geometry,
textures or external code is incorporated into generation. Training rights
remain unreviewed. The archive, existing profiles, partitions and weights are
unchanged. The source record also pins the local original-Q2 door/movedirection
code used only to bound mechanism exclusion.

No native run, game client, input device or OS capture was used. No new
unrelated implementation defect was identified; the shallow stored-plane
difference and narrow-neck failure are retained evidence limits of this study.
