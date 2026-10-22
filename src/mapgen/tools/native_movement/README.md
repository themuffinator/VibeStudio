# Native Quake II movement experiments

> **VibeStudio:** imported as source only. VibeStudio's Meson build never compiles or links
> `harness.c`; this directory's `CMakeLists.txt` remains Quake-MapGen's separate, private GPL build
> (`python -B -m tools.native_movement.build`, run from `src/mapgen`), and nothing it produces is
> linked into or shipped with VibeStudio. See [VIBESTUDIO.md](../../VIBESTUDIO.md).

This optional harness calls the **unmodified official Quake II 3.19 `Pmove`,
`CM_BoxTrace`, and `CM_PointContents` functions** against a compiled IBSP38 world.
It runs a bounded stream of synthetic `usercmd_t` values in a command-line process.
It does not start a client, game module, renderer, network connection or input
device, and does not read or control the user's keyboard, mouse or display.

## Licence and dependency boundary

The repository and the original orchestration files in this directory remain
MIT-licensed. The optional combined executable links GPL engine code and must be
treated as a GPL combined work. It is a **local, private test dependency**, with
no install or packaging target. Its executable, object files and build cache stay
under `.agents/tmp/native-movement/`; never place them in a map download or MIT
library distribution. No external implementation or game asset is copied into
this repository. Generated original MAP/BSP controls and numerical experiment
records can be retained separately under `out/native-movement/`.

