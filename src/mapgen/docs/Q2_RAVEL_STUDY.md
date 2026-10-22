# Ravel — compact interleaving and landing recovery

Ravel is an original Quake II deathmatch blockout intended for 4–6 players.
Six unequal rooms form a staggered ladder within a 1,696 × 1,440 envelope.
Its long ramp, overhead crossing, bent galleries and small third-floor ledge
serve different approaches. The [recipe](../tools/map_recipes/ravel.cpp),
MAP/BSP package (`out/ravel/qmg_ravel.zip`, written by `tools/map_recipes/build.ps1 -Recipe ravel`) and
[experiment record](../knowledge/q2/experiments/ravel-v1.json) retain the result.

This iteration combines two kinds of learning. The
[Reclamation reference study](Q2_INTERLEAVED_ROUTE_STUDY.md) demonstrates actual
interleaved flat routes and rejects tempting corner cuts and a false stacked
descent. The [native Talus comparison](Q2_NATIVE_ASCENT_STUDY.md) measures a
matched ramp/stair pair: the tested uphill times tie, while the definition of
arrival changes the interpretation of downhill results. Neither study proves
that a smaller map or a ramp automatically plays better.

## Spatial roles

| Space | Intended route and encounter role |
| --- | --- |
| Spindle | A 160-wide ramp rises along the west side. Its flat upper landing turns toward Header or Crossing; lower Rocket access and offset cover give the room a different purpose below. |
| Crossing | An east–west walkway crosses a north–south lower route. Mega sits below; a separate stair reaches the small third-floor grenade ledge. |
| Crown | An inset solid bends the upper route between Header and Return, interrupting a straight view through the room. |
| Return | An eastern gallery wraps around a lower lane. Its stair faces Dock, giving the east climb a different orientation from the west ramp. |
| Dock | A short southern recovery room connects two lower approaches around offset cover. |
| Header | A short northern connector carries Chaingun and Quad, drawing a resource journey away from lower Mega. |

Main play occupies feet 0 and 128; the local ledge reaches 256. This is not three
complete stacked floors. Three eight-tread stairs use 36-unit depths and
16-unit rises. The ramp has an 8-unit entry lip, then rises 120 over 480 units to
the 128-high landing. The third-floor ledge has one ordinary stair and an
optional 128-unit drop, an intentional access commitment that needs playtesting.

Nine short face-to-face passages connect the rooms. All 264 spawn-to-gameplay
anchor journeys remain reachable using walk, step and door edges only. Removing
each passage's navigation nodes in turn loses no required pair. This tests the
nine inter-room passages, not every stair, internal choke or physical opponent
blockade. The declared drop's landing has an independently bound 13-edge
ordinary graph return to its takeoff; the entire compiled return is not swept
or natively traversed by that graph result.

## Compaction with explicit tradeoffs

The packing comparison (`out/ravel/packing-comparison.json`, written by `python -B -m tools.map_recipes.packing_audit`) uses the union of
authored room and passage boxes, without subtracting internal solids.

| Descriptor | Fold | Lockyard | Ravel |
| --- | ---: | ---: | ---: |
| XY envelope | 1,792 × 1,664 | 2,016 × 1,728 | 1,696 × 1,440 |
| Gross envelope coverage | 94.8% | 91.0% | 94.0% |
| Edge-connected uncovered area | 16,896 | 141,312 | 35,328 |
| Sampled sightline median / p90 | 268.9 / 737.4 | 239.2 / 758.9 | 223.3 / 581.2 |
| Nearest-weapon straight-line median / p90 | 329.5 / 423.2 | 403.0 / 521.3 | 352.7 / 481.2 |

Ravel's envelope is 18.1% smaller than Fold's and 29.9% smaller than Lockyard's.
Its exterior gaps are smaller than Lockyard's but larger than Fold's. Shorter
sampled sightlines and direct equipment distances relative to Lockyard are
descriptors, not measured encounters or recovery times. Several design variables
changed; these maps are not a single-variable compactness experiment.

