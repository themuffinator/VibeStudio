# A drop with a demonstrated ordinary return

This study closes one missing measurement from the [continuous-movement study](Q2_CONTINUOUS_MOVEMENT_STUDY.md): a complete geometric return from the `gladdm1` drop landing to its upper departure ledge. **Violent & Funky**, by Todd “Mr.CleaN” Rose, provides a useful example of a descent whose return crosses other spaces and changes exposure. The result is one demonstrated route, not a shortest-path result, native movement test or recommendation to reproduce its dimensions.

The [machine record](../knowledge/q2/drop-return-study.json) binds the reference BSP, readme, metadata, original movement source and analysis tools to exact local SHA-256 hashes. The analytical plan (`out/drop-return-study/gladdm1-drop-return.png`, upstream study evidence, not imported), complete waypoint/sweep audit and width/LOS observations remain in `out/drop-return-study/`. The archive was read only. Existing studies, partitions and profiles were not changed; no reference geometry or assets were incorporated into generation and no weights were trained.

## The complete route

The earlier candidate began at standing origin **(704,512,408.125)**, crossed to **(656,512,408.125)**, then descended to **(656,512,136.125)**. This study replaces the clearance offset with exact compiled support: origins **(704,512,408) → (656,512,408) → (656,512,136)**. Both piecewise hull sweeps are clear. The departure foot is Z 384; the landing foot is Z 112, on an interior stair. This is a **272-unit drop-space candidate**, not a verified ballistic trajectory or safe fall.

The return follows these route points. Coordinates are player-center XY and hull-foot Z; exact rational elevations, all 554 poses and the 553 connecting segments are in `gladdm1-return-audit.json`.

| Route location | XY | Foot Z | Ordinary transition |
| --- | --- | ---: | --- |
| Drop landing | (656,512) | 112 | Seven 16-unit descents lead west toward the central floor. |
| Central crosswalk | (320,512) | 0 | Continue west, then climb eight 16-unit stair changes. |
| West landing | (−160,512) | 128 | Turn north toward the lower end of the west ramp. |
| Northwest connector | (−160,1120) | 256 | The west ramp covers the nominal 128→256 transition over 384 units of run. |
| Northeast connector | (800,1120) | 256 | Cross the north connector, then turn south toward the east ramp. |
| Upper east landing | (800,512) | 384 | The east ramp covers the nominal 256→384 transition over 384 units of run. |
| Departure ledge reached again | (704,512) | 384 | Approach the same ledge from its upper shelf. |

The path has **3,088 units of planar length**, **384 units of accumulated ascent**, and **112 units of additional descent after the drop landing**. These lengths describe this chosen route. The analysis neither finds nor excludes a shorter ordinary return. Exact compiled slope heights retain small stored-plane differences from nominal authored heights; they were not snapped away.

The embedded entity list contains no `func_*`, `trigger_*`, or `misc_tele*` mechanisms. This particular demonstrated return uses walking space, stairs and ramps, with no required lift, teleporter or jump edge. The map’s largely symmetric organization remains a property of the reference, not a template for the original asymmetric examples.

## What the geometric certificate proves

The current `tools/map_recipes/geometry.py` reads the actual compiled convex brushes and preserves stored plane coefficients as exact rationals. Model zero contains **345 brushes selected by the player collision mask**: solid, window, playerclip and monster contents. All pose and sweep checks include the supporting solids. Collision tolerance is zero; positive-volume overlap is rejected while mere boundary contact is allowed.

Every ordinary pose uses a 32×32×56 standing hull. The earlier conservative plane-expanded probe proposes a supporting brush, but it does not certify the result. Exact convex clipping finds the support height; stationary hull checks and complete translated-box SAT sweeps then check every route segment against the compiled world. At tread boundaries a conservative cast can select a brush touched only at an edge. That contributes no positive support area, so the new helper resolves an actual neighboring tread instead of accepting the proposed contact uncritically.

The path contains two explicit forms of support evidence:

- **515 continuously supported segments.** The same convex solid contacts the translating bottom rectangle at both endpoints. Interpolating the two contact points stays inside that convex solid and inside the moving bottom rectangle for the full segment. The independent swept collision check excludes intervening hull penetration. Shared-boundary contact is retained exactly where adjacent floor pieces meet.
- **38 segments forming 19 bounded step pairs.** Each pair has a supported source and destination, a height change no greater than 18 units, a horizontal span no greater than 8 units, and clear raise/settle and horizontal sweeps. The intermediate pose can be airborne. Fifteen pairs account for the authored 16-unit stair changes; four preserve compiled ramp-join differences below 0.001 map unit. These are explicit geometric stepping constructions, not sampled native `Pmove` results.

Actual convex support-profile vertices and leading/trailing hull-edge offsets are included as route knots. A direct diagonal between otherwise valid stair poses can cut a tread; such a segment is rejected before the bounded two-leg stepping construction is considered. Clearance alone never substitutes for support.

