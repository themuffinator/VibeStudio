# Dedicated engine smoke check

`engine_smoke.py` tests whether the pinned Q2REPRO dedicated server and legacy Q2
game module can load a generated IBSP38 map, initialize its entities, and run 40
additional server frames without matched initialization errors. It is an
independent engine check after compilation and static layout analysis.

```powershell
python -B -m tools.map_recipes.engine_smoke `
  --bsp out/counterweight/qmg_counterweight.bsp `
  --assets E:/_SOURCE/_ASSETS/Q2 `
  --engine-root E:/Repositories/q2repro/builddir `
  --runtime .agents/tmp/counterweight-engine `
  --output out/counterweight/engine-smoke
```

The output contains `engine-smoke.json`, the engine log, redirected standard
output/error, the exact profile, and `authored-entities.ent`. The JSON binds the
tested BSP, executable and module hashes and reports every gate independently.
An engine exit code of zero is insufficient: the profile must complete, the
correct map and module must be reported, and item/entity diagnostics must pass.

The harness copies the dedicated executable and legacy module independently into
the named project scratch directory. Each run gets a fresh home directory and a
copy of the BSP. Empty local startup configs shadow installed configs. Supported
`basedir`/`game` lookup reads the extracted stock assets; `homedir` owns generated
files. Nothing is installed into a real game directory and no hardlinks are used.
Writable paths must remain inside the project and cannot traverse filesystem
links.

The process uses `CREATE_NO_WINDOW`, redirected standard handles and a closed
input stream. It launches no game client, creates no game window, reads no
keyboard/mouse input, and takes no screenshots. Networking binds an ephemeral
IPv4 UDP port on `127.0.0.1`, disables IPv6, locks client admission, clears master
servers, and disables public advertising and downloads. The test also requires
the engine to report zero transmitted and received packets. A 30-second watchdog
terminates only the harness's own child if it fails to quit.

The source audit found that the ordinary Windows client calls
`SetForegroundWindow` and `SetFocus` during window creation even with mouse
capture disabled. It is therefore excluded from this harness. The dedicated
build's redirected console handles fail console setup before input mode changes;
its diagnostic is retained as an expected startup notice. A missing re-release
MapDB in the legacy asset set and the compiler's ignored `mapversion` world key
are also retained explicitly.

The legacy module runs at 10 Hz. Dedicated `wait` advances with server frames;
the harness verifies the reported `sv_fps` value and schedules 40 frames after
the engine's two initialization frames. It does not override the cheat-protected
`fixedtime` cvar or infer measured player travel times from this schedule.

The positive control used the existing Spillway delivery BSP and passed. A
separate negative fixture buried the rocket launcher inside the floor. The
engine reported `droptofloor: weapon_rocketlauncher startsolid`, and the harness
correctly rejected it despite a normal process exit. The delivered Spillway BSP
was not changed.

The retained control reports, logs, exact profiles and entity text are under
`out/counterweight/engine-smoke/controls/control-spillway/` and
`out/counterweight/engine-smoke/controls/control-baditem/`. The adjacent
`create-negative-control.py` records the fixture transformation at its original
scratch location; `qmg_smoke_baditem.bsp` preserves the resulting generated test
fixture for exact reproduction of the smoke gate. Runtime paths
inside those historical reports identify the isolated directories used during
the checks; the copied evidence remains usable after those scratch directories
are removed. The invalid fixture is not a delivered playable map.

`dumpents` exports the original BSP entity text. It does **not** expose live edict
origins, item drop positions or lift state. The standard legacy module provides
no live edict dump command. No player connects, so this check cannot establish
spawn selection, pickup contact, navigation, lift activation, teleporter use,
rendered readability, multiplayer balance or performance under a populated match.
Those remain separate tests.

The test dependency is
[Q2REPRO at aeeb335f](https://github.com/themuffinator/q2repro/tree/aeeb335f9a4de296c73d66b39e2a1efcfe7ade0d),
derived from [Q2PRO](https://github.com/skullernet/q2pro) and id Software's Quake II.
Its engine and legacy game source are GPL-2.0-or-later. They remain separately
installed test dependencies; no engine source is copied or linked into the MIT
generator or this original driver, and no engine binary or stock asset is placed
in the generated map package.

The accepted local binaries are pinned in the driver:

| Dependency | SHA-256 |
| --- | --- |
| `q2reproded.exe` | `6a41422ad60fc3e8679192feb45a9b56da225a29e8d4d09e2a9cd87d824c3053` |
| `gamex86_64.dll` | `9f59a9f0c0c69e746cc0eec708920d8f5a52fcf843e74dee099af601a023a036` |

Changing these pins requires another source review of window/input handling,
asset and write paths, networking, command scheduling and game-module behavior.
