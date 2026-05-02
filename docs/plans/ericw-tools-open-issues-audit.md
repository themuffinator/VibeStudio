# ericw-tools Open Issues Audit

Audit date: 2026-05-02

Upstream source: <https://github.com/ericwa/ericw-tools/issues>

Local submodule: `external/compilers/ericw-tools` at `f80b1e216a415581aea7475cb52b16b8c4859084`

Remaining-pass matrix:
`docs/plans/ericw-tools-remaining-bugs-resolution.md`

This audit covers the 125 open upstream GitHub issues in `ericwa/ericw-tools`,
excluding pull requests. VibeStudio currently treats ericw-tools as an external
compiler submodule and invokes `qbsp`, `vis`, and `light` through wrapper
profiles, so "benefit" here means benefit to VibeStudio through one or more of:

- command-profile options and presets;
- wrapper safety, path handling, manifest, or provenance behavior;
- diagnostics that can be parsed into editor markers;
- map validation before invoking external tools;
- version recommendations and regression tracking;
- future compiler fork/submodule update planning.

## Deduplication Audit

No duplicate issue numbers were found in the open upstream issue set. Several
issues are closely related and should be tracked as implementation clusters in
VibeStudio so one product task can reference multiple upstream issues.

| Cluster | Canonical VibeStudio tracking theme | Upstream issues | Deduplication note |
|---|---|---|---|
| D01 | Compiler provenance and BSPX metadata | #167, #309, #399, #415, #483 | #483 is effectively a modernized form of #167. #309, #399, and #415 are related BSPX/lightmap metadata and documentation work, not duplicates. |
| D02 | Light entity validation | #122, #173, #475 | #475 extends #173 by preserving overwritten style data. #122 is a sibling validation rule for grouped/toggled lights. |
| D03 | Debug-lighting output regressions | #77, #400, #416 | #400 and #416 appear to describe the same `-bouncedebug` breakage. #77 is a separate debug-mode bug. |
| D04 | Parser, path, and warning diagnostics | #87, #105, #135, #245, #287, #288, #293 | These overlap as diagnostic quality work; #87 and #288 are the closest pair. #293 is also privacy and path-cleanup work. |
| D05 | External map and prefab import | #193, #194, #199, #207, #231, #327, #333 | Related prefab/external-map behavior, but each covers a separate failure mode or feature. |
| D06 | Region and partial-compile behavior | #390, #417, #422, #444 | Related region workflows. They should be validated together in VibeStudio's region compile smoke tests. |
| D07 | Lighting, shadows, Phong, bounce, and visual regressions | #91, #109, #134, #145, #172, #185, #190, #203, #214, #217, #221, #238, #247, #255, #256, #262, #268, #291, #349, #351, #364, #375, #377, #389, #401, #405, #439, #456, #484 | Large issue family, not duplicates. VibeStudio should track these as regression cases by compiler version and map feature. |
| D08 | Texture, palette, and asset path support | #192, #201, #346, #450, #451 | Related to nonstandard assets and custom-game workflows. Not duplicates. |
| D09 | CLI, build, packaging, and tool discovery | #215, #223, #225, #289, #335, #435, #449, #480, #482 | Mostly wrapper/distribution concerns. #480 only matters if VibeStudio exposes `lightpreview`. |
| D10 | VIS behavior and optimization | #103, #131, #205, #464 | Related VIS concerns, but distinct features/bugs. |
| D11 | Lightmap, lightgrid, and compile-cost controls | #249, #374, #425, #436, #485 | Related output/performance controls. Not duplicates. |
| D12 | Game-format and total-conversion compatibility | #301, #343, #437, #470 | Related to broader compatibility profiles and per-game validation. |

## Issue Table

