# The Rage: raised armor approaches and a rejected circuit

The measured part of **The Rage** (`ztn2dm3`, Sten “ztn” Uusvali) provides two approaches to one authored Jacket Armor station with different visibility at their starting stations. The eastern floor 64 arm reaches that station by a flat, supported bend. A floor 0 central station reaches it through four geometrically checked 16-unit rises. **The proposed closed circuit fails:** its additional 64-unit drop connection clips solid geometry. The failed edge remains in the record; this study supplies connected subroutes, not a certified loop or a native movement result.

This is one bounded area of one existing local reference, selected from the established inventory. It does not rank the whole map, establish item balance, or copy its geometry into the generator. The reproducible [record](../knowledge/q2/raised-resource-circuit.json), frozen protocol (`out/raised-resource-circuit/protocol.json`, written by `python -B -m tools.map_study.raised_resource_circuit`), full audit (`out/raised-resource-circuit/audit.json`, written by `python -B -m tools.map_study.raised_resource_circuit`), and analytical figure (`out/raised-resource-circuit/resource-approaches.png`, upstream study evidence, not imported) retain positive and negative evidence.

## Source and selection

The inspected source is `E:/Development/q2maparchive-wip/maps/ztn2dm3/pack/maps/ztn2dm3.bsp`, Quake II IBSP38, SHA256 `18a265da5b4be7664b22e6eff80adca7ce4c41da3ee1e152479b24b9ea36a1e2`. Authorship comes from the local readme and metadata, also pinned in the study record. The existing dossier (`out/map-study/records/ztn2dm3-6351c2b9cf.json`, written by `python -B -m tools.map_study ingest`) supplies the reference partition and provisional author/family split; those are unchanged. Embedded stock entities are inspected without executing mode filtering, item settling or game logic.

Initial source inspection considered the central/western Mega, Railgun and raised resources. The exploratory projections and inventory (`out/raised-resource-circuit/exploratory-inventory.json`, upstream study evidence, not imported) narrowed the test to the actual floor 0/64 center and northern armor room. This selection history is not a failed movement trial. After selecting the waypoints, the protocol was frozen at SHA256 `0c7f78bd452360cb7ea4cdcdf5c0fc6ccc5e819c32fd60adb6a12c6449062bb2`; no later rerouting or tolerance expansion was used to make the circuit pass.

The map readme dates the release 04/10/1998; local metadata resolves it as 10 April 1998. The readme lists 2–8 players and 7 respawn spots, while local metadata lists 2–6 players and the existing parsed dossier counts 8 spawns. These source disagreements remain unresolved. Metadata also retains its previously recorded malformed line 12. No new population or capacity recommendation is derived here.

The local readme permits distribution subject to keeping the readme, attribution and notifying the author. This study does not redistribute the BSP, send a notification, grant modification rights, or declare ML training permission. Rights remain unreviewed for training. The delivered images are original analytical views; the archive was read without modification.

## Fixed geometric contracts

The audit checks all 808 model0 blocking brushes under `SOLID | WINDOW | PLAYERCLIP | MONSTER` using an exact 32×32×56 standing hull and zero collision epsilon. There are 0 model0 liquid brushes. Convex brush planes supply collision and floor support; draw-surface projections are context only.

For a flat axial segment, the entire swept hull must remain clear and its complete 32-unit-wide footprint must have actual solid support at the declared foot height. The support certificate partitions exact cap rectangles and checks their vertices against every plane of the corresponding convex solid. A conservative rectangle rejection is not proof that all other routes fail.

Each stair construction checks a supported source and target station, an unobstructed vertical raise at the source XY, and a horizontal advance at the target height with complete projected ground within 16 units below the envelope. The four source-informed rises are 16 units each, with 32 units between station centers. This is a static geometric construction; continuous foot contact, native stepping, velocity, timing and player control were not tested. The old interlocking study's shallow-ground helper remains unchanged: a separate strictly 16-unit routine serves this different contract. The first tool run stopped at that older helper's band guard before producing results; correcting this implementation incompatibility did not change the protocol or count as a geometric trial.

The drop candidate separately requires supported takeoff and landing, a clear lateral hull envelope, and a clear vertical descent envelope. Even a pass would not predict an airborne trajectory. Its lateral clearance actually fails.

All six authored brush-entity submodels are excluded conservatively. The two `func_plat` bounds expand in both vertical directions by the larger of their authored height and model height; the other authored submodel bounds remain exclusions. Every tested hull envelope and selected eye-ray bounding box is strictly disjoint from these six envelopes. No moving platform, trigger or other mechanism is executed.

## Resource approaches and exact joins

Entity 73 is `item_armor_jacket` at authored origin `[-640,848,80]`. The station at the same XY has feet `[-640,848,64]` and passes full-hull clearance and support. The 16-unit difference is recorded literally; it is not an engine-settled pickup height or pickup-contact proof.

| Measured approach | Waypoint feet, in map units | Result |
|---|---|---|
| Raised east arm E → armor J | `[-320,480,64] → [-320,800,64] → [-640,800,64] → [-640,848,64]` | 3 flat segments pass; 688 XY units |
| Center C → stair base | `[-608,480,0] → [-608,504,0]` | Flat segment passes; 24 XY units |
| Center stair | `[-608,504,0] → [-608,536,16] → [-608,568,32] → [-608,600,48] → [-608,632,64]` | 4 static step constructions pass; 128 XY units, 64 rise |
| Stair top → armor J | `[-608,632,64] → [-608,800,64] → [-640,800,64] → [-640,848,64]` | 3 flat segments pass; 248 XY units |

