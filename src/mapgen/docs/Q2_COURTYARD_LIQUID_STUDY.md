# Open courts and a deep-water exit

This study adds two bounded results to the [spatial-pattern research](MP_SPATIAL_PATTERNS.md): a supported upper circuit around **Manhunt**'s outdoor court, and a complete deep-basin-to-dry-connector path in **Violent & Funky** (`gladdm1`). They address the open-court and deeper-liquid fixtures identified in the [roadmap](ROADMAP.md). Exact source paths, SHA-256 hashes, waypoint records and method limits are in the [research record](../knowledge/q2/courtyard-liquid-study.json). New plans and audits remain in `out/courtyard-liquid-study/`.

These are compiled geometry and original-source findings. No game was launched, no native movement or damage was measured, and no archive geometry/assets were incorporated into generation. Reference capacities and source restrictions remain separate from design hypotheses; no profile, partition or weight was changed.

## Manhunt: a court with open and covered approaches

Steve “Scary-one” Lyne's author readme describes tight corridors combined with high outdoor areas, reachable ledges and a roughly 6–12-player LAN/FFA target. Archive metadata instead labels 5–10 players. The dimensions below are consequently a larger-map comparison, not suggested duel defaults. The readme mentions an Eraser-bot route file; its presence or historical praise is not current navigation evidence.

The analytical plan (`out/courtyard-liquid-study/manhunt-plans.png`, upstream study evidence, not imported) shows a faceted upper horseshoe around a lower space. One upper circuit is now independently checked at **floor Z 272**, using these center-XY points:

`(480,352) → (480,640) → (576,768) → (704,832) → (960,832) → (1088,768) → (1184,640) → (1184,352)`

All **eight standing poses and seven complete hull sweeps** pass against the **1,684 model-zero player-blocking brushes**, with the 32×32×56 hull and zero collision epsilon. For support, exact rational 2D separating-axis intervals identify when each horizontal compiled cap touches the moving bottom rectangle. The union covers every segment continuously. This proves a supported static route of about **1,438.217 planar units**; it neither finds the shortest route nor tests movement speed. Its swept XY region is separate from the authored door-lift shafts, teleporter pads and the two small train cycles. Dynamic states were not simulated.

The two ends have different authored rewards: a machinegun at `(480,344,304)` and Body Armor at `(1184,352,312)`. Their origins are not settled pickup centers. The circuit goes around the encounter instead of crossing its center, which offers a concrete hypothesis: a player can change approach while remaining aware of the same court. A proposed direct western connector at Y 512 was blocked by compiled geometry and was not included in this certificate.

**Sky is checked at viewpoints, not inferred from a sky count.** Upward point rays from the eight route eyes at Z 318 reach sky at Z 752 at the six exposed middle positions. Both endpoint eyes first hit a solid roof at Z 496. A separate central query at `(832,512,318)` reaches sky at Z 752; this point is a spatial query, not a supported player pose. Exact sky-face IDs and trace heights are recorded. The result is an open middle with covered approaches, rather than an assertion that every part of the court is outdoors.

Vertical access has separate evidence. Entity 74 is a `func_door` named `lift3`, with angle −2, `spawnflags=1`, speed 130, lip 34 and authored submodel bounds `(768,928,80)..(896,1056,272)`. Entity 78 targets it from a low trigger volume. Original `SP_func_door` computes distance from axial size minus lip and reverses its endpoints for START_OPEN; these bytes therefore imply a nominal 158-unit vertical translation and lower top Z 114. Nearby rendered surfaces are at Z 96. The resulting 18-unit difference is a source/geometry diagnostic, **not a verified boarding step, cycle or ordinary connection**. Another teleporter explicitly targets the court's higher platform area. These are distinct ways to change elevation with potential waiting and arrival costs. Their existence does not prove a complete ordinary lower-to-upper return.

Carry forward the relationship: an open court can host a wraparound upper path, a lower encounter, covered entry positions and a separately contested ascent. Keep a readable view into the shared space while testing whether the gallery can control too many entrances. An original compact map can use stairs or ramps for ordinary ascent rather than inheriting this reference's mover dependency.

## Violent & Funky: a complete wet route to dry floor

Todd “Mr.CleaN” Rose's `gladdm1` was previously studied for its [drop and ordinary return](Q2_DROP_RETURN_STUDY.md). That proof concerned upper routes. This study independently closes a different gap: the lower water basin and its northern exit.

Compiled water brush 64 has contents **32**, with bounds `(-64,0,-320)..(704,1024,-136)`. The selected starting bed is **Z −256**, so water stands **120 units above that bed**. The brush's 184-unit total thickness is not the playable depth. Mega-health's authored origin is `(320,512,-240)`, near the selected starting bed. The northern bed ramp has nominal rise **112 over run 320**, from Z −256 to −144, with normal Z approximately **0.943858**. The last eight units remain below the water surface before a nominal 16-unit lip reaches dry floor Z −128.

The successful route uses these points. Coordinates are center XY and actual hull-foot Z; the full audit retains exact rational heights and intermediate transitions.

