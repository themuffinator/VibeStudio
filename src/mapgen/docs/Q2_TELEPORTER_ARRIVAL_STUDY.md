# Teleporter arrivals: commitment before escape

This bounded study checks one arrival in **Painkiller** by Sten “ztn” Uusvali and one in **Torn Glory** by Dan “CardO” Shannon. Both were already identified in the [traversal inventory](Q2_TRAVERSAL_STUDY.md). The new question is whether a standing player can leave the selected landing toward two fixed onward stations, and how destination facing relates to resources and exposure. It is not another count of teleporter entities or a whole-map safety assessment.

Torn Glory supplies two passing geometric departures, but the selected paths share **336 units before their branch**. Painkiller supplies one passing departure and a useful retained failure: its visible grenade launcher has a supported standing station, while the chosen west route clips a raised cap during its four-unit descent. These observations motivate separate arrival, commitment and branch contracts; they do not establish that either map needs a redesign.

Image: Original schematic of passing segments and retained failed departure candidates (`out/teleporter-arrival-study/arrival-departures.png`, upstream study evidence, not imported)

## Frozen scope and reproducibility

The versioned protocol (`out/teleporter-arrival-study/protocol-v1.json`, written by `python -B -m tools.map_study.teleporter_arrivals`), SHA-256 `584900ad28e6cccd3fe6e9e9cc8c973993bad45bfaeed308f5f5d57ff7c3f3b3`, was written before hull certification. Source selection used embedded entities and a bounded inspection of upward draw surfaces. Every waypoint, resource station, candidate branch and facing-ray length was then fixed. No failed waypoint was repaired or alternate path searched after measurement.

The audit (`out/teleporter-arrival-study/audit.json`, written by `python -B -m tools.map_study.teleporter_arrivals`) comes from [teleporter_arrivals.py](../tools/map_study/teleporter_arrivals.py). The [study record](../knowledge/q2/teleporter-arrival-study.json) pins the archive BSPs, author documents, protocol, analytical dependencies, tests and diagram. The record also retains a separate check that the selected routes are disjoint from the stock source trigger boxes; a trigger being absent from a solid collision mask is not proof that it cannot activate.

From the repository root:

```powershell
python -B -m unittest discover -s tests -p test_teleporter_arrivals.py -v
python -B -m tools.map_study.teleporter_arrivals --verify
python -B out/teleporter-arrival-study/record_evidence.py --verify
```

The analytical contract uses exact rational coefficients from the BSP's stored planes. It requires full **32×32×56** standing-hull sweep clearance, full swept-footprint floor support, no liquid intersection and disjoint conservative mover envelopes. Floor support admits a rectangle only when its cap corners satisfy the actual convex planes; bounding-box overlap alone is insufficient. This conservative method can reject a usable path. The fixed floor-band departure and vertical settle are geometric constructions, not simulated motion or continuously grounded native trajectories.

Model-zero blocking masks include solid, window, player clip and monster content. Liquids use mask 56. Stock teleporter source and destination pads are included as separate solid boxes, with relative bounds `[-32,-32,-24]` to `[32,32,-16]`. Moving brush entities are not run: plats use an expanded vertical envelope, rotating entities use a conservative cube about their authored origin, and the button uses a conservative translation envelope. All selected passing routes are disjoint from these envelopes. Dynamic players, monsters, item collision/settlement, telefrag outcomes and game-mod behavior are outside the certificate.

## Sources and arrival poses

| Map | Embedded source → destination | Destination origin / angle | Source-derived arrival feet | Geometric landing |
|---|---|---|---|---|
| Painkiller (`ztn2dm1`) | 11 → 12, target `t1` | `[576,-32,210]`, 180° | `[576,-32,196]` | Same feet, on the raised world cap |
| Torn Glory (`card2dm1`) | 17 → 67, target `dest1` | `[-496,-416,-208]`, 273° | `[-496,-416,-222]` | Two units down to `[-496,-416,-224]` |

Both selected targets match exactly one embedded destination. Source origins are `[-320,-136,78]` and `[216,-1136,-536]` respectively. The audited local `Quake-2-master/game/g_misc.c` supplies the stock semantics: copy the destination origin, add 10 Z, reset velocity, set the teleport hold, adjust delta angles, call `KillBox`, and relink. Standing mins Z −24 produce the feet above. This is source interpretation, not execution of those game functions in either archive map.

The destination's authored angle supplies the **fixed analytical ray orientation**. No actual post-teleport view was recorded; short-angle encoding and subsequent command state are not measured. In particular, 273° is retained rather than rounded to south. The source sets `pm_time` to 20 ticks (`160 >> 3`), but this study does not measure hold duration, recovery input or the first movement frame.

Painkiller's world cap reaches Z 196, while its stock destination pad reaches Z 194. The initial standing pose is therefore supported by world geometry above the stock pad. Torn Glory's pad top and selected world floor are both −224. Treating every destination entity origin as a universal floor-plus-24 placement would miss both local conditions.

## Passing departures and retained failures

| Map and fixed route | XY length | Height construction | Result |
|---|---:|---|---|
| Painkiller: north to room spur | 224 | Four-unit ground band, then four-unit vertical settle | Pass |
| Painkiller: west to grenade launcher | 480 | Same declared four-unit departure construction | Fail: cap clips the chosen settle and following flat leg |
| Painkiller: east wall candidate | 96 | Four-unit ground band only | Fail: blocker and missing support |
| Torn Glory: south neck then east | 464 | Flat at −224 after initial settle | Pass |
| Torn Glory: south neck then south | 416 | Flat at −224 after initial settle | Pass |
| Torn Glory: rear wall candidate | 128 | Flat at −224 | Fail: blocker and missing support |
| Torn Glory: projected Jacket approach | 464 | Flat at −224 | Fail: both legs lack full floor support |

