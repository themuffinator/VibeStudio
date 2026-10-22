# The Killing Machine: stair approaches and a separate high crossing

In one inspected area of **The Killing Machine** (`ztn2dm2`, Sten “ztn” Uusvali), two differently oriented stair approaches connect a useful lower route to separate middle-level endpoints. Each staircase rises from Z 0 to Z 128. A short high route at Z 288 reuses part of the lower route's XY footprint. These are supported geometric relationships, but they do **not** establish that both staircases feed that high crossing. The complete proposed high route fails its final bend, and all fixed sightlines are obstructed.

The [study record](../knowledge/q2/atrium-route-study.json), pre-measurement protocol (`out/atrium-route-study/protocol-v1.json`, written by `python -B -m tools.map_study.atrium_routes`), complete first audit (`out/atrium-route-study/audit.json`, written by `python -B -m tools.map_study.atrium_routes`) and original route figure (`out/atrium-route-study/atrium-routes.png`, upstream study evidence, not imported) retain the successes and failures. This bounded result informs the proposed original Span motif by showing which relationships require separate proofs. No source layout was copied into generation, and this is not an archive precedent for a connected opposed-stair Z-shaped bridge.

## Source, selection and rights

The inspected file is the archive's IBSP38 `ztn2dm2/pack/maps/ztn2dm2.bsp`, SHA256 `8b548b52106d66fdef005ed3b64a5959442255b29203b22729bf79af5e83dcb4`. Its local readme is dated 22 March 1998, credits Sten Uusvali, recommends 2–8 players and names several feedback and beta-test contributors. These are author statements, not a new playtest or independent quality score. The existing dossier (`out/map-study/records/ztn2dm2-793d2fcf79.json`, written by `python -B -m tools.map_study ingest`) places it in the reference partition and split group `50259728799005a8`; sharing that author's group with earlier studies does not supply independent author diversity.

The readme makes redistribution conditional on retaining its text and credits and contacting the author. This study sends no message and distributes no source BSP, textures or other game assets. Rights remain `unreviewed_local_reference`, and training eligibility remains false. New scripts, selected coordinates and analytical figures are original research artifacts, not copied generator geometry or trained weights.

Two BSPs were inspected through authored entities and upward draw surfaces before the route protocol: The Killing Machine and **Cunning Plan**, Paul “PeeJ” James, Q2R Edition (`q2rdm6`, SHA256 `858108d3c2400528c510f6b8d80137da86b3c0e18dbe4ed8d3b9d2a3604fcc7c`). Cunning Plan remains an unselected draw-only observation with two lift entities; no route attempt or failed ordinary ascent is attributed to it. Its dossier (`out/map-study/records/q2rdm6-e4dca6965d.json`, written by `python -B -m tools.map_study ingest`) supplies no readme. The retained source inspection (`out/atrium-route-study/source-inspection.json`, written by `python -B -m tools.map_study.atrium_routes`) records both selections, with Killing Machine (`out/atrium-route-study/ztn2dm2-inspection.png`, upstream study evidence, not imported) and Cunning Plan (`out/atrium-route-study/q2rdm6-inspection.png`, upstream study evidence, not imported) height slices. Surface drawings include trim and do not certify navigation.

The inventory also consulted existing dossiers and the earlier [Powertrip upper-connection study](Q2_UPPER_CONNECTION_STUDY.md) and [traversal inventory](Q2_TRAVERSAL_STUDY.md), without repeating their measurements or expanding the BSP search. Both dossiers retain the pre-existing `malformed metadata line 12` issue; no source metadata was changed. The initial drawing script referenced the reader's wrong face-key name; that implementation error was corrected before drawings or route measurements. It is selection history, not a geometry trial. The final protocol SHA256 is `b98d35c12fe4e4df237ad1e3edab6680dd3edeb4c17d766517cf8de7f9058de3`. No route, station or ray changed after measurement, and no failed candidate was repaired or replaced.

## Physical contract and fixed outcomes

The audit checks the exact stored planes of all 830 model0 blocking brushes under `SOLID | WINDOW | PLAYERCLIP | MONSTER`, with a 32×32×56 standing hull and zero collision epsilon. Full-footprint support requires a union of cap rectangles admitted by the actual convex planes; brush AABBs alone cannot establish support. There are no model0 liquid brushes or stock teleporter pads in this source. Three `func_plat` entities and one Y-moving `func_door` are excluded through conservative swept bounds; all tested constructions and ray bounding boxes remain strictly disjoint from them. No mover executes.

