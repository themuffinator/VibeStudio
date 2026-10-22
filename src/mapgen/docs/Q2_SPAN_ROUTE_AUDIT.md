# Span: opposed stairs, upper bridge and lower return

All 11 selected high-verticality maps pass the same compiled route audit, covering 14 opposed-stair bridge rooms. The three low-verticality controls separately retain no new motif. On the frozen Span hero, four fixed native trials completed the open upper journey between opposite ground approaches. Each includes four explicit pauses before turning and ends at a wall-adjacent grounded goal. The lower aisle closes a separately checked static geometric cycle; no native return along that aisle was executed.

The hero audit (`out/span/atrium-audit.json`, upstream study evidence, not imported), portfolio summary (`out/span-study/portfolio-audit-summary.json`, upstream study evidence, not imported), native declaration (`out/span-study/native-protocol.json`, upstream study evidence, not imported), first native results (`out/span-study/native-comparison.json`, upstream study evidence, not imported), independent observations (`out/span-study/native-observations.json`, upstream study evidence, not imported) and final evidence record (`out/span-study/record.json`, upstream study evidence, not imported) retain the geometry, commands, checks and limits. The normal generator and candidate-selection comparison are documented separately by the Span iteration.

## Static contract and independent failure controls

The auditor reads the actual `opposed_stair_bridge` tag, rotation, source solids and typed report, then tests model-zero BSP collision geometry. Every raw world brush must be exactly SOLID before any collision-mask filtering; every accepted compiled brush must be an exact AABB. Dynamic mechanisms and ramps are excluded. Canonical layout and report identities, the stock Q2 standing profile, source and compiled file hashes are bound.

The motif has two opposing 128-unit-wide stair runs, with eight or twelve 16-unit rises and 36-unit treads, reaching height 128 or 192. Three 24-unit-thick decks join through full 160-unit faces. Before implementation, design review found that the initially suggested middle interval could invert in a long room or leave only a partial joining face. Its corrected extent covers both complete end-deck faces. The audit includes explicit 960- and 1536-unit depth controls; coincident upper turns still require a complete 160-unit supported square.

Each stair check covers the complete tread footprint, every standing center across its width, and fixed collision-free translations: raise 18, advance, then lower to the next tread. Both stair-to-deck seams, all upper strips and both turn squares require full-footprint coplanar support and standing-hull clearance. The independent lower check covers a 160-unit center aisle, its opposite floor connectors and the ground approaches to both stairs. A missing bridge cannot borrow support from the lower floor. A clear centerline cannot substitute for the full turn or stair width.

These raised translations are exact static constructions, not native internal step traces or a claim of continuous foot contact. The pure source constructor permits standing headroom of height plus 56; the compiled audit separately checks each actual 18-unit raised construction. A universal extra 18 units above every flat walking pose is not inferred as necessary traversal clearance.

The 25 synthetic controls include all four rotations and both heights, long and coincident turns, missing/partial stairs, a missing bridge, an eighth-unit join gap, insufficient standing clearance, an overhang that blocks raise-before-advance despite a clear standing pose, lower-aisle obstruction, wrong native entity class, coincident entities, invalid navigation binding and a raw liquid brush hidden by a hypothetical mask filter. Native declaration controls cover signed timing across coordinate zero and opposite physical goal bands; independent goal controls reject incomplete gate order, eight-sample dwell, time gaps and fast target crossing.

Across the selected portfolio, 13 bridge rooms reach height 128 and one reaches 192. All four rotations occur. Their 56 actual local gameplay stations—20 on ground and 36 raised—have complete compiled standing support/clearance, exact stock class/weapon-variant binding and a typed navigation node containing the actual station. This certifies local stations, not a full native journey to every resource. Fixed standing-eye rays remain separate observations and never decide the movement audit's success.

Span's Bridge 1 has a supported raised shotgun, raised combat armor, raised deathmatch spawn and ground health station. Three of their six fixed unordered eye pairs are visible, all between raised stations; the health station's three tested rays are blocked. Those few rays do not establish spawn safety, item balance or exposure during play.

## Four frozen open upper journeys