Painkiller's north path is `[576,-32,196] → [576,32,196] → [576,32,192] → [448,32,192] → [448,64,192]`. The band proves actual ground within four units beneath the clear envelope, followed by a clear descent to a fully supported endpoint. It does not prove the engine's precise step-off motion.

The west attempt advances to `[528,-32,196]`, then tries to descend at that same XY to Z 192. The descent and following leg to `[384,-32,192]` intersect **world brush 383**, although floor coverage passes. Later segments toward the grenade launcher independently pass. This is a failed chosen cap departure, not proof that the grenade launcher is inaccessible. The failed east probe hits brush 232 and an uncovered footprint cell at X 656–688.

Torn Glory's two paths start `[-496,-416,-224] → [-496,-544,-224] → [-496,-656,-224] → [-400,-656,-224]`. From that shared station one continues east to `[-272,-656,-224]`, the other south to `[-400,-736,-224]`. The common 336-unit segment is part of both fixed certificates; it is not a shortest-path or universal compulsory-travel claim. Their branch directions differ by 90°. Counting these as two immediate independent exits at the teleporter would overstate what was demonstrated.

The rear probe hits brush 75 and loses floor coverage. The two-leg projected Jacket path has **no blocking hull intersection**, yet fails support: the first uncovered rectangle is X −416…−256, Y −432…−400, and the second is X −288…−256, Y −432…−288. Empty space and a nearby upper item marker do not supply a floor.

There are **seven fixed paths, 21 tested legs, four intended onward routes and three passing onward routes**. Fifteen individual legs pass. Both initial landing/settle constructions pass. All six named arrival/onward stations have standing support, but Painkiller's grenade station is not connected by its failed chosen route. These counts deliberately separate local segments, connected routes and supported endpoints.

## Facing and resource consequences

Fixed point rays run 256 units from eyes 46 above the chosen feet, at authored yaw and ±90°. They use the BSP point clipper, mask 3 and plane comparison tolerance `1e-5`, plus stock pad intersections and mover-envelope exclusions. They are not hull sweeps. “Clear” describes that one sampled ray, not a safe firing cone or an escape direction.

| Supported station | Connected by the selected arrival route? | Forward / left / right rays |
|---|---|---|
| Painkiller arrival | Yes | Clear / blocked / blocked |
| Painkiller north spur | Yes | Blocked / blocked / clear |
| Painkiller grenade station | **No** | Clear / clear / clear |
| Torn Glory arrival | Yes | Clear / clear / blocked |
| Torn Glory east endpoint | Yes | Clear / blocked / clear |
| Torn Glory south endpoint | Yes | Blocked / blocked / blocked |

Nine of 18 facing rays are clear; only six of those nine originate at stations reached by the selected passing routes. This prevents the disconnected grenade endpoint from being counted as certified arrival exposure. Another 12 rays target **authored item origins**, six clear. Item origin height is not a settled pickup-contact result.

Painkiller's grenade launcher (entity 35) is authored at `[128,0,208]`; the separately supported station is `[128,0,192]`. It is visible from arrival and the reached north spur, even though the attempted west approach fails. Torn Glory's Railgun (entity 36) at `[320,-496,-208]` is visible from arrival but blocked from both reached branch endpoints. Its Jacket (38) at `[-272,-176,-208]` is blocked from arrival but visible from both branch endpoints. Mega-health (28), authored at `[-736,-712,-368]`, is blocked from all three sampled Torn Glory stations. Its selected support floor is −384, **160 units below** the upper floor. The Railgun, Jacket and Mega standing stations pass independently; none has a connected approach certified by these paths, and no pickup contact is tested.

The useful design inference is to give an arrival a deliberate recovery pocket, measure the travel shared before real branch choice, and verify resource approaches separately from visibility. A generator should retain failed branches and pose/settle assumptions as typed evidence. A standing landing and two clear eye rays alone should not satisfy a “two ordinary escapes” requirement. Neither the sampled 336-unit commitment nor any sampled ray length becomes a universal design target.

## Provenance, rights and limits

The sources are the local curated **reference** partition, not held-out validation or a fresh empirical quality benchmark. Painkiller has split group `50259728799005a8`; Torn Glory has `edeff59a840ff1a0`. The existing registry marks both training-ineligible with unreviewed local-reference rights; this study does not alter those statuses. Both metadata files retain the prior malformed-line-12 observation. Torn Glory's author recommends duel or 3–4 FFA, while registry metadata records 2–6; those are source labels, not a measured capacity result.

- Painkiller BSP SHA-256: `2f8a3bab196b20193057919d600e91eca6a97322e8e0a3429abe2115f71ef0dc`.
- Torn Glory BSP SHA-256: `fff80700ce171e611066bdd09b84bc2d0bb421ca736fe1eaf6b3dec7398b2c7f`.

The local author readmes were read and pinned. Painkiller permits specified noncommercial electronic distribution only without modification and with its readme; Torn Glory forbids use as a base for additional levels and limits redistribution/commercial use. No map, texture, mesh or raw archive asset is copied into the generator or this output. The figure is an original schematic of query coordinates and outcomes, not a map reconstruction. No weights are trained, no emails sent, and no reference installation is modified.

The [12 original analytical controls](../tests/test_teleporter_arrivals.py) cover full-hull width, head obstruction, real pad solidity, shallow settlement, missing support, unsupported diagonal/deep-drop protocols, mover/liquid exclusion, local-origin rotation envelopes, target ambiguity and exact authored yaw sampling. They validate the test contract, not Quake II gameplay. No native archive run, human match, item pickup, telefrag, timing comparison, sound or whole-map balance judgment is claimed. Existing studies and their failed observations remain unchanged.