| Location | XY | Foot Z | What changes |
| --- | --- | ---: | --- |
| Deep bed | (320,512) | −256 | Fully immersed starting location beneath an overhead structure. |
| Bed approach | (384,512) | −256 | Moves toward the northern sloped bed. |
| Ramp entry | (384,704) | about −228.000013 | The bed rises; a clear surfacing column becomes available. |
| Lateral shift | (448,704) | about −228.000013 | Moves within the widening ramp to avoid the early inner wall. |
| Shallow crest | (448,944) | about −144.000014 | Nearly out of the water; continuing straight is blocked. |
| Dry lip reached | (288,944) | −128 | A left turn and bounded step reach dry support. |
| Dry connector | (288,1120) | −128 | Continues to a distinct dry landing. |

The selected route has **896 planar units**, **128 units of ascent** and no descent. **188 poses and 187 sweeps** pass against all **345 model-zero player-blocking brushes**. Of the sweeps, **183** have a same-convex-solid endpoint contact certificate covering the complete segment. The other **four** form two explicit bounded step pairs: one approximately 0.000330-unit stored-plane transition at the ramp join, and one approximately 16.000014-unit dry lip. Each pair has supported endpoints, a short horizontal span and clear raise/translate sweeps; its intermediate pose may be airborne. No tolerance snaps away the compiled differences. The final segment pass rechecks clearance and support independently.

This is a **continuous supported geometric path through genuine deep water to a dry connector**, not a native swimming trajectory. Ground contact can be maintained geometrically along the sloped bed, but original Q2 uses swimming movement at waterlevel ≥2. Acceleration, buoyancy from player input, gravity, time, oxygen and stepping behavior must still be tested in the actual movement code/game.

The failed proposals are useful design evidence. Going east to `(424,512)` from the starting bed hits brush 78. Staying at X 384 too far up the inner edge fails near Y 912 against brush 241. Continuing the X 448 ascent beyond Y 944 fails against brush 152. The successful route reaches the shallow crest and then turns left. A large ramp silhouette therefore did not guarantee a generous or straight exit; the actual turn and lip matter.

## Immersion, breathing space and exit exposure

Original `PM_CategorizePosition` samples standing water at **foot+1, foot+23 and foot+46**. Exact membership tests along the supported poses give 99 fully immersed poses, nine at level 2, fifteen at level 1 and 63 dry poses. These are deliberately uneven geometric sample counts, **not percentages of travel time**. Boundary membership is flagged; no engine state is integrated. Crouched sample heights differ, as documented in the preceding study.

A stronger air-space check asks whether the entire standing hull can rise vertically until its eye is one unit above the Z −136 water surface. That target has feet at Z −181 and hull top at Z −125. At the starting XY `(320,512)`, the sweep hits brush 179; the upward point ray first reaches an underside at Z −144, below the water surface. At `(384,704)` and `(448,704)`, the same geometric surfacing sweep is clear, with an overhead point hit at Z −32. Thus the route contains an identifiable emergence zone after an initially covered underwater location. Clear air volume is part of the route contract, not something the water's plan footprint proves.

A fixed dry-exit eye at `(288,1120,-82)` sees the distinct dry-lip sample but none of the five sampled wet approach/crest eyes. Model-zero solid/window point rays were used, with water excluded from that mask. This describes one occlusion arrangement. It is not a global visibility or safety result: an exit can still be predictable, audible and contestable even when the submerged approach is hidden.

Original `PM_WaterMove` halves the clamped wish speed and uses water friction/acceleration and current terms. `P_WorldEffects` handles entry/submersion sounds, air depletion, drowning and slime/lava effects; `P_FallingDamage` depends on movement state and water level. None of those contracts turns geometric depth into a measured journey time, damage amount or safe drop. Exact source hashes and function locations are carried in the record.

## Tests for the next original example

For the proposed **Lockyard** experiment, use an elongated outdoor court with offset cover and a partial gallery, while giving the optional deeper culvert an obvious exit and a verified air space. The proposed 112-unit liquid depth is a design choice. This study makes no final Lockyard compile, runtime or balance claim.

1. Certify the ordinary dry graph with the wet link disabled. Then prove the wet entrance, submerged segment, emergence space, exit stairs/ramp, dry lip and turn as one complete route.
2. Check sky-first rays from selected supported viewpoints. Keep covered approaches if they serve a purpose; a sky-textured ceiling alone is insufficient evidence of an open court.
3. Sweep a full hull through surfacing columns. With the standing eye ten units below the hull top, an apparently visible water surface can still lack room to breathe beneath a low ceiling.
4. Record movement-state transitions and oxygen availability in native tests, separating settled start time, first input, water entry, first submerged state, emergence, dry contact and arrival. Compare a wet lane with the same compiled geometry drained, then change only one exit feature.
5. Compare one gallery or cover change at a time. Measure route choice and control of entry, ascent and exit separately; a compact footprint or a short path cannot establish enjoyable flow.

No new unrelated repository defect was found. The capacity conflicts remain: Manhunt's archive 5–10 versus author roughly 6–12, and `gladdm1`'s archive 2–5 versus author 2–4 with duel preferred. Both existing manifest entries retain a malformed-metadata-line warning; `gladdm1` also retains its Gladiators/stock-Q2 context. These observations do not repair or overwrite earlier records.