The hero BSP SHA-256 is `4a896858f0da9a9aa9cc1987def6e4cf0f3f2576367c23294ed2df075c90c37b`; its canonical layout SHA-256 is `b8765c42d09c09c1d9879c7a55c6d1da9f893747bc4f48f4b6a2aa8bb3121c61`. Bridge 1 uses rotation 2 and height 128. The forward declared route is:

`[224,328,0] → [224,-72,128] → [0,-72,128] → [0,72,128] → [-224,72,128] → [-224,-328,0]`

The reverse trials use those endpoints in reverse order. Both directions run literal forward inputs 200 and 400, giving four first attempts. Native Quake II caps the latter's wish speed at 300. Each trial starts with eight stationary 16 ms frames, uses 24 zero-input frames between its five movement legs, and retains 128 final release frames. The active counts are `[121,65,42,69,124]` at input 200 and `[78,41,26,43,80]` at input 400 in either direction.

All counts and physical gates were frozen before the first native call. Timing uses retained Weft stair-startup and Ravel ground-coast observations. Their historical declaration assumed each leg stayed on one side of coordinate zero; Span explicitly extends the estimate piecewise across zero. This is an approximate pre-run transfer, including the signed eighth-unit grid, not timing fitted to Span's results. No profile, geometry or gate was changed after execution.

The unchanged optional stock 3.19 CM/Pmove world harness executes the commands. Acceptance requires the complete ordered physical phases, actual 128-unit stair bands and 160-unit deck bands, supported reconstructed upper chords, standing/dry/clear native endpoints, and a separate ground finish. Nine consecutive endpoint states must span 128 ms with horizontal and absolute vertical speed at most 10. The broad native-runner station result is only a diagnostic. The final observer independently regenerates the declarations and finds the first complete gate-ordered goal window from raw frames.

| First trial | Accepted movement time | Full-budget final feet | Nearest wall gap at acceptance |
| --- | --- | --- | --- |
| Forward, input 200 | 8624 ms | `[-224.125,-343.875,0.125]` | 0.125 |
| Forward, input 400 | 6192 ms | `[-221.25,-343.625,0.125]` | 0.375 |
| Reverse, input 200 | 8624 ms | `[224.125,343.875,0.125]` | 0.125 |
| Reverse, input 400 | 6192 ms | `[221.25,343.625,0.125]` | 0.375 |

All four passed unchanged. Times exclude the initial 128 ms settle. Each accepted window contains nine identical poses with zero recorded velocity. At input 400, a separate pocket-only window starts one frame earlier with residual speed 3; the accepted complete-route window remains distinct. The full raw budgets are 653 frames at input 200 and 500 at input 400 per direction, totalling 2306 states. All four native processes completed with empty stderr.

The final goals occupy the actual floor band near the end of each stair, and these stops are wall-adjacent. They do not prove accurate stopping in the middle of an open room or identify the cause of braking. The four 384 ms pauses are part of the tested policy; they do not establish running combat fluidity. Ascent records ground contact near every tread height. Descent skips recorded ground contact on some treads while staying within the declared stair envelope, so continuous contact with every tread is not claimed. The observer retains those counts and all pause displacement/velocity states.

## Verification and scope

`python out/span-study/verify_evidence.py` rederives the hero and all 11 selected compiled audits, regenerates native inputs from the pinned hero geometry and prior timing evidence, checks raw initial states and all four first native outcomes, and verifies the final file bindings. `python -m unittest discover -s tests -p test_atrium_audit.py -v` runs the 25 synthetic controls without native execution.

The frozen native file historically uses “circuits” in its docstring; the executed trajectories are open upper journeys. The lower return remains an exact static geometric check. This experiment does not simulate the game module, execute a closed native loop, prove movement to every pickup or establish multiplayer balance. Upper chord proofs reconstruct the authored floor plane from native endpoint XY coordinates; they do not expose every internal Pmove substep.

No new native build or scratch directory was needed. Earlier harnesses and evidence remain unchanged. Useful logs and all first outputs are retained under `out/span-study`; the new Python caches are disposable and cleanup is tracked separately. No unrelated repository defect was found during this audit.
