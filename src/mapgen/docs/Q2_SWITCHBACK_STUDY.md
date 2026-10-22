# Switchback study — 10 September 2026

Switchback responds to Crosscurrent's repeated four-room arrangement with five unequal spaces, staggered doorways, ten connections and six stair routes. Floors at 0, 192 and 384 units serve different circulation and pickup roles. This is an original experimental recipe using the project's construction and analysis APIs. It does not train weights or make the ordinary CLI automatically generate this style.

The [recipe](../tools/map_recipes/switchback.cpp), [reproduction instructions](../tools/map_recipes/README.md) and [experiment record](../knowledge/q2/experiments/switchback-v1.json) are tracked. MAP/BSP files, compiler logs, measurements and analytical floor diagrams remain local in `out/switchback/`.

## Reference observations

The local study examined BSP floor surfaces and item locations in Aerowalk (`aeroq2`, Mattias “Preacher” Konradsson), Powertrip (`ptrip`, Dennis “headshot” Kaltwasser) and The Rage (`ztn2dm3`, Sten “ztn” Uusvali). The experiment record identifies their archive paths and hashes. No reference geometry, textures or code were copied into the recipe.

These examples distribute combat anchors across several elevations within uneven arrangements of spaces. Their sampled median world sightlines are about 136, 132 and 138 units respectively, compared with Crosscurrent's 259. Their combat elevation spreads are 352, 330 and 302 units. Those observations suggest testing shorter local connections and useful intermediate floors; they do not establish universal targets or prove why these maps play well. The archive's AAS sidecars do not provide usable reachability evidence.

## Spatial decisions

| Space | Gameplay purpose | Floors |
| --- | --- | --- |
| Turbine | Main rocket contest, partial galleries, two independent lower-to-middle ascents, upper grenade route | 0 / 192 / 384 |
| Pumpwell | Lower Mega Health recovery circuit; middle return to Turbine and Accumulator | 0 / 192 |
| Loading | Lower loop and super shotgun; a small Quad mezzanine feeds the east bypass | 0 / 192 |
| Relay | Railgun on the middle floor; upper armor route returns to Turbine through a separate doorway | 192 / 384 |
| Accumulator | Single-floor Hyperblaster space joining three middle routes outside the main atrium | 192 |

Turbine–Pumpwell and Turbine–Relay each have two spatially separate connections at different elevations. Pumpwell–Loading closes a lower triangle, while the three outer middle routes allow travel around Turbine. Direct thresholds are short; bent outer routes trade distance for an alternative approach. The third floor is concentrated in Turbine and Relay rather than repeated in every room. Different cover arrangements interrupt door-to-door sightlines and screen spawn positions.

After an initial pass produced longer graph routes, the recipe added a second ascent through Turbine and compressed XY by 8%, preserving 16-unit stair rises and all floor heights. The resulting tread depths are 33–34 units, with positive standing-hull support. This was an iterative design pass changing several variables, not a controlled experiment isolating a single cause.

There are eight spawns, seven weapon types, two Combat Armors, eight ordinary health pickups and six ammo pickups. Quad is on Loading's mezzanine; Mega Health is on Pumpwell's lower floor. The intended capacity is four to six players. No trick jumps, teleporters or movers are required by the checked routes.

## Measured comparison

| Descriptor | Crosscurrent | Switchback |
| --- | ---: | ---: |
| Room connections | 8 | 10 |
| Connected room-graph cycle rank | 4 | 6 |
| Articulation rooms | 0 | 0 |
| Occupied combat height bands, 128-unit bins | 2 | 3 |
| Combat elevation p90–p10 | 256 | 384 |
| Sampled world sightline median | 259.1 | 154.3 |
| Sampled world sightline p90 | 739.2 | 580.4 |
| Visible spawn-eye pairs in compiled world | 2 / 28 | 0 / 28 |
| Upward world surface area | 3,305,472 | 2,847,936 |

Upward surface area is about 14% smaller, but includes trim and is not an exact walkable-area measure. Parallel room-graph edges only count as useful alternatives when their real approaches and floor connections work. Point LOS does not guarantee safe respawns. Switchback's nearest-weapon median straight-line distance is 245 units, below the descriptive small-DM reference band; check whether this makes particular spawns too strong. Reference bands are comparison aids, not quality scores.

## Validation and implementation

The final 216-brush map compiles as sealed IBSP38 with full VIS and `extra4` lighting, one bounce and soft shadows. All 264 spawn-to-gameplay-anchor paths pass the typed navigation analysis. Standing player hulls at all 33 compiled anchors pass 7,128 axial brush intersection checks. Compiled spawn eyes are valid, all final material names resolve to stock assets, and the typed structural, navigation, visibility, lighting and timing analyses have zero errors and warnings. The ZIP was checked against the final BSP hash. These are static checks; no engine rendering, traversal or multiplayer match has been tested.

Core validation now recognizes distinct declared passage geometry between the same endpoint rooms. Repair preserves valid explicit bent and stacked paths when another connection needs correction. Duplicate direction, volume order and repeated-volume declarations are normalized. This key compares declared volumes, not arbitrary alternative decompositions of the same geometric union. The normal generator's stair tread depth also changed from 32 to 36 units to avoid zero interior support after navigation hull erosion.

The existing 17-test CTest suite passed, including the configured Q1/Q2/Q3 compiler and model gates. New assertions cover parallel-route validation, duplicate rejection, repair preservation and normal stair navigation. A separate `qmg_switchback_layout` CTest now reproduces the recipe and rejects new advisory findings; it passed after the final build. The pinned build script also passed end to end.

An additional analyzer limitation was found: graph travel cost omits approach distances inside the area to which an anchor binds. Distinct spawn and pickup positions can therefore report zero cost when they share a node. Raw costs are retained for diagnosis, but small differences must not be presented as faster real travel or better balance. Correct endpoint costs and verify them against engine traversal before using these timings as a ranking objective.

## Next playtest and controlled experiments

1. In a windowed engine, walk every stair in both directions and check every gallery landing and doorway with normal movement. Capture rendering only with the engine's screenshot command. Inspect players under galleries and verify pickup contact at all three heights.
2. Test each spawn's first weapon, health recovery and alternative exit. Record elapsed time and encounters, with particular attention to the nearby Relay railgun and the high Turbine spawn.
3. Run four-player and six-player matches. Record route use, repeated deaths after respawn, time spent controlling each floor, and whether one player can cycle both major items uncontested. Review the lower Pumpwell–Loading bypass and the long eastern route separately.
4. Compare one change at a time: remove only Turbine's second ascent; close only the upper Turbine–Relay route; then move only Quad off Loading's mezzanine. Hold geometry, item counts, players and match settings fixed otherwise. Use observations to accept or reject each hypothesis, not the room-graph cycle count alone.

Keep Switchback and its variants outside the reference calibration profile. Promote a rule only after repeated runtime evidence across varied layouts; retain failed variants and the reason they failed in the experiment record.