The exact route therefore demonstrates an ordinary **geometric** return. It does not reproduce engine acceleration, friction, stepping decisions, gravity, live entity collisions, input steering or fall damage. The original local `qcommon/pmove.c` supplies the 18-unit step and 0.7 ground-normal rules, and its standing bounds are −16/−16/−24 through +16/+16/+32. `game/p_client.c` dispatches the player through `gi.Pmove`; `game/p_view.c` determines falling damage using velocity change, water and flags. Their exact files and hashes are carried forward and freshly checked in the machine record. No native trajectory or damage conclusion is made here.

## Width and exposure along the return

Six transverse stations were checked using exact standing-hull poses, support heights and continuous lateral sweeps. At the east landing tread, central crosswalk and west stair, a 256-unit surface span permits **224 units of checked center movement**, from −112 to +112 relative to the route. At each climbing ramp and the north connector, a 192-unit surface span permits **160 units of checked center movement**, from −80 to +80. The remaining 32 units account for the player footprint.

These are station measurements, not a minimum-width certificate for every possible return trajectory. A 24-unit-long tread does not support the entire 32-unit footprint longitudinally on one brush; adjoining treads and the exact maximum support height determine its standing pose. That distinction prevents “tread narrower than player” from being confused with a blocked stair.

For one reproducible exposure observation, an eye at **(704,512,430)** looks toward route eyes 46 units above the local hull-foot height. Fifty samples are placed every 64 units of planar travel, plus the endpoint. Model-zero solid/window point traces yield:

| Return section | Clear rays from the departure eye | Interpretation to test |
| --- | ---: | --- |
| Interior stairs and crosswalk | 6/13 | The immediate escape crosses exposed and obscured positions. |
| West climb | 0/10 | The opposite climb breaks this particular ledge’s direct sightline. |
| North connector | 3/15 | Much of the cross-map movement remains obscured from this eye. |
| East climb | 9/9 | The final climb reappears in the departure ledge’s sightline. |
| Shelf approach | 3/3 | The final approach is directly visible from the sampled eye. |

The total is 21/50 clear rays. This is a sampled, fixed-source, omnidirectional point-LOS observation. It is not a percentage of combat exposure time, a global safety score or proof that a defender can hold the route. Weapon splash, other viewpoints, players, facing direction, sound and movement timing remain unmeasured.

## Rules to carry into generation

1. **A useful descent needs a complete return policy.** Save the landing, every ordinary transition and the actual upper endpoint. A reachable ramp somewhere else in the map is insufficient evidence.
2. **Measure commitment separately from connectivity.** This route exists, yet includes further descent and a long climb around other space. Compare the return’s measured time, exposure and resource opportunities with the shortcut’s reward during play; do not adopt 3,088 units as a target.
3. **Let recovery and re-entry have different sightlines.** This example’s opposite climb is hidden from the chosen ledge while the final climb is visible. A connected loop can still have a predictable final approach. Test an alternate approach or one sightline screen before adding unrelated geometry.
4. **Keep support and collision evidence separate.** Export exact hull-foot poses, support-brush IDs, interval support certificates and full sweeps. Record bounded stepping transitions explicitly; do not present floating intermediate poses as continuously grounded travel.
5. **Use station width as a diagnostic, not a rating.** Check turns, exits and narrow sections in addition to straight stair/ramp lanes. Test lateral travel under contest and whether the extra width creates a usable choice.

Concrete single-change tests for an original layout: remove only the drop edge and retain ordinary reachability; move only a landing screen and remeasure ledge/landing rays; add only a second final approach and compare interceptions; or hold endpoints and item placement constant while changing one return stair to a ramp. Failure signals include unsupported intervals, an over-18-unit required step, blocked landing turns, ordinary reachability that depends on the drop itself, or an escape whose only re-entry can be held without a meaningful tradeoff. These are playtest hypotheses, not outcomes inferred from the reference.

## Provenance and unresolved limits

The BSP is `E:/Development/q2maparchive-wip/maps/gladdm1/pack/maps/gladdm1.bsp`, SHA-256 **`6f6af2acdd5108b3dea18ae181358dc81708b6540aab5a0a6417f3c98a4feec1`**, matching the previous study. The author’s readme recommends duel, allows 2–4 players, and identifies construction for The Gladiators while also describing stock-Q2 deathmatch support. Archive metadata instead says 2–5 and has an existing malformed-line warning. These capacities and ruleset contexts remain distinct; the stock BSP study does not certify a mod’s runtime behavior. The readme’s distribution/modification restrictions remain intact, and no training licence is assumed.

This study closes only the `gladdm1` ordinary-return gap. The previously studied `trdm05a` landing-to-ramp return and its `trdm05`/`trdm05a` readme revision mismatch remain unresolved. Existing research outputs and archive records retain their original status. Read this result alongside the [design playbook](MAP_DESIGN_PLAYBOOK.md), [route-diversity study](Q2_ROUTE_DIVERSITY_STUDY.md) and [mechanism traversal study](Q2_TRAVERSAL_STUDY.md).
