# Continuous movement, ledges and ordinary returns

This local study adds three reference-partition maps to the design skillset: **Violent & Funky** (`gladdm1`, Todd “Mr.CleaN” Rose), **Vertical Asylum** (`trdm05a`, Daniel “Trebz” Nolan), and **The Cake is a Lie** (`spirit2dm9`, Tim “spirit” Schaefer). It investigates complete ramp approaches and exits, usable width, and the distinction between dropping down and getting back up. It does not establish that a particular shape causes enjoyable play.

The [machine record](../knowledge/q2/continuous-movement-study.json) contains exact archive paths, full BSP/readme/metadata hashes, source hashes, measured coordinates and evidence-file hashes. Fresh analytical plans and raw probe results are retained locally in `out/continuous-movement-study/`. They contain schematic analysis of the references; no textures, playable geometry, weights or map-generation inputs were copied. Existing reference profiles, partitions and capacity records were left unchanged.

## What movement code actually permits

The local original Quake II source tree is `E:/_SOURCE/_CODE/Quake-2-master/`. Its player path uses **`game/p_client.c`**, not a file named `g_client.c`: `PM_trace` (1536–1542) delegates the player hull to the engine, and `ClientThink` (1570 onward, notably 1629–1663) invokes `gi.Pmove` and adopts its hull and ground state. This study has not established that every user engine/mod is behaviorally identical.

| Source and location | Relevant behavior | Design consequence |
| --- | --- | --- |
| `qcommon/pmove.c`: 25, 111, `PM_StepSlideMove` 271–330; `PM_CatagorizePosition` 671–725 | 18-unit step; ground and step-result checks use a normal-Z threshold of 0.7 | Angle alone does not establish a route. Both landings and the step/ramp join matter. |
| `qcommon/pmove.c`: standing bounds 980–1025 | Standing footprint 32×32, origin-relative bottom −24 and top +32: 56 units tall | Test the upright hull, not a point or eye ray. On an incline, its uphill bottom corner reaches the plane before the footprint center. |
| `game/q_shared.h`: contents 338–366, masks 387–393 | Player collision includes solid, window, playerclip and monster contents | A visually open lane can still be clipped. Our static checks include these world-brush contents; live entities are separate. |
| `game/p_view.c`: `P_FallingDamage` 501–594 | Damage depends on squared vertical-velocity change, water depth, and deathmatch flags; `DF_NO_FALLING` suppresses damage | Do not label a 128-unit or other fixed-height drop safe from geometry alone. Record settings and actual landing velocity/damage in engine tests. |
| `game/g_phys.c`: `SV_FlyMove` 183 onward; `SV_AddGravity` 322; toss handling around 727 | Generic entity movement also classifies floors by normal and applies gravity | This supports mechanism/entity reasoning, but is not a substitute for the player `Pmove` path. |

For an ideal rise 128/run 512 plane, normal.z is about 0.970. Talus’s constructed surface rises 120/run 480, also a 1:4 grade, beginning above an 8-unit lip. The favorable normal is only one necessary condition; neither proposal is a runtime result.

## Method and rejection criteria

The initial archive scan selected upward world faces with normal.z in [0.7, 0.995), area at least 5,000 square units and at least 32 units of rise, excluding the reader’s sky/warp/translucent/nodraw flag set. **This is a candidate filter, not a floor classifier.** Rendered trim and inaccessible roofs can pass it.

For each chosen face, the probe joins contiguous coplanar face intervals on an uphill line. It checks a standing hull along the assembled incline, extending 48 units onto each landing, with at most 8 units between poses and continuous linear sweeps between them. Ground comes from downward expanded-hull casts, and a 0.125-unit clearance offset avoids classifying mere support contact as overlap. World brushes come only from model zero’s BSP subtree. Ground normals must be at least 0.7 and all standing poses and sweeps must be clear.

The independently written helper expands brush planes by the hull’s support extent. This is a **conservative geometric test**, not the engine’s full movement solver: omitted box/edge separating axes can reject a clear convex corner, while a fully clear sweep is evidence of static clearance. It does not simulate acceleration, step snapping, friction, gravity, live entities, mover states or steering. Rejected samples remain inconclusive unless investigated further. The one reported obstructed lateral lane was also checked with the repository’s complete exact convex/AABB intersection code.

The approach is deliberately narrower than a full navigation claim. It rules out “a sloped face exists” as sufficient proof, checks supported entry and exit, and records where lateral occupancy changes the conclusion. The archived AAS data does not supply usable reachability proof here; the wider extraction found no reachability records in those sidecars.

## Three concrete observations

| Map | Assembled ramp, world units | Static evidence | Limits and route interpretation |
| --- | --- | --- | --- |
| `gladdm1` | Faces 204, 205, 269; rise 128/run 384; Z 256→384; normal.z 0.948683 | 61 supported clear center poses, 60 clear continuous sweeps. Both ±40-unit lateral lanes also pass including landings. | The ramp rises along the outer circulation toward the body-armor shelf. The plan is substantially symmetric; it is evidence for a continuous perimeter climb, not an asymmetry template. |
| `trdm05a` | Faces 87, 88; rise 128/run 320; Z 128→256; normal.z 0.928477 | 53 center poses and 52 sweeps pass; one +40-unit lateral lane also passes. The −40 lane is obstructed. | The face’s roughly 130-unit visual width overstates accessible width near side geometry. The ramp reaches the upper railgun-side height while other combat space remains below. Usable width must be measured against solids. |
| `spirit2dm9` | Faces 1788, 1789; rise 128/run 256; Z 0→128; normal.z 0.894427 | 45 center poses and 44 sweeps pass, as do both ±40-unit lateral lanes and their landings. | The local ramp is an ordinary transition inside a larger layered layout. Embedded entities also contain four push triggers, two teleporters and one platform; this is not evidence that the whole map remains connected after removing fast mechanisms. |

