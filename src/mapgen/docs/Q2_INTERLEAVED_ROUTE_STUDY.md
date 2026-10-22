# Interleaved routes, elbow clearance and return policy

Reclamation (`q2rdm7`, Corey “Kamarov” Peters) provides a concrete compactness lesson: lower and upper paths can occupy the same part of the plan while taking different turns and serving different resources. This study certifies selected ordinary **flat** paths in the archived Q2R Edition. It does not establish an ordinary connection between those floors or a successful multiplayer layout.

The focused analytical plan (`out/interleaved-route-study/q2rdm7-nested-routes.png`, upstream study evidence, not imported) shows the actual compiled upward surfaces with checked paths overlaid. The [machine record](../knowledge/q2/interleaved-route-study.json) pins the BSP, source context, helpers and results. These are original analytical diagrams, not engine screenshots or reference geometry incorporated into generation. No archive files, partitions, profiles or model weights were changed.

## The nested paths

Coordinates below are player-center XY and hull-foot Z. The selected floor-zero convention is the map's original coordinate system; no heights were shifted for presentation.

| Selected path | Complete stations | Plan length |
| --- | --- | ---: |
| Lower circuit, foot Z −272 | (448,−256) → (448,−768) → (992,−768) → (992,−256) → (448,−256) | 2,112 |
| Rocket approach, foot Z −272 | (−128,−256) → (448,−256) | 576 |
| Inward rail spur, foot Z −272 | (672,−768) → (672,−480) | 288 |
| Upper bent path, foot Z −48 | (−128,−544) → (448,−544) → (448,−832) → (1024,−832) → (1024,−448) → (800,−448) → (800,−416) → (624,−416) | 2,256 |

All **17 listed poses and 13 full connecting sweeps** pass against **418 model-zero player-blocking brushes**, using the `SOLID | WINDOW | PLAYERCLIP | MONSTER` mask (`33619971`). The lower circuit closes geometrically; the upper result is a path between two stations, not a claimed closed loop. The rail spur joins the lower circuit at a checked station. Its endpoint is the authored rail origin's XY, but item settling, pickup touch and the player's tactical commitment were not simulated. These chosen lengths are neither shortest paths nor recommended dimensions.

Each standing hull is 32×32×56 with no collision epsilon. Exact stored BSP plane coefficients define convex solids. For every flat segment, the swept bottom footprint is partitioned at candidate floor-cap edges. Every positive-area rectangular cell must lie wholly in at least one actual coplanar convex top face. All cell corners are checked against that solid's planes; a brush's rectangular bounds alone never certify support. This proves continuous **full-footprint floor coverage**, including seams. An independent continuous translated-box SAT check rejects any hull penetration along the complete segment. Unsupported cells and intersecting brushes are retained in rejected results.

This is stronger evidence than sampled waypoint clearance, but it remains a static construction. Native acceleration, turn steering, step choice, other players and runtime game rules are outside the experiment. The floor and collision methods are recorded in the audit (`out/interleaved-route-study/q2rdm7-route-audit.json`, upstream study evidence, not imported).

## Two failures that a top-down route line hides

The lower circuit's southwest elbow requires the turn. Travel through **(448,−640) → (448,−768) → (576,−768)** stays on the already certified ordinary legs. The direct diagonal between the first and last points hits world brushes **38 and 45**. Both endpoints are valid, yet their straight connection is not. A planner that connects sockets by proximity would invent a shortcut here.

An initial upper path also cut its corners too tightly. Using X384 for the southbound leg intersects brush **80** and loses full floor support; aiming directly toward (736,−448) intersects brush **105**. The corrected path uses X448 and the offset two-turn approach through (800,−448) and (800,−416). The rejected route remains in the record. This is a corrected measurement of existing geometry, not an alteration of the reference map.

At **XY(448,−640)** and **XY(448,−736)**, standing hulls are fully supported and clear at both foot Z−272 and foot Z−48. These are two actual vertically stacked traversable stations, separated by 224 units. Upward point rays from the lower eye first hit the deck underside at **Z−64**. The attempted vertical hull descent at the first station intersects brush **75**. Shared XY therefore demonstrates reuse of plan area, while the deck deliberately separates these routes. It establishes neither a drop nor direct cross-level sight.

## What an elbow changes in a sightline

From the fixed lower rocket eye **(−128,−256,−226)**, the long northern lane remains visible to the selected eye at (992,−256,−226). Moving down the eastern leg, samples at Y−288 and −320 remain visible; the sample at Y−384 is blocked. On the western leg, the samples at Y−320 and −384 are visible, while Y−768 is blocked. The selected rail eye is also blocked from this source. Six of the ten explicitly listed rays are clear.

