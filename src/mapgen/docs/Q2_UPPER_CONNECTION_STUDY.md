# Powertrip: an upper connection above a lower corridor

In the inspected part of **Powertrip** (`ptrip`, Dennis “headshot” Kaltwasser), a complete upper route links the western atrium side to the eastern bay while reusing part of a lower corridor's XY footprint. Both fixed routes pass exact standing-hull clearance and full-footprint support. The selected shared connector is 80 units long, with feet at Z 0 and Z −256. A projected upper shortcut fails, and a direct vertical sweep between the two supported bridge stations hits solid geometry.

This is a new bounded measurement of Powertrip, not another certificate for the earlier q2rdm2 lower bypass, Reclamation nested route or The Rage armor approaches. It demonstrates a real geometric connection between the selected western and eastern spaces at each level. It does **not** certify an ordinary connection between those levels, a shortest path, a whole-map graph or native gameplay.

The [study record](../knowledge/q2/upper-connection-study.json), fixed protocol (`out/upper-connection-study/protocol.json`, written by `python -B -m tools.map_study.upper_connections`), complete audit (`out/upper-connection-study/audit.json`, written by `python -B -m tools.map_study.upper_connections`) and route figure (`out/upper-connection-study/upper-and-lower-routes.png`, upstream study evidence, not imported) are retained. The figure uses one XY envelope and scale for both elevations; grey source draw surfaces are context, while route results come from the collision brushes.

## Source, rights and selection

Source: `E:/Development/q2maparchive-wip/maps/ptrip/pack/maps/ptrip.bsp`, Quake II IBSP38, SHA256 `7e237f4d0d6f018d58bd06969fd0bbe09479b6aa89665d349e19f47c9ed7e2f2`. The existing dossier (`out/map-study/records/ptrip-467cff01da.json`, written by `python -B -m tools.map_study ingest`) records the reference partition and provisional author/family split group `01b7818a69ea19b5`. The readme says Powertrip is heavily based on D-Lite; the family relationship is retained rather than treating those names as independent training samples.

The author readme and local metadata agree on 15 June 1998. The readme describes a duel map made for the QuakeCon 98 tournament and recommends duel or four-player FFA; this is an author statement, not independent evidence of tournament use or modern balance. Local metadata instead supplies a 2–5 player range and retains malformed line 12. This study does not resolve those capacity differences or infer a spawn-count policy.

The original readme prohibits using the map as a base for additional levels and limits distribution. No archive geometry, BSP, textures or third-party code is incorporated into the generator or redistributed by this study. The archive remains read-only; training rights remain unreviewed and training eligibility remains false. The analytical images and new research scripts are project-authored.

Powertrip was the only BSP geometrically inspected for this study. Existing q2rdm2 and Reclamation records supplied context without another source search. The source inspection (`out/upper-connection-study/source-inspection.json`, upstream study evidence, not imported) and initial surface figure (`out/upper-connection-study/source-floors.png`, upstream study evidence, not imported) preceded the fixed collision routes. Protocol SHA256 `2dc2998a4e5df9641ac975951b9bab7c440d94a5ac0c99d0925a12f35c6c90cd` was recorded before measurement. No waypoint changed afterward, and no failed path was replaced by a later search result.

## Certificate and complete selected routes

The audit uses all 551 model0 blocking brushes under `SOLID | WINDOW | PLAYERCLIP | MONSTER`; there are 0 model0 liquid brushes. Every route segment has a 32×32×56 standing hull, exact stored-plane collision checks, zero collision epsilon and continuous support under its complete swept footprint at the declared flat foot height. Exact cap-rectangle partitions admit support only after checking the actual convex solid planes. The selected main routes use no slope, stair, jump, lift or shallow-floor tolerance.

Two `func_plat` submodels, entities 59/60, are conservative exclusions. Their bounds expand in both vertical directions by the larger of authored height and model height. Every tested hull envelope and fixed ray bounding box is strictly disjoint from both envelopes; no platform state or trigger behavior was executed. This establishes that the selected certificates do not depend on those mechanisms, not that the platforms are irrelevant elsewhere in the map.

The region names below describe the selected western atrium side, narrow connector and eastern bay visible in source geometry. They are analytical labels, not an inferred whole-map room segmentation or combat-balance classification.

| Selected route | Frozen feet waypoints | Result |
|---|---|---|
| Upper western side → eastern bay | `[64,-448,0] → [112,-448,0] → [112,32,0] → [480,32,0] → [480,-256,0] → [520,-256,0] → [560,-256,0] → [560,-128,0] → [736,-128,0]` | All 8 flat segments pass; 1,568 XY units |
| Lower western side → eastern bay | `[64,-448,-256] → [64,-256,-256] → [520,-256,-256] → [736,-256,-256] → [736,-128,-256]` | All 4 flat segments pass; 992 XY units |

