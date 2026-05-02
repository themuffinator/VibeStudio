# Support Matrix

This matrix describes the intended support contract. Items marked `planned`
are not implemented in the initial scaffold.

## Engine Families
| Family | Status | Notes |
|---|---|---|
| idTech1 / Doom family | planned | WAD/IWAD/PWAD, Doom-format maps, Hexen-format maps, UDMF, node builders. |
| idTech2 / Quake family | planned | PAK, WAD2/WAD3, BSP, MAP, MDL/MD2, WAL/MIP textures, ericw-tools pipeline. |
| idTech3 / Quake III family | planned | PK3, BSP, MAP, MD3/MDC/MDR, shader scripts, q3map2 pipeline. |

## Studio Workflows
| Workflow | Status |
|---|---|
| Workspace/project hub | scaffolded with current project manifest, project-local overrides, health dashboard, problems/search/changes/timeline panels, and detail drawers |
| User-visible task/loading/progress feedback | scaffolded for setup, activity, package scanning, and package extraction |
| Progressive detail disclosure | scaffolded for inspector, activity, package entries, and package previews |
| Graphical project/package/compiler state views | scaffolded for project health, package composition, compiler pipeline readiness, and dependency graph placeholders; run graphs planned |
| High-visibility themes | preference and setup controls active; smoke/audit gate active; full editor-surface coverage planned |
| UI/text scaling settings | preference and setup controls active for 100%, 125%, 150%, 175%, and 200%; full layout audits ongoing |
| Keyboard and screen-reader accessibility | accessible shell metadata and release audit gate active; full editor audits planned |
| OS-backed text-to-speech | enablement preference and smoke-path documentation active; playback engine integration planned |
| 20-language localization target | locale selection and release audit gate active; translation catalogs and runtime loading planned |
| First-run setup flow | scaffolded with skip/resume/later states, including a game installation step |
| Game installation detection | manual profiles plus confirmable Steam/GOG candidate detection active; source-port auto-detection planned |
| Package manager | read-only folder/PAK/WAD/ZIP/PK3 browsing scaffolded from PakFu lineage; tree/list browsing, text/image/binary metadata previews, and safe selected/all extraction active; write-back planned |
| Compiler registry | imported compiler descriptors, executable discovery, ericw-tools, Doom node-builder, and q3map2 wrapper plans/manifests active; version probing and process run results planned |
| MVP sample projects | tiny Doom, Quake, and Quake III-family sample workspaces active for CLI/CI smoke checks |
| Level editor | planned |
| GtkRadiant 1.6.0-style editor profile | placeholder preset active; behavior wiring planned |
| NetRadiant Custom-style editor profile | placeholder preset active; behavior wiring planned |
| TrenchBroom-style editor profile | placeholder preset active; behavior wiring planned |
| QuArK-style editor profile | placeholder preset active; behavior wiring planned |
| Model tools | planned |
| Texture editor | planned |
| Audio editor | planned |
| Sprite creator | planned |
| Code/script IDE | planned |
| idTech3 shader graph | planned |
| Compiler orchestration | ericw-tools `qbsp`/`vis`/`light`, ZDBSP/ZokumBSP, and q3map2 command planning, task-log manifests, watch/task-state output, process stdout/stderr capture, and output registration active |
| Full-featured CLI | subcommand router, command registry, JSON output, quiet/verbose, stable exit codes, diagnostics, package inspect/extract/validate, project/install/compiler workflows, credits validation, and AI experiment commands active; broader release/asset workflow coverage planned |
| Provider-neutral AI connector workflows | opt-in settings, connector/model registry, redacted credential status, safe tool descriptors, no-write manifests, and first CLI experiments active; provider network execution planned |
| Portable packaging | Windows/macOS/Linux target package scripts, offline guide, checksums, license bundle, and release asset validation active; Qt deployment/signing planned |
| About/Credits/license surface | GUI inspector and CLI surface active; modal About dialog planned later if needed |
| OpenAI connector | first general-purpose connector scaffold active for configuration, model routing, credential discovery, and no-write workflow experiments |
| Claude connector | design stub active |
| Gemini connector | design stub active |
| ElevenLabs connector | design stub active for audio/voice experiments |
| Meshy connector | design stub active for 3D/texture generation experiments |
| Local/offline AI connector | design stub active for user-configured local runtimes |

## Imported Compilers
| Tool | Status |
|---|---|
| ericw-tools | imported as submodule |
| q3map2-nrc | q3map2 from NetRadiant Custom imported as submodule |
| ZDBSP | imported as submodule |
| ZokumBSP | imported as submodule |