| Issue | Nature | Underlying issue | Dedup audit | VibeStudio benefit |
|---|---|---|---|---|
| [#485](https://github.com/ericwa/ericw-tools/issues/485) Add a force override `world_units_per_luxel` setting | Feature | Let a command-line option override per-entity `world_units_per_luxel` values. | D11 | High: useful as a quality preset and temporary override in compile profiles. |
| [#484](https://github.com/ericwa/ericw-tools/issues/484) BSPX `LIGHTINGDIR` unexpected results with `_sunlight2` | Bug | Directional lighting data artifacts appear when `_sunlight2` is enabled for Q2 BSPX output. | D07 | High: important for modern Q2 lighting QA and preview accuracy. |
| [#483](https://github.com/ericwa/ericw-tools/issues/483) Embed light compilation parameters | Metadata feature | Store light command parameters in BSP or BSPX so relighting/provenance is inspectable. | D01, overlaps #167 | High: aligns directly with VibeStudio command manifests and reproducible builds. |
| [#482](https://github.com/ericwa/ericw-tools/issues/482) Cannot build Embree on Linux Mint | Build/docs | Current branch requires newer Embree/TBB setup than the reporter's distro provides. | D09 | High: informs compiler build helper, probe messages, and dependency docs. |
| [#480](https://github.com/ericwa/ericw-tools/issues/480) `lightpreview` will not launch on Arch with Nvidia Card | Runtime/platform | Qt/OpenGL context creation fails under Arch/KDE/Wayland/Nvidia; `xcb` workaround suggested upstream. | D09 | Medium: relevant if VibeStudio launches `lightpreview` or documents workarounds. |
| [#475](https://github.com/ericwa/ericw-tools/issues/475) Preserve `style` set on toggled lights | Feature/diagnostic | Toggled lights overwrite user-set `style`; request to preserve old value and warn on conflicts. | D02, extends #173 | High: good map validation and light-entity inspector rule. |
| [#470](https://github.com/ericwa/ericw-tools/issues/470) Autodetect float vs integer `_minlight` | Compatibility | `_minlight` scale differs across output formats, creating ambiguity for Q1/Q2/HL workflows. | D12 | High: profile-aware warning and conversion aid. |
| [#464](https://github.com/ericwa/ericw-tools/issues/464) `func_viscluster` for VIS optimization | Feature | Add Source-style VIS clustering to reduce VIS complexity in open or messy maps. | D10 | Medium: valuable long-term, but mainly upstream compiler work. |
| [#463](https://github.com/ericwa/ericw-tools/issues/463) `lightpreview` should use temporary directory | Safety bug | Preview builds happen beside the source map and may overwrite outputs. | None | High: VibeStudio should run preview/temporary compiles in isolated work dirs. |
| [#456](https://github.com/ericwa/ericw-tools/issues/456) Wrong shadows between structural meshes and `func_*` entities | Lighting bug | Shadow rendering differs for structural geometry, `func_detail`, `func_detail_illusionary`, and `func_illusionary_visblocker`. | D07 | High: important regression tracking for entity-based map authoring. |
| [#455](https://github.com/ericwa/ericw-tools/issues/455) `func_detail_null` | Feature | Compiler-only brush entity for lighting/shadow work without runtime bmodel cost. | None | Medium: useful advanced authoring profile if upstream implements it. |
| [#451](https://github.com/ericwa/ericw-tools/issues/451) Textures must be under folder named `textures` | Path/asset support | Loose texture lookup hardcodes a `textures` folder, limiting custom engines. | D08 | High: custom-project texture roots should be configurable in VibeStudio. |
| [#450](https://github.com/ericwa/ericw-tools/issues/450) Unable to find texture on non-WAD map compile | Bug | `-notex` and non-WAD workflows still report missing texture data. | D08 | High: important for custom engines and generated BSP post-processing. |
| [#449](https://github.com/ericwa/ericw-tools/issues/449) Use standard C++ format for logging | Internal refactor | Replace third-party `fmt` logging with standard library formatting. | D09 | Low: little direct product benefit unless VibeStudio forks upstream. |
| [#444](https://github.com/ericwa/ericw-tools/issues/444) Region or antiregion brushes prevent Q2 areaportals | Bug | Region compiles can leave areaportals ineffective in Quake 2 maps. | D06 | High: region test compiles need areaportal warnings. |
| [#443](https://github.com/ericwa/ericw-tools/issues/443) Set entity order for map-hack custom models | Feature | Users need stable bmodel/entity ordering for Quake map hacks using `*n` models. | None | Medium: could become an export/order tool or warning. |
| [#441](https://github.com/ericwa/ericw-tools/issues/441) `func_illusionary_visblocker` still does not work as expected | VIS/render bug | New behavior differs from 0.18.1 for visible faces inside visblocker brushes. | D07 | High: version-specific regression worth surfacing. |
| [#439](https://github.com/ericwa/ericw-tools/issues/439) Light bleed on brush model edges | Lighting regression | 2.0 alpha builds show edge light bleed on brush models versus 0.18.2rc1. | D07 | High: warn on affected versions and maintain regression fixtures. |
| [#437](https://github.com/ericwa/ericw-tools/issues/437) HLBSP parity with original GoldSrc compilers | Compatibility | Missing GoldSrc compiler features such as HL entities and behavior parity. | D12 | Medium: useful if VibeStudio expands GoldSrc/HLBSP support. |
| [#436](https://github.com/ericwa/ericw-tools/issues/436) Disable lightmaps for a face or brush | Optimization feature | Allow fullbright/special faces to skip lightmap allocation. | D11 | Medium-high: useful for compile and runtime efficiency profiles. |
| [#435](https://github.com/ericwa/ericw-tools/issues/435) New `bsputil` argument parsing broken | CLI bug | Some `bsputil` operations incorrectly require dummy arguments. | D09 | High: VibeStudio wrappers should smoke-test `bsputil` operations. |
| [#425](https://github.com/ericwa/ericw-tools/issues/425) Speed up lightgrid calculation | Performance feature | Lightgrid computes many unreachable points; request map-marked playable volumes. | D11 | Medium-high: relevant for large Q2/remaster compile UX. |
| [#422](https://github.com/ericwa/ericw-tools/issues/422) Region brush does not cull brush entities | Bug | Region builds can keep brush entities outside the intended region and break entity logic. | D06 | High: partial-compile validation should catch this. |
| [#417](https://github.com/ericwa/ericw-tools/issues/417) Origin brush in rotating door leaks with `region+` | Bug | Q2 region+ builds leak when a rotating door has an origin brush. | D06 | High: strong candidate for known-issue diagnostics. |
| [#416](https://github.com/ericwa/ericw-tools/issues/416) `-bouncedebug` broken | Debug bug | `-bouncedebug` is reported broken. | D03, likely duplicate of #400 | Low-medium: only matters for debug visualization. |
| [#415](https://github.com/ericwa/ericw-tools/issues/415) Describe light-specific BSPX lumps in docs | Docs | Light-related BSPX lumps need documentation. | D01 | Medium: useful for VibeStudio BSPX inspectors. |
| [#413](https://github.com/ericwa/ericw-tools/issues/413) Translucency support | Feature | Add light transmission through thin/opaque materials with optional tinting. | None | Medium: advanced lighting/material workflow. |
| [#411](https://github.com/ericwa/ericw-tools/issues/411) Special texture for decompiled output | Feature | Decompile output needs a special texture that does not generate nodes/clipnodes/faces. | None | Medium: useful for BSP decompile and map round-trip workflows. |
| [#405](https://github.com/ericwa/ericw-tools/issues/405) Phong does not seem to be working right | Lighting bug | `_phong` can produce light bleed on cube edges and confusing `phong_angle` behavior. | D07 | High: map validation can warn about risky Phong settings. |
| [#401](https://github.com/ericwa/ericw-tools/issues/401) `-tex_saturation_boost` changes brightness | Lighting bug | Saturation boost also changes overall brightness in Q2/Kingpin relighting workflows. | D07 | Medium: preset docs and warning value. |
| [#400](https://github.com/ericwa/ericw-tools/issues/400) `-bouncedebug` result all black | Debug bug | Expected indirect-light debug output becomes all black. | D03, likely duplicate of #416 | Low-medium: debug-only, but useful in diagnostics mode. |
| [#399](https://github.com/ericwa/ericw-tools/issues/399) `LMSHIFT` lump not generated in 2.0.0-alpha4 | Regression | Custom `_lmscale` no longer produces expected BSPX lumps. | D01 | High: critical for lightmap scale metadata compatibility. |
| [#390](https://github.com/ericwa/ericw-tools/issues/390) Support multiple region brushes | Feature | Region compiler supports one region brush only; request union of multiple brushes. | D06 | High: improves ergonomic partial builds from editor selection. |
| [#389](https://github.com/ericwa/ericw-tools/issues/389) Q2 surface lights no longer emit on alpha 3 | Regression | Surface lights appear visually bright but do not illuminate the room. | D07 | High: Q2 compile profile regression tracking. |
| [#377](https://github.com/ericwa/ericw-tools/issues/377) Autominlight should be suppressed if bmodel has explicit minlight | Lighting bug | Global autominlight can override explicit bmodel minlight intent. | D07 | Medium-high: candidate validation warning. |
| [#375](https://github.com/ericwa/ericw-tools/issues/375) Q2 relighting old BSPs gives very bright results | Relight bug | Recomputing lighting for existing Q2 BSPs can become fullbright, too bright, or too dark. | D07 | High: relight workflows need version/preset caution. |
| [#374](https://github.com/ericwa/ericw-tools/issues/374) Embedded lightmaps | Feature | Bake lightmaps into diffuse textures for surfaces or engines that cannot use standard lightmaps. | D11 | Medium-high: useful for package/export transformations. |
| [#364](https://github.com/ericwa/ericw-tools/issues/364) Light artifacts on face corners | Regression | Q2 lightmaps show corner/grid point artifacts introduced by a recent commit range. | D07 | High: visual regression fixture candidate. |
| [#351](https://github.com/ericwa/ericw-tools/issues/351) Per-light `bouncescale` broken | Bug | Per-light bounce scale does not work as intended. | D07 | Medium: preset validation can flag this. |
| [#350](https://github.com/ericwa/ericw-tools/issues/350) Leaking Q1 maps have excessive face splitting | Compile bug | Leaks can trigger too much face splitting. | None | High: leak diagnostics and map-health overlays. |
| [#349](https://github.com/ericwa/ericw-tools/issues/349) Dirt keys not working on bmodels | Bug | `_dirt` and `_dirtscale` work on worldspawn but not on bmodels. | D07, related #140 | Medium-high: bmodel lighting validation. |
| [#346](https://github.com/ericwa/ericw-tools/issues/346) PCX and PNG support for external textures | Format support | Extend loose/replacement texture support beyond TGA/WAL/MIP, with PCX still open. | D08 | High: texture editor/package manager value. |
| [#344](https://github.com/ericwa/ericw-tools/issues/344) Bad surface extents bug | Compile/runtime bug | A map crashes in-game, likely from invalid surface extents. | None | High: map compile validation and warnings. |
| [#343](https://github.com/ericwa/ericw-tools/issues/343) Conditional compile of entities | Workflow feature | Maintain alternate entity sets in one map for different mods/builds. | D12 | High: maps cleanly to VibeStudio build profiles. |
| [#342](https://github.com/ericwa/ericw-tools/issues/342) Empty texture names break decompile/extract-textures | Round-trip bug | Unnamed textures decompile/extract with incorrect mapping and scale. | None | Medium: BSP decompile and texture extraction support. |
| [#335](https://github.com/ericwa/ericw-tools/issues/335) Rename `light` and `vis` to less generic names | Packaging | Binary names conflict with other Linux packages. | D09 | Medium: VibeStudio should use descriptors, not bare names alone. |
| [#333](https://github.com/ericwa/ericw-tools/issues/333) Support entities in `_external_map` | Prefab feature | Import whole external maps, including entities, rather than only worldspawn brushes. | D05 | High: core prefab/kitbash workflow. |
| [#327](https://github.com/ericwa/ericw-tools/issues/327) Merge multiple `misc_external_map` entities into one target entity | Prefab feature | Combine many external-map pieces into a single final brush entity. | D05 | Medium-high: advanced prefab composition. |
| [#310](https://github.com/ericwa/ericw-tools/issues/310) Q2 `angle` interferes with `mangle` on light | Lighting bug | Nonzero `angle` stops `mangle` from working on Q2 light entities. | None | Medium: entity inspector warning. |
| [#309](https://github.com/ericwa/ericw-tools/issues/309) Write BSPX lightmap dimensions | Metadata feature | Store compiler-calculated lightmap dimensions so engines and compilers agree. | D01 | Medium-high: helpful for BSPX inspection, partly addressed upstream by newer mechanisms. |
| [#308](https://github.com/ericwa/ericw-tools/issues/308) Brush entity with `origin` key causes leak | Compile bug | Brush entities with `origin` are incorrectly used in leak detection. | None | High: map validation should catch this before compile. |
| [#301](https://github.com/ericwa/ericw-tools/issues/301) Customize hull sizes | Feature | Total conversions need hull sizes beyond stock Quake defaults. | D12 | High: important for custom engine/project profiles. |
| [#298](https://github.com/ericwa/ericw-tools/issues/298) Strip `mapversion 220` | Cleanup | Remove editor metadata to avoid engine warning spam. | None | Medium: package/export cleanup option. |
| [#297](https://github.com/ericwa/ericw-tools/issues/297) Document `_dirt_off_radius` and `_dirt_on_radius` | Docs | Existing dirt radius keys are missing from online docs. | None | Low-medium: expose in help/tooltips if supported. |
| [#293](https://github.com/ericwa/ericw-tools/issues/293) Strip directories from absolute WAD paths | Privacy/path cleanup | Avoid embedding private absolute paths and reduce engine buffer risks. | D04 | High: VibeStudio should sanitize exported entity lumps. |
| [#291](https://github.com/ericwa/ericw-tools/issues/291) Deviance/penumbra does not work on sky light entity | Lighting bug | Penumbra behaves incorrectly for sky-lighting stored on a light entity. | D07 | Medium: warning for non-worldspawn sun setups. |
| [#289](https://github.com/ericwa/ericw-tools/issues/289) Port features from `bjptools bspinfo` | Diagnostics feature | Add richer validation, entity extraction, light stats, PRT generation, and summary tools. | D09 | High: maps to VibeStudio inspection and diagnostics panes. |
| [#288](https://github.com/ericwa/ericw-tools/issues/288) Clearer warning about escape sequences | Diagnostic | Backslashes in entity keys can be parsed unexpectedly; warnings should be clearer. | D04, related #87 | High: editor-visible warning and fix suggestion. |
| [#287](https://github.com/ericwa/ericw-tools/issues/287) Better error messages for common issues | Diagnostic | Common errors should include more specific user-facing guidance. | D04 | High: VibeStudio can layer explanations over compiler logs. |
| [#285](https://github.com/ericwa/ericw-tools/issues/285) Constant color for translucency | Lighting feature | Let translucency use an explicit color. | None | Low-medium: specialized material workflow. |
| [#281](https://github.com/ericwa/ericw-tools/issues/281) `-nosunlight` option | CLI feature | Disable all sun/sunlight sources from the command line. | None | Medium: useful quick compile override. |
| [#278](https://github.com/ericwa/ericw-tools/issues/278) Corrupt BSP misidentified as Hexen2 | Serious bug | A Q1 map can compile/light into a corrupt BSP detected as Hexen2. | None | High: post-compile BSP validation should detect format anomalies. |
| [#277](https://github.com/ericwa/ericw-tools/issues/277) Lighting models at alternate position | Feature | Bake moving/secret door faces using an alternate location or alternate lightmaps. | None | Medium: advanced animated bmodel workflow. |
| [#270](https://github.com/ericwa/ericw-tools/issues/270) Monsters 24 units from floor should block outside-filling | Compile bug | Monsters inconsistently block outside filling in maps with floor-offset entities. | None | High: map-health/leak diagnostics. |
| [#268](https://github.com/ericwa/ericw-tools/issues/268) Deviance not working on `_sun 1` entities | Lighting bug | Sun deviance/penumbra does not work on `_sun 1` entities. | D07 | Medium: related sun lighting warning. |
| [#264](https://github.com/ericwa/ericw-tools/issues/264) Leafs with content `0` | Compile/runtime bug | Hexen2 BSP2 maps can contain leafs with content 0, making entities invisible. | None | High: BSP validation and source-port compatibility. |
| [#262](https://github.com/ericwa/ericw-tools/issues/262) Regression in shadows and `_mirrorinside` | Lighting regression | Shadow quality differs from earlier builds when `_mirrorinside` is involved. | D07 | Medium: versioned regression case. |
| [#257](https://github.com/ericwa/ericw-tools/issues/257) Missing faces on non-integer vertices | Geometry bug | Non-integer brush vertices can produce missing faces due to content assignment issues. | None | High: editor grid/snap validation. |
| [#256](https://github.com/ericwa/ericw-tools/issues/256) Switchable shadow should imply `_shadow 1` | Behavior cleanup | Switchable-shadow key probably should enable shadow casting unless overridden. | D07 | Medium: light/bmodel inspector rule. |
| [#255](https://github.com/ericwa/ericw-tools/issues/255) Switchable shadows incompatible with skip on bmodels | Lighting bug | Skip-textured bmodels interact badly with switchable shadows. | D07 | Medium: compile warning. |
| [#254](https://github.com/ericwa/ericw-tools/issues/254) Planenum error with `-forcegoodtree` | Compile crash | Hexen2 BSP2 with `-forcegoodtree` can hit an assertion. | None | Medium-high: avoid or warn on risky flag combinations. |
| [#249](https://github.com/ericwa/ericw-tools/issues/249) Drop legacy `-lmscale/-lmshift` version | Compatibility cleanup | Prefer modern BSPX lightmap-scale metadata over legacy behavior. | D11 | Low: mainly upstream cleanup, but affects compatibility docs. |
| [#247](https://github.com/ericwa/ericw-tools/issues/247) Let `misc_model` cast shadow | Feature | Allow model geometry to cast shadows during light compile. | D07 | Medium-high: valuable for foliage/prop-heavy maps. |
| [#246](https://github.com/ericwa/ericw-tools/issues/246) Brightness/contrast adjustment to final lightmap | Lighting feature | Add global post-adjustment of lightmap brightness/contrast. | None | Low-medium: can be a preset if implemented. |
| [#245](https://github.com/ericwa/ericw-tools/issues/245) Warn about excessively long key values | Diagnostic | Long entity key values, especially WAD lists, can crash engines. | D04 | High: pre-compile validation and export sanitization. |
| [#241](https://github.com/ericwa/ericw-tools/issues/241) Brush as Light | Feature | Use brush volumes as lighting design tools, ignored/skipped by `qbsp`. | None | Medium: editor-visible lighting authoring concept. |
| [#238](https://github.com/ericwa/ericw-tools/issues/238) Sunlight penumbra pitch/yaw deviation | Lighting bug | Penumbra deviates differently in pitch vs yaw when sunlight is mostly vertical. | D07 | Medium: sun-lighting regression case. |
| [#234](https://github.com/ericwa/ericw-tools/issues/234) Global HSV adjustment | Lighting feature | Add global hue/saturation/value adjustment. | None | Low: optional post-lighting control. |
| [#233](https://github.com/ericwa/ericw-tools/issues/233) Entity has no valid brushes with `func_detail_illusionary` | Compile bug | Certain generated or model-style maps warn/fail with no valid brushes. | None | Medium: map validation can explain. |
| [#231](https://github.com/ericwa/ericw-tools/issues/231) `misc_external_map` `_phong` ignored | Prefab/light bug | `_phong` and special lighting keys on the external-map entity are not forwarded. | D05 | High: important for prefab lighting fidelity. |
| [#230](https://github.com/ericwa/ericw-tools/issues/230) Double extension not handled correctly | Path bug | `light` drops part of names such as `my-map.1.bsp`. | None | High: wrapper path tests should cover dotted filenames. |
| [#225](https://github.com/ericwa/ericw-tools/issues/225) `bspinfo` write log file | CLI feature | Add log-file output to `bspinfo`. | D09 | Low: VibeStudio already captures process output. |
| [#223](https://github.com/ericwa/ericw-tools/issues/223) Rename `-Darwin` package suffix to `-macOS` | Packaging | Release artifact naming cleanup. | D09 | Low: upstream release packaging only. |
| [#222](https://github.com/ericwa/ericw-tools/issues/222) Set Q2 rangescale in a cleaner way | Internal cleanup | Refactor how default Q2 rangescale is represented. | None | Low: little wrapper-level impact. |
| [#221](https://github.com/ericwa/ericw-tools/issues/221) Sunlight artifacts shining through world | Lighting bug | Sunlight leaks through world/sky geometry in a test case. | D07 | High: map-health and lighting regression tracking. |
| [#217](https://github.com/ericwa/ericw-tools/issues/217) `_surface` light entity issues with liquids | Lighting bug | Liquid surface light names/settings collide or are ignored unexpectedly. | D07 | Medium-high: texture/light validation. |
| [#215](https://github.com/ericwa/ericw-tools/issues/215) Flatpak bundle for Linux distros | Packaging | Package ericw-tools as Flatpak/Flathub. | D09 | Low: external packaging, though useful for discovery docs. |
| [#214](https://github.com/ericwa/ericw-tools/issues/214) `-gate 4` causes sunlight issues | Lighting bug | High gate value can prevent sunlight from appearing. | D07 | Medium: risky-flag warning. |
| [#213](https://github.com/ericwa/ericw-tools/issues/213) Valve convert postfix unnecessary with destination filename | CLI/path bug | `qbsp -convert valve` still adds `-valve` even when destination was supplied. | None | Medium: wrapper path expectation tests. |
| [#209](https://github.com/ericwa/ericw-tools/issues/209) `_project_texture` on `_surface` lights broken | Lighting bug | Projected texture support fails on surface lights. | None | Medium: advanced lighting warning. |
| [#207](https://github.com/ericwa/ericw-tools/issues/207) External map with all brushes in `func_group` gets corrupted | Prefab bug | External-map test case corrupts when content is entirely grouped. | D05 | High: prefab import regression tracking. |
| [#205](https://github.com/ericwa/ericw-tools/issues/205) One-way VIS leafs | VIS feature | Let windows see outward but block visibility inward. | D10 | Medium: specialized VIS optimization. |
| [#203](https://github.com/ericwa/ericw-tools/issues/203) Disable bounce on suns/sunlight2 | Lighting feature | Command/key support to suppress bounce from sun sources. | D07 | Medium: useful profile override. |
| [#201](https://github.com/ericwa/ericw-tools/issues/201) `qbsp` `-outputdir` and `-waddir` flags | CLI feature | Add explicit output and WAD search directories. | D08 | High: strongly benefits wrapper-controlled working directories. |
| [#200](https://github.com/ericwa/ericw-tools/issues/200) Random texture tiling | Feature | Add randomization for texture tiling. | None | Low: stylistic authoring feature. |
| [#199](https://github.com/ericwa/ericw-tools/issues/199) Map-file-relative paths for external map prefabs | Path/prefab feature | `_external_map` paths should resolve relative to the source map file. | D05 | High: essential project portability behavior. |
| [#195](https://github.com/ericwa/ericw-tools/issues/195) AD `start.bsp` odd looking face | Visual bug | Specific Arcane Dimensions face renders oddly. | None | Low-medium: compatibility fixture only. |
| [#194](https://github.com/ericwa/ericw-tools/issues/194) Crash if `_external_map_classname` is not set | Crash bug | Missing classname on `misc_external_map` triggers assertion; default `func_detail` suggested. | D05 | High: VibeStudio can validate before compile. |
| [#193](https://github.com/ericwa/ericw-tools/issues/193) `0 360 0` rotation on external map entities | Prefab transform bug | External-map rotations can produce weird shadows. | D05 | Medium-high: prefab transform validation. |
| [#192](https://github.com/ericwa/ericw-tools/issues/192) Specify custom palette for Light | Mod support | Bounce lighting hardcodes or assumes Quake palette; request custom palette. | D08 | High: important for custom palettes and total conversions. |
| [#190](https://github.com/ericwa/ericw-tools/issues/190) Messed up lighting | Lighting bug | Example map compiled with bounce/soft/extra4 shows incorrect lighting. | D07 | Medium: regression fixture if reproducible. |
| [#189](https://github.com/ericwa/ericw-tools/issues/189) `_noskydeletion` for brushes inside sky | Feature | Preserve selected sky-contained brushes for skybox/lightmap workflows. | None | Medium: advanced skybox/lightmap authoring. |
| [#186](https://github.com/ericwa/ericw-tools/issues/186) Support `_falloff` on delay 1/2/5 | Lighting feature | Extend falloff control to more delay modes. | None | Low-medium: profile/UI option if implemented. |
| [#185](https://github.com/ericwa/ericw-tools/issues/185) Black seams on specific maps | Lighting bug | Investigate black seams on `shib5.bsp`/`shib1.bsp`. | D07 | Medium: visual regression fixture. |
| [#173](https://github.com/ericwa/ericw-tools/issues/173) Warn if `style` is set on toggled light | Diagnostic | Toggled lights with explicit style need warning. | D02, extended by #475 | High: simple pre-compile validation. |
| [#172](https://github.com/ericwa/ericw-tools/issues/172) Invalid spotlight on models with `_phong 1` | Lighting bug | Spotlighting behaves incorrectly on Phong-smoothed models. | D07 | Medium-high: Phong/light interaction warning. |
| [#167](https://github.com/ericwa/ericw-tools/issues/167) Save commandline options and tools version to BSP | Metadata feature | Store `_light_cmdline` and `_light_ver` in BSP. | D01, older form of #483 | High: direct provenance and manifest synergy. |
| [#145](https://github.com/ericwa/ericw-tools/issues/145) Artifacts on negke's map | Lighting bug | Specific map has H-shaped artifacts around angled beam/pedestals. | D07 | Medium: regression fixture. |
| [#140](https://github.com/ericwa/ericw-tools/issues/140) Dirt options on `func_group/detail/wall` | Feature | Support `_dirtmode`, `_dirtdepth`, `_dirtscale`, `_dirtangle`, and `_dirtgain` beyond worldspawn. | D07, related #349 | Medium: bmodel/entity lighting controls. |
| [#136](https://github.com/ericwa/ericw-tools/issues/136) Higher clipnodes than txqbsp-xt | Optimization | Investigate increased clipnode/marksurface counts versus other compilers. | None | Medium-high: compile metrics and comparison view. |
| [#135](https://github.com/ericwa/ericw-tools/issues/135) Show entity coordinates for parse warning | Diagnostic | Warning about invalid numeric key values should identify entity location. | D04 | High: maps directly to clickable diagnostics. |
| [#134](https://github.com/ericwa/ericw-tools/issues/134) Hard lines in shadows/dirtmap | Lighting regression | Regression introduced hard lines and lighting differences in dirt/shadow output. | D07 | Medium-high: versioned visual regression. |
| [#131](https://github.com/ericwa/ericw-tools/issues/131) `visapprox` can get tripped up | Heuristic issue | Long falloff light through occluding bars can defeat `visapprox`. | D10 | Medium: warn when approximate lighting may be misleading. |
| [#129](https://github.com/ericwa/ericw-tools/issues/129) Move `\b` handling to `qbsp` | Parser behavior | Escape/backslash handling should move earlier in the compile pipeline. | None | Medium: parser consistency for map validation. |
| [#122](https://github.com/ericwa/ericw-tools/issues/122) Warn about differing `START_OFF` flag | Diagnostic | Lights sharing a targetname should consistently set or unset start-off spawnflag. | D02 | High: straightforward light-group validation. |
| [#121](https://github.com/ericwa/ericw-tools/issues/121) Confirm whether `entdict_t` keys are case-insensitive | Parser semantics | Need to establish entity key case-sensitivity expectations. | None | Medium: informs editor validation and normalization. |
| [#114](https://github.com/ericwa/ericw-tools/issues/114) `testmaps/csg_fail.map` issue | Compile bug | Wrong leaf contents type is assigned in the test map. | None | High: geometry/content validation fixture. |
| [#109](https://github.com/ericwa/ericw-tools/issues/109) Nudge lights on surface of submodels | Lighting bug | Surface lights should be nudged on submodels as well as world surfaces. | D07 | Medium: bmodel lighting correctness. |
| [#106](https://github.com/ericwa/ericw-tools/issues/106) Support bounce on specific lights or sunlight | Lighting feature | Add per-light or sunlight bounce control. | None | Medium: advanced lighting profile control. |
| [#105](https://github.com/ericwa/ericw-tools/issues/105) Confusing `_sunlight_mangle` status output | Diagnostic | Print result in mangle format. | D04 | Low-medium: improves log parsing and UX. |
| [#103](https://github.com/ericwa/ericw-tools/issues/103) Hint brush issues | VIS bug | Hint brushes reportedly fail with liquids and horizontal layouts. | D10 | Medium: VIS troubleshooting guidance. |
| [#91](https://github.com/ericwa/ericw-tools/issues/91) Bounce lights on contrasting textures | Lighting quality | Bounce color averaging should use face subdivisions, not whole-texture averaging. | D07 | Medium: lighting quality regression/feature tracking. |
| [#87](https://github.com/ericwa/ericw-tools/issues/87) Revisit escape sequence warnings in `qbsp` | Diagnostic/parser | Double-quote escaping and WAD path warnings need better handling. | D04, related #288 | High: preflight warnings can prevent confusing compiler output. |
| [#81](https://github.com/ericwa/ericw-tools/issues/81) Flip projected textures | Texture/light feature | Add a way to flip projected textures. | None | Low: specialized light projection workflow. |
| [#77](https://github.com/ericwa/ericw-tools/issues/77) `-dirtdebug` draws AO despite `_dirt -1` | Debug bug | Dirt debug ignores an entity disabling dirt. | D03 | Low-medium: debug output correctness. |

## VibeStudio Recommendations

1. Maintain the implemented known-issues layer in compiler profiles, with
   first-class warnings for high-value and remaining-pass issues that can be
   explained locally. User-facing messages must include upstream issue IDs and
   must not imply that VibeStudio has fixed ericw-tools compiler algorithms.
2. Use `docs/plans/ericw-tools-remaining-bugs-resolution.md` as the acceptance
   matrix for the 62 remaining-pass IDs. Keep that matrix grouped by resolution
   status instead of adding duplicate issue rows to this audit table.
3. Continue expanding compiler smoke tests beyond the current coverage for path
   safety, map preflight, external-map hazards, dotted filenames, isolated
   temporary directories, manifest warning propagation, known-issue catalog
   matching, helper readiness, and artifact validation.
4. Keep upstream issues as references rather than duplicating them into
   separate VibeStudio bugs unless VibeStudio can mitigate the behavior in its
   wrapper, UI validation, command planning, helper probes, or artifact gates.
5. Prefer wrapper-level mitigations first: isolated temporary directories,
   command manifests, path diagnostics, executable probes, preflight map checks,
   and post-run validation before promotion or packaging.

## High-Value Mitigation Plan

VibeStudio should split high-value ericw-tools issues into product-owned
mitigations and upstream-only fixes. Product-owned work is valuable when it can
prevent a bad compile, explain a known failure, preserve reproducibility, or
make a risky workflow explicit before invoking an external executable.
Upstream-only work should remain linked from diagnostics and regression notes so
users can see whether a newer ericw-tools revision is expected to help.

| Area | Upstream issues | VibeStudio resolution | Remaining upstream-only behavior |
|---|---|---|---|
| Provenance and BSPX metadata | #167, #309, #399, #415, #483 | Implemented: command plans/manifests store executable provenance, arguments, input/output paths, hashes, parsed diagnostics, and separate `knownIssueWarnings`/`preflightWarnings`. Planned: post-compile checks for profile-required BSPX metadata. | Embedding command lines, versions, or lightmap dimensions directly into BSP/BSPX must be implemented by ericw-tools. |
| Path privacy and portable project layout | #87, #199, #201, #230, #245, #288, #293, #450, #451 | Implemented: preflight warns on absolute WAD paths, long entity values, suspicious backslashes, external-map path portability, dotted light input names, `-notex`, and working-directory fragility. | Native compiler path resolution, warning wording, and new `-outputdir`/`-waddir` support remain upstream features. |
| Light entity validation | #122, #173, #310, #351, #377, #405, #470, #475 | Implemented: preflight warns on grouped/toggled light conflicts, `START_OFF` mismatches, Q2 `angle`/`mangle`, `_minlight`, Phong on brush entities, and selected known-risk light arguments or keywords. | Correct preservation of overwritten styles, lighting math fixes, and changed entity semantics remain upstream fixes. |
| Region and partial compiles | #390, #417, #422, #444 | Implemented: preflight warns on multiple region markers, areaportals with region markers, rotating brush entities with origin keys, and region workflows that may retain out-of-region brush entities. | Multiple-region support and compiler-side areaportal/entity culling fixes remain upstream work. |
| External map and prefab workflows | #193, #194, #199, #207, #231, #327, #333 | Implemented: preflight warns on missing `_external_map_classname`, nonportable external-map paths, prefab rotations containing `360`, Phong keys on `misc_external_map`, and limited external-map entity import. Planned: deeper grouped-prefab composition checks. | Correct import of entities, grouped prefabs, merged target entities, Phong forwarding, and transform-shadow fixes remain upstream work. |
| Compiler discovery, packaging, and helper tools | #335, #435, #463, #480, #482 | Implemented: descriptor-based tool discovery, known-issue diagnostics, manifest provenance, missing-working-directory refusal, and isolated `TMP`/`TEMP`/`TMPDIR` for compiler processes. Planned: `bspinfo`/`bsputil` operation smoke tests and optional `lightpreview` launch diagnostics. | Native packaging names, `bsputil` parser fixes, `lightpreview` launch behavior, and upstream build dependency changes remain upstream work. |
| Visual and compile correctness regressions | #77, #91, #109, #134, #145, #172, #185, #190, #203, #214, #217, #221, #238, #247, #255, #256, #262, #268, #291, #349, #351, #364, #375, #377, #389, #400, #401, #405, #416, #439, #441, #456, #484 | Implemented: known-issue catalog and keyword/profile warnings for selected high-value lighting/debug risks. Planned: representative visual regression fixtures and broader version-specific checks. | Lighting, shadow, VIS, and debug image correctness must be fixed in ericw-tools. VibeStudio should not claim output parity from wrapper checks alone. |

## Documentation And Test Plan Updates

Documentation should keep these boundaries visible in three places:
- `docs/COMPILER_INTEGRATION.md` explains the known-issue mitigation model,
  including which risks wrappers and preflight checks can own.
- `docs/plans/ericw-tools-high-value-resolution.md` tracks implementation
  sequencing, user-facing diagnostics, and regression tests for the high-value
  subset.
- CLI plan JSON already exposes `knownIssueWarnings` and `preflightWarnings`;
  future GUI help text should reference the same known-issue IDs when a warning
  is actionable by changing a profile, path, map entity, or compiler version.

Current test coverage includes user-visible behavior for:
- Preflight fixtures for WAD path privacy, long entity values, escape sequences,
  external-map path/classname hazards, grouped/toggled light conflicts,
  `START_OFF` mismatches, Q2 `angle`/`mangle`, brush-entity origins, and region
  origin-brush risk.
- Compiler profile fixtures for known-issue warnings, preflight warning
  propagation, dotted filenames, manifest warning fields, isolated compiler
  temporary directories, parsed diagnostics, output registration, and missing
  working-directory refusal.
- Known-issue catalog fixtures for issue lookup, cluster lookup, profile
  filtering, keyword matching, warning/action text, and upstream URLs.

Remaining test-plan gaps:
- Texture-root ambiguity, Q2 surface lighting, BSPX metadata validation,
  `bspinfo`/`bsputil` operation probes, build dependency diagnostics, and
  visual regression fixtures.
