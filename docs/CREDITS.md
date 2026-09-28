# VibeStudio Credits

## Project
- Creator and lead: [themuffinator](https://github.com/themuffinator) (DarkMatter Productions)

## Core Technology
- [Qt 6](https://www.qt.io/product/qt6)
  - Application framework and primary UI toolkit.
- [Qt Accessibility](https://doc.qt.io/qt-6/accessible.html), [Qt TextToSpeech](https://doc.qt.io/qt-6/qttexttospeech-index.html), and [Qt internationalization](https://doc.qt.io/qt-6/internationalization.html)
  - Planned accessibility, OS-backed speech, and localization support.
- [Meson](https://mesonbuild.com/) and [Ninja](https://ninja-build.org/)
  - Build configuration and build execution.

## PakFu Lineage
VibeStudio intentionally uses [PakFu](https://github.com/themuffinator/PakFu)
as a reference for repository layout, Meson/Qt conventions, README style,
GitHub Actions structure, archive/package handling, game installation
detection, parser hardening, and credits practice.

When PakFu code is moved into VibeStudio, this file must name the source module
and revision when practical.

Current adapted modules:

| VibeStudio area | PakFu reference | Revision | Notes |
|---|---|---|---|
| Package/archive interface, read-only readers, and virtual path safety in `src/core/package_archive.*` | `src/archive/archive_entry.h`, `src/archive/archive.h`, `src/archive/archive_session.h`, `src/archive/dir_archive.*`, `src/archive/path_safety.h`, `src/pak/pak_archive.*`, `src/wad/wad_archive.*`, and `src/zip/zip_archive.*` | `c82dfb0ef0b5d7442e243ace8cd83bc45f82f257` | VibeStudio reimplements the concepts as a smaller core layer with explicit path-safety issue reporting, normalized virtual paths, safe output joining, folder/PAK/WAD/ZIP/PK3 entry listing, and mount-layer metadata. |
| Package staging, manifest, and save-as writer concepts in `src/core/package_staging.*` | PakFu archive write-back direction, including deterministic archive handling and explicit safe output paths | `c82dfb0ef0b5d7442e243ace8cd83bc45f82f257` | Conceptual adaptation only: VibeStudio owns the staged operation model, conflict reporting, package manifests, deterministic PAK/ZIP/PK3 writers, and tested PWAD writer implementation in this repository. |
| Asset preview/edit workflow concepts in `src/core/asset_tools.*` and `src/core/package_preview.*` | PakFu package browsing, format-aware preview, and durable CLI workflow direction | `c82dfb0ef0b5d7442e243ace8cd83bc45f82f257` | Conceptual adaptation only: VibeStudio owns the Qt image conversion path, WAV helper, local text highlighter boundary, and native MDL/MD2/MD3 metadata readers in this repository. No third-party game assets are imported. |
| Game installation profile and detection workflow concepts in `src/core/game_installation.*` | PakFu's profile-driven game installation and detection workflow direction | `c82dfb0ef0b5d7442e243ace8cd83bc45f82f257` | Conceptual adaptation only: VibeStudio owns the Qt-native profile structs, defaults, validation, and Steam/GOG candidate heuristics in this repository. |

## Imported Compiler Toolchains

| Tool | Role | Upstream | Imported revision | License notes |
|---|---|---|---|---|
| ericw-tools | Quake/idTech2-style `qbsp`, `vis`, `light`, `bspinfo`, `bsputil` | [ericwa/ericw-tools](https://github.com/ericwa/ericw-tools) | `f80b1e216a415581aea7475cb52b16b8c4859084` | GPL-2.0-or-later; upstream notes GPL-3.0+ compatibility for Embree-enabled builds. See `external/compilers/ericw-tools/COPYING` and `gpl_v3.txt`. |
| q3map2-nrc | q3map2 compiler from NetRadiant Custom for idTech3 BSP, lighting, conversion, and packaging | [Garux/netradiant-custom](https://github.com/Garux/netradiant-custom) | `68ecbed64b7be78741878c730279b5471d978c7c` | Mixed GPL/LGPL/BSD by file; upstream marks Quake III tools, including q3map2, as GPL. See `external/compilers/q3map2-nrc/LICENSE`, `GPL`, and `LGPL`. |
| ZDBSP | Doom-family node builder | [rheit/zdbsp](https://github.com/rheit/zdbsp) | `bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659` | GPL-2.0-or-later. See `external/compilers/zdbsp/COPYING`. |
| ZokumBSP | Doom-family node/blockmap/reject builder | [zokum-no/zokumbsp](https://github.com/zokum-no/zokumbsp) | `22af6defeb84ce836e0b184d6be5e80f127d9451` | GPL-2.0 text in `external/compilers/zokumbsp/src/COPYING`; based on ZenNode lineage credited upstream. |

These compiler projects are imported as submodules so their history and license
files remain intact. VibeStudio should invoke them as external tools until a
specific source-level integration has a documented compatibility review.

## Format And Engine Lineage
VibeStudio targets public idTech-era formats and workflows from the Doom,
Quake, Quake II, and Quake III ecosystems. Format behavior should be credited
to public specifications, open-source engine/tool references, or source-port
documentation whenever implementation details are derived from them.

Every format and algorithm implemented in `src/core` is credited in the next
section with a link, the module that relies on it, and a revision or date where
the source carries one. The tables mirror the citation comments kept at the top
of each header and implementation, so a reader can move from a credit to the
code that depends on it without guessing.

## Specification And Format References

### Asset And Licensing Boundary
- No commercial game asset, palette, texture, sound, model, or data file is
  embedded in this repository. The only game-shaped sample file,
  `samples/projects/doom-minimal/maps/doom_stub.wad`, is plain text that says so
  in its own first bytes; it exists only so command-planning smoke tests have a
  path to point at.
- Real palettes are resolved at runtime from packages the user already owns.
  `resolveIdTechPalette()` and `resolveIdTechPaletteFromDirectory()` in
  `src/core/idtech_image.cpp` look for `gfx/palette.lmp`, `PLAYPAL`, and
  `pics/colormap.pcx` inside the opened package, and WAD3 miptex entries carry
  their own palette tail.
- The built-in palettes are generated stand-ins, not shipped palettes.
  `generatedIdTechPalette()` builds each one procedurally from a hue/saturation
  recipe in `kPaletteRecipes`; every display name ends in "(generated)", each
  describes itself as "Procedurally generated ramp palette (not a game
  palette)", and a decode that falls back to one raises an explicit warning.
  They intentionally do not reproduce any commercial palette.
- Specifications are consulted for layout and algorithm only. Where VibeStudio
  follows a released tool's convention - for example the `PlaneFromPoints`
  winding convention and the qbsp epsilon constants in
  `src/core/map_geometry.cpp` - the code reimplements it and says so in the
  comment beside the constant.

### Compression And Archive Formats
| Specification | Reference | VibeStudio modules | Revision / date |
|---|---|---|---|
| DEFLATE Compressed Data Format Specification | [IETF RFC 1951](https://www.rfc-editor.org/rfc/rfc1951) | `src/core/deflate.h`, `src/core/deflate.cpp` - inflate, fixed-Huffman deflate, length/distance/code-length tables transcribed from sections 3.2.4-3.2.7 | Version 1.3, May 1996 |
| DEFLATE dynamic Huffman blocks, RFC 1951 sections 3.2.2 and 3.2.7 | [IETF RFC 1951](https://www.rfc-editor.org/rfc/rfc1951) | `src/core/deflate.cpp` - `buildDynamicTrees()` writes the `HLIT`/`HDIST`/`HCLEN` header, `runLengthEncodeLengths()` emits the 16/17/18 repeat codes in the code-length transmission order, `canonicaliseCodes()` assigns canonical codes, and `deflateRaw()` measures a stored, a fixed and a dynamic block per chunk and keeps the smallest | Version 1.3, May 1996 |
| Package-merge length-limited Huffman construction | Larmore and Hirschberg, "A Fast Algorithm for Optimal Length-Limited Huffman Codes", [Journal of the ACM 37(3)](https://doi.org/10.1145/79147.79150) | `src/core/deflate.cpp` - `buildLimitedLengths()`, which produces the 15-bit literal/length and distance code lengths and the 7-bit code-length alphabet that RFC 1951 section 3.2.7 requires | 1990 |
| ZLIB Compressed Data Format Specification | [IETF RFC 1950](https://www.rfc-editor.org/rfc/rfc1950) | `src/core/deflate.cpp` - `inflateZlib()` header/Adler-32 handling, `adler32Bytes()` | Version 3.3, May 1996 |
| CRC-32 as used by GZIP and the ZIP appnote (ITU-T V.42 polynomial) | [IETF RFC 1952](https://www.rfc-editor.org/rfc/rfc1952) | `src/core/deflate.cpp` - `crc32Bytes()`, consumed by the ZIP reader and writer | Version 4.3, May 1996 |
| PKWARE .ZIP File Format Specification (APPNOTE.TXT), including the ZIP64 extensions | [APPNOTE.TXT](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT) | `src/core/package_archive.cpp` - central directory, local headers, data descriptors, ZIP64 end-of-central-directory record and locator, ZIP64 extra field `0x0001`; `src/core/package_staging.cpp` - the deterministic PK3/ZIP writer | Sections 4.3.6-4.3.16; ZIP64 extra field id `0x0001` |
| idTech2 PACK (`.pak`) directory layout | [Quake Wiki `.pak`](https://quakewiki.org/wiki/.pak) and the released id Software Quake sources | `src/core/package_archive.cpp` - `PackageArchive::loadPak()` | Living wiki page; no pinned revision |
| Doom IWAD/PWAD and Quake/Half-Life WAD2/WAD3 directories | [Unofficial Doom Specs v1.666](https://www.gamers.org/dhs/helpdocs/dmsp1666.html), [Quake Wiki WAD](https://quakewiki.org/wiki/WAD), [Valve Developer Community WAD3](https://developer.valvesoftware.com/wiki/WAD) | `src/core/package_archive.cpp` - `PackageArchive::loadWad()`; `src/core/package_staging.cpp` - the PWAD and WAD2/WAD3 writers | Unofficial Doom Specs v1.666 (1994); wiki pages have no pinned revision |

### idTech Image And Palette Formats
All rows below are implemented in `src/core/idtech_image.h` and
`src/core/idtech_image.cpp`.

| Specification | Reference | What it covers | Revision / date |
|---|---|---|---|
| Unofficial Doom Specs | [dmsp1666](https://www.gamers.org/dhs/helpdocs/dmsp1666.html) | Doom picture posts, flats, `PLAYPAL`, `COLORMAP`, and the WAD directory | v1.666, 1994 |
| Doom Wiki picture format | [Picture format](https://doomwiki.org/wiki/Picture_format) | Patch header, per-column offsets, post `{topdelta, length, pad, pixels, pad}` runs | Living wiki page |
| Doom Wiki flat format | [Flat](https://doomwiki.org/wiki/Flat) | Raw 64x64 indexed flat lumps | Living wiki page |
| Doom Wiki PLAYPAL | [PLAYPAL](https://doomwiki.org/wiki/PLAYPAL) | 14 banks of 256 RGB triplets; VibeStudio reads the first bank | Living wiki page |
| Quake Specifications (Olivier Montanuy / Quake Standards Group) | [Quake Documentation](https://www.gamers.org/dEngine/quake/spec/quake-spec34/) | Quake `.lmp` pictures, WAD2 miptex, `gfx/palette.lmp`, and the `.spr` sprite header | "Quake Documentation Version 3.4" |
| Released id Software Quake sources | [id-Software/Quake](https://github.com/id-Software/Quake) | Cross-check for miptex and `spritegn.h` sprite layout; layouts reimplemented, no code copied | GPL source release |
| Quake II BSP / WAL format notes | [flipcode archive](https://www.flipcode.com/archives/Quake_2_BSP_File_Format.shtml) plus the released id Software Quake II headers | Quake II `.wal` 100-byte header, mip chain, and `pics/colormap.pcx` palette | Community reference; no pinned revision |
| Valve Developer Community WAD3 | [WAD](https://developer.valvesoftware.com/wiki/WAD) | Half-Life WAD3 miptex entries and the per-texture 256-colour palette tail | Living wiki page |
| ZSoft PCX File Format Technical Reference Manual | ZSoft's own manual is no longer vendor-hosted; the public [Encyclopedia of Graphics File Formats PCX summary](https://www.fileformat.info/format/pcx/egff.htm) documents the same structure | PCX header, RLE planes, and the 768-byte VGA palette tail | ZSoft revision 5 era |
| Truevision TGA File Format Specification | Truevision's document is likewise unhosted; see the public [Encyclopedia of Graphics File Formats TGA summary](https://www.fileformat.info/format/tga/egff.htm) | Targa header, colour map, RLE and uncompressed image types, origin handling | Version 2.0 |
| GPL Quake II engine sources, `m8tex_t` and `m32tex_t` | [yquake2 `src/common/header/files.h`](https://github.com/yquake2/yquake2/blob/master/src/common/header/files.h), matching id Software's release at [id-Software/Quake-2](https://github.com/id-Software/Quake-2) | Quake II `.m8` (version 2, 1040-byte header, embedded 768-byte palette, sixteen mip entries) and `.m32` (version 4, 968-byte header, RGBA pixels, sixteen mip entries), plus the surface flags, contents, value, animation name and texture scale fields both carry | GPL source release; no pinned revision |
| GPL Quake II engine sources, `dsprite_t` and `dsprframe_t` | The same `files.h` layouts listed above | Quake II `.sp2` sprite container: the `IDS2` ident, version 2, and the per-frame `width`, `height`, `origin_x`, `origin_y` and `name[64]` records naming external images. The container holds no pixels of its own | GPL source release; no pinned revision |
| Half-Life SDK sprite generator | [`utils/sprgen/sprgen.c`](https://github.com/ValveSoftware/halflife/blob/master/utils/sprgen/sprgen.c) | Half-Life `IDSP` version 2 sprites: the extra `texFormat` field (normal, additive, index alpha, alpha test), the `short` palette count and embedded RGB palette that follow the 40-byte header, and the frame/group stream shared with the Quake version 1 dialect | Valve SDK source release; no pinned revision |

### idTech Map Formats
| Specification | Reference | VibeStudio modules | Revision / date |
|---|---|---|---|
| Doom Wiki map lump pages | [WAD](https://doomwiki.org/wiki/WAD), [Linedef](https://doomwiki.org/wiki/Linedef), [Sidedef](https://doomwiki.org/wiki/Sidedef), [Sector](https://doomwiki.org/wiki/Sector), [Thing](https://doomwiki.org/wiki/Thing) | `src/core/level_map.cpp` (lump recognition, record strides, two-sided flag `0x0004`, geometry validation), `src/core/map_geometry.cpp` (sector outline tracing), `src/core/map_render.cpp` and `src/app/map_viewport.cpp` (thing angles, light levels) | Living wiki pages |
| Doom Wiki Hexen map format | [Hexen map format](https://doomwiki.org/wiki/Hexen_map_format) | `src/core/level_map.cpp` - 16-byte linedef and 20-byte thing strides selected by the `BEHAVIOR` lump | Living wiki page |
| Doom Wiki UDMF | [UDMF](https://doomwiki.org/wiki/UDMF) | `src/core/level_map.h`, `src/core/level_map.cpp` - `TEXTMAP`/`ENDMAP` detection and the ZDoom-era lump set that must survive a round trip | Living wiki page |
| Quake `.map` text format | [Quake Wiki Quake Map Format](https://quakewiki.org/wiki/Quake_Map_Format) | `src/core/level_map.cpp` - the shared brush/face tokenizer | Living wiki page |
| Quake II `.map` surface arguments | [Quake Wiki Quake 2 Map Format](https://quakewiki.org/wiki/Quake_2_Map_Format) | `src/core/level_map.cpp` - `readOptionalFlags()` for the trailing `contents surface value` triple | Living wiki page |
| Valve 220 `.map` texture axes | [Valve Developer Community MAP (file format)](https://developer.valvesoftware.com/wiki/MAP_%28file_format%29) | `src/core/level_map.cpp` - Valve 220 face parsing | Living wiki page |
| Q3Radiant / GtkRadiant brush primitives | [Q3Radiant manual](https://icculus.org/gtkradiant/documentation/q3radiant_manual/) | `src/core/level_map.cpp` - `brushDef`, `brushDef3`, `patchDef2`, `patchDef3` | Manual as published by the GtkRadiant project |
| q3map2 shader manual | [q3map2 shader manual](https://q3map2.robotrenegade.com/docs/shader_manual/) | `src/app/syntax_highlight.cpp` - shader keyword highlighting | Living document |
| ericw-tools sources (imported submodule) | [ericwa/ericw-tools](https://github.com/ericwa/ericw-tools), local copy under `external/compilers/ericw-tools` | `src/core/map_geometry.cpp` - the `PlaneFromPoints` winding convention and the `ON_EPSILON`/`DIST_EPSILON` tolerance conventions, reimplemented rather than copied | Pinned revision `f80b1e216a415581aea7475cb52b16b8c4859084` |
| q3map2 sources from NetRadiant Custom (imported submodule) | [Garux/netradiant-custom](https://github.com/Garux/netradiant-custom), local copy under `external/compilers/q3map2-nrc` | `src/core/map_geometry.cpp` - the +/-65536 `MAX_WORLD_COORD` base-winding extent and quadratic Bezier patch tessellation of `(2n+1)x(2m+1)` control grids | Pinned revision `68ecbed64b7be78741878c730279b5471d978c7c` |

### Compiled Artifact Formats
All rows below are implemented in `src/core/bsp_inspect.h` and
`src/core/bsp_inspect.cpp`.

| Specification | Reference | What it covers | Revision / date |
|---|---|---|---|
| Quake Specifications, chapter 4 "BSP files" | [qkspec_4](https://www.gamers.org/dEngine/quake/spec/quake-spec34/qkspec_4.htm) | BSP29 lump order and record layouts | "Quake Documentation Version 3.4" |
| ericw-tools documentation | [ericw-tools docs](https://ericwa.github.io/ericw-tools/) | The `BSP2` and `2PSB` widened node/leaf/clipnode/edge/marksurface records, and the `.prt` / `.pts` / `.lin` files the compilers write | Matches the pinned submodule revision `f80b1e216a415581aea7475cb52b16b8c4859084` |
| Released id Software Quake II sources | [`qcommon/qfiles.h`](https://github.com/id-Software/Quake-2/blob/master/qcommon/qfiles.h) | IBSP v38 header and lump records | GPL source release |
| q2tools-220 / qbism extended Quake II BSP | [qbism/q2tools-220](https://github.com/qbism/q2tools-220) | The `QBSP` widened `dqnode_t`, `dqleaf_t`, `dqface_t`, `dqbrushside_t`, `dqedge_t` records | Upstream project; not imported as a submodule |
| Released id Software Quake III Arena sources | [`code/qcommon/qfiles.h`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/qcommon/qfiles.h) | IBSP v46 header and lump records | GPL source release |
| q3map2 sources from NetRadiant Custom (imported submodule) | `external/compilers/q3map2-nrc` | Raven `RBSP` v1 draw-surface and draw-vertex records | Pinned revision `68ecbed64b7be78741878c730279b5471d978c7c` |

`src/core/asset_tools.cpp` reads the `IDPO` (Quake MDL), `IDP2` (MD2) and
`IDP3` (MD3) headers for metadata only; those layouts come from the same
Quake Specifications and released id Software sources listed above.

### idTech Model Formats
All rows below are implemented in `src/core/model_mesh.h` and
`src/core/model_mesh.cpp`, which decodes geometry rather than headers alone.
Only Quake MDL version 6, Quake II MD2 version 8, and Quake III MD3 version 15
are decoded to vertices; the MDC, MDR, and IQM rows cover header fields only.
No commercial model, skin, or animation data is embedded in this repository.

| Specification | Reference | What it covers | Revision / date |
|---|---|---|---|
| Quake Specifications, chapter 5 "MDL files" | [qkspec_5](https://www.gamers.org/dEngine/quake/spec/quake-spec34/qkspec_5.htm) | Quake MDL (`IDPO` version 6) header, the scale/translate vertex compression, the `onseam` skin-vertex rule, simple and group frames, and the Z-up model axis convention (X forward, Y left, Z up) that `src/app/model_viewport.cpp` draws with | "Quake Documentation Version 3.4" |
| Released id Software Quake sources, `modelgen.h` | [`WinQuake/modelgen.h`](https://github.com/id-Software/Quake/blob/master/WinQuake/modelgen.h) | `mdl_t`, `stvert_t`, `dtriangle_t`, `daliasframe_t`, `daliasgroup_t` and `trivertx_t` field order, and the `ALIAS_ONSEAM` `0x0020` flag; layouts reimplemented, no code copied | GPL source release |
| Released id Software Quake II sources, `qcommon/qfiles.h` | [`qcommon/qfiles.h`](https://github.com/id-Software/Quake-2/blob/master/qcommon/qfiles.h) | Quake II MD2 (`IDP2` version 8): `dmdl_t`, `dstvert_t`, `dtriangle_t`, `daliasframe_t` and `dtrivertx_t`, the separate position and texture-coordinate indexing that `decodeQuake2Md2()` recombines, the 64-byte external skin names, and the GL command block whose count is reported but whose contents are not decoded | GPL source release |
| id Software `anorms.h` vertex normal table | [`qcommon/anorms.h`](https://github.com/id-Software/Quake-2/blob/master/qcommon/anorms.h) | The 162 vertex normals that MDL and MD2 index with one byte per vertex, transcribed as `kAliasNormals` in `src/core/model_mesh.cpp`. It is a fixed mathematical constant of the two formats rather than game content | GPL source release |
| Released id Software Quake III Arena sources, `md3.h` | [`code/qcommon/qfiles.h`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/qcommon/qfiles.h) and `code/renderer/tr_types.h` | Quake III MD3 (`IDP3` version 15): `md3Header_t`, `md3Frame_t`, `md3Tag_t`, `md3Surface_t`, `md3Shader_t`, `md3Triangle_t`, `md3St_t` and `md3XyzNormal_t`, the `MD3_XYZ_SCALE` 1/64 unit step, the per-surface `IDP3` chain walked by each surface's own `ofsEnd`, and the packed latitude/longitude normal pair reconstructed by `md3Normal()` | GPL source release |
| Return to Castle Wolfenstein and Elite Force / ioquake3 `qfiles.h` | The `mdcHeader_t` and `mdrHeader_t` layouts published in those source releases | MDC and MDR header fields only - version, internal name, and the frame, tag, surface, skin, bone and LOD counts. Their geometry layouts are deliberately not guessed at, so `decodeMdcHeader()` and `decodeMdrHeader()` leave `geometryAvailable` false and warn that geometry decoding is not implemented | Public source releases; no pinned revision |
| Inter-Quake Model specification | [sauerbraten.org/iqm](http://sauerbraten.org/iqm/) | The `iqmheader` fields only - version, file size, and the mesh, vertex, triangle, joint, animation and frame counts read by `decodeIqmHeader()`. IQM geometry and skeletal data are not decoded | Inter-Quake Model public specification |

### Entity Definition Formats
All rows below are implemented in `src/core/entity_definitions.h` and
`src/core/entity_definitions.cpp`. No game's entity definitions are shipped
here; the studio parses whatever `.def`, `.fgd`, `.ent`, or `.qc` file the user
points it at, through `loadEntityDefinitions()` and the `entity definitions`
and `entity validate` CLI commands.

| Specification | Reference | What it covers | Revision / date |
|---|---|---|---|
| Radiant `/*QUAKED` definition blocks | [GtkRadiant](https://github.com/TTimo/GtkRadiant), `radiant/eclass_def.cpp`, and the q3map2 entity documentation shipped with [NetRadiant Custom](https://github.com/Garux/netradiant-custom) | `parseRadiantBlock()` - the `/*QUAKED <classname> (r g b) (mins) (maxs) FLAG1 .. FLAG8` header, the 0..1 colour triple, the literal `?` written in place of the size pair to mean "brush entity", up to eight header flags mapped to spawnflag bits 0..7, and the documented key and flag lines in the comment body. QuakeC `.qc` sources carry the same block and parse through the same path | Living repositories; no pinned revision |
| Valve Forge Game Data (`.fgd`) | [Valve Developer Community FGD](https://developer.valvesoftware.com/wiki/FGD) | `parseFgdDefinitions()` - the `@PointClass`, `@SolidClass`, `@BaseClass` and related class statements, the `base()`, `size()`, `color()`, `model()`, `studio()`, `iconsprite()` and `sprite()` helpers, `key(type) : "display" : "default" : "description"` bodies, `choices` and `flags` row blocks, and `@include` resolution relative to the including file | Living wiki page |
| Quake III Arena BSP entity lump (`.ent`) | The released q3map2 sources and the [Quake III Arena shader and entity manual](https://www.qeradiant.com/manual/Q3AShader_Manual/) | `parseEntDefinitions()` - the plain `{ "key" "value" }` blocks q3map2 writes back out. This is a list of placed entities rather than a definition file, so the reader recovers classnames and the union of keys each class was seen with, and warns that key types, defaults, descriptions and spawnflag names are unavailable | Living document |
| Quake Specifications, "Entities" appendix | [Quake Documentation](https://www.gamers.org/dEngine/quake/spec/quake-spec34/) | The skill and deathmatch spawnflag bits 8..11 the game code owns (`NOT_EASY` `0x100`, `NOT_MEDIUM` `0x200`, `NOT_HARD` `0x400`, `NOT_DEATHMATCH` `0x800`). `validateLevelMapEntities()` skips those bits for `LevelMapFormat::QuakeMap` documents instead of reporting them as undeclared spawnflags | "Quake Documentation Version 3.4" |

### Audio Container Formats
All rows below are implemented in `src/core/asset_tools.cpp`.

| Specification | Reference | What it covers | Revision / date |
|---|---|---|---|
| Microsoft "Multimedia Programming Interface and Data Specifications 1.0" | [IETF RFC 2361](https://www.rfc-editor.org/rfc/rfc2361) for the wave format tag registry | RIFF/WAVE chunk layout, PCM / IEEE float / A-law / mu-law tags, `WAVE_FORMAT_EXTENSIBLE` | RFC 2361, June 1998 |
| Ogg encapsulation format | [IETF RFC 3533](https://www.rfc-editor.org/rfc/rfc3533) | Ogg page header and segment table parsing | May 2003 |
| Vorbis I specification | [Xiph Vorbis I spec](https://xiph.org/vorbis/doc/Vorbis_I_spec.html) | Section 4.2.2 identification header | Xiph.Org |
| Ogg Opus | [IETF RFC 7845](https://www.rfc-editor.org/rfc/rfc7845) | `OpusHead` and the fixed 48 kHz decode rate (section 5.1) | April 2016 |
| FLAC format | [Xiph FLAC format](https://xiph.org/flac/format.html) | Metadata block headers and `STREAMINFO` | Xiph.Org |
| MPEG-1 / MPEG-2 audio frame headers (ISO/IEC 11172-3, ISO/IEC 13818-3) | [MPEG audio frame header reference](http://www.mp3-tech.org/programmer/frame_header.html) | Bitrate and sample-rate tables, frame sizing | Public reference document |

### Output And Presentation Formats
| Specification | Reference | VibeStudio modules | Revision / date |
|---|---|---|---|
| SVG 1.1 (Second Edition) | [W3C SVG 1.1](https://www.w3.org/TR/SVG11/) | `src/core/map_render.cpp` - the deterministic headless map renderer emits only the static subset: no scripting, no external references, no embedded raster data | W3C Recommendation, 16 August 2011 |
| Unicode CLDR layout guidance | [CLDR layout](https://cldr.unicode.org/translation/getting-started/layout) | `src/core/localization.cpp` - `rightToLeftLanguageCodes()`, derived from CLDR's per-language `characterOrder` | Living document |
| Engine and source-port command lines | [Quake](https://quakewiki.org/wiki/Command_line_parameters), [Quake II](https://www.quake2.com/q2guide/q2cmdline.html), [ioquake3](https://ioquake3.org/help/command-line-options/), [ZDoom](https://zdoom.org/wiki/Command_line_parameters) | `src/core/build_pipeline.cpp` - the game launch profiles and their argument templates | Living documents |

## Editor Workflow Inspirations
VibeStudio's adaptable level-editor profiles and the look of its shell are
intended to help users feel at home without copying third-party assets or
proprietary content. Profile and interface inspiration and compatibility
research should credit:
- [GtkRadiant](https://github.com/TTimo/GtkRadiant), especially the GtkRadiant 1.6.0-era layout and control expectations.
- [NetRadiant Custom](https://github.com/Garux/netradiant-custom), for modern Radiant-family workflow refinements and q3map2-oriented editing expectations.
- [TrenchBroom](https://trenchbroom.github.io/), for modern single-window brush editing and project workflow expectations.
- [QuArK](https://quark.sourceforge.io/), for integrated object/package/map editing lineage.
- [idStudio](https://idstudio.idsoftware.com/), id Software's editor for DOOM Eternal (public beta, August 2024), for the visual language of the studio shell: neutral charcoal panels with an orange accent, black-backed viewports with corner readouts, dense panel groups with bottom-edge tabs, a grouped Key / Value entity property grid, and an asset browser with a folder tree, breadcrumb path, and thumbnail tiles. Inspiration only: idStudio is proprietary, and no idStudio code, icons, assets, or content are used. The corresponding implementation is VibeStudio's own, in `src/app/studio_theme.*`, `src/app/studio_icons.*`, `src/app/studio_layout.*`, and `src/app/application_shell.cpp`.

## AI Integration References
- [OpenAI API documentation](https://platform.openai.com/docs/quickstart), planned as the first optional general-purpose provider reference for prompt-based and agentic automation experiments.
- [Claude API documentation](https://platform.claude.com/docs/en/home), planned as an optional provider reference for reasoning, coding, long-context, and agentic planning workflows.
- [Gemini API documentation](https://ai.google.dev/api), planned as an optional provider reference for multimodal and large-context workflows.
- [ElevenLabs documentation](https://elevenlabs.io/docs/overview/intro), planned as an optional provider reference for voice, speech, sound effects, and audio experiments.
- [Meshy API documentation](https://docs.meshy.ai/en), planned as an optional provider reference for prompt/image-to-3D, AI texturing, and rapid placeholder asset workflows.

## Accessibility And Localization References
- [WCAG 2.2](https://www.w3.org/TR/WCAG22/), planned as the baseline accessibility reference where web-oriented guidance applies to desktop UI.
- [Ethnologue 200](https://www.ethnologue.com/insights/ethnologue200/), used as one reference point for reviewing the initial 20-language localization target set.

## Community Thanks
- The idTech mapping, modding, speedrunning, source-port, and preservation communities who kept these workflows usable and documented across decades.
- The maintainers and contributors of the imported compiler projects listed above.
