# External Compiler Imports

This directory contains level compiler and node-builder sources imported as Git
submodules. They are preserved as external projects so VibeStudio can track
upstream history, licenses, and local patches cleanly.

## Imported Toolchains
| Path | Upstream | Role |
|---|---|---|
| `ericw-tools` | [ericwa/ericw-tools](https://github.com/ericwa/ericw-tools) | Quake/idTech2 BSP, VIS, and lighting tools. |
| `netradiant-custom` | [Garux/netradiant-custom](https://github.com/Garux/netradiant-custom) | NetRadiant Custom source tree containing q3map2 at `tools/quake3/q3map2`. |
| `zdbsp` | [rheit/zdbsp](https://github.com/rheit/zdbsp) | Doom-family node builder. |
| `zokumbsp` | [zokum-no/zokumbsp](https://github.com/zokum-no/zokumbsp) | Doom-family node, blockmap, and reject builder. |

## Setup
```sh
git submodule update --init --recursive
```

## Maintenance Rules
- Do not paste upstream compiler code elsewhere in the repository without a documented reason.
- Keep `README.md`, `docs/CREDITS.md`, and `docs/COMPILER_INTEGRATION.md` updated when a compiler submodule is added, removed, updated, forked, or patched.
- Keep local patches small and explain why they cannot live upstream.
- Treat compiled compiler binaries as separate deliverables with their own license bundle.
