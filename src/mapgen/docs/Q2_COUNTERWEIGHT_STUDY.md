# Counterweight study — 13 September 2026

Counterweight explores faceted encounter boundaries and optional traversal
mechanisms in an original five-room Quake II deathmatch layout. Its eight
passage routes, five eight-tread stairs and occupied floors at 0, 128 and 256
units provide ordinary circulation; one native lift and a one-way Service
Bay–Kiln teleporter offer different commitments. The intended population is
4–6 players, with a two-player stress condition in the proposed evaluation.

The [recipe](../tools/map_recipes/counterweight.cpp) is explicit source-based
construction. General principles came from the local archive and original Q2
game-source study; no reference geometry, game assets or external implementation
were copied into the map, and no model weights were trained. This example remains
outside reference-profile calibration. Exact artifact identities and separate
evidence categories are retained in the
[experiment record](../knowledge/q2/experiments/counterweight-v1.json).

## Research translated into a layout

The [traversal study](Q2_TRAVERSAL_STUDY.md) examined Painkiller, Torn Glory and
Bio Rust. Painkiller supplies contrasting ascent arrivals; Torn Glory combines
faceted spaces with a directed height shortcut; Bio Rust supplies several
physical lift approaches without a teleporter. These observations suggest tests,
not copied layouts or proven causal rules. Reference AAS files have zero
reachabilities, and Torn Glory/Bio Rust have unresolved author/archive capacity
differences. Those limitations and source hashes remain in the research record.

| Space | Intended role | Occupied floors |
| --- | --- | --- |
| A · Counterweight Hall | Faceted rocket encounter, middle shotgun return and upper armor; optional direct lift ascent | 0 / 128 / 256 |
| B · Kiln | Lower super-shotgun/Mega recovery, upper chaingun and teleporter arrival | 0 / 128 |
| C · Spool | L-shaped middle rail encounter and upper armor route | 128 / 256 |
| D · Switchroom | Middle Quad contest and upper grenade route linking Spool to Hall | 128 / 256 |
| E · Service Bay | L-shaped hyperblaster recovery route, ascent toward Switchroom and committed teleport source | 0 / 128 |

The chamfered Hall corners and octagonal low machine alter the encounter
cross-section. Filled corners make Spool and Service Bay actual L-shaped
footprints. Each stair has eight 36-unit treads and 16-unit rises; the five runs
do not repeat a staircase at every floor in every room. Kiln uses warmer light
values and Switchroom cooler ones, but material/light declarations do not prove
that players recognize these spaces in a rendered match.

The Hall has **five incident passages**, counting its separate lower and middle
links to Kiln. Spillway's maximum was four. Counterweight has eight passage edges
versus ten and cycle rank four versus five; both room graphs have no articulation
room. This is broader shape/traversal exploration, not evidence of more route
choice or a controlled improvement. Hall control and traffic concentration need
particular scrutiny.

## Mechanisms have ordinary alternatives and explicit limits

All **264 spawn-to-gameplay-anchor paths** also pass with jump, lift and
teleporter edges excluded. The accepted ordinary policy is walk, step and bounded
drop; it is not a claim that every journey uses stairs alone. The full navigation
graph additionally contains two directed lift edges and one directed teleporter
edge. Those edges represent modeled possibilities, not observed activation.

The lift is a 128×128×16 platform. Its compiled upper brush spans Z=240–256;
native `height=240` implies a lower standing surface at Z=16 and an upper one at
Z=256. Boarding from the Hall floor requires a normal 16-unit step. It has no
external trigger/targetname, preserving stock touch activation. The
[mechanism contract](Q2_MECHANISMS.md) explains its source-defined lower starting
station, three-second top return delay, occupancy postponement and blocked-motion
reversal. These are source semantics, not measured cycle times. No live edict
dump was available to observe its actual initial pose.

The Hall has no adjacent 128→256 stair. Its ordinary upward alternative goes
through Spool and Switchroom before returning along the upper Hall connection.
Compare that full journey with waiting, boarding, riding and leaving the lift;
do not describe the bypass as cost-free redundancy.

The source disc is centered at `(544,-96,24)` in Service Bay. Its canonical touch
volume is `(536,-104,32)..(552,-88,48)`. The unique Kiln destination origin is
`(-832,192,152)`, facing +X; the native player arrival origin adds ten Z units,
giving `(-832,192,162)`. Source inspection says velocity is cleared, a brief hold
applies and occupied arrivals can telefrag. The chaingun lies about 97 XY units
ahead of the destination, while a Kiln upper spawn is about 113 XY units behind
it. Those are straight-line descriptors, not travel times. Test stacked resource
acquisition, defender control, back-to-back arrivals and spawn-to-arrival exposure;
zero spawn-to-spawn LOS does not cover this interaction.

## Static and dedicated-engine evidence

The final source contains 169 brushes: 168 world collision solids plus one lift
brush. It compiles to sealed IBSP38 with full VIS and `extra4` lighting, one bounce
and soft shadows. Structural, navigation, point-visibility, analytical-lighting
and timing findings are zero. All final texture names resolve to stock Q2 assets.

