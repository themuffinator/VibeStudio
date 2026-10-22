# Talus — continuous movement and connected height

Talus is an original Quake II deathmatch layout intended for 4–6 players. Six
unequal spaces share nine passages, four stair runs and two continuous ramps.
Occupied floors sit at 0, 128 and 256 units, with two occupied heights within
each room. Galleries occupy only part of each room; the lower space stays useful
for approach, recovery or fighting across height.

The [recipe](../tools/map_recipes/talus.cpp) develops the vocabulary recorded in
the [continuous movement study](Q2_CONTINUOUS_MOVEMENT_STUDY.md). It is explicit
construction using the project core, not evidence of trained weights or automatic
motif selection by the default planner. No archive geometry or game assets are
included. Exact artifacts and evidence are bound in the
[experiment record](../knowledge/q2/experiments/talus-v1.json).

## Design decisions

| Space | Encounter and route purpose |
| --- | --- |
| A · Bench | Low rocket/Mega contest around an octagonal machine and separate screen. A west-side ramp rises toward the north connection; the east rim offers a second onward route. |
| B · Crusher | Lower super-shotgun encounter with a tall offset baffle. The north/east gallery carries chaingun pressure and a route toward Relay. |
| C · Headworks | Railgun on the middle floor, armor on an upper gallery. A west-to-east ramp changes approach direction from Bench's northbound ramp. Two exits toward Relay operate at different heights. |
| D · Relay | Quad beneath a partial upper gallery, with grenades above. Its stairs and three distinct neighboring approaches make height control a rotation decision. |
| E · Cableworks | Southern shotgun/health recovery, stairs leading to the elevated Apron link, and a ground return toward Bench. |
| F · Apron | Armor on the west gallery and hyperblaster on the east. The low route returns to Crusher while the middle route connects Cableworks. |

The maximum incident-passage count falls from Counterweight's five to four.
Cycle rank remains four and neither graph has an articulation room. Talus has
more rooms and passages; a count alone does not prove richer combat. The intended
benefit is distributing recovery and upper-route access across several encounters.

Each ramp is 160 units wide, with an 8-unit entrance lip and a 120-unit rise over
a 480-unit run: a 1:4 incline. Its landings connect floors separated by 128 units.
The ramps are real sloped brushes and continuous navigation surfaces, rather than
subdivided steps. Four other climbs use eight 36-unit treads with 16-unit rises.
Passages are 144 or 160 units wide and 192 high. Ordinary under-gallery space has
104 units of headroom where the gallery is 24 units thick.

The core and an independent traversal of the serialized graph both verify all
264 spawn-to-gameplay-anchor pairs with **only walk, step and door edges allowed**;
Talus has no door entities, lift or teleporter. Jump and drop edges are excluded.
Thus a ledge descent is not needed to repair missing ordinary connectivity.
There is no claim yet about native drop trajectories, damage or pursuit balance.

Stock cut-stone, mine and metal materials distinguish this quarry experiment
from Counterweight's industrial palette. Warm Bench and cooler Headworks lights
are an authored orientation cue. Recognition and visual readability remain
render/play questions rather than conclusions from material names.

## What the checks taught us

**Usable ramp width needs collision evidence.** Vertical Asylum's rendered incline
looked broad, but a lateral lane collided with actual side geometry. Talus checks
the center and two lanes at ±56 units. It also sweeps laterally across the supported
lower and upper landing stations, 48 units beyond each end. This verifies those
specific routes, not every possible movement trajectory.

**Slopes need real support functions.** A standing 32×32×56 hull rests on its uphill
bottom edge. The first fully supported position on Talus's lower ramp has feet at
16, rather than the plane's center height. Navigation keeps that height function
and requires ordinary contacts at both ends. QBSP adds tiny axial crest caps;
the compiled audit preserves their exact support breakpoints instead of smoothing
through a solid or ignoring a collision. The [ramp contract](Q2_RAMPS.md) records
the supported geometry and conservative limits.

**Materials can change physics.** The initial `e2u1/mud1_1` choice carries WAL
WATER contents and WARP surface flags. It made 26 architectural brushes, including
ramps and stairs, non-solid after compilation. The compiled stair support gate
failed. Replacing it with `e2u1/mine04_1` fixes the material choice; new WAL and
compiled-content checks prevent that failure from passing unnoticed. The rejected
material's verified metadata is retained in the experiment record.

**Visibility policies must match before comparing reports.** The core formerly
tested spawn body origins while compiled research tested Q2 eyes at origin+22.
Q2 core spawn samples now use the same eye offset and report it explicitly. Other
entity targets remain at their origins, and historical reports retain their old
policy. Conservative convex occlusion and dynamic actors remain separate limits.

## Evidence on the delivered map

