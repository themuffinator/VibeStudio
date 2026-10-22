# Quake II mechanism contracts

These notes bind the experimental recipes to stock Quake II behavior. The local official source release was read as a behavioral reference; no implementation was copied. Variants and rereleases require their own checks. Static evidence does not replace an engine playthrough.

## Reusable lifts

Use `ConstructionBuilder` with `LiftAssemblySpec`, apply the resulting `ConstructionResult` to the layout, and retain that result when calling the three-argument `MapBuilder::build`. This preserves both collision ownership and the explicit travel/speed directive.

`platform_bounds` and `platform_bounds + travel_delta` describe the two stations. Stock `func_plat` requires the compiled brush at the higher station, then subtracts its native `height` to find the lower station. Q2 lowering therefore translates a positive-delta authored brush to the upper station; a negative-delta upper brush is already in the right place. No brush-entity `origin` is emitted, so the translation is applied once to geometry. The typed stations and their swept-hull checks remain unchanged. Q2 motion markers use the supported `info_notnull` classname, since stock Q2 has no `target_position` spawn function. See [`SP_func_plat`](https://github.com/id-Software/Quake-2/blob/master/game/g_func.c#L513) and the [native spawn table](https://github.com/id-Software/Quake-2/blob/master/game/g_spawn.c).

For an ordinary repeatedly usable lift, omit `trigger_bounds`. Stock Q2 creates its own touch trigger and starts the platform at the lower station. Giving the lift an external trigger also gives it a `targetname`; it then starts disabled at the upper station until activated. Its own trigger raises it when a living player touches it at the bottom. At the top, player presence postpones its return. Stock Q2's return delay is hard-coded to three seconds after arrival, with one-second postponements while touched; the emitted `wait` key does not change that behavior. Use `wait_milliseconds = 3000` to keep intended metadata aligned with this engine. The native speed also accelerates and decelerates, so distance/speed alone is not a complete ride time. See [`plat_hit_top`, `Use_Plat`, `Touch_Plat_Center`](https://github.com/id-Software/Quake-2/blob/master/game/g_func.c#L358).

Provide free space for the full platform sweep and a standing passenger, both boarding/exit areas, and a route that does not depend on finding the lift already at the desired station. Navigation checks mover ownership, explicit free-space coverage, the platform sweep and passenger sweep. Its bidirectional lift edges do not simulate activation, waiting, boarding, return timing, crush reversal or competition for a small platform. Ground-monster lift edges remain geometric ride possibilities, not proof that a monster can independently summon or operate a player-triggered platform.

## Stock teleport discs

For Q2, `TeleporterAssemblySpec::trigger_bounds.center()` locates the source disc. Construction replaces the requested volume with the stock touch bounds relative to that point: `[-8,-8,8]..[8,8,24]`. Q1 and Q3 retain the requested brush-trigger volume. The Q2 lowerer and navigator reject manually authored noncanonical volumes, since stock `misc_teleporter` cannot reproduce an arbitrary brush trigger. The visible source and destination discs both have solid bounds `[-32,-32,-24]..[32,32,-16]` relative to their origins. See [`SP_misc_teleporter` and `SP_misc_teleporter_dest`](https://github.com/id-Software/Quake-2/blob/master/game/g_misc.c#L1830).

For a source on floor `f`, request `{{x-32,y-32,f},{x+32,y+32,f+48}}`; its center is `{x,y,f+24}`. Use destination origin `{dx,dy,destination_floor+24}` and an explicit destination yaw. At runtime the arriving player is placed another ten units above that destination origin, has velocity cleared and is held briefly. Destination angles determine the new facing. The destination can telefrag another player; navigation does not model that interaction. See [`teleporter_touch`](https://github.com/id-Software/Quake-2/blob/master/game/g_misc.c#L1783).

The Q2 navigation contract requires both discs to rest on their bound support floors, checks each full 64-by-64 disc against world/solid bounds, checks standing clearance on the source disc, and checks the player hull at the actual destination-plus-ten arrival point. Thus a normal 56-unit-high standing gap can still be too low: the source needs 64 units above its support floor, and the destination needs 66. The native disc itself is eight units high. The edge is directed and is unavailable to Q2 ground monsters, because the stock touch function accepts clients only. Native entity collision, trigger touching, post-arrival movement and multiplayer safety still require engine tests; a clear destination is not a safe destination.

## Verification and provenance

Dedicated tests cover both signs of lift travel, exact upper-station native brush bounds with no duplicate origin translation, supported marker classnames, canonical source touch geometry, destination facing, low arrival ceilings, floating or wall-clipped discs, player-only teleport use, and preservation of Q1/Q3 brush triggers. These tests validate native contract interpretation and deterministic output, not gameplay behavior.

The local reference files are under `E:/_SOURCE/_CODE/Quake-2-master/game/`. SHA-256 values read during this study:

| File | SHA-256 |
| --- | --- |
| `g_func.c` | `aa12e2ef06f30bba957adb46cc9caecf843c575041ca5287abec5ecaad2c7873` |
| `g_misc.c` | `d39e96f6e8793ea768c56df820edcd987d90c9f43b7c3c7037e6ed7cd92413ee` |
| `g_spawn.c` | `4c401dfb37076aa1186bf1b321d4606a997a1324fd4c2c3afb933d0a045a0eff` |