The upper route bends around the central opening, passes through the selected upper connector and turns into the eastern space. The lower route crosses the western room and its narrower link into the eastern room. Both are fully supported geometric routes within their own level. They share endpoint XY but differ by 256 Z units, so their lengths are **not** interchangeable costs between identical 3D endpoints. The selected upper detour is not claimed to be shortest. No ordinary ground-to-upper ascent or return route was measured.

The authored Railgun at `[640,-448,48]` and Grenade Launcher at `[32,-448,48]` help orient the upper-space source context. Their entity heights are retained literally. The measured western endpoint is near the Grenade Launcher side; neither pickup contact, item settling nor a complete resource-access route to either entity is certified here.

## Two navigation layers in one footprint

The audit separately rechecks the connector centerline `[480,-256] → [560,-256]` at both foot heights. The exact swept footprint is `X[464,576] × Y[-272,-240]`:80 units of centerline plus 16-unit hull padding at each end. Both sweeps are collision-free and fully supported. Upper support uses brush 71; lower support uses brushes 5/115. These are subsets of the complete passing main paths, not isolated clear stations disconnected from their endpoints.

The bridge stations have identical XY `[520,-256]` and a 256-unit elevation difference. A standing hull can occupy either station, but the direct vertical sweep from the lower to the upper station intersects brushes 15/71. Thus projection overlap creates **two distinct route layers**, with no inferred vertical edge between them. The overlap measurement is local; it is not a map-wide packing or density score.

This is a useful construction lesson for an original generator: an upper passage can continue between spaces above a lower link, instead of ending as a platform inside one room. Its support and collision checks must remain attached to the upper connection, while the lower passage retains its own navigation identity. Explicit stairs, ramps or other validated transitions are still needed to connect the layers.

## Failed projection shortcut

The frozen negative candidate lifts the lower route's plan into Z 0:

`[64,-448,0] → [64,-256,0] → [736,-256,0] → [736,-128,0]`.

Its first leg passes. The long middle leg intersects brush 93 and lacks complete upper support over `X[176,432] × Y[-272,-240]`. Its eastern leg intersects brushes 83/429, even though the exact floor-support check alone passes there. A lower corridor's projected plan therefore cannot be promoted to an upper route by changing its Z value. Both floor support and full hull clearance matter.

The failed middle/eastern legs and the failed vertical sweep remain in the audit. They reject these specific candidates; they do not prove that all other shortcuts, jumps or lift-assisted routes are impossible. No alternative was searched after measurement.

## Fixed viewing stations and exposure

Six named stations W/B/E on each level all pass standing support and clearance, and each is an explicit waypoint of its complete passing main route. Eyes are 46 units above feet. The 30 directed point rays use the existing model0 `SOLID | WINDOW` BSP clipper with `1e-5` plane-comparison tolerance, separately from the exact hull contract. All ray bounding boxes also avoid both mover envelopes.

| Source station | Feet | Clear targets among the other 5 fixed stations |
|---|---|---|
| Upper W | `[64,-448,0]` | None |
| Upper B | `[520,-256,0]` | None |
| Upper E | `[736,-128,0]` | None |
| Lower W | `[64,-448,-256]` | Lower B |
| Lower B | `[520,-256,-256]` | Lower W |
| Lower E | `[736,-128,-256]` | None |

Only the lower W↔B pair is clear, giving 2 directed clear rays of 30. The corresponding upper W↔B rays are blocked even though that upper route passes. This is a fixed-station example of different exposure on stacked routes; it does not establish that the whole upper path is safer, less visible or preferable. Facing, sound, splash, moving actors and duration of exposure were not represented. No sightline result is converted into a movement edge or a gameplay score.

## Reproduction and knowledge limits

Run from the repository root:

```powershell
python -B -m tools.map_study.upper_connections
python -B out/upper-connection-study/controls.py
python -B out/upper-connection-study/render.py
python -B out/upper-connection-study/record_evidence.py --verify
```

Nine original synthetic controls cover separately supported stacked routes, a blocked vertical connection, refusal to use a lower floor as upper support, a missing upper-deck section, independent lower-route continuity, a head obstruction affecting only the upper path and exclusion of a mover envelope. All 9 pass. The evidence record pins the source BSP/readme/metadata, existing dossier, protocol, audit, helper implementations, scripts, figures and this document; its verifier checks those bytes and reproduces the full audit without changing it.

The new knowledge concerns **connected upper geometry and layer identity**, rather than another room-local platform or an unverified projection. It complements the earlier [Reclamation study](Q2_INTERLEAVED_ROUTE_STUDY.md) and [failed The Rage circuit](Q2_RAISED_RESOURCE_CIRCUIT_STUDY.md) while preserving their original scope. No source map is copied into a generated layout, no model weights or common rule cards are changed by this study, and no native or human match validation is claimed. Inter-floor access, combat balance and route timing remain open.