The center-to-armor approach therefore measures 400 XY units with 64 units of rise. The east approach measures 688 XY units on floor 64. **These are different starting stations**, so their lengths do not show a faster alternative between identical endpoints. Their final 80 XY units are shared, including the exact same endpoint. The lower landing L `[-464,480,0]` also has a 144-unit supported connection to C. The audit derives directed connections from passed segments and binds all four E/C/L/J stations to those measured armor approaches, rather than treating unconnected supported stations as routes.

The frozen circuit was J → E → proposed drop → L → C → stair → J. All 5 endpoint joins, including closure, match exactly. Its proposed total is 1376 XY units and 128 absolute vertical units. **The circuit result is false**, because endpoint agreement does not repair a failed physical edge. There is no claim of an ordinary return or closed native circuit.

## Failed candidates retained

| Frozen candidate | Actual measurement | Limit |
|---|---|---|
| East 64-unit drop | Takeoff path, landing and vertical envelope pass; lateral `[-368,480,64] → [-464,480,64]` intersects brushes 404/690 | That declared drop connection fails; no revised launch or trajectory was searched |
| Direct E → J diagonal | Standing hull intersects 408/410/447/690 | The supported bent approach does not imply a central shortcut |
| Direct C → Rail station at feet 128 | Standing hull intersects 743/744/747; both endpoints support | Visibility and endpoint support do not establish an ascent route; other routes remain untested |
| Upper Mega flat extension at feet 320 | First 192-unit leg intersects 585/598; its full footprint has support from brush 443. Final 64-unit leg to Mega passes | The apparent gap in upward draw surfaces is not missing collision floor. Solid geometry blocks this declared approach; this does not establish whole-map Mega inaccessibility |

The higher Mega and Rail stations individually pass the standing-hull test but have no passed connection into the measured resource graph. They remain sightline targets and negative route endpoints, not extra connected floors. The tested geometric stair makes floor 0/64 relevant here; the unproven128/320 access is not counted as another playable layer in this certificate.

## Supported sightline stations

All 6 named standing stations pass exact clearance and support. Four of those stations emit a fixed set of 20 point rays, at 46 units above feet, to the other named stations. These are model0 `SOLID | WINDOW` rays using the existing floating-point BSP-tree clipper with a `1e-5` plane-comparison tolerance; they are distinct from the exact hull collision contract. All are also disjoint from the recorded mechanism envelopes. No dynamic actors, facing, sound, splash damage or exposure duration is represented.

| Source station | Clear targets among the 5 fixed others | Clear count |
|---|---|---:|
| E, east arm feet 64 | None |0/5|
| C, lower center feet 0 | Armor J, Rail, lower landing L |3/5|
| J, armor feet 64 | Center C, Rail, lower landing L |3/5|
| Mega, feet 320 | None |0/5|

C and E are actually on the passing approaches to J. Their point-eye results differ: C can see the armor station, while E cannot. This supports an approach-specific visibility distinction; it does not establish combat superiority, total visible area, a safe route or item contest balance. C can also see the Rail station despite its direct ascent candidate failing collision. Visual connection and traversable connection must remain separate data.

## Relation to the existing Quake Live atlas

The prior [Blood Run horseshoe study](MP_SPATIAL_PATTERNS.md) examines local pre-Steam QL surfaces 1095/1353/1354 atZ512. Its western section is joined while the two east arms are separated; X480 has a 138-unit projected gap and X640 a 64-unit gap. That retained diagram (`out/mp-spatial-patterns/bloodrun-upper-horseshoe.png`, upstream study evidence, not imported) is a spatial-form comparison only. Its IBSP47 source hash and earlier record are pinned here; current Steam equivalence, collision traversal and Q2 movement equivalence remain unverified. It is not an additional route trial or a source of transplanted geometry.

Together, the two evidence types suggest a useful generation contract: an arm can wrap a playable center and lead to a resource, but the generator must certify each physical connection and bind visibility stations to actual routes. A horseshoe silhouette alone supplies none of those certificates.

## Knowledge recommendations and reproduction

1. Describe resource access as typed, endpoint-bound approaches: station, height, actual supported segments, merge point and mechanism dependencies. Distinguish different starts from an endpoint-paired detour comparison.
2. Keep clear eye rays separate from movement edges. Certify raised-to-lower shortcuts and ordinary returns before counting a circuit; retain disconnected height bands and collision failures.
3. Use local exposure contrasts as structural design intent, followed by native and human evaluation. Neither room shape, layer count nor these 20 rays is a universal map-quality score.

Run from the repository root:

```powershell
python -B -m tools.map_study.raised_resource_circuit
python -B out/raised-resource-circuit/controls.py
python -B out/raised-resource-circuit/render.py
python -B out/raised-resource-circuit/record_evidence.py --verify
```

The 9 original synthetic controls cover adjacent stair caps, a one-unit gap, ground outside the 16-unit band, a head obstruction, flat support, liquid, a mechanism envelope, clearance without floor support, and rejection of undeclared bands. They all pass. The study record binds source hashes, frozen waypoints, audit, scripts, figure and this document; verification also reproduces the audit and checks the retained outcomes. No model weights, profile, planner, ranking formula or common rule card was edited by this reference study. Native movement, item settling/contact, full circuits elsewhere, mode-specific spawning and human multiplayer evaluation remain open.
