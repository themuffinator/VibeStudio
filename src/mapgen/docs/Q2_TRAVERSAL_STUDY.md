# Traversal mechanisms study — 13 September 2026

Lifts and teleporters should change the cost and commitment of a journey, with arrival positions that players can understand and contest. This study adds three reference maps to the [design playbook](MAP_DESIGN_PLAYBOOK.md): Painkiller, Torn Glory and Bio Rust. It compares their authored entities, upward world surfaces and local readmes, then checks relevant behavior in a local copy of original Quake II game source. No game was launched. Exact paths, SHA-256 hashes and evidence types are in the [research record](../knowledge/q2/traversal-study.json).

Fresh analytical plans and a detailed local entity inventory remain in `out/traversal-study/`. The plans show projected lift bounds, directed teleporter links and exit angles. They include inaccessible trim, omit moving submodel shapes, and keep mechanism markers visible across height slices. They are geometry diagrams, not engine renders or validated navigation graphs. No reference geometry, assets or source code was incorporated into generation; no weights were trained.

## Reference observations

| Reference | Authored mechanisms | Useful comparison |
| --- | --- | --- |
| Painkiller (`ztn2dm1`), Sten “ztn” Uusvali | Two lifts, explicit heights 192/256 and speed 250; two directed teleporter links with origin-height gains 132/260 and exit angles 180°/90° | Alternative vertical access can arrive in different parts of an upper circuit, rather than always discharge at the same contest |
| Torn Glory (`card2dm1`), Dan “CardO” Shannon | One lift, height 160 and speed 200; one directed teleporter gaining 328 origin-height units, exit angle 273°; six small rotating screen entities | A low route can offer a committed ascent shortcut; beveled and segmented encounter spaces can make the arrival and subsequent approach distinctive |
| Bio Rust (`koldduel1`), kold | Three lifts, heights 256/193/192 and speed 250; no embedded teleporter links | Multiple lift positions can distribute ascent opportunities; ordinary return routes and lift waiting states need separate checks |

These are declared entity properties, not measured travel times. Origin-height changes are not player step heights or necessarily equal to floor separation. All three maps remain in the reference partition; their AAS sidecars have zero reachabilities and unverified BSP association.

**Painkiller.** Its two teleporters target distinct destinations; the BSP does not declare an inverse edge between either source/destination pair. One transfers about 902 units in XY and gains 132 in Z; the other transfers about 1,389 in XY and gains 260 in Z. These are displacement descriptors, not saved route distance. The plan places these destinations at different edges of the arrangement, facing into their local approaches. The upper route contains rockets, chaingun and Body Armor, while rail and Combat Armor occupy lower heights. The resulting question is whether several ascent choices let a recovering player contest the upper resources, or merely deliver them into predictable fire. The author's readme specifies 2–4 players, highlights duel play and credits playtesters; that does not supply a new measured result for this run.

**Torn Glory.** Its single teleporter moves from a low position to a separate upper destination, about 1,013 units away in XY. The inspected plan includes a faceted Mega-health space, segmented changes in direction and unequal floor footprints. The lift and teleport are spatially separate methods of gaining height; neither proves the presence of a reliable ordinary alternative. The six `func_door_rotating` entities are small screen-sized submodels, sharing `targetname=screens` and a button controller. Their authored bounds are 8×64×64 or 64×8×64 units. Do not count these as six traversable doorways. This is a useful warning against inferring movement mechanics from class counts alone. The author explicitly forbids using the level as the base of another level; only general principles are carried forward.

**Bio Rust.** The plan uses chamfered encounter boundaries and offset upper/lower paths. Three lifts occupy different approaches, and two super shotguns occur at different heights. This supplies a contrast to a teleporter-heavy map: ascent can have an observable physical route and waiting cost. The lift submodel bounds include substantial vertical geometry, so their bounding boxes do not directly describe the usable standing area. No drop route or ordinary lift bypass was confirmed by this study. Those must be traced with the player hull and tested in the engine. The author describes a small duel map and reports several weeks of testing/tweaking, while the archive labels it for up to six players.

## Source behavior that changes the design test