The earlier exposed-spawn draft (`out/ravel/design-controls/exposed-spawns/README.md`, upstream study evidence, not imported)
already had all required ordinary paths. It retained six directed hostile
sightlines, two crossfire findings and one lighting-contrast finding. The final
revision moved two Spindle spawns, added a small ledge shield and adjusted
lighting. All 28 final compiled spawn-eye pairs are occluded. This multi-change
revision does not prove spawn safety against moving opponents or other positions.

## Native movement and the drop's actual landing

Six Ravel route probes (`out/ravel/native-routes/summary.json`, written by `python -B -m tools.native_movement.ravel`) use the same
unmodified external Quake II 3.19 CM/Pmove baseline as the Talus study. They
open no client or input device. They measure world collision and movement,
without game-module damage, weapons, dynamic entities or multiplayer interaction.
All six complete the declared dry, standing, grounded goal within its bounded
overshoot. Eight 16 ms settling commands precede each route and are excluded.

| Literal forward input | Ramp up | Ramp down | Drop to grounded X ≤ 0 |
| --- | ---: | ---: | ---: |
| 400 | 1,952 ms | 2,032 ms | 736 ms |
| 200 | 2,896 ms | 3,008 ms | 928 ms |

These are fixed command profiles, not optimal times or human running/walking
speeds. Ravel and Talus are different maps; their ramp results do not establish
a controlled cross-map speed comparison.

The drawn drop candidate departs from (176,16,256), clears the ledge at
(112,16,256), then translates vertically to (112,16,128). This is the geometric
clearance construction. Native momentum produces different landings: X−30.375
at forward 400 and X28.625 at forward 200, both at feet 128.125. The slower
profile then continues to the declared X0 goal. No jump or upward command is
used. The native trajectories must not be drawn as the candidate's two straight
geometric legs.

Forward 400 reaches the goal with velocity X−300 and only 1.625 units remaining
inside the experiment's 32-unit overshoot allowance. That is a protocol margin,
not a measurement of remaining physical platform width. Stopping, turning,
avoiding an occupied landing and fall damage remain untested. The drop needs a
separate braking/turning trial before its landing is treated as a recovery pocket.

## Verification and presentation

The sealed final BSP passes full VIS, extra4 lighting with one bounce and all
16 dedicated Q2REPRO initialization gates. Its 33 gameplay anchors pass 5,379
compiled standing-hull/brush checks, and all 24 ordinary tread centers have
support and clearance. The ramp receives three-lane continuous compiled audits;
the drop retains takeoff/landing support, clear geometric sweeps and its ordinary
graph return.

The [interleaving audit](../tools/map_recipes/ravel_audit.py) checks twelve full
axial route legs and floor-union coverage across the crossing and three bent
routes, against all 163 player-blocking world brushes. The actual crossing slab
is bound to its compiled vertices: underside 104, top 128. A standing lower hull
has 48 units above its head at the crossing. The twelve synthetic controls
include a changed underside, missing support, an intervening barrier, a floor
gap, a missing elbow cap and false support from a chamfer's bounds.

Twenty-three selected CTest gates pass; the full configured suite was not rerun.
Seven older published MAPs remain byte-identical to their rebuilt recipe outputs.
Nine openings remain local chokepoints and only two meet the center-to-center
visibility-readability descriptor. Zero reported findings is not a quality score.

The analytical preview passes 24 width/theme/floor states, with 72 compatibility
states for Talus, Lockyard and Counterweight. Individual floor views already
used true XY projection; this iteration keeps the complete envelope, scale and
alignment fixed while switching floors. A map-unit bar and dashed other-floor
room outlines make those comparisons explicit. These images are diagrams, not
engine render-target evidence.

The knowledge base now has 40 rule cards. New cards distinguish crossing from
grounded arrival, reject step count as an uncalibrated timer, combine position
with velocity/contact evidence, and require full-hull elbow clearance. Automatic
planner motif selection, reference-profile admission and model weights remain
separate work. Next tests should cover native cornering, braking after the drop,
opposing traffic and matched 2/4/6-player resource control.

No new unrelated implementation defect was found. Existing underwater-light
placement, default liquid-material and Windows client-focus limitations remain
recorded in the roadmap.
