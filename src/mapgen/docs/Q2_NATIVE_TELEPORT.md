# Stock Quake II teleporter activation and Relay exits

Four fixed Relay trials completed actual source entry, stock teleporter activation, arrival hold, an intentional east or west departure and a grounded stop. This is a separate optional game-module experiment: the earlier world-only movement harness cannot execute teleporter entities. The Relay geometry, commands and acceptance contract were frozen before these four first attempts; no failed commands were tuned or gates widened.

The compact summary (`out/native-teleport/summary.json`, written by `python -B -m tools.native_teleport.release`), Relay protocol (`out/native-teleport/relay-protocol.json`, written by `python -B -m tools.native_teleport.relay`), raw-trial comparison (`out/native-teleport/relay-v1/comparison.json`, written by `python -B -m tools.native_teleport.relay`) and final evidence manifest (`out/native-teleport/record.json`, written by `python -B -m tools.native_teleport.release`) bind the results to the actual BSP and canonical layout. This does not change the default generator or claim player-tested balance.

## What executes

The original MIT glue in [tools/native_teleport](../tools/native_teleport/README.md) links the complete, unmodified external [id Software Quake II 3.19 game source](https://github.com/id-Software/Quake-2), stock server entity-world queries, CM and Pmove. All 83 compiled/reference inputs are hash-pinned. The repository's MIT licence and the external GPLv2-or-later licence were checked before incorporation: the combined private test executable is GPL, remains under `.agents/tmp/relay-native-v1`, and is neither installed nor packaged with maps. No external source, binary or game asset is copied into the map deliverables.

This host calls `GetGameAPI`, `Init`, actual BSP `SpawnEntities`, two initialization `RunFrame` ticks, stock `ClientConnect`/`ClientBegin`, then fixed synthetic `ClientThink` commands. Actor positioning, destination-yaw overrides and occupancy controls occur once during explicit fixture setup. The host does not reposition the actor after that setup. Stock `ClientThink` calls `G_TouchTriggers`; real `SV_AreaEdicts` finds the native trigger and invokes its stock touch callback. The host never calls `teleporter_touch` directly. Stock `SV_Trace`, CM, dynamic pad collision, `KillBox`, damage and player death remain native.

The private build uses Clang 20.1.7, `x86_64-pc-windows-msvc`, game API 3 and an observed 1120-byte game edict. Shared game/server prefix offsets and integer sizes are checked. This is a modern private host for the pinned implementation, not the historical 32-bit engine ABI. Asset/model/sound indexing and message imports are bounded substitutes; no renderer, client, sockets, network packets, mouse, keyboard or OS capture is used. Saving, loading and arbitrary game console requests are unsupported. Old source implicit-int and pointer-cast compiler warnings are retained; save paths are not exercised.

Each command advances 16 ms. Stock game frames run at 100 ms boundaries after the two initialization ticks. Recorded times are synthetic command elapsed time, not wall-clock duration or a network server/client equivalence. The raw events distinguish actor relink, `after_clientthink` and `after_gameframe` snapshots; endpoint records do not reveal every internal Pmove substep.

## Development controls and retained host failure

The existing Counterweight BSP is a development fixture, including its actual authored entities and inline model. The first protocol (`out/native-teleport/development-protocol.json`, written by `python -B -m tools.native_teleport.run`) declared eight cases: four unoccupied speed/yaw combinations, an occupied real client, an unkillable blocker, a missing target and a 40-unit trigger near-miss. All used the same fixed eight settle, 48 active and 64 release commands. Their eight first attempts exited during `SpawnEntities`, before any command, because the original host rejected a null configstring. All empty event files and error logs remain in development-v1 (`out/native-teleport/development-v1/comparison.json`, written by `python -B -m tools.native_teleport.run`).

Stock `SetItemNames` includes an empty first item name; stock `PF_Configstring` accepts null as an empty string. The host-only guard was corrected to match that import contract. Build 01's earlier compilation failure, successful build 02, failed startup trials, local source snapshot and repaired build 03 are retained. The separate v2 declaration (`out/native-teleport/development-v2-protocol.json`, written by `python -B -m tools.native_teleport.development_v2`) pins the repaired executable and preserves every previous command and expectation. Its eight first attempts observed all eight expected outcomes. This is host development followed by a versioned correction, not eight initially successful trials.

| Development condition | Actual stock observation |
| --- | --- |
| Input 200 or 400; target yaw 0 or 90 | One trigger activation; clear arrival; immediate cleared entity velocity/view, followed by the declared next-command yaw and hold |
| Real client occupying the arrival | `MOD_TELEFRAG` 21; victim health −999, solid 0; arrival hull clears |
| Unkillable solid occupying the arrival | Actor transports and emits the teleport event, but remains `startsolid`; blocker health 100, solid 2. Retained `Bad InitialSnapPosition` diagnostics are consistent with the blocked arrival |
| Missing target | No teleport; stock missing-destination diagnostic retained |
| 40-unit lateral near-miss | Actor passes the source without activation |

`KillBox` can return false for an unkillable blocker, and stock `teleporter_touch` ignores that return. A teleport event therefore does not prove a usable arrival. The unkillable case is a successful negative control for the host and a failed safe-arrival condition.

Stock touch copies destination origin, preserves that copied value in `old_origin`, adds 10 to the actor's entity Z, clears entity velocity, sets `pm_time=20` and the teleport flag, and clears immediate entity angles, player-state view angles and `v_angle`. The Pmove origin and velocity shorts at the relink still describe the pre-touch movement. They must not be labelled as the new arrival state. On the next `ClientThink`, Pmove reconstructs position/velocity and applies the destination-relative delta angles. Nine subsequent 16 ms states retain the same arrival position with timers 18 through 2; the tenth clears the timer/flag and permits movement. Local Q2REPRO legacy code differs in immediate view handling, so neither its rendering nor client prediction is claimed by this stock result.

## Relay's fixed first journeys

The frozen BSP SHA-256 is `5a79c583841829e5acd714a5009db441f4a9d2e3cd8902c448b9a4ec3aa2b594`; canonical layout SHA-256 is `7e072a7c0f6fb7e4121dfa36f848580d602c7887f0f1945bb8c0cca315adad51`. Source pad origin is `[-704,-624,24]`, and the actor starts at feet `[-704,-720,0.125]`. Destination origin is `[-480,672,24]`, yaw 0. Actual immediate arrival origin is `[-480,672,34]`: feet 10, two units above the native disc top at 8. The fixed zero-input stage lets the actor settle before departure.

All four trials use eight initial settle frames, source approach at the selected input, 32 zero-input hold/settle frames, a declared departure and 64 release frames. The source approach is 27 frames at input 200 or 19 at input 400. Raw yaw 90 enters the source; stock delta yaw −90 produces an east-facing arrival. East departure keeps raw yaw 90; west departure intentionally changes it to 270, producing view yaw 180.

Departure counts are 41/43 frames for east/west input 200 and 25/26 for east/west input 400. Before declaration, counts were derived from previously retained Weft startup and Ravel stopping evidence, including signed-coordinate eighth-unit quantization. The fixed estimate aims for a 144-unit stop within the declared 128–160-unit strip. Input 400 is an analog command capped by native movement to a 300-unit wish speed; input 200 remains nominally 200. Transferring these timings from flat movement to an eight-unit disc departure is explicitly approximate. No Relay output was used to fit the counts.

The complete retained source/departure center bands must remain alive, dry, standing and free from solid overlap. Teleportation is the single expected discontinuity. After release begins, the goal requires signed X displacement 128–160, Y within 671–673, feet Z within 0±0.25, clear 32×32×56 hull, ground contact and total velocity at most 1. Nine consecutive 16 ms end states span 128 ms. The first such interval is reported independently; simply crossing the target at speed is insufficient.

| First attempt | Activation | First completed dwell | Actual final feet | Final speed |
| --- | --- | --- | --- | --- |
| East, input 200 | 544 ms | 2144 ms | `[-335.125,672,0.25]` | 0 |
| West, input 200 | 544 ms | 2176 ms | `[-623.875,672,0.25]` | 0 |
| East, input 400 | 416 ms | 1824 ms | `[-336.375,672,0.25]` | 0 |
| West, input 400 | 416 ms | 1840 ms | `[-622.625,672,0.25]` | 0 |

All four passed the unchanged contract. The final stops are 142.625–144.875 units from arrival, rather than exactly at the geometric east/west 128-unit stations. Their XY centers lie on the first segment of each separately audited static onward departure. Full-floor and hull support for those corridors belongs to the static Relay audit; this experiment does not extend the native journey to the chaingun, assert a pickup contact, or certify the whole circulation loop. No wall contact is needed for these interior stops. Two exits and a useful downstream resource remain design hypotheses beyond these measured initial departures, not competitive-balance results.

## Recheck and boundaries

`python -m tools.native_teleport.verify` rechecks source/BSP/protocol bindings, the eight retained startup failures, eight repaired development cases and four Relay cases. It independently derives native activation/hold/telefrag behavior and the first physical goal interval from raw events; it also reproduces every reported secondary result. The manifest binds the original glue, 23 synthetic protocol controls, study text, commands, raw states and useful logs. The controls reject shortened or discontinuous dwell, high-speed target crossing, wrong direction, unsupported/wet/blocked/crouched endpoints, wrong yaw overrides, conflated immediate/next-command view state and false telefrag evidence.

There are 1603 retained `ClientThink` states across the 12 completed native executions. All 20 native processes closed: eight startup failures exited 2 and the 12 completed trials exited 0. This study leaves earlier native outputs and reference assets unchanged. The new private build directory is `E:\Repositories\Quake-MapGen\.agents\tmp\relay-native-v1`; useful provenance and logs are under `out/native-teleport`. Cleanup status is reported separately from evidence validation. No unrelated repository defect was found; the host import error and legacy-build warnings above are retained explicitly.