Flat legs require constant full-footprint support at their specified height. Each declared 16-unit stair change requires fully supported standing endpoints, a clear vertical hull raise, and a clear horizontal advance with actual ground everywhere within 16 units below the raised footprint. The reverse construction checks retreat and vertical descent. The support band cannot bridge an empty gap. This is a bidirectional geometric construction, not an executed native step, an assertion of continuous grounded motion or a movement-time measurement.

| Frozen path | Actual endpoints (feet XYZ) | XY length | Result |
| --- | --- | ---: | --- |
| West bent ascent | `(80,-32,0)` to `(-128,-176,128)` | 352 | Both directions pass; eight 16-unit rises |
| East straight ascent | `(448,-48,0)` to `(448,208,128)` | 256 | Both directions pass; eight 16-unit rises |
| Lower route between stair starts | `(80,-32,0)` to `(448,-48,0)` | 640 | Both directions pass |
| High crossing candidate | `(160,16,288)` to `(480,-128,288)` | 464 | First 320 XY pass; final 144-unit bend fails |
| Lower overlap subroute | `(160,16,0)` to `(224,16,0)` | 64 | Both directions pass |
| Upper overlap subroute | `(160,16,288)` to `(224,16,288)` | 64 | Both directions pass |
| Projected middle connection | `(-128,-176,128)` to `(448,208,128)` via fixed bends | 960 | All three legs fail |

The lower route binds the **exact distinct bottom endpoints** of the two stair paths. Consequently the selected middle endpoints connect through reverse west stairs, lower route and forward east stairs: 1,248 XY units and a descent and ascent of 128 units each. This is a single constructed connection; the two staircase lengths are not alternative routes between identical endpoints. No shortest-path or ordinary whole-map return claim follows.

The two overlap subroutes are already exact segments of the frozen lower and high candidates. Their swept footprint is X `[144,240]`, Y `[0,32]`, at feet Z 0 and Z 288. The high candidate's first three legs form a passing 320-unit prefix; retaining that prefix does not convert the failed full candidate into a success. Its last bend has complete floor support from brushes 11 and 32 but intersects brush 14. A direct hull rise from `(224,16,0)` to `(224,16,288)` intersects brush 427. Shared XY therefore establishes neither a vertical connection nor a complete higher-level crossing.

The projected Z 128 candidate retains three failures. The first has no hull blocker but lacks support; the second lacks support and intersects brush 467; the third lacks support and intersects brush 141. These are failures of the chosen paths, not claims that the actual map has no route between those places.

There are 7 fixed paths, of which 5 pass, and 33 paired legs, of which 29 pass. These counts include the two overlap subroutes that deliberately repeat main-path legs. They are not independent routes or 33 native trials. Among the four main routes, three pass and the complete high route fails; the additional projected middle candidate also fails.

## Station-specific exposure and limits

All eight fixed standing stations pass full support and clearance. Five bind to complete passing stair or lower paths. The three high stations stand locally, but their declared complete high path fails; the record preserves that distinction even though two lie on its passing prefix.

The protocol declares nine unordered station pairs in both directions: 18 point rays from feet plus 46 units, using `SOLID | WINDOW` and the reader's `1e-5` plane tolerance. Every ray is blocked by model0 geometry. Twelve are cross-level observations between the specified Z 0 and Z 288 stations; six are within the lower, middle or high level. These failures do not show universal invisibility or safe firing positions. They show that standing on a raised path and overlapping a lower route do not guarantee exposure between chosen stations. No extra rays were sampled to seek a positive result.

Hyperblaster entity 18, grenade launcher 19 and body armor 36 remain authored resource context only. Their origins are not certified pickup feet, and no supported resource approach or pickup contact was measured. This study tests no native movement, jump, crouch, liquid travel, lift timing, combat, spawn balance, human match or model learning.

For generation, connect each named stair endpoint to its intended raised deck and lower aisle explicitly; prove overlapping layers independently; then sample chosen cross-level eye stations. Keep a failed corner, unsupported projected shortcut and blocked ray as separate outcomes. A bridge count or attractive orthographic drawing should not stand in for those tests.

## Reproduction

From the project root, `python -B -m tools.map_study.atrium_routes --verify` rederives the full unchanged audit. `python -B -m unittest discover -s tests -p test_atrium_routes.py -v` runs 10 original controls covering step clearance, full support, missing floor, real convex planes, layered support, blocked projection, mover/liquid exclusion and undeclared step geometry. `python -B out/atrium-route-study/record_evidence.py --verify` checks bound file hashes, selected source identity, the complete audit, summary derivation, controls, document links and rendered figure pixels without altering retained evidence.

The new measurements add no native archive attempts and no changes to earlier evidence. The pre-existing metadata line issues remain outside this study's scope; no additional unrelated repository issue was identified.
