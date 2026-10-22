# Spillway study — 13 September 2026

Spillway tests circulation between distributed encounters rather than routing most journeys through one atrium. Six unequal industrial spaces have ten connections, six stair runs and floors at 0, 144 and 288 units. An east-west bridge crosses an independent north-south lower trench. Separator and Control provide a local upper return route; the rest of the map does not repeat that third floor.

The original [recipe](../tools/map_recipes/spillway.cpp) and [build instructions](../tools/map_recipes/README.md) are tracked. Final artifact identities and outcomes are in the [experiment record](../knowledge/q2/experiments/spillway-v1.json); generated MAP, BSP, diagrams, package and logs remain in `out/spillway/`. This is an explicit construction experiment, not automatic CLI motif selection or trained model weights.

## Research translated into design

The [four-map route diversity study](Q2_ROUTE_DIVERSITY_STUDY.md) examined The Chastity Belt Duel, Reclamation, The Triple Towers and Paranoia. It suggests giving routes different resource and movement purposes, using local height changes, and testing capacity rather than minimizing every dimension. The [playbook](MAP_DESIGN_PLAYBOOK.md) turns those observations into a repeatable workflow with counterexamples and failure signals. No reference geometry, game assets or external code were copied into this map.

| Space | Role | Occupied floors |
| --- | --- | --- |
| Intake | Wider lower rocket encounter; middle bridge and north return | 0 / 144 |
| Pressure | Offset close-range super-shotgun encounter and separate ascent | 0 / 144 |
| Separator | Lower rail access from the trench; middle circuit and upper armor approach | 0 / 144 / 288 |
| Control | Middle grenade route, upper armor return and east bypass | 144 / 288 |
| Workshop | Lower Mega Health recovery destination and chaingun approach | 0 |
| Outfall | Lower hyperblaster route; Quad on the middle approach to the eastern bypass | 0 / 144 |

Lower weapons and recovery give a player holding the two upper armors reasons to leave that circuit. The long Workshop–Outfall route bends twice to interrupt a continuous end-to-end view. A second eastern bypass connects Outfall to Control without entering Pressure. Shared industrial materials, different machinery footprints and warm Workshop/cool Outfall lighting establish intended identities; first-time navigation and rendered readability still need observation.

This is a different tradeoff from Switchback. It retains more room for encounters and reduces the concentration of connections in one space. Counting parallel links, maximum incident room degree falls from six to four. Both maps have ten connections and no articulation room; Spillway's cycle rank is five versus six. These counts describe topology, not independent tactical choices or proven flow.

## Static results and limits

The final source has 212 brushes. It compiles as sealed IBSP38 with full VIS and final `extra4` lighting, one bounce and soft shadows. All 264 spawn-to-gameplay-anchor checks pass; standing player hulls at all 33 compiled anchors pass 6,963 axial brush intersection checks. An additional 122 compiled-hull samples cover all 54 stair centers and both crossing lanes; every stair center has solid support below it. Structural, navigation, visibility, analytical lighting and timing findings are all zero. All final texture references resolve to stock Quake II assets. Both crossing lanes pass compiled point traces and a point inside their 24-unit separator is solid.

| Descriptor | Switchback | Spillway |
| --- | ---: | ---: |
| Combat-anchor elevation p90–p10 | 384 | 288 |
| Occupied 128-unit height bins | 3 | 3 |
| Sampled world sightline median | 154.3 | 215.0 |
| Sampled world sightline p90 | 580.4 | 555.3 |
| Visible compiled spawn-eye pairs | 0 / 28 | 0 / 28 |
| Upward world surface area | 2,847,936 | 3,461,568 |
| Median straight-line spawn-to-nearest-weapon distance | 245.0 | 348.0 |

The larger upward surface area includes trim and is not exact walkable area. The median sampled sightline is longer while its p90 is shorter; that provides a different distribution to test, not a quality improvement score. Major-item straight-line separation is about 1,871 units, above the descriptive small-DM band. Test whether this creates competing destinations or merely long empty travel, especially at four players. Zero point LOS between spawn eyes is not a spawn-safety guarantee.

There has been no engine traversal, render validation, first-time navigation study or multiplayer match. No weights were trained. Keep this example outside the reference calibration profile.

## Reusable implementation changes

- Placement timing includes same-area XY distance and endpoint approaches on cross-area paths. Versioned flow exports distinguish this basis from older artifacts. It still uses Manhattan distances, area centers and heuristic transition costs; do not report it as engine travel time.
- Typed point lights can occupy sealed passage volumes, allowing corridor bends to be illuminated directly. Gameplay placements retain the existing room-binding contract; outside lights and embedded points remain invalid.
- Recipe evidence now includes explicit stair runs and declared floor heights. The new viewer handles nine-tread, 144-unit ascents and crops each floor to its relevant rooms.
- The shared build/inspection pipeline selects either Switchback or Spillway and reports missing previous-map comparison data explicitly.

All 19 repository tests passed, including endpoint costs, passage-light validation/lowering, both original recipes, the configured Q1/Q2/Q3 compiler gates and GGUF planning. `out/spillway/ctest.log` retains the test evidence. The viewer passed 24 floor/width/theme states, recorded in `preview-checks.json`. The final pinned build script also reproduced the compiled map and package successfully.

## Next controlled review

First walk all six stair runs in both directions, both crossing lanes, every landing and all pickup positions in a windowed engine. Use engine-written screenshots to inspect players beneath galleries and at the low trench ceiling. Record exact map hash, module and settings.

Then compare one change at a time: close the eastern bypass; close only the upper Separator–Control return; move only one upper armor to a validated middle socket; or move only Mega closer to Intake. Observe route use, congestion, recovery time, upper control and empty travel separately at four and six players. These variants are proposed tests, not results already collected.

The research also found conflicting author/archive capacity labels for `q2rdm2` and `grdm1`, and missing exact-version/settings provenance for historical Reclamation opinions. Those labels remain unresolved and the reference profile was preserved.
