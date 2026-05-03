# Support Matrix

This matrix describes the current support contract. Items marked `planned`
are not implemented yet; items marked `MVP active` are implemented as focused
vertical slices and still have full-production depth planned.

## Engine Families
| Family | Status | Notes |
|---|---|---|
| idTech1 / Doom family | MVP active | WAD/PWAD browsing and writing, Doom map inspection/edit slices, sprite naming plans, ZDBSP/ZokumBSP command plans, and node-builder diagnostics active; Hexen/UDMF depth planned. |
| idTech2 / Quake family | MVP active | PAK/folder/WAD browsing, Quake `.map` inspection/edit slices, MDL/MD2 metadata, sprite plans, and ericw-tools command plans/runs active; full BSP/model editing planned. |
| idTech3 / Quake III family | MVP active | PK3/ZIP browsing and writing, Quake III `.map` inspection, MD3 metadata, shader graph/script round-tripping, and q3map2 plans/runs active; full BSP/model editing planned. |

## Studio Workflows
| Workflow | Status |
|---|---|
| Workspace/project hub | scaffolded with current project manifest, project-local overrides, health dashboard, problems/search/changes/timeline panels, and detail drawers |
| User-visible task/loading/progress feedback | scaffolded for setup, activity, package scanning, and package extraction |
| Status chips, shortcuts, and command palette semantics | shared status chip registry, non-color cues, shortcut registry, conflict smoke checks, and command-palette metadata active; full interactive palette UI planned |
| Progressive detail disclosure | scaffolded for inspector, activity, package entries, package previews, maps, and Advanced Studio shader/sprite/code/AI/extension details |
| Graphical project/package/compiler state views | scaffolded for project health, package composition, map health/statistics, compiler pipeline readiness, shader stage graphs, and dependency graph placeholders; run graphs planned |
| High-visibility themes | preference and setup controls active; smoke/audit gate active; full editor-surface coverage planned |
| UI/text scaling settings | preference and setup controls active for 100%, 125%, 150%, 175%, and 200%; full layout audits ongoing |
| Keyboard and screen-reader accessibility | accessible shell metadata and release audit gate active; full editor audits planned |
| OS-backed text-to-speech | enablement preference and smoke-path documentation active; playback engine integration planned |
| 20-language localization target | locale selection, shared target registry, seed Qt TS catalogs, pseudo-localization, Arabic/Urdu RTL smoke, `QLocale` formatting, pluralization samples, translation expansion stress and layout smoke checks, dry-run `lupdate` extraction validation, and stale/untranslated catalog reporting active; runtime `QTranslator` loading and complete translated bundles planned |
| First-run setup flow | scaffolded with skip/resume/later states, including a game installation step |
| Game installation detection | manual profiles plus confirmable Steam/GOG candidate detection active; source-port auto-detection planned |
| Package manager | folder/PAK/WAD/ZIP/PK3 browsing scaffolded from PakFu lineage; tree/list browsing, text/image/model/audio/script metadata previews, safe selected/all extraction, staged add/replace/rename/delete, package manifests, and save-as PAK/ZIP/PK3/tested PWAD writers active |
| Compiler registry | imported compiler descriptors, executable discovery, version probing, ericw-tools, Doom node-builder, and q3map2 wrapper plans/manifests active; broader live health monitoring planned |
| MVP sample projects | tiny Doom, Quake, and Quake III-family sample workspaces active for CLI/CI smoke checks |
| Level editor | MVP active for map inspect/edit/move/save-as, profile selection, health summaries, and compile-plan/run handoff; full Radiant/Doom Builder-class editing planned |
| GtkRadiant 1.6.0-style editor profile | routed MVP preset active with stable command IDs, layout/camera/selection/grid/shortcut metadata, and conflict smoke coverage; full fidelity planned |
| NetRadiant Custom-style editor profile | routed MVP preset active with stable command IDs, layout/camera/selection/grid/shortcut metadata, and conflict smoke coverage; full fidelity planned |
| TrenchBroom-style editor profile | routed MVP preset active with stable command IDs, layout/camera/selection/grid/shortcut metadata, and conflict smoke coverage; full fidelity planned |
| QuArK-style editor profile | routed MVP preset active with stable command IDs, layout/camera/selection/grid/shortcut metadata, and conflict smoke coverage; full fidelity planned |
| Model tools | metadata preview active for MDL/MD2/MD3 with native-loader boundary, viewport summary, skin/material dependencies, animation names, and export hook metadata; full editing planned |
| Texture editor | metadata preview, palette-aware inspection, crop/resize/palette conversion, and batch conversion CLI active; full paint/sprite editing planned |
| Audio editor | WAV metadata, platform playback candidacy, waveform summary, and WAV export helper active; compressed decoding/playback fallback planned |
| Sprite creator | Doom lump naming, Quake sprite sequence plans, palette preview notes, frame rotations, and package staging paths active; full paint/edit UI planned |
| Code/script IDE | CFG, shader, QuakeC, and idTech text highlighter/diagnostic boundaries, project find/replace, source tree indexing, language hook descriptors, diagnostics, symbol search, build task hints, and launch profile summaries active; full IDE planned |
| idTech3 shader graph | shader script parsing, stage/dependency graph lines, stage previews, raw text detail, stage directive round-tripping, and mounted package-reference validation active |
| Compiler orchestration | ericw-tools `qbsp`/`vis`/`light`, ZDBSP/ZokumBSP, and q3map2 command planning, task-log manifests, watch/task-state output, process stdout/stderr capture, and output registration active |
| Full-featured CLI | subcommand router, command registry, JSON output, quiet/verbose, stable exit codes, diagnostics, package inspect/extract/validate, asset inspect/convert/audio-wav/find/replace, map inspect/edit/move/compile-plan, shader inspect/set-stage, sprite plan, code index, extension discover/inspect/run, project/install/compiler workflows, localization reports, diagnostic bundle export, credits validation, and AI experiment commands active; broader release workflow coverage planned |
| Provider-neutral AI connector workflows | opt-in settings, connector/model registry, redacted credential status, safe tool descriptors, no-write manifests, shader/entity/package/batch/CLI proposal commands, AI proposal review surfaces, and first CLI experiments active; provider network execution planned |
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
