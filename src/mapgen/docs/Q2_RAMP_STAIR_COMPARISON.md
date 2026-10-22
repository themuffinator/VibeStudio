# Talus Bench: one ramp replaced by eight treads

The controlled Talus pair passes construction, compiled collision/support and dedicated initialization checks. Both maps retain all 264 required spawn-to-anchor journeys using only walk, step and door edges. This establishes two buildable versions of the same ascent for a future movement trial; it does not establish which feels faster or plays better.

The machine-readable results are in comparison.json (`out/talus-stairs/comparison.json`, written by `python -B -m tools.map_recipes.ramp_stair_comparison`), with the retained experiment record in [ramp-stair-comparison.json](../knowledge/q2/ramp-stair-comparison.json). The published Talus baseline (`out/talus/`, written by `tools/map_recipes/build.ps1 -Recipe talus`) was read without regeneration or modification. The new MAP/BSP is `out/talus-stairs/qmg_talus_stairs`.

## Intervention and held controls

| Property | Talus | Talus Bench Stairs |
| --- | --- | --- |
| Bench ascent | One continuous axial wedge | Eight axial solid treads |
| Bounds | `(-736,-160,0)` to `(-576,320,128)` | Identical combined bounds |
| Direction and width | Positive Y; 160 units | Identical |
| Horizontal run | 480 units | Eight 60-unit treads |
| Internal top profile | Height 8 to 128; rise/run 120/480 | Heights 16, 32, 48, 64, 80, 96, 112, 128 |
| Flat approach / upper deck | Floors 0 / 128 | Identical |
| Ascent face material | `e2u1/mine04_1` | Identical material on all eight boxes |
| Other ascent features | Four stairs; Headworks ramp | Unchanged |
| World brushes | 174 | 181 |
| Rooms / connections / gameplay anchors | 6 / 9 / 33 | Identical |

The 8-unit ramp lip and 16-unit first tread are part of the intentional support-profile intervention. Matching footprint and landings does not imply matching every internal height. The Headworks ramp is still continuous; no hidden stair geometry was added to either ramp.

The [variant recipe](../tools/map_recipes/talus_stairs.cpp) compiles the unchanged `talus.cpp` recipe body with a narrowly scoped `Recipe` namespace adapter. It intercepts exactly the first known ramp call and delegates the second. Start, direction, width, run, rise, source title, prior solid count and prior stair count are checked; source drift fails instead of silently changing the experiment. The macro exists only around that include. The shared helper and original recipe remain unedited.

The source audit proves 173 complete brush blocks unchanged, including plane points, texture projections, materials and face metadata. All 81 nonworld entities, including the 33 gameplay anchors and 48 lights, are identical. Worldspawn changes only its display title. The compiled audit independently finds the same 173 unchanged brushes by stored plane coefficients, complete texinfo values and contents; exactly one wedge is removed and eight expected boxes added. Native brush/entity IDs may change, so signatures normalize indices without discarding geometry or material data.

### Material confound caught and removed

The shared palette originally converted the seven intermediate tread tops to `e1u1/stairs1_1` and the last top to `e1u1/grate1_5`, despite the retained typed ramp material role. That would have introduced a second authored variable. The adapter now reads the generated variant through the project's native `MapParser`, identifies the eight exact six-plane boxes, checks every expected face and changes only those eight top materials back to `mine04_1`. `MapDocumentWriter` validates the resulting document before writing. The comparison checks both final source materials and actual compiled brushside materials, so an older BSP with the wrong textures cannot pass.

All architectural BSP brushes have `CONTENTS_SOLID`. Every referenced WAL in this pair has zero contents and no warp/translucency flags. Keeping material identities, light entities and compiler settings fixed still cannot keep illumination or rendered images identical: new face orientations and subdivisions change lightmaps and bounced lighting. No rendered equivalence is claimed.

## Validation and observed differences

The project `windows-clang` recipe target built and `qmg_talus_stairs_layout` passed. The pinned ericw alpha11 QBSP leak test, full VIS and extra4 lighting with one bounce completed. Compiler commands and SHA-256 pins are retained in the comparison, alongside source/core/tool hashes. Both artifacts use the same core library hash. Both dedicated Q2REPRO initialization reports passed 16 checks and are bound to their respective BSP hashes. These server-only smokes do not run a connected player through the ascent.

The independent [comparison harness](../tools/map_recipes/ramp_stair_comparison.py) checks the compiled world, not just the planned node graph:

