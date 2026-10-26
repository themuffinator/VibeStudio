# External Compiler Imports

This directory contains level compiler and node-builder sources imported as Git
submodules. They are preserved as external projects so VibeStudio can track
upstream history, licenses, and local patches cleanly.

VibeMap2 and VibeMap3 are VibeStudio's own compilers, developed as part of the
project in their own repositories: VibeMap2 is derived from
[ericw-tools](https://github.com/ericwa/ericw-tools) and VibeMap3 continues
q3map2 from [NetRadiant Custom](https://github.com/Garux/netradiant-custom).
Both keep their upstream history and licence notices. Their `.gitmodules` URLs
are relative (`../VibeyMapTools.git`, `../q3mapx.git`), so they resolve next to
whichever VibeStudio remote you cloned. See `docs/COMPILER_INTEGRATION.md` for
the pinned revisions, executables and build layouts.

## Imported Toolchains
| Path | Upstream | Role |
|---|---|---|
| `vibemap2` | [themuffinator/VibeyMapTools](https://github.com/themuffinator/VibeyMapTools) (to be renamed VibeMap2) | VibeMap2: Quake/idTech2 BSP, VIS and lighting (`vibemap2-bsp`, `vibemap2-vis`, `vibemap2-light`), the `vibemap2-bspinfo`, `vibemap2-bsputil` and `vibemap2-maputil` utilities, and the `vibemap2-hub` build and preview GUI. Derived from ericw-tools. |
| `vibemap3` | [themuffinator/q3mapx](https://github.com/themuffinator/q3mapx) (to be renamed VibeMap3) | VibeMap3: Quake III/idTech3 BSP compile, VIS, light, conversion and packaging (`vibemap3`, plus the `vibemap3-workbench` GUI). Continues q3map2 from NetRadiant Custom; the compiler source keeps the upstream layout at `tools/quake3/q3map2`. |
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