The sealed IBSP38 contains 174 SOLID world brushes. Full VIS and final `extra4`
lighting with one bounce and soft shadows complete without empty lightmaps.
All 33 gameplay anchors pass 5,742 exact hull-versus-brush checks, and all 32 stair
centers have compiled support and standing clearance. The two ramps pass 92
continuous translations, including ascent/descent in three lanes and landing
turns, checked against all 174 player-blocking world brushes. Their supporting
brushes participate in the collision tests; there is no collision epsilon.

| Descriptor | Counterweight | Talus |
| --- | ---: | ---: |
| Rooms / passages | 5 / 8 | 6 / 9 |
| Maximum incident passages | 5 | 4 |
| World diagonal | 2,643.3 | 2,793.5 |
| Sampled world sightline median | 240.0 | 228.0 |
| Sampled world sightline p90 | 668.7 | 561.2 |
| Visible compiled spawn-eye pairs | 0 / 28 | 0 / 28 |
| Straight-line spawn-to-nearest-weapon median | 251.9 | 320.4 |

Talus has shorter sampled sightlines but a larger footprint and a longer median
straight-line weapon distance. It is not uniformly more compact or faster to
recover. These are descriptive comparisons across multiple changed variables;
they do not isolate the effect of ramps. Planner costs include endpoint approaches
and slope rise but remain graph/Manhattan estimates, not measured travel times.

The pinned dedicated Q2REPRO smoke harness passes all 16 gates: map/entity loading,
40 scheduled legacy 10 Hz server frames after two settling frames, no matched
initialization failure, normal exit, no connected clients and no network packets.
This establishes loading and initialization only. The analytical floor viewer is
a diagram, not an engine render. Exact verification, test and preview records are
listed in the experiment record.

All 22 repository CTests passed, followed by the final 24-test compiled geometry
suite. The viewer passed 24 viewport/theme/floor states. Rebuild with
`tools/map_recipes/build.ps1 -Recipe talus`; the script expects the separately
installed, pinned compiler tools and stock assets described in the repository.
The test logs bind the delivered evidence; compiler lighting bytes are not
claimed to be deterministic across rebuilds.

## Bespoke next tests — defined, not executed

1. **Ramp versus stairs, fixed endpoints.** Keep the same map, items, corridor
   widths and landing footprints. Replace only one 480-unit ramp with eight
   60-unit treads rising 16 units each; both variants finish 128 units above the
   start. Compile and verify each variant independently. Use matched starts 48
   units outside the ends, run ten paired uphill and downhill journeys per lane,
   counterbalance order, and record time, wall contacts, missed exits, aim tracking
   and damage from an identically placed defender. Separate each ramp's result.
2. **Landing width.** Hold the incline fixed. Change only one landing's clear turn
   width while keeping its endpoint and onward doorway. Repeat lateral entry,
   reversal and contested two-player passing. Measure collisions and route choice;
   do not assume a wider platform improves combat.
3. **Drop and return.** Instrument one Headworks gallery descent and record the
   actual native trajectory, landing velocity, health change, water state and
   deathmatch flags. Check the landing and the ordinary return via the ramp. Then
   compare a closed ledge with the same items and spawn state. Evaluate pursuit,
   escape and interception separately. No fixed-height harmless-fall rule applies.
4. **Recovery and control.** Run matched 2-, 4- and 6-player sessions with players
   rotating starting positions. Record spawn-to-first-weapon and armor times,
   damage before arming, repeated deaths, Quad/Mega ownership streaks, route use
   and floor use. Review lower/upper Relay pressure and Apron's long recovery path.
5. **Orientation and sightlines.** In a windowed engine session, collect images
   through the engine `screenshot` command at the ramp ends, Quad pocket and
   southern junction. Ask unfamiliar players to identify destinations and return
   routes. Validate actual lightmaps, occlusion and material identity before
   accepting the visual cues as successful.

Retain the engine/module/BSP hashes, settings and raw trial results. Promote a
rule only after its predicted outcome survives a matched comparison; preserve
failures and author/capacity conflicts in the reference records. This iteration
adds a tested construction capability and a new layout, while its gameplay
hypotheses remain open.

Remaining engineering issues outside this recipe are the public
`StairRunSpec` default of 32-unit treads and the normal Windows client's startup
focus behavior. Talus uses explicit 36-unit stairs and a dedicated smoke server.
Default-planner motif selection, supervised movement, engine rendering and
multiplayer balance remain unfinished work in the broader iteration roadmap.

Automatic approval review rejected cleanup of `.agents/tmp/talus-engine` and
`.agents/tmp/talus-viewer` as “blocked by policy”. The deletion did not execute
and was not retried. Useful logs and previews remain in `out/talus`; the temporary
folders are recorded in `out/talus/cleanup-status.json`.
