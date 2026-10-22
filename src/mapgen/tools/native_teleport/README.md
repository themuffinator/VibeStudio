# Optional stock Quake II teleporter host

> **VibeStudio:** imported as source only. VibeStudio's Meson build never compiles or links
> `host.c` or `server_bridge.c`; this directory's `CMakeLists.txt` remains Quake-MapGen's separate,
> private GPL build (`python -B -m tools.native_teleport.build`, run from `src/mapgen`), and nothing it
> produces is linked into or shipped with VibeStudio. See [VIBESTUDIO.md](../../VIBESTUDIO.md).

This separate headless host links the unmodified local [id Software Quake II 3.19 source](https://github.com/id-Software/Quake-2): the complete stock game module, server entity-world queries, CM and Pmove. It does not launch an engine client, read input devices, create sockets or modify an installation. Fixed synthetic `ClientThink` calls exercise the game API; they are not human input.

The repository is MIT. The new API bridge and Python orchestration are original MIT code. The external reference headers and `readme.txt` identify GPLv2-or-later, and its `gnu.txt` was reviewed before linking. MIT glue is compatible with the GPL combined executable. That executable remains a private optional GPL test dependency under `.agents/tmp/relay-native-v1`; no external source is copied into the repository, no external binary/source/assets are packaged with maps, and there is no install/package target. This is the same licence boundary as the older independent world-only test harness, whose files and evidence remain unchanged.

Build inputs are hash-pinned in `reference.json`. Build provenance, fixture changes, commands, raw states and diagnostics belong under `out/native-teleport`. No rendering, networking, client prediction or Q2REPRO equivalence is claimed. The host's resource registries stand in for asset indexing only, and reject unsupported file or command operations. All collision, entity-area membership, trigger dispatch, movement, damage and player-death behavior uses the external native code.

The official module clears the immediate view vectors in `teleporter_touch`; the local Q2REPRO legacy source instead copies destination angles there. Stock immediate state and next-`ClientThink` facing must therefore be reported separately.