The read-only reference is
`E:/_SOURCE/_CODE/Quake-2-master`, the official
[id Software Quake II source release](https://github.com/id-Software/Quake-2).
Its `readme.txt` identifies version 3.19 and the GPL release; the engine source
files carry GPL-2.0-or-later notices. The linked files are `qcommon/pmove.c`,
`qcommon/cmodel.c`, `game/q_shared.c` and `qcommon/md4.c`, using the original
`qcommon.h`, `qfiles.h` and `q_shared.h` headers. The final utility is the
**RSA Data Security, Inc. MD4 Message-Digest Algorithm**, retained externally with
its original attribution terms; it supplies the native BSP checksum. SHA-256
artifact identities are computed separately by the orchestration code.

[`reference.json`](reference.json) pins all eight external source/header/readme
files. CMake rejects a different revision. The build uses the project's
`windows-clang` compiler/generator convention in a separate preset: x64
Clang/MSVC ABI, portable C (`C_ONLY`), `/Od`, `/fp:strict`. The shim asserts the
native Windows integer widths. [`build.py`](build.py) retains source, licence,
compiler, linker, local-shim and executable hashes; every trial verifies this
record before execution. This is one pinned baseline, **not an assertion of
equivalence to Q2REPRO, rerelease movement, a complete stock server, or other
toolchains**.

## Running

From the repository root:

```powershell
python -B -m tools.native_movement.build
python -B -m unittest discover -s tests -p test_native_movement.py -v
python -B -m tools.native_movement.experiment fold
python -B -m tools.native_movement.experiment lockyard
```

The experiments require the generated source/BSPs in `out/fold` and
`out/lockyard`. They compile only the water-removed control with the pinned
`E:/_SOURCE/_CODE/ericw-tools-2.0.0-alpha11/qbsp.exe`, using read-only asset lookup
at `E:/_SOURCE/_ASSETS/Q2`. The control's MAP, BSP, compiler log and identity proof
are retained in `out/native-movement/design-controls/`. A collision experiment
does not need VIS or light for that control; the published wet BSP is the full
VIS/light build.

For a specified trial:

```powershell
python -B -m tools.native_movement.run out/native-movement/lockyard-wet-forward400-constant-pitch30-16ms/trial.json
```

The JSON contract is version 1:

```json
{
  "schema_version": 1,
  "id": "lockyard-example",
  "bsp": "out/lockyard/qmg_lockyard.bsp",
  "start": {"feet": [-320, -560, 0], "clearance_z": 0.125, "velocity": [0, 0, 0]},
  "gravity": 800,
  "timestep_ms": 16,
  "settle_frames": 8,
  "commands": [{"frames": 800, "forward": 400, "side": 0, "up": 0, "yaw": 0, "pitch": 30}],
  "goal": {"axis": "x", "coordinate": 832, "direction": 1, "feet_z": 0,
           "z_tolerance": 1, "cross_axis_range": [-576, -544], "waterlevel": 0, "grounded": true}
}
```

Axes, pitch and command values are native conventions: yaw 0 faces +X; yaw 90
faces +Y; positive pitch looks down. These are literal scripted forward values,
not captured human run/walk behaviour. Each phase has a fixed frame count; no
adaptive steering, teleportation or path correction occurs. The harness allows
8–50 ms commands, at most 4,096 frames/120 simulated seconds, 32 phases,
coordinates on the native eighth-unit grid, and movement inputs bounded to ±400.
The executable accepts one BSP and an explicitly checked start, then consumes
`msec forward side up yaw pitch buttons` lines on its own stdin. That protocol
does not inject operating-system or game input.

## What a completion proves

The native collision loader consumes the actual BSP. `MASK_PLAYERSOLID` traces
use world model 0; a world-hit identity supplies the pointer that the server trace
wrapper normally gives Pmove. Contents results are returned unchanged, including
compiler-added DETAIL/TRANSLUCENT bits. There is no approximate collision shim,
filtered water callback, replacement stair approximation or artificial movement
edge. Python's independent BSP reader only checks file/index bounds before the
legacy native loader is called.

The initial native stationary hull must be clear. Exact contact with a floor is
reported startsolid by this native stationary trace; therefore the experiment
explicitly declares a +0.125 launch separation. It is not an automatic recovery
from an embedded start. Eight 16 ms zero-input frames settle the start and are
excluded from measured movement time. The final two settle poses must match,
remain standing and grounded at the specified floor/lane, and have stable native
velocity. Dry starts require zero velocity. A stationary water check can retain
the native downward sink velocity against a floor while its grounded pose remains
unchanged; this is recorded, not clamped.

The first accepted goal sample must be beyond the specified horizontal station,
inside the specified lane, within one unit of the destination floor, dry,
grounded, standing 32×32×56, and outside solids. Every preceding movement sample
must stay in the lane. Goal time is **simulated native usercmd time**, resolved
to one 16 ms command, not wall time. Later scripted frames cannot extend it.
There is no optimal-traversal or competitive-flow claim.

Every attempt saves its input, command stream, full native frame output, stderr
and result. The result is reset before launch; validation failure, native error,
timeout or malformed output cannot leave a stale success. Timeouts retain partial
output. Failed goals keep their full command budget and terminal state. A stall
means the last 32 commanded frames move less than one unit on every axis; it is
not a complete diagnosis of the collision or movement constraint.

`zero_horizontal_progress_ms` counts commands with exactly unchanged quantized
XY under nonzero movement input. It includes startup or constraints and does not
uniquely identify wall contact. `waterlevel_duration_ms` classifies each command
by its **frame-end** waterlevel through the goal, or the entire failed budget;
it is not exact continuous immersion or oxygen time. Callback counts cover the
entire scripted budget, including settling and post-goal frames.

## Retained outcomes, 13 September 2026

The final Lockyard BSP is
`66799ef3829a28d04759c33be72d3ac131f17c86b3cb6e3f5eae933d34445f60`;
Fold is `0957907667208b6aea4fa7f5826377c7a107ea4b4b2518e819c13bc6b0b4bdd0`.
The paired control removes exactly one authored/compiled water volume. Every
remaining solid brush's native planes, complete texinfo and contents match,
separately for every model. Fold retains 178 solid brushes and 91 non-world
entities; Lockyard retains 188 and 133. No item, light, brush geometry or material
changes accompany the intervention. Water-related BSP subdivision may change;
the proof normalizes indices and compares the actual compiled collision data.

| Map and fixed input | Wet goal time | Same geometry without water |
|---|---:|---:|
| Fold, forward 400, level pitch | 1,680 ms | 1,424 ms |
| Fold, forward 200, level pitch | 2,560 ms | 2,128 ms |
| Lockyard, forward 400, level pitch | Stalls at X239.875 | 3,920 ms |
| Lockyard, forward 400, phased pitch 30 | 6,672 ms | 3,920 ms |
| Lockyard, forward 400, constant pitch 30 | 6,400 ms | 3,920 ms |
| Lockyard, forward 200, level pitch | Stalls at X239.875 | 5,904 ms |
| Lockyard, forward 200, phased pitch 30 | 9,664 ms | 5,904 ms |
| Lockyard, forward 200, constant pitch 30 | 10,048 ms | 5,904 ms |

Fold completes both retaining edges with waterlevels 0→1→2→1→0. Lockyard's
horizontal-only profiles stop at the overhead beam. Successful profiles enter,
descend beneath it with a full standing hull, emerge and climb to the far dry
landing: 0→1→2→3→2→1→0. The phased profiles hold level pitch for 160/220 frames,
look down 30° for 96/144 frames, then return to level pitch (forward 400/200
respectively). The forward-400 phased example includes 22 zero-XY-progress
frames (352 ms); its constant-pitch example includes none. Both forward-200 dive
examples include none. This small set establishes valid instances and an input
sensitivity; it does not establish the fastest path or best view-pitch profile.

The dry comparison is **the same culvert with water removed**, not the actual
ordinary dry perimeter route around Lockyard. It cannot show which route is
strategically preferable. No dynamic entities, weapons, damage, oxygen timer,
server command filtering, client prediction, network conditions or opponents
are simulated. Talus ramp/stair native paired movement remains a separate future
experiment; the earlier compiled static comparison is not upgraded by these
water results.

See `out/native-movement/fold-comparison.json` (`out/native-movement/fold-comparison.json`, written by `python -B -m tools.native_movement.experiment fold`),
`lockyard-comparison.json` (`out/native-movement/lockyard-comparison.json`, written by `python -B -m tools.native_movement.experiment lockyard`)
and `negative-controls.json` (`out/native-movement/negative-controls.json`, upstream study evidence, not imported).
The current test file has 12 passing checks, including actual native obstruction,
startsolid, stationary waterlevels 2/3, repeated deterministic frames and invalid
timestep/overflow controls; goal-height/lane/water/stance and failure-retention
controls supplement them. Native integration tests explicitly skip when their
private dependency or generated fixture is unavailable.