This observation suggests a useful hypothesis: a bend can shorten the interval during which one opponent sees a route while retaining an ordinary circuit nearby. It does not locate the exact visibility threshold, establish a safe retreat, measure exposure time or show that one rocket position can control the map. The rays ignore facing, players, splash, sound and temporal movement. The source eye and all target eyes use foot+46; this is one reproducible static viewpoint comparison.

## A return must have its own evidence

This BSP contains **two `trigger_push` volumes**, two `func_rotating` entities and two teleporter links. The push volumes are model *2 at `[544,32,−304]..[608,96,−272]` and model *3 at `[−48,−880,−208]..[48,−784,−200]`. Their authored speeds are 75 and 55, and both use angle −1. In the pinned original-Q2 source, `trigger_push_touch` assigns velocity from direction and ten times speed; it is special movement, not an ordinary stair. This study does not execute either push or infer its landing.

Every one of the 13 successful route segments is strictly disjoint from ten conservative known mechanism envelopes: rotation-sphere AABBs, authored push submodels, stock teleporter pad solids and source triggers. All **130 exclusions pass**. Rotating envelopes use the full model radius at the authored origin, rather than checking only one rotation. This separates the selected ordinary flat-path certificates from those mechanisms; it does not establish the whole map's runtime connectivity.

The compiled world also contains three lava brushes with top Z−296. All **39 complete selected-segment versus liquid checks** pass without the standing hull entering lava. The lower checked routes sit 24 units above those liquid tops. This is a dry-path geometry check, not a falling-damage or hazard-escape test.

The earlier [gladdm1 drop/ordinary-return study](Q2_DROP_RETURN_STUDY.md) supplies a different, complementary example: a specific 272-unit drop-space candidate and a demonstrated 3,088-unit return with stairs and ramps. That existing result remains geometric, not a native falling-damage or fastest-return test. Its source and evidence bindings were checked again for this record; the old route was not rerun here. Neither its mostly symmetric organization nor Reclamation's mechanism dependence is a prescription for the original Ravel layout.

## Transfer to the original Ravel experiment

1. **Pack destinations and real paths together.** Place an upper approach above part of a lower return when their different resource or combat roles justify the overlap. Keep floor separation, support, clearance and inter-floor access as separate certificates.
2. **Give every elbow an actual hull trajectory.** Check the complete turn, then retain a diagonal negative control where geometry is intended to block corner cutting. Two good stations are insufficient.
3. **Put a drop beside a platform edge.** Save its departure, clear lateral exit, descent volume, supported landing and ordinary return to the exact departure. Closing the optional drop must leave the intended ordinary graph intact. Shared XY alone proves none of these.
4. **Make nearby routes differ in exposure.** Compare fixed eyes before and after a bend or screen, then test timing and interception under native motion. A blocked ray is a useful local measurement, not a tactical quality score.
5. **Retain committed item branches deliberately.** The inward rail spur provides a concrete local contrast with a through circuit. For a new layout, test whether its reward, exit visibility and recovery options justify entering it; do not impose a zero-dead-end rule without considering the item's role.

The recipe and these measurements should be reviewed independently. This research makes no claim about a final Ravel compile, score, player capacity or playtest outcome.

## Version, rights and remaining limits

The certified BSP is `q2rmappack/pack/maps/q2rdm7.bsp`, SHA-256 **`9220bcea7b7cf4f3f0ac5c6a1d106251954408d3b562f52e977c8eb8909f0095`**. Archive metadata calls it Q2R Edition, lists duel/FFA and 2–4 players, and records the alias `kamq2dm3`. Archived player reviews disagree about size and health scarcity and do not bind their tested copy to this hash. The separately pinned original readme describes a March 1999 completion and recommends 2–4 players; this does not establish byte or gameplay equivalence with the Q2R Edition metadata's August 2001 date. The metadata's existing malformed `score_level` line is preserved.

The original readme prohibits using the level as a base for additional levels and restricts distribution. Nothing here licenses geometry reuse, asset redistribution or training on protected map contents. The reference archive remains read only; the generator receives abstract, provisional design rules and original experiments.

A fresh q2rdm2 surface plan (`out/interleaved-route-study/q2rdm2-plans.png`, upstream study evidence, not imported) was inspected as a second context candidate. Its unequal partial upper routes are useful visual context, but no new movement certificate is assigned to that map. Its existing authorship, rights and revision context remain in the [route-diversity study](Q2_ROUTE_DIVERSITY_STUDY.md). Likewise, QL orthographics remain rendered-surface evidence and cannot supply a Q2 movement contract.

No new unrelated implementation defect was found. The newly explicit Reclamation push volumes correct the completeness of this study's mechanism inventory; earlier reports retain their original narrower scope. No game client, input control, OS capture or native runtime was used for this reference study.
