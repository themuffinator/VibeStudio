# Experimental map recipes

Switchback is an original Quake II deathmatch experiment using `qmg_core`'s typed layout, brush construction and analyzers. Its fixed seed is identity metadata, not a promise of random layout variants. The [study](../../docs/Q2_SWITCHBACK_STUDY.md) records design choices and limits.

[Spillway](../../docs/Q2_SPILLWAY_STUDY.md) extends the same workflow with six distributed spaces, an independent overpass/underpass and three local floor heights. The [design playbook](../../docs/MAP_DESIGN_PLAYBOOK.md) describes the reusable process. Both recipes are explicit experiments; the ordinary CLI does not automatically select their motifs.

After configuring the project's `windows-clang` preset, reproduce the MAP, BSP, measurements, interactive floor fragment and playable ZIP from the project root:

```powershell
./tools/map_recipes/build.ps1
./tools/map_recipes/build.ps1 -Recipe spillway
./tools/map_recipes/build.ps1 -Recipe lockyard
./tools/map_recipes/build.ps1 -Recipe ravel
```

The script uses the existing build directory, pinned ericw-tools 2.0.0-alpha11 executables at `E:/_SOURCE/_CODE/ericw-tools-2.0.0-alpha11`, and read-only stock assets at `E:/_SOURCE/_ASSETS/Q2`. Override `-ToolRoot` and `-Assets` as needed. It expects the local study profile at `out/map-study/profile.json`. Earlier Crosscurrent/Switchback compiled analyses are optional comparison inputs; missing baselines are reported as unavailable. Generate the profile first using the main README's regimen. Compiler binaries and game assets are not bundled or downloaded.

To exercise the recipe without external compilers or assets:

```powershell
cmake --build --preset windows-clang --target qmg_switchback_recipe
ctest --preset windows-clang -R '^qmg_switchback_layout$' --output-on-failure
cmake --build --preset windows-clang --target qmg_spillway_recipe
ctest --preset windows-clang -R '^qmg_spillway_layout$' --output-on-failure
```

`recipe_support.hpp` writes typed layout, explicit stair-run geometry and versioned flow evidence. `inspect.py` checks the compiled BSP, native textures, all gameplay anchor hulls and compiler results, binds hashes, compares with the preceding example and packages original source and BSP. Spillway also checks both crossing lanes and their solid separator in the compiled world. Persistent generated output goes to `out/<recipe>/`; CTest uses the preset's `recipe-test/<recipe>` directory. Historical flow files without `cost_basis` use the earlier node-only timing model and are not directly comparable.

`layout-view.html` is a self-contained analytical HTML fragment template populated from exact layout bounds and compiled anchor locations. It depicts geometry and route structure, not an engine render. Historical recipe records remain unchanged; the separate optional Fold/Lockyard liquid trials and Talus ramp/stair experiment below add only their stated movement evidence. Multiplayer balance remains unverified.

Spillway uses `layout-view-v2.html` and `python -B -m tools.map_recipes.render_layout --output out/spillway`. This viewer takes arbitrary declared floor heights and explicit stair runs, instead of assuming twelve treads or fixed elevations. `tests/recipe_preview_check.cjs` accepts a wrapped browser-preview path and an evidence directory, with Playwright available through Node resolution or `NODE_PATH`; it checks 24 floor/width/theme states without controlling user input.

## Lockyard and native liquid trials

[Lockyard](../../docs/Q2_LOCKYARD_STUDY.md) has six unequal rooms, ten passages,
main combat floors 0/128, a partial court gallery and a separate local culvert
bed at −128. `lockyard_audit.py` checks supported wet stairs, continuous floor
coverage, surfacing columns, the low-roof obstruction and the sealed sky roof.
`gate_audit.py` independently tests ordinary passage dependencies on the complete
serialized navigation graph. The v2 preview binds local bed, water, wet stairs
and roof projections to the final layout/features and preserves major-item labels.
The retained verification covers 24 Lockyard states and 42 prior-map controls.

For source-only generation and the focused layout test:

```powershell
cmake --build --preset windows-clang --target qmg_lockyard_recipe
ctest --preset windows-clang -R '^qmg_lockyard_layout$' --output-on-failure
```

The [optional native harness](../native_movement/README.md) builds separately
from `qmg_core`, using external, hash-pinned Quake II 3.19 CM/Pmove sources. Its
private combined GPL executable stays under `.agents/tmp/native-movement/` and
is never packaged with maps. After generating the wet MAP/BSPs:

```powershell
python -B -m tools.native_movement.build
python -B -m tools.native_movement.experiment fold
python -B -m tools.native_movement.experiment lockyard
```

Experiments preserve complete command/frame/result records and compile a control
with only the water volume removed. Sixteen current trials retain fourteen
completions and two level-view stalls at Lockyard's low roof; dive profiles
complete its wet route. This is native static-world movement, without game-module
oxygen, damage, entities, rendering or multiplayer interactions. The drained
control is not Lockyard's ordinary dry perimeter. Native tests skip explicitly
when the optional executable or generated fixtures are absent.

## Ravel and matched native ascents

