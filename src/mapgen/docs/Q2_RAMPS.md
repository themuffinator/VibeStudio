# Continuous axial ramp navigation

`RampSpec` now has a genuine sloping navigation surface. Construction emits its existing convex wedge, and navigation derives an exact axial height function from the emitted plane triples. It does not replace the wedge with stairs or claim that its aggregate maximum Z is a walkable floor. This is a conservative geometric route proof, not a simulation of Quake II movement or an engine playthrough.

## Recipe contract

Use an ordinary solid ramp with structural or detail ownership and an explicit surrounding room or passage volume. The supported shape has exactly four axial vertical sides, one horizontal bottom and one inclined top. The incline can run in either X or Y direction. Both endpoint top heights must be integral, rise must be positive, and rise/run must be at most 1. All plane points must lie within ±32768 even when a custom navigation world limit is larger. Other convex supports remain conservative blockers and produce an unsupported-support finding; they cannot silently become flat bounding-box tops.

For a floor-0 to floor-128 connection:

```cpp
qmg::RampSpec ramp;
ramp.key = qmg::ConstructionKey{1};
ramp.start = {0, 0, 0};
ramp.direction = qmg::HorizontalDirection::positive_x;
ramp.width = 160;
ramp.run = 480;
ramp.low_height = 8;
ramp.rise = 120;
const auto construction = qmg::ConstructionBuilder{}.build(layout, {ramp});
construction.apply_to(layout);
```

Provide a lower landing ending at X=0 with floor Z=0, and an upper landing beginning at X=480 with floor Z=128. The transverse opening must overlap the ramp's hull-eroded width. An enclosing volume covering the complete wedge and its standing hull, for example Z=0 through Z=256, is suitable. Moving the entire assembly to floor 128 produces a floor-128 to floor-256 route. Reverse or rotate the ramp with `HorizontalDirection` and place the landings at the corresponding physical ends.

Navigation accepts only ordinary walk/step joins at the longitudinal ends, with physical contact and a feet-height difference no greater than the profile's `max_step_up`. It does not infer mid-slope side entry, a gap crossing, a ramp-to-ramp join, a jump onto a slope or direct door-side support on a slope. Put a flat landing between consecutive ramps or between a ramp and a door. Q2 teleport discs also require flat support. A valid walkable incline can still have an unclimbable low lip.

The stock 32-unit player hull rests on the highest part of the plane beneath its footprint. In this example its first fully supported center is X=16, and its uphill edge is X=32. Feet are therefore at `8 + 120*32/480 = 16`, within the 18-unit step budget. At the last fully supported center X=464, feet are at Z=128. The 64-unit large-monster hull first reaches feet Z=24, so this same lip does not receive an ordinary low-landing join for that profile. Widening the ramp alone does not fix that lip.

## Surface and clearance evidence

`NavigationArea::slope` carries `NavigationSlope` metadata:

| Field | Meaning |
| --- | --- |
| `along_x` | Selects X; false selects Y. |
| `start`, `end` | Low and high physical end coordinates; `end` may be smaller. |
| `low_z`, `rise` | Actual top-plane low height and positive rise. |
| `support_offset` | The selected hull's uphill X/Y extent. |

The exact feet-height function before integer rounding is:

`low_z + rise * (axis - start + support_offset) / (end - start)`.

Call `navigation_floor_z(area, x, y)` for the conservative grid height rounded toward positive infinity. A standing origin is that height minus `profile.hull_mins.z`. This rounding is a representation choice, not constructed treads; the map and retained height function are continuous. The native engine resolves its own collision epsilon, ground contact and position quantization.

A ramp receives one surface node covering its full hull-eroded rectangle only when the entire standing-hull envelope is clear of other blockers and covered by the union of explicit free-space volumes. The proof encloses every continuously supported hull, so a thin blocker cannot hide between samples. It is conservative: an overhang that intersects the enclosing prism can reject a physically clear lower part of a ramp. The initial implementation does not partition partially obstructed slopes or accept unsupported surrounding convex free space. Collision against other nonaxial solids still uses conservative aggregate bounds.

Ramp end joins prove the complete swept hull over the end lip, including the in-place rise or final descent, while excluding only the ramp itself from blocker tests. The longitudinal contact and affine support geometry establish that the ramp plane beneath this crossing never exceeds the higher feet height. Other solids and explicit free-space boundaries remain active in this proof. Exact extracted side bounds avoid inventing a one-unit overlap with an adjacent landing from floating aggregate-bound rounding.