The inspected source is local `E:\_SOURCE\_CODE\Quake-2-master\game\`, with exact file hashes and function line references in the research record. It is a reference for original baseq2 behavior, not proof of a particular installed game module's behavior. No code was copied.

In `g_misc.c`, `teleporter_touch` clears player velocity, applies a brief teleport hold, sets the destination orientation, places the player ten units above the destination origin, and calls `KillBox`. In `g_utils.c`, `KillBox` attempts to eliminate overlapping occupants with telefrag damage. Thus a shortcut can sacrifice momentum and information while exposing a predictable arrival. A destination marker that looks clear from above still needs player-hull, occupant and forward-movement tests. Do not describe an occupied destination as automatically safe.

In `g_func.c`, `plat_hit_top` schedules descent three seconds later; top-trigger occupancy postpones descent by one second. `SP_func_plat` applies speed/acceleration scaling and replaces zero lip with its default. Painkiller's `wait=0.2` field is therefore not evidence of a 0.2-second top dwell in this source. Height divided by the authored speed is also insufficient to establish cycle time. `plat_blocked` damages an obstruction and reverses direction for a blocking player/monster. Test the shaft, edges and waiting positions throughout the cycle, not just the top and bottom poses.

## Original design hypotheses and controlled tests

| Design hypothesis | Construct in the original level | Test or ablate |
| --- | --- | --- |
| A lift trades travel distance for waiting and exposed ascent | Distinct lower waiting cover, visible upper landing and an ordinary alternative route | Compare lift use at bottom, rising, top, descending and occupied states; close only the ordinary alternative to measure forced waits and control |
| A one-way teleporter provides speed with arrival commitment | Clear source cue, unique target, explicit facing, supported landing and a usable first escape direction | Keep geometry/items fixed and disable only the link; measure actual journey time, use, post-arrival deaths and resource acquisition |
| A shortcut's exit remains contestable without becoming a trap | An exit oriented toward navigable space with more than one subsequent tactical choice where appropriate | Occupy the destination, approach from both directions, test back-to-back arrivals and record telefrags/escape opportunities under the exact game module |
| A drop provides a deliberate one-way commitment | A hull-checked landing, visible destination and a known return journey | Record landing damage, loss of control, commitment and pursuit; do not infer a valid drop from projected floor overlap |
| Faceted space changes combat rather than only decoration | Bevel selected corners or segment an approach while preserving fighting width and headroom | Compare one beveled versus square corner with items and routes fixed; measure visibility, movement snagging and encounter angles |

For each special edge, record source/destination IDs, direction, ordinary-versus-special requirement, trigger volume, movement state and timing assumptions. Analyze normal connectivity with special edges disabled, then with them enabled. A successful special-edge graph cannot substitute for proving the required ordinary routes. For a lift, retain the shaft's swept collision region and landing support in the model; for a teleporter, bind to the effective player arrival pose rather than a point symbol alone.

Measure wait time, platform ride time, time to leave the landing, exposure during those phases and the ordinary route time separately. Include attacker control of the waiting area and upper platform, repeated arrivals, blocked motion and the actual pickup circuit after arrival. Use matched starts and change one mechanism or placement at a time. More lift/teleporter entities, shorter graph paths or lower sightline medians are not evidence of better flow.

## Evidence issues and next gates

The new readmes extend the capacity-conflict record: Torn Glory recommends duel or 3–4-player FFA, while metadata says 2–6; Bio Rust specifies duel, while metadata says 2–6. Preserve these alongside the earlier Paranoia and Chastity Belt Duel conflicts in the [route diversity study](Q2_ROUTE_DIVERSITY_STUDY.md). Do not silently change the existing profile or promote disputed capacities to training labels.

The mechanism-count warning for Torn Glory and ignored/defaulted entity properties are study findings, not newly reproduced engine bugs. No additional unrelated repository defect was found. The next original example still needs compile/leak/VIS/light gates, static hull and directed-route checks, and windowed engine verification of moving platforms, teleporter arrival, rendering and play. Use engine-written screenshots and respect project permissions for any user-input control.
