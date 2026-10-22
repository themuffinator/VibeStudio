# Lockyard — an outdoor spine and a deliberate dive

Lockyard is an original Quake II deathmatch blockout for an intended 4–6 players.
Six unequal spaces form an outdoor spine with a bent dry perimeter and an
optional submerged connection. Main combat floors are 0 and 128. The culvert bed
at −128 is a local wet space, shown separately in the analytical preview.

The [recipe](../tools/map_recipes/lockyard.cpp), MAP/BSP package (`out/lockyard/qmg_lockyard.zip`, written by `tools/map_recipes/build.ps1 -Recipe lockyard`)
and [experiment record](../knowledge/q2/experiments/lockyard-v1.json) retain the
construction and evidence. The [courtyard/liquid reference study](Q2_COURTYARD_LIQUID_STUDY.md)
examines Manhunt and gladdm1 without copying their geometry. This recipe expands
the generator vocabulary; automatic motif selection and trained weights remain
separate work.

## Spatial decisions

| Space | Intended role |
| --- | --- |
| Lock Court | An elongated outdoor encounter with an offset loading island. Partial north/east galleries share the encounter with lower crossings; other approaches retain open sky. Rocket is lower, Mega beneath the north gallery, armor above. |
| Boiler | Offset cover and a long upper return connect the court, Crane and Winding. Its stair and openings create different approaches to the same space. |
| Crane | A screened upper crossing carries Rail and grenades, without also collecting both major items. |
| Lockhouse | An east return with a partial gallery and access to the dry east culvert station. |
| Winding | Quad contests an upper recovery space on an independently connected dry loop. |
| Culvert | West/east dry stations, two eight-tread wet profiles and a short low roof. Entering commits to water and a deliberate dive; the longer ordinary return remains dry. |

Four ordinary stair runs use 36-unit treads and 16-unit rises to join floors
0/128. Two separate wet stair profiles connect the dry stations to the bed.
They are structural geometry, excluded from the required ordinary graph. The
water surface is −16: up to 112 units above the bed, and shallower over treads.
The cross-roof underside is −40, leaving 88 units above the bed. Three selected
court rays reach the sealed sky roof; this does not make every lower point
sky-exposed or establish rendered sunlight.

## A stronger connectivity test

The initial nine-connection layout passed all 264 required ordinary journeys
and reported no articulation rooms. Nevertheless, the west culvert station and
Winding depended on the single Boiler doorway when water travel was excluded.
One abstract room node had concealed the culvert's disconnected dry stations.

The new [passage dependency audit](../tools/map_recipes/gate_audit.py) removes
one passage's navigation nodes from the directed walk/step/door graph. In the
retained early control (`out/lockyard/design-controls/dry-deadend/ordinary-gate-dependencies.json`, upstream study evidence, not imported),
closing Boiler–Winding loses 51 required spawn-to-anchor pairs. This control has
two unfinished lighting warnings and is not a compiled gameplay comparison.

A new Court–west-station opening creates the dry loop Winding → Boiler → Court
→ west station → Winding. On the final layout, closing Boiler–Winding loses no
required pairs. A second comparison holds the final graph/items fixed: also
disabling the new opening makes that same Boiler closure lose 57 pairs. These
are graph interventions, not new physical walls or measured matches. The east
culvert health recess still intentionally depends on its Lockhouse opening;
closing that opening loses eight pairs. A universal zero-dependency score would
erase useful optional recesses.

The matched final-graph comparison (`out/lockyard/dry-return-comparison.json`, upstream study evidence, not imported)
binds both conditions to the same layout and separates the earlier unfinished
control from this narrower intervention.

Quad was subsequently moved from Crane to Winding, giving the new loop a
contested destination. The source comparison (`out/lockyard/item-placement-comparison.json`, upstream study evidence, not imported)
checks that only its origin changed; every world brush, other entity and light
remains identical. Major-item straight-line separation changes from 463.7 to
1,000.4 units. Distributed control is the hypothesis; better balance is unproven.

## Native movement evidence