Standing player hulls at 33 compiled gameplay anchors pass 5,544 world-prism
intersection checks. The collision method uses exact vertical convex prisms
against AABBs, so chamfers are not replaced by their enclosing boxes. All 40
stair-center hull samples pass. Mechanism checks separately inspect lift shaft
and passenger clearance, supported discs and the native source/arrival poses;
their detailed counts and policies are retained in `out/counterweight/verification.json`.

| Descriptor | Spillway | Counterweight |
| --- | ---: | ---: |
| World diagonal | 2,952.1 | 2,643.3 |
| Upward world surface area | 3,461,568 | 2,804,480 |
| Combat-anchor elevation p90–p10 | 288 | 256 |
| Sampled world sightline median | 215.0 | 240.0 |
| Sampled world sightline p90 | 555.3 | 668.7 |
| Visible compiled spawn-eye pairs | 0 / 28 | 0 / 28 |
| Median straight-line spawn-to-nearest-weapon distance | 348.0 | 251.9 |

The smaller footprint coexists with longer sampled sightlines. It is not
uniformly tighter or more sheltered. Upward area includes trim; point LOS omits
dynamic actors/mover states; straight-line distance is not journey time. Multiple
geometry, item and traversal variables changed, so comparisons cannot isolate
the benefit of faceting, lifts or teleporters.

The [dedicated smoke harness](../tools/map_recipes/ENGINE_SMOKE.md) passed on this
BSP with the pinned Q2REPRO r4580 executable and legacy API 3 module. It initialized
entities with zero inhibited, completed 40 scheduled 10 Hz server frames after
two settling frames, and reported no matched item/entity initialization failure.
It exited normally, with no connected clients, masters or network packets.
Exact settings, hashes, engine log and original entity dump are retained in
`out/counterweight/engine-smoke/`. The gate was also checked against a deliberately
buried pickup in an independent negative fixture, which the native engine rejected.

This test launched no client, created no window/render capture, and read or
controlled no keyboard/mouse input. It does not establish lift motion, teleport
use, player traversal, pickup contact, visual readability or multiplayer balance.
All 21 repository tests passed. After the final shared recipe metadata change,
all four focused recipe tests passed again, followed by the complete Counterweight
build pipeline. The analytical preview passed 24 states across three viewport
widths, light/dark themes, and overview/individual floors. It is a diagram, not
an engine render. Test logs and final preview hashes are bound in the experiment
record.

The final full build changed lighting bytes while source geometry and the
compiled analysis remained unchanged. The smoke test and final preview evidence
are bound to the exact delivered BSP hash in the experiment record. Compiler
output is not claimed to be byte-stable across repeated builds.

## Matched-start 2×2 play regimen — proposed, not executed

Use four conditions with the same world geometry, items, timers, spawn inventory,
movement settings and game module: lift off/teleport off, lift on/teleport off,
lift off/teleport on, and both on. Implement activation toggles in a separately
hashed test module so disabled teleport discs retain their model/collision and a
disabled lift stays at its lower station. Adding a lift `targetname` is not an
equivalent toggle: stock behavior changes its initial station. Each condition
needs its own verified activation state and provenance before data collection.

First use matched journeys. Start the lift comparison from the same covered Hall
approach and finish beyond the same upper exit; the off condition takes the full
Spool–Switchroom ordinary route. Run ten paired trials for each active-lift
starting condition: bottom, rising, top, descending and occupied. The corresponding
off trial starts the player and defenders at the same positions with the disabled
platform parked below; a moving-platform state is not applicable to that trial.
Measure arrival-to-boarding
wait, upward ride duration, top-arrival-to-exit time, and total start-to-goal time
separately. For the off condition, ride/wait are not applicable rather than zero.
Record failed boarding, obstruction/reversal, damage and defender sight of each
phase. Repeat blocked and occupied cases under supervision after basic clearance
is established.

For teleport comparisons, use the same Service Bay start and Kiln onward goal in
both conditions. Separate approach, teleport hold, arrival-to-exit and full
journey time. Repeat with a vacant exit, a defender ahead, a defender at the
nearby upper spawn, and paired arrivals. Record telefrags, failed arrivals,
post-arrival damage/deaths within three seconds, chaingun acquisition and which
onward route is chosen. Reset health, ammo and timers between paired trials.

Then run the full four-condition set separately at **2, 4 and 6 players**. Four
and six test intended capacity; two probes empty travel and concentrated control.
Use four matched blocks per population, one four-minute round per condition in
each block. Rotate condition order and starting positions across blocks; keep
participants/familiarity and rule settings recorded. Compare within each
population before pooling anything. Record route and mechanism use, Hall traffic,
congestion, empty travel, recovery after death, sustained upper control, resource
acquisition and post-arrival deaths. Retain failed trials and variation alongside
medians; do not promote a rule from a small apparent difference or one match.

These are human playtest preparations. No input automation is authorized or
performed by the study. Any later automated input control needs the project's
specific permission; rendering evidence must use a windowed engine's own
registered screenshot command. Remaining research issues include the reference
capacity conflicts and the distinction between authored parameters and actual
game-module behavior.