For a sloped area, `floor_z` is the minimum supported feet height, and `ceiling_z` is the top of the certified standing envelope. Neither describes a flat floor/ceiling surface across the ramp. Export the slope metadata in any navigation artifact or inspector. Node origin Z uses the local height at its center; placement binding compares against the local height at each placement's XY, not the center's Z. A nonzero placement snap policy remains a planning tolerance and does not establish that an authored spawn/entity origin is collision-free.

Schema-v4 reports now include this full evidence under `analysis.navigation.<agent>.graph`: nodes with complete area/slope metadata, directed edges and placement bindings. The adjacent `profile` records the hull and movement limits. Flat areas explicitly export `slope: null`, and edge costs are decimal strings to preserve their unsigned 64-bit range in JSON readers.

Position-path costs project both endpoint positions onto their bound surfaces and include the slope's vertical change. Ramp edges include the approach to the proved end gate, its lip crossing and the approach to the next node anchor. Distances are Manhattan proxies with the existing configured transition penalties. They are not Euclidean path length, optimal continuous paths, speed-adjusted traversal times or measurements of engine acceleration. Flat-area costs retain their prior behavior.

The independent `tools/map_recipes/ramp_audit.py` delivery gate has a narrower
scope: rise/run at most 1:2, width at least 64, three explicit lanes, and flat
landing stations 48 units beyond each end. It starts with partial-footprint
entry, so Talus's first geometric entry step is about 8.25 units before following
the incline. The core's 16-unit gate is the later, first fully supported center;
these describe different stages of the same approach. The compiled audit uses
exact stored-plane support and continuous sweeps, including the supporting ramp,
while preserving compiler crest caps and checking lateral landing turns. Neither
gate establishes native step execution, steering or acceleration.

## Native grounding and limits

The local official Quake II source checks ground normal Z against 0.7 in `PM_CatagorizePosition`, and `PM_StepSlideMove` uses an 18-unit step and a minimum step normal of 0.7. A 1:1 axial slope has normal Z equal to `1/sqrt(2)`, above this threshold. The planner deliberately uses this simple stricter slope bound rather than accepting the small additional range below a 0.7 normal. See the official [`qcommon/pmove.c`](https://github.com/id-Software/Quake-2/blob/master/qcommon/pmove.c), particularly `PM_StepSlideMove` and `PM_CatagorizePosition`.

QuakeWorld's [`QW/client/pmove.c`](https://github.com/id-Software/Quake/blob/master/QW/client/pmove.c) also uses a 0.7 ground-plane threshold. Quake III defines `MIN_WALK_NORMAL` as 0.7 in [`code/game/bg_local.h`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/game/bg_local.h), used by its ground trace. The same conservative incline geometry is therefore retained for all three player profiles. This is not proof that their movement feels identical. Ground-monster support retains its own hull and step limits; AI steering and native monster movement remain untested.

Momentum, ground detachment at speed, friction, slide acceleration, frame timing, collision bevel generation, engine-specific trace epsilon, prediction and player control still need native testing. A static ordinary route means the planned standing hull can traverse this supported geometry within the stated limits. It does not prove competitive flow or measured travel time.

No external implementation was copied. The local behavior references under `E:/_SOURCE/_CODE/` were read with these SHA-256 values:

| File | SHA-256 |
| --- | --- |
| `Quake-2-master/qcommon/pmove.c` | `8861daaeff7efd7bc6d3c5e65ef99801567f3e06eb14020787206d39dd0038a8` |
| `Quake-master/QW/client/pmove.c` | `eea79eb8cc5c45144a495ef7a5ee07ef8aeef4c34a4add13888833c844945f8c` |
| `Quake-III-Arena-master/code/game/bg_local.h` | `2743f8b6c5fac16d2267b73d3d7af597e481db6a8dc4fa27b31f7d9fb01f38ab` |

The navigation regression suite exercises all four axial directions for Q1/Q2/Q3 with jumping and drops disabled, both landings, local origin/placement height, endpoint costs, wide-hull lip rejection, steep and narrow ramps, thin obstacles, ceiling contact, missing landing contact, side-entry rejection, negative fractional heights and ordering stability. No game was launched for these checks.