The new [local movement harness](../tools/native_movement/README.md) links
unmodified, externally located Quake II 3.19 Pmove and collision code. Every
movement trace and contents query uses native CM_BoxTrace/CM_PointContents on
the compiled BSP. It opens no client, renderer, network or input device.
This is native world-collision movement evidence, below a full game-module or
Q2REPRO playtest: entities, oxygen, damage, sound and multiplayer interactions
are not simulated.

Trials explicitly start 0.125 units above the authored floor, then verify a
stable standing, grounded start. Eight 16 ms settling commands are excluded
from timing. Arrival requires the declared lane, floor height, standing hull,
dry state and ground contact. Results retain per-command states and failures;
reported time stops at the first valid goal and is resolved to 16 ms.

| Lockyard command profile | Wet culvert | Same geometry with water removed |
| --- | ---: | ---: |
| Forward 400, level view | Stalls at low roof | 3,920 ms |
| Forward 200, level view | Stalls at low roof | 5,904 ms |
| Forward 400, constant 30° downward view | 6,400 ms | 3,920 ms |
| Forward 200, constant 30° downward view | 10,048 ms | 5,904 ms |

The native comparison (`out/native-movement/lockyard-comparison.json`, written by `python -B -m tools.native_movement.experiment lockyard`) also
retains phased dive profiles: 6,672 ms at forward 400 and 9,664 ms at forward
200. The 400 phased attempt includes 352 ms with no XY movement; the constant
400 attempt avoids that stall. These are particular scripted approaches, not
optimal times, player skill measurements or a single exact water-speed ratio.
Successful wet trials pass through water levels 0 → 1 → 2 → 3 → 2 → 1 → 0 and
cross below the roof with the standing hull, without jump/upmove commands.

The drained control removes only the water brush, preserving 188 complete
compiled solid brushes and 133 nonworld source entities. It isolates water's
movement effect on the same route. It does **not** measure the actual dry
perimeter's journey time or prove that swimming is the faster tactical choice.

Fold's earlier shallow crossing is also now measured in this harness. Forward
400 takes 1,680 ms wet versus 1,424 ms drained; forward 200 takes 2,560 versus
2,128 ms. Both complete the curb entry and dry exit. These new results are
retained separately from Fold's historical static-only experiment record.

## Verification and remaining tradeoffs

The final sealed BSP passes full VIS, extra4 lighting with one bounce, and all
sixteen dedicated Q2REPRO initialization gates. Thirty-three gameplay anchors
pass 6,204 compiled hull/brush checks; all 32 ordinary treads have support and
clearance. The wet audit checks 16 supported tread centers, 19 piecewise route
intervals, continuous horizontal floor support, two emergence columns and the
blocked straight-up surfacing attempt beneath the low roof. Five selected flat
routes and both directed dry station returns are also checked. Their scope is
recorded in verification (`out/lockyard/verification.json`, written by `tools/map_recipes/build.ps1 -Recipe lockyard`).

Seventeen affected CTest gates pass, including fourteen synthetic Lockyard
cases, six passage-dependency cases and twelve movement cases with the local
native executable present. The full configured suite was not rerun. The new
preview passes 24 size/theme/floor states, plus 42 prior-map compatibility
states. These are analytical diagrams, not engine render-target images.

Lockyard is 2,016 × 1,728, larger than Fold's 1,792 × 1,664. Gross room/passage
envelope coverage is 91.0% versus 94.8%; it does not subtract internal solids or
measure walkable area. Median sampled sightline falls from 268.9 to 239.2, while
p90 rises from 737.4 to 758.9. Nearest-weapon straight-line median/p90 rise from
329.5/423.2 to 403.0/521.3. This iteration adds a useful dry return and a tested
movement commitment; it does not establish uniformly tighter or better play.
All ten openings remain local chokepoints; two meet the center-to-center
visibility-readability descriptor. These point queries do not establish that
the entrances are readable at movement speed.

Next, measure the actual ordinary return with matched starts, inspect underwater
visibility using engine-written images, and play 2/4/6-player trials for route
use, exit ambushes, breath/damage and resource control. Test discoverability of
the dive with first-time players before promoting it into automatic generation.

An unrelated core limitation surfaced: point lights inside liquid bounds are
rejected as `placement_in_solid`, even though native underwater lights are
valid. Lockyard uses dry/above-water lights. Existing conservative clearance,
liquid material defaults and Windows client focus limitations remain recorded.