Raw face coordinates retain compiler precision, so computed rises differ from 128 by tiny floating-point amounts in the first two maps. Face counts describe compiler subdivision, not three or two independent ramps. In `trdm05a`, the seed face’s vertex-average center is X 327.222 rather than the visual bounds midpoint; the failed lane is X 367.222. Brush 297 has minimum X about 368.415 and overlaps that standing hull at Y 160. The exact convex check confirms a real overlap, not just the conservative probe’s corner overestimate.

The plans (`gladdm1-plan.png`, `trdm05a-plan.png`, `spirit2dm9-plan.png`) show the full world-face context, a ramp inset and the hull-support profile. Other upward faces remain unverified, and stacked plan projections can obscure lower surfaces. Numbered weapon/major-item markers resolve through each map’s `*-anchor-key.json`; those origins are embedded entity positions, not all settled pickup heights.

## Drops are directional choices, not free return edges

Two nearby ledges provide concrete candidates for deliberate descent. The label “deliberate” is a design interpretation, not a quoted statement of author intent:

- `gladdm1`: a horizontal departure from player origin (704,512,408.125) to (656,512,408.125), followed by a vertical segment to (656,512,136.125), has clear static hull sweeps and a clear landing pose. This is a 272-unit descent from the armor-side upper shelf toward the interior stair height. It does not certify a real trajectory or harmless fall.
- `trdm05a`: origins (80,352,280.125) → (80,320,280.125) → (80,320,24.125) similarly give clear segments and a 256-unit descent near the railgun/chaingun stack. The separate ordinary ramp only covers Z 128→256. This study has **not** verified the complete Z 0 landing-to-Z 128 ramp approach, so no complete ordinary return loop is claimed.

These piecewise probes test available space, not the curved ballistic path a player will take. For both references, the researched ramp itself has geometric clearance in either direction; the complete return from the drop landing is an explicit missing measurement. This is a useful failure condition for generation: a drop cannot compensate for an absent ordinary return merely because a ramp exists elsewhere.

## Reusable rules and single-change tests

1. **Represent one ramp as surface plus low and high landings.** Export real XYZ surface vertices, lip, run, rise, width and level context. Reject unsupported ends, intrusive trim or insufficient standing height. Artifact: compiled hull poses and continuous sweep evidence for the center and both lateral lanes.
2. **Assign vertical space a route role.** Use partial upper floors where they create a crossing, readable ledge or alternate approach. Leave other rooms single height when another floor adds travel without a meaningful choice. Artifact: a route-role graph and an ordinary-only reachability report.
3. **Spend width at turns and encounter entries.** A broad incline whose exit forces one exact line can still feel tight. Change only landing width/turn clearance, then compare missed turns, wall contacts, contested passage and route preference in play.
4. **Pair every useful drop with a verified return policy.** Record directed descent, landing exposure, damage settings and the ordinary route back. Disable the drop edge and retest reachability; change only its landing screen to test whether commitment stays interesting without becoming a trap.
5. **Separate travel rhythm from global safety.** Compare one continuous ramp with an equal-endpoint stair route while holding items, widths and topology fixed. Measure travel-time distributions, aim disruption, sound cues and pursuit outcomes. A smoother traversal need not produce better encounters.
6. **Vary the route’s exposure, not its slope merely for appearance.** Keep the same incline but move one sightline screen or doorway. Measure visible route distance, interceptions and time spent exposed. Do not treat a gentler grade or a larger count of height bands as a quality score.

For Talus, the immediate tests are its actual 8→128 and 136→256 ramp tops, 160-unit ramps and 144/160-unit passages, lip transitions, two lateral lanes, turns at both landings, and the ordinary return after any candidate drop. No outcome metrics are inferred in this study. Use the [design playbook](MAP_DESIGN_PLAYBOOK.md), [traversal study](Q2_TRAVERSAL_STUDY.md) and [route-diversity study](Q2_ROUTE_DIVERSITY_STUDY.md) together; a ramp is one design tool within the circulation model.

## Evidence conflicts retained

All three readmes recommend lower populations than archive maxima: `gladdm1` archive 2–5 versus author 2–4 with duel recommended; `trdm05a` archive 2–6 versus author duel, possibly 2–4 and explicitly rejecting 5–16; `spirit2dm9` archive 2–6 versus author duel/FFA 2–4. The `trdm05a` readme names `trdm05.bsp`, so exact revision equivalence is not established. Metadata parsing also reports one malformed line in each reference. These conflicts are recorded, not silently resolved. Rights remain unreviewed for training: local analysis grants no licence to copy geometry or train weights.