[Ravel](../../docs/Q2_RAVEL_STUDY.md) uses six unequal rooms, nine passages,
three stair runs, one continuous ramp, one optional drop and local floors
0/128/256. `ravel_audit.py` checks supported bent routes and the above/below
crossing alongside the existing ramp, drop and ordinary passage audits. All
264 required walk/step/door journeys remain available without the optional drop.
The [Reclamation study](../../docs/Q2_INTERLEAVED_ROUTE_STUDY.md) supplies local
reference evidence and retained blocked corner/vertical shortcuts; it does not
provide copied geometry or a whole-map movement guarantee.

```powershell
cmake --build --preset windows-clang --target qmg_ravel_recipe
ctest --preset windows-clang -R '^qmg_ravel_(layout|audit_tests)$' --output-on-failure
```

The current v2 floorplan mode preserves one XY envelope, origin and scale across
floor changes, includes a map-unit bar and keeps other floors as dashed context.
True XY floorplans already existed; this change makes their switching comparison
consistent. The final Ravel record covers 24 preview states and 72 regenerated
Counterweight/Talus/Lockyard controls, with 23 selected CTest gates and 16
dedicated initialization gates. Analytical previews are not engine renders;
initialization is not connected-player traversal or a match.

The [native Talus ascent study](../../docs/Q2_NATIVE_ASCENT_STUDY.md) uses the
existing unchanged Bench ramp/stair MAP/BSP pair. Its separate orchestrator
retains 24 primary journeys and eight deterministic repeats under
`out/native-ascent/`, without changing the historical liquid-trial output root:

```powershell
python -B -m tools.native_movement.build
python -B -m tools.native_movement.ascent
ctest --preset windows-clang -R '^qmg_native_ascent_tests$' --output-on-failure
```

All native attempts complete. Uphill times tie; on descent, plane crossing and
grounded recovery are different events. Five interpretation controls pass.
These static-world Quake II 3.19 trajectories do not test Ravel's movement,
game-module damage, camera behavior, human steering or multiplayer quality.
Historical study records remain unchanged, core/profile identities are retained,
and that historical iteration does not claim automatic default-planner selection
or trained weights. The later Weft integration below changes the default
construction policy while retaining the reference profile and weights.

Six separate native Ravel probes (`out/ravel/native-routes/README.md`, upstream study evidence, not imported)
complete four center-ramp journeys and two ledge descents on the final BSP.
The result preserves actual first landing and grounded-station events, rather
than treating the geometric drop candidate as a native predicted landing.
Three retained-frame controls check goal bounds and event ordering.

```powershell
python -B -m tools.native_movement.ravel
python -B -m tools.native_movement.ravel --verify
ctest --preset windows-clang -R '^qmg_native_ravel_tests$' --output-on-failure
```

These remain fixed-input static-world probes. Damage, braking, turning recovery,
complete ordinary return journeys and combat require further evidence.

The later [native recovery study](../../docs/Q2_NATIVE_RECOVERY_STUDY.md) retains
eight fixed post-drop profiles on the unchanged Ravel BSP: five successful
low-speed grounded pauses and three failures. Releasing input succeeds at both
approach inputs; a north-turn burst succeeds only at the slower input within
the declared pause area. Six analysis controls protect the result. Successful
turn/pause profiles do not certify onward doorway traversal or game damage.

## Weft and the default generator portfolio

[Weft](../../docs/Q2_WEFT_STUDY.md) is a predeclared default-pipeline example,
rooms6-seed42, rather than another authored recipe. The Q2 deathmatch/duel
generator now varies compact adjoining rooms, offset passages, raised
crosswalks, horseshoe galleries and barriers with two stair approaches per
elevated room. Other title/mode paths and the reference profile remain unchanged.

Use the [portfolio exporter](../mp_portfolio/README.md) to obtain the selected
MAP and exact schema-v4 report plus a viewer-compatible typed layout. It invokes
normal six-candidate selection and does not rewrite geometry or item classes.
The display title affects only the layout viewer metadata. The existing
`render_layout.py` and analytical viewer consume that layout; the recipe build
script does not construct Weft.

The fixed nine-case baseline comparison and separate low/high verticality
controls are recorded in the study. Initial compiled envelopes shrink
31.7–41.7%, with two retained spawn-sightline regressions motivating a bounded
placement refinement. The revised nine outputs retain the same extents and have
zero compiled visible spawn pairs, while 39 analytical lighting warnings
remain. All nine primary outputs, two separate semantic controls and the
duplicate Weft hero compile pass full VIS and extra4 one-bounce lighting.
All 42 configured CTest gates and 216 analytical width/theme/floor preview
states pass: 162 primary, 36 semantic control and 18 Weft states.
Final CLI MAP/report parity and eight published recipe MAP controls pass; see
the parity record (`out/mp-portfolio/recipe-parity/parity.json`, upstream study evidence, not imported). The
portfolio (`out/mp-portfolio/index.html`, upstream study evidence, not imported) and
local package (`out/mp-portfolio/qmg_compact_portfolio.zip`, upstream study evidence, not imported) expose all 11
generated blockouts. Weft passes 16 dedicated initialization gates on the final
lit BSP. Dedicated ticks without connected players do not add native Weft
journeys, pickup contact, engine renders or a multiplayer playtest claim.