- The same 32 × 32 × 56 standing hull clears all 33 gameplay anchor positions: 5,973 brush/hull pairs in the variant.
- Three lanes at X = -712, -656 and -600 have supported lower/upper stations and full-footprint support before and after every tread. Each rise is 16 units, below the planned/native 18-unit step bound.
- The stairs have 27 continuous geometric segments per lane in each direction, plus four lateral landing sweeps: 166 total. All 30,046 segment/world-brush pairs are clear by nonoverlapping or touching swept bounds; no further convex SAT test is needed for this axial route. Boundary touching is allowed, while positive-volume intersection is not.
- The baseline's two genuine ramps and the variant's remaining Headworks ramp pass the separate stored-plane support and continuous hull-sweep audit, including compiler crest caps.
- In-memory entity-change and intervention-material-change negative controls are rejected for the intended reason. Neither artifact is modified by those controls.

| Typed graph property | Baseline | Variant |
| --- | --- | --- |
| Nodes / directed edges | 106 / 359 | 113 / 387 |
| Walk / step edges | 194 / 82 | 194 / 98 |
| Optional jump / drop edges | 18 / 65 | 22 / 73 |
| Required journeys using only walk, step, door | 264 / 264 | 264 / 264 |
| Articulation rooms | 0 | 0 |

For the fixed local endpoints `(-656,-208,24)` and `(-656,368,152)`, the ordinary graph proxy rises from 1,578,000 to 1,648,000 cost units. Both endpoint approaches cost 400,000. The selected local path changes from one step plus two walk edges to eight steps plus two walk edges; the extra 70,000 equals seven configured 10,000 step penalties. These are Manhattan/node-anchor/end-gate costs, not native seconds, shortest continuous distance or observed speed.

Of the 200 paired spawn-to-pickup quality-cost rows, 24 increase, 25 decrease and 151 stay unchanged; deltas range from -574,000 to +70,000. This broader quality graph includes optional jump/drop shortcuts and changes its nodes and links after subdivision. Neither negative nor positive changes rank real flow. The 264 ordinary-route proof is evaluated separately and excludes all jump/drop/lift/teleporter edges.

The combat height spread remains 256, with three 128-unit bands. Spawn eye rays remain 0 visible pairs out of 28, using the same spawn origins plus 22 Z. Global sampled sightline median changes from 227.97 to 201.82 and p90 from 561.19 to 568.80. Those global samples come from rendered face centers, so brush subdivision changes the sampling population. They are descriptive observations, not matched exposure measurements.

A separate fixed-origin comparison makes 48 matched horizontal static-world ray queries: eight directions from each of six identical flat approach/landing eye points, 46 units above their floors. Only three queries change. The three lower-approach rays facing straight uphill hit the ramp at approximately 200 units and the treads at 168 units. This is a local geometric occlusion difference of approximately 32 units. It does not establish better cover, spawn safety, target visibility during motion or an encounter advantage. All ray origins, directions and results are recorded.

## Next controlled trial

Keep this pair and its hashes fixed. In the same windowed engine build and movement configuration, run repeated ascent and descent trials from the recorded flat stations, separately for walking and running. Match the starting origin, yaw, stance and velocity; measure completion time, ground contact, velocity loss and accidental falls with engine-side instrumentation. Inspect turning and landing clearance in the three lanes. Compare camera/aim rhythm, pursuit, sound and visible targets with the same weapon and opponent conditions. Record observations before preferring either ascent. Any automated input needs the user's separately scoped authorization.

No paired player movement, client rendering or human playtest has occurred. These results are evidence-tagged construction rules and a controlled test fixture; no weights were trained and no archive geometry was copied. The separate `StairRunSpec` default of 32 units remains a conservative planner/support-policy issue, not evidence that stock Quake II cannot traverse narrow treads. This fixture explicitly uses 60-unit treads and does not resolve that default. The existing Windows client startup/focus limitation also remains outside this comparison.

## Reproduce without overwriting the baseline

```powershell
cmake --build --preset windows-clang --target qmg_talus_stairs_recipe
ctest --preset windows-clang -R '^qmg_talus_stairs_layout$'
& ./build/windows-clang/qmg_talus_stairs_recipe.exe E:/Repositories/Quake-MapGen/out/talus-stairs
python -B -m tools.map_recipes.ramp_stair_comparison --compile
```

The last command writes only `out/talus-stairs`; it hashes the published baseline before and after inspection. Omit `--compile` to re-audit the retained compiled files. Compiler binaries and stock assets are read from their designated local reference locations. Dedicated smoke is a separate evidence step; rebuilding must invalidate or replace a smoke report if its BSP hash changes.
