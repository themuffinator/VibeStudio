# Support Matrix

This matrix describes what VibeStudio actually does with each format and
workflow today. It is written against the code, not against the roadmap: where
support is partial, the row says what the limit is instead of ticking a box.
Longer-term direction lives in [`docs/ROADMAP.md`](ROADMAP.md) and
[`docs/STACK.md`](STACK.md).

Status words used below:

- **Full** — the format is parsed or written end to end for the stated purpose.
- **Partial** — implemented with a named limit, spelled out in the row.
- **Detect only** — recognised and reported, but not decoded or edited.
- **Header only** — the header is read and reported; the payload behind it is
  not decoded.
- **Planned** — not implemented.

## Engine Families

| Family | Status | What works today |
|---|---|---|
| idTech1 / Doom | Partial | WAD browsing and writing, including multi-map WADs; Doom and Hexen map lumps parse and save back; patches, flats, PLAYPAL and COLORMAP decode to pixels; sector outlines, SVG rendering and node-builder pipelines run. UDMF is detected only. |
| idTech2 / Quake, Quake II | Partial | PAK/WAD2/WAD3/folder browsing; `.map` parsing with classic and Valve 220 faces; miptex, `.lmp`, `.wal`, `.m8`, `.m32`, `.sp2`, PCX and `.spr` decode; MDL and MD2 geometry decodes, draws and exports as OBJ; Radiant `.def` and Valve `.fgd` entity definitions parse and validate a map; BSP29/BSP2/IBSP38 inspection; ericw-tools pipelines run. |
| idTech3 / Quake III | Partial | PK3/ZIP browsing and writing with DEFLATE; `.map` parsing with `brushDef`, `brushDef3`, `patchDef2` and `patchDef3`; shader script round-tripping; MD3 geometry decodes, draws and exports as OBJ; `.ent` entity lists parse; IBSP46/47 and RBSP inspection; q3map2 pipelines run. |

## Image Formats

All decoders live in `src/core/idtech_image.{h,cpp}` and produce a `QImage`,
except the `.sp2` container, which carries no pixels of its own and produces a
list of frames naming images stored elsewhere in the package.
"Palette source" is what colours the indexed formats: VibeStudio first looks for
a real palette inside the open package, and only falls back to a generated,
license-clean ramp when none is found. The fallback is always reported as
generated, never presented as a game palette.

| Format | Decodes to pixels | Palette source | Limits |
|---|---|---|---|
| Doom patch (graphics lump) | Full | `PLAYPAL` from the open package, else the generated Doom ramp | Column/post format with transparency; left and top offsets are preserved. |
| Doom flat | Full | Same as Doom patch | Sizes 4096, 4160 and 16384 bytes only, and the entry must sit in a flat-like path (`flats/`, a `.flat` suffix, or a name containing `flat`), because flats carry no header. |
| Doom `PLAYPAL` | Full | Itself | Rendered as a 16x16 swatch grid of the first bank; extra banks are counted, not drawn. |
| Doom `COLORMAP` | Full | `PLAYPAL` from the open package, else the generated Doom ramp | Must be exactly 8704 bytes; drawn as 34 rows of 256 indices. |
| Quake `.lmp` | Full | `gfx/palette.lmp` from the open package, else the generated Quake ramp | Header is width/height plus exactly `width * height` bytes; anything else is rejected. |
| Quake WAD2 miptex | Full | External Quake palette | All four mip levels are decoded. Width and height must be multiples of 8. |
| Half-Life WAD3 miptex | Full | The 256-entry palette appended to the lump | Same four mip levels. Index 255 is treated as transparent only for `{`-prefixed fence textures. |
| Quake II `.wal` | Full | `pics/colormap.pcx` from the open package, else the generated Quake II ramp | Four mip levels; surface flags, content flags, surface value and the next-animation name are read from the header. |
| Quake II `.m8` | Full | The 768-byte palette in the `m8tex_t` header (`m8-embedded`), which wins over the package palette unless all 256 entries are black | Version 2 only. Up to 16 mip levels; only the populated ones are exposed, and the chain must end at the first zero width/height/offset with nothing populated after it. Texture name, next-animation name, surface flags, content flags and surface value are read. `.m8` carries no magic, so detection requires the version word plus sixteen self-consistent, in-bounds mip entries. |
| Quake II `.m32` | Full | n/a — truecolour | Version 4 only. RGBA pixels straight to `QImage::Format_ARGB32`, so `result.paletted` stays false; any alpha below 255 sets `hasTransparency`. Up to 16 mip levels, same chain rule as `.m8`. Substitute, damaged and next-animation texture names, texture scale and mip scale are reported. |
| PCX | Full | Embedded 256-entry tail palette when present, otherwise the resolved package palette | 8-bit single-plane and 8-bit 3/4-plane only. 1-, 2- and 4-bit PCX headers parse but are rejected by the decoder. |
| Targa | Full | Embedded colour map for colour-mapped images | Types 1, 2, 3, 9, 10 and 11 at 8/15/16/24/32 bits. An all-zero alpha channel is treated as opaque and warned about. |
| Quake `.spr` (`IDSP` version 1) | Full | Resolved Quake palette | Single frames and interval groups both decode, with per-frame origins and durations differenced out of the group's absolute interval list. |
| Half-Life `.spr` (`IDSP` version 2) | Full | The palette written after the header (`spr-embedded`) | `texFormat` 0–3 only. `SPR_ALPHTEST` masks palette index 255; `SPR_INDEXALPHA` decodes to `Format_ARGB32`, taking colour from the last palette entry and coverage from the index; normal and additive stay opaque indexed. The frame stream is the same as version 1, so groups and per-frame durations decode the same way. Additive blending itself is recorded as a detail line, not applied. |
| Quake II `.sp2` | Partial | Resolved package palette, used for the images the frames name | The container itself holds no pixels. The `dsprite_t` frame table always decodes — per-frame width, height, origin and the 64-byte external image name — and `externalFrames` is set. Frame images are only filled in when a `PackageArchiveReader` is supplied: each name is looked up bare and then under the sprite's own directory, the first 256 frames at most, and each is decoded with an empty context so a chain of sprites cannot recurse. Reported width and height are the largest declared frame. |
| PNG, JPEG, GIF, BMP, TIFF, WebP | Full | n/a | Handled by Qt's own image plugins, so availability follows the Qt build. |
| Raw indexed payloads | Partial | Resolved package palette | Only square payloads are guessed, and the guess is reported as a warning. This path is never chosen by format detection; it has to be asked for. |

### Palettes

| Capability | Status | Notes |
|---|---|---|
| Palette resolution from the open package | Full | Candidate paths are searched in order per family: `gfx/palette.lmp` for Quake, `pics/colormap.pcx` then `pics/palette.pcx` for Quake II, `PLAYPAL` for Doom/Heretic/Hexen. The resolution records which path won and every path tried. |
| Generated fallback palettes | Full | Six deterministic ramps (Quake, Quake II, Doom, Heretic, Hexen, generic). They are invented for VibeStudio, contain no shipped game data, and are always flagged as generated. |
| Palette quantization | Full | Nearest-colour mapping of an RGB image onto a 256-entry palette, with optional dithering. |
| Palette swatch rendering | Full | 16x16 grid at a configurable cell size, used by the palette view and the `PLAYPAL` preview. |

## Archive And Package Formats

Readers live in `src/core/package_archive.{h,cpp}`, writers in
`src/core/package_staging.{h,cpp}`, and the DEFLATE codec both sides use is
`src/core/deflate.{h,cpp}`.

| Format | Read | Write | Compression | Limits |
|---|---|---|---|---|
| Folder | Full | Not supported | n/a | Recursive listing, byte reads, and use as an extraction source. Writing a package back out as a folder tree is not implemented. |
| Quake PAK | Full | Full | None — PAK stores everything uncompressed | Deterministic writer. Every entry is readable because the container has no compression. |
| Doom IWAD / PWAD | Full | Full | None | 16-byte directory records. The writer preserves the source magic, falling back to `PWAD`. **Multi-map WADs write back.** A lump name is not a key in a Doom WAD — every map repeats `THINGS`, `LINEDEFS` and the rest — so `PackageStagingModel` keeps the source directory in `sourceWadLumps()`, gives every base entry a `sourceOrdinal` into it, reads bytes by that ordinal rather than by name, and plans a WAD source in source order instead of the path-sorted order. `wadGroupedEntries` then walks the plan once, opens a group at each `ExMy`/`MAPxx` marker, and re-emits each map as its marker followed by its own lumps in `doomMapLumpRank` order, so a malformed run is repaired without moving a lump between maps. Still refused: a plan holding two or more markers that is not in source order — a known map lump before any marker ("Cannot tell which map owns the lump %1: the plan is not in WAD source order.") or one map claiming a name twice ("One map cannot hold the lump %1 twice."), which is what a WAD assembled from loose files hits; a repeated lump name in an at-most-one-map plan ("Doom WAD write-back cannot represent duplicate lump names: %1"); a name that is not Latin-1, contains a folder separator, is empty or exceeds 8 characters; and any lump size or directory offset past the 32-bit bound. A refused write leaves no output file behind. |
| Quake WAD2 / Half-Life WAD3 | Full | Full | None for written lumps | 32-byte directory records with the type byte preserved. Compressed WAD2/WAD3 lumps are listed with a note but are not decoded on read, and the writer emits uncompressed lumps only. |
| ZIP | Full | Full | Read: stored and DEFLATE. Write: stored or DEFLATE | ZIP64 central directories and ZIP64 extra fields are handled on both sides. CRC-32 is verified on read. Encrypted entries and any method other than 0 or 8 are listed but not decoded. |
| Quake III PK3 | Full | Full | Same as ZIP | Identical container handling, plus layer shadowing when several PK3s are mounted in one session. |
| Nested archives | Partial | n/a | n/a | A package inside a package is flagged and can be mounted as an extra layer. Nested archives are not written back into their host. |

Extraction refuses absolute paths, drive-qualified paths, `..` traversal,
reserved Windows device names, control characters and trailing dots or spaces,
and it resolves symbolic links and NTFS junctions along the output path before
writing, so a crafted entry cannot escape the chosen root.

The written DEFLATE stream comes from the in-tree encoder in
`src/core/deflate.{h,cpp}`. Every level except `store` cuts the input into
blocks of at most 65535 bytes and measures all three RFC 1951 block types —
stored, fixed Huffman and dynamic Huffman — keeping the smallest, so a block is
never larger than storing its bytes would be. The four levels (`store`, `fast`,
`default`, `best`) differ only in how hard the LZ77 hash-chain match search
works. `PackageWriteRequest::compression` defaults to `default` and is applied
to ZIP/PK3 only; PAK and WAD output is always stored, and no shipped CLI or GUI
path selects `fast` or `best`. Entries that do not get smaller are stored
verbatim, so an already-compressed asset is never inflated by re-packing. See
[`docs/STACK.md`](STACK.md) for why an in-tree codec was taken over zlib.

### Package Comparison

`src/core/package_compare.{h,cpp}` answers what differs between two packages,
entry by entry. Paths are paired by `normalizePackageVirtualPath` and then
`toCaseFolded()`, and repeated paths are paired by occurrence index within each
side's own order, so a Doom WAD's repeated lump names line up one map at a time
instead of collapsing onto each other. Keys are sorted before emission, so the
entry list, the summary and the JSON are a pure function of the two inputs.

| Capability | Status | Notes |
|---|---|---|
| `comparePackages(left, right, request)` | Full | Both readers must be open, or the result carries only the labels and the warning "Both packages must be open to compare them." |
| `comparePackageToPlan(left, plan, request)` | Partial | The staged plan is the right-hand side, so `Added` means the plan adds it, and the plan side resolves entries positionally so a repeated path reads its own bytes. Only `src/tests/package_compare_smoke_test.cpp` calls it; neither the CLI nor the shell does. |
| Status categories | Full | `Identical`, `Added`, `Removed`, `Changed` and `CaseOnly`. `CaseOnly` is its own category because a path that differs only in letter case works on Windows and fails on Linux; it takes precedence over `Changed`, with any byte difference carried in `contentChanged`. |
| Content test | Full | Reported per entry as `NotCompared`, `SizeOnly`, `Crc32` or `Sha256`. Differing sizes decide on their own; a stored CRC-32 is only trusted when the sizes already match; otherwise both sides are read and hashed. Directories, `metadataOnly`, an unreadable side, a repeated path on the archive side, and an entry above `maxEntryBytes` (default `kPackageCompareDefaultMaxEntryBytes`, 256 MiB) all fall back to `NotCompared` with a note saying which. |
| Reporting | Full | `packageCompareLines` / `packageCompareText` skip identical rows and print case-only rows as `(left -> right)`; `packageCompareJson` is `schemaVersion` 1 and `packageCompareJsonBytes` is indented UTF-8, byte for byte reproducible. |
| CLI `package compare` | Full | `vibestudio --cli package compare <left> <right>`, with the right side also accepted as `--against`, plus `--metadata-only`, `--include-directories`, `--max-entry-bytes <n>` and `--json`. Identical exits 0; **any difference exits 4 (`validation-failed`)** so a release script can gate on "these two packages match". A package that will not open exits 3. |
| Shell **Compare** button | Partial | On the Packages page, backed by the `package.compare` command. It compares the two on-disk archives; the staged plan is not passed, so this is not a staged-plan comparison. Results land in the detail drawer as Summary, Entries and Warnings sections, and a difference completes the activity task with a warning rather than a failure. |
| Directory records | Partial | Excluded unless `includeDirectories` is set, and records the reader invented (`storageMethod == "synthetic"`) are always skipped. |

## Model Formats

Geometry decoding lives in `src/core/model_mesh.{h,cpp}`; the widget that draws
it is `src/app/model_viewport.{h,cpp}`; the header-only metadata summaries that
packaging and dependency work use are still in `src/core/asset_tools.{h,cpp}`.
`detectModelMeshFormat` checks magic first and falls back to the extension so a
damaged file still reports which decoder was expected. Each decoded format
accepts exactly one version; any other version is rejected with a named error
rather than guessed at.

| Format | Status | What decodes, and what does not |
|---|---|---|
| Quake MDL (`IDPO` version 6) | Full | Decodes: one surface, positions decompressed as `scale * byte + translate`, normals from the published 162-entry `anorms` table, texture coordinates at texel centres with `onseam` vertices duplicated for back-facing triangles, simple frames and group frames flattened into one pose each with per-pose name and bounds, and embedded indexed skins through the resolved palette. Does not: only the first member image of a skin group is decoded; skin-group and frame-group interval floats are read past and discarded, so no file-authored frame timing exists; every frame reports the single bounding radius from the header. Version 6 only, so a Half-Life `.mdl` (`IDST`) fails the version check. |
| Quake II MD2 (`IDP2` version 8) | Full | Decodes: one surface built from the (position, ST) pairs the triangles actually use, `int16` texture coordinates divided by the header skin size, normals from the same 162-entry table, per-frame scale/translate/name, and up to `numSkins` 64-byte external skin paths. Does not: the GL command block is counted and skipped, never decoded into strips and fans; frame bounds are derived as `mins = translate`, `maxs = scale * 255 + translate` because MD2 stores none, and the frame radius stays 0. A zero skin size warns and leaves texture coordinates unscaled. |
| Quake III MD3 (`IDP3` version 15) | Full | Decodes: per-frame mins, maxs, local origin, radius and name; `numTags x numFrames` tags with name, origin and a 3x3 axis; and a surface chain walked by each surface's own `ofsEnd` with every hop bounds-checked, giving per-surface shader names, float STs, `int32` triangles and `int16` XYZ at 1/64 unit steps, with normals unpacked from the 16-bit lat/long pair. Does not: nothing assembles a multi-part model from the tags — there is no tag following and no head/upper/lower attachment; shader names are treated as texture paths only, with no `.shader` script parsed and `shaderIndex` unused. Surfaces with fewer frames than the model have their last frame repeated, with a warning. If no surface survives the walk, geometry is unavailable and "No MD3 surface could be decoded." is recorded. |
| MDC (`IDPC`) | Header only | Version, internal name, flags, and the frame, tag, surface and skin counts. No geometry, tags, animations or skins; `geometryAvailable` stays false and "Geometry decoding is not implemented for MDC; only the header was read." is always recorded. The version field is read and reported but never validated. |
| MDR (`RDM5`) | Header only | Version, internal name, frame count, bone count, level-of-detail count and tag count. The bone count is printed as text; no skeletal data is decoded. Same "Geometry decoding is not implemented" warning and no version check. |
| IQM (`INTERQUAKEMODEL`) | Header only | Version, declared file size, mesh, vertex, triangle, joint, animation and frame counts, with a warning when the declared size does not match the file. Joints and animations are counts only. Same "Geometry decoding is not implemented" warning and no version check. |
| Half-Life MDL (`IDST`) | Not supported | The magic is not matched, so the file falls through to the `.mdl` extension and is then rejected by the IDPO version 6 check. |
| Everything else (MD5, MDX, ASE, LWO, OBJ import, glTF, …) | Planned | `detectModelMeshFormat` returns `Unknown` and the decode fails with "The file is not a recognised idTech model." Assimp remains an optional future path and is not linked. |

| Model capability | Status | Notes |
|---|---|---|
| Software viewport | Full | `ModelViewport` is a `QPainter` widget with no OpenGL dependency: orthographic projection, painter's-algorithm depth sort and affine texture mapping, built around the idTech Z-up axis convention. Wireframe, flat-shaded and textured modes; orbit, pan and zoom by mouse and keyboard; per-triangle hover reporting. Backface culling, grid, axes and edge overlays exist on the widget but are not wired to shell controls. |
| Skin resolution | Partial | `resolveModelSkin` collects the mesh and surface skin paths, normalises them, and tries the literal path plus the stem with each of `pcx`, `tga`, `jpg`, `png`, `wal` and `lmp`, matching package entries exactly and then case-insensitively. The first that decodes wins. If nothing resolves it falls back to the first embedded skin, which only MDL has. There is no directory scanning, fuzzy matching or lookup outside the open package. |
| Animations | Partial | `ModelAnimation` ranges are inferred from frame names by `inferAnimations`, not read from any file: trailing digits and separators are chopped off to form a stem, and consecutive frames sharing a stem merge into one animation. Frames with no usable name collapse to a single `frame` animation, which is what blank-named MD3 frames produce. Playback uses a user-set frames-per-second; frames are integers and no interpolation is exposed. |
| Skeletal animation | Not supported | Nothing decodes bones or joints in any format. |
| Export | Partial | `exportModelFrameObj` writes one frame as Wavefront OBJ — `g` per surface, `v`/`vt`/`vn` per vertex and `f a/a/a b/b/b c/c/c` with file-wide 1-based indices and V flipped for OBJ's bottom-up convention. A `materialName` only emits a `usemtl` line; no `.mtl` companion is written. Nothing writes MDL, MD2 or MD3. |
| CLI | Full | `model inspect` prints `modelMeshSummaryText` or JSON for a loose file or a package entry, with `--palette` for MDL's indexed skins. `model export` takes `--frame`, `--material`, `--output`, `--overwrite` and `--dry-run`, and exits 5 (`unavailable`) when geometry is unavailable, which is what MDC, MDR and IQM return. |
| Hardening | Full | Every read is range-checked. Caps: 8192 frames, 2048 surfaces, 1048576 vertices and triangles per surface, 1024 skins, 8192 tags, 1024 skin-group frames, and roughly 4M vertex slots per model. Non-finite scale or translate is an error; a file past any cap is rejected rather than decoded. |

## Audio Formats

Audio analysis lives in `src/core/asset_tools.{h,cpp}`.

| Format | Header metadata | Waveform | Export to WAV | Limits |
|---|---|---|---|---|
| RIFF/WAVE | Full | Full | Full | Waveform and export cover PCM at 8/16/24/32 bits and IEEE float at 32/64 bits, including `WAVE_FORMAT_EXTENSIBLE`. A-law, mu-law and every other codec tag are reported but produce neither peaks nor an export. Export writes canonical 16-bit PCM, copying the bytes when the source already is that. |
| Ogg Vorbis | Full | None | None | Channels, sample rate and nominal/min/max bitrate from the identification header; duration from the last reachable page granule. |
| Ogg Opus | Full | None | None | Channels, original input rate, and the fixed 48 kHz decode rate from `OpusHead`. |
| FLAC (native `fLaC`) | Full | None | None | `STREAMINFO` fields only. |
| FLAC-in-Ogg | Partial | None | None | Recognised and named; the mapped `STREAMINFO` is not parsed. |
| MP3 | Full | None | None | First valid frame header plus ID3v2 tag length; bitrate and duration are estimated from the stream size. |
| Doom `DMX` sound lumps, `MUS`, MIDI | Planned | Planned | Planned | No parser exists. |

There is no audio playback and no decoder for compressed streams; a transcode
request for a compressed entry fails with an explicit message rather than a
silent partial result. A portable decode backend remains planned in
[`docs/DEPENDENCIES.md`](DEPENDENCIES.md).

## Map Source Formats

Parsing, editing and save-back live in `src/core/level_map.{h,cpp}`; geometry
reconstruction in `src/core/map_geometry.{h,cpp}`; headless drawing in
`src/core/map_render.{h,cpp}`.

| Format or dialect | Parse | Save-back | Limits |
|---|---|---|---|
| Doom binary map lumps | Full | Full | `THINGS`, `LINEDEFS`, `SIDEDEFS`, `VERTEXES` and `SECTORS` are rewritten in place inside a copy of the source WAD; every other lump is carried through byte for byte. |
| Hexen binary map lumps | Full | Full | The 16-byte linedef and 20-byte thing strides are read and written, including the thing id, action special and five arguments. Detected by the presence of a `BEHAVIOR` lump. |
| UDMF (`TEXTMAP`) | Detect only | Copied unchanged | The map is recognised, reported as UDMF, and passed through untouched on save-as, with a warning. Text UDMF is not parsed or edited. |
| Quake / Quake II `.map`, classic faces | Full | Partial | Three-point planes with shift/rotate/scale texture parameters. |
| Valve 220 faces | Full | Partial | Explicit `[ x y z offset ]` texture axes are parsed and kept. |
| Quake III `brushDef` | Full | Partial | Three-point planes with the 2x3 texture matrix. |
| idTech4 / Radiant `brushDef3` | Full | Partial | Plane-plus-distance faces with the 2x3 texture matrix; a basis is built from the plane so the brush still solves. |
| Quake III `patchDef2` | Full | Partial | Control mesh is parsed and can be tessellated as a quadratic Bezier surface. |
| Quake III `patchDef3` | Full | Partial | As `patchDef2`, plus the explicit subdivision counts from the header tuple. |

"Partial" save-back for `.map` files means this: the writer is line-preserving.
It rewrites, inserts and deletes entity key/value lines in place, keeping the
original indentation, line endings, comments and brush bodies. Brush and patch
translations are applied to the in-memory model — so bounds, statistics, the
viewport and SVG output all move — but they are **not** written back to the
file. Persisting brush and patch geometry edits is still to come.

Engine family for a `.map` is decided by evidence rather than a filename guess:
a brush or patch primitive, a Quake III-only key, a shader-style texture path,
or a Radiant header comment selects idTech3; an explicit hint overrides it.

| Map capability | Status | Notes |
|---|---|---|
| Brush solving | Full | Half-space intersection of the face planes, using the same plane convention as the imported qbsp sources. Faces clipped fully away are reported as empty polygons so face indices stay aligned. |
| Real brush bounds | Full | Computed from the solved polygons, not from a bounding guess. |
| Doom sector outlines | Full | Traced from linedef/sidedef/sector relationships; unclosed loops are counted and reported. |
| Patch tessellation | Full | Quadratic Bezier evaluation at a configurable subdivision level. |
| Headless SVG rendering | Full | Deterministic string output with top, front and side projections, optional grid, sector fill, things, entities, vertices and labels. No GUI session or display is required. |
| Interactive 2D viewport | Full | `QPainter` widget with zoom, pan, picking and selection highlight, reading the same geometry as the SVG renderer. |
| 3D viewport | Planned | No renderer backend is linked. |
| Undo/redo | Full | Entity property edits and moves are undoable, with a saved-depth marker so "modified" versus "saved" is reported rather than guessed. |

## Entity Definition Formats

Parsing, inheritance folding and map validation live in
`src/core/entity_definitions.{h,cpp}`. Nothing ships a game's definitions: the
studio reads whatever file or folder it is pointed at. Format is decided by
content first — whichever comes earlier in the file, one of the FGD markers
`@PointClass`, `@BaseClass`, `@SolidClass`, `@NPCClass`, `@include` or a
`/*QUAKED` block — and only then by suffix. The suffixes loaded from a folder
are exactly `def`, `fgd`, `ent` and `qc`. No definition format is written back.

| Format | Parse | What is recovered |
|---|---|---|
| Radiant `.def` and QuakeC `.qc` (`/*QUAKED`) | Full | The header line gives the classname, a colour triple read as 0..1 and rescaled to 0..255, either a `?` size marker (brush class) or a `(mins) (maxs)` pair (point class), and up to 8 flag names bound to spawnflag bits 0..7 in order, where `-`, `?` and `x` consume a bit slot without naming a flag. The free-text body is sectioned by `---` banners: an all-upper token alone on a line becomes a spawnflag taking the next free bit, `"name" description` and `name : "description"` lines become spawnflags or keys, and what is left becomes the class description, capped at 64 lines. The format declares no key types, so they are inferred from the key name by `inferKeyTypeFromName`. A duplicate classname replaces the earlier block and warns. |
| Valve `.fgd` | Full | `@BaseClass` maps to a base class, `@SolidClass` to a brush class, and `@PointClass`, `@NPCClass`, `@KeyFrameClass`, `@MoveClass`, `@FilterClass`, `@PathClass` and `@OverrideClass` to point classes; any other `@` statement is skipped. Helpers handled: `base()`, `size()` with either 6 or 3 numbers, `color()` as 0..255, and `model()`, `studio()`, `studioprop()`, `iconsprite()` and `sprite()` for a model hint. Keys carry a type, display name, default and description, with `= [ ... ]` row blocks becoming spawnflags for a `flags` key named `spawnflags` and choices otherwise. `input` and `output` declarations parse and are discarded, as are identifier modifiers such as `readonly`. `@include "file.fgd"` resolves relative to the including file, to a depth of 8, with cycles broken and reported. |
| Quake III `.ent` | Partial | This is a list of placed entities, not a definition file: it reads plain `{ "key" "value" }` blocks and recovers each classname plus the union of the keys it was seen with. A `model` value starting with `*` marks the class as a brush class, otherwise it is a point class, and key types are inferred from the name. Every successful load appends the warning that "`.ent` files list placed entities, so key types, defaults, descriptions and spawnflag names are unavailable." |

| Entity capability | Status | Notes |
|---|---|---|
| Inheritance folding | Full | `loadEntityDefinitions` resolves `baseClasses` depth-first: inherited keys and flags first in base declaration order, then the derived class's own entries overriding by case-insensitive key name or by bit. Description, model hint, size and colour are inherited only where the derived class sets none. Cycles, unknown bases and chains deeper than 32 are each reported as a named warning. `parseEntityDefinitions` on a single file deliberately does not resolve inheritance, because a file may legitimately inherit from a sibling not yet read. |
| Required keys | Partial | No definition format has a required-key marker, so VibeStudio uses its own convention: a key whose description opens with `(required)`, `[required]` or `required:` is marked required and the marker is stripped from the description. |
| Map validation | Full | `validateLevelMapEntities` emits ten issue codes: `entity-missing-classname`, `entity-unknown-class`, `entity-base-class-used`, `entity-class-kind-mismatch`, `entity-undeclared-key`, `entity-key-value-invalid`, `entity-required-key-missing`, `entity-unknown-spawnflag-bit`, `entity-dangling-target` and `entity-unreachable-targetname`. It checks declared key value shapes, `spawnflags` as an integer in `0 .. 0xFFFFFFFF`, every set bit 0..31 against the class's declared flags, and the map's whole `target`/`targetname` graph in both directions. The report state is `Completed` or `Warning`, never `Failed`. |
| Dialect exceptions | Full | For a document the parser classified as `LevelMapFormat::QuakeMap` — not `Quake3Map` — spawnflag bits 8..11 are skipped, because the game code itself owns `NOT_EASY`, `NOT_MEDIUM`, `NOT_HARD` and `NOT_DEATHMATCH`. For a Doom WAD, `thing:<type>` classes are not reported as unknown, undeclared keys are not reported because Doom entity properties mirror the binary `THINGS` record, required-key checks are skipped, and the point/brush kind checks are skipped entirely. Keys beginning with `_`, plus `origin`, `spawnflags`, `angle`, `angles`, `targetname` and `target`, are never reported as undeclared. |
| Project search paths | Full | With no path given, `entityDefinitionSearchPaths` probes `.vibestudio/definitions`, `definitions`, `defs`, `scripts`, `base/scripts` and `entities` under the project root, in that order. It does not check whether any of them exist. |
| Bounded loading | Full | 512 files, 16 MiB per file, 20000 classes, 512 keys per class, 512 choices per key, include depth 8, 2000000 FGD tokens, 20000 `/*QUAKED` blocks, 65536 `.ent` blocks and 4000 reported validation issues. Per-directory results are sorted so the merge, the warnings and duplicate resolution stay deterministic. |
| Shell surface | Full | An **Entity** tab on the Levels page shows the selected entity's class summary, its keys with per-key help, and its spawnflags as `[x]`/`[ ]` bit rows — a text marker, not a colour. A definition path row with Browse and Load sits above it, and validation issues are folded into the Levels **Health** tab with selectors that navigate to the entity. |
| CLI | Full | `entity definitions` lists the classes a catalogue declares, with `--class` to print one, `--no-recursive`, and repeatable path options. `entity validate` checks a map against a catalogue and exits 4 when `errorCount > 0`, or when `--strict` is passed and `warningCount > 0`. |

## Compiled BSP And Build Artifacts

Inspection is read-only and bounds-checked; it never trusts a lump offset or
count taken from the file. Nothing writes a BSP.

| Family | Status | What is inspected |
|---|---|---|
| Quake BSP v29 | Full | 15-lump directory with per-lump offsets, lengths, entry sizes and range checks; entity lump; embedded miptex names and sizes; model, face, vertex, leaf, node and plane counts; vis and light data presence. |
| Quake BSP2 and BSP2-RMQ (`2PSB`) | Full | Same lump order with the widened node/leaf/clipnode/edge/marksurface records. |
| Half-Life BSP v30 | Full | Read through the Quake layout. |
| Quake II IBSP v38 | Full | 19-lump directory, entities, texture info with surface and content flags, brush counts. The Qbism `QBSP` extension shares the layout. |
| Quake III IBSP v46 and Quake Live v47 | Full | 17-lump directory, entities, shader references, lightmap counts. |
| Raven RBSP v1 | Full | 18-lump directory through the same Quake III path. |
| Source/VBSP and other families | Not supported | Unrecognised identifiers are reported as unknown rather than guessed at. |
| `.pts` / `.lin` leak files | Full | Point list with bounds, ready to overlay on a map view. |
| `.prt` portal files | Full | `PRT1`, `PRT2`, `PRT1-AM` and `PORTALFILE` magics with portal, leaf and cluster counts. |

## Studio Workflows

| Workflow | Status |
|---|---|
| Application shell | Q_OBJECT/moc-backed `QMainWindow` with a mode rail driving a `QStackedWidget` across Workspace, Levels, Models, Textures, Audio, Packages, Code, Shaders, Build and Settings; menu bar, toolbar, status chips, drag-and-drop file opening, and confirmation prompts for destructive actions. |
| Command palette | Interactive type-to-filter dialog over the `QAction` registry plus the documented palette entries, with shortcut and enablement state. |
| Runtime localization | Active. `.ts` catalogs for 20 targets plus pseudo-localization are compiled to `.qm` by `lrelease` at build time and installed with `QTranslator` at run time, with locale, base-language and source-language fallback, layout direction applied per locale, and an overridable catalog directory. |
| Localization tooling | Shared target registry, pseudo-localization, Arabic/Urdu RTL smoke, `QLocale` formatting and pluralization samples, expansion stress and layout-budget checks, stale/untranslated reporting, and dry-run `lupdate` extraction validation. Fully translated catalogs are still seeds, not finished translations. |
| Syntax highlighting | `QSyntaxHighlighter` with data-driven rules for plain text, config, idTech3 shader scripts, QuakeC, `.map` source, entity definitions, INI-style key-value files and JSON, themed from the active studio theme including high-contrast. |
| Package manager | Folder/PAK/WAD/ZIP/PK3 browsing, tree and list views, text/image/model/audio/script previews, selected and whole-package extraction with dry-run reporting, staged add/replace/rename/delete with conflict reporting, manifests, entry-by-entry comparison against another archive, and save-as PAK/ZIP/PK3/WAD writers. Replacing an existing package in place is GUI-only, is confirmed by a prompt that defaults to No, and moves the original to a `.bak` file only after the replacement has been written and re-hashed. |
| Texture and sprite surfaces | Zoomable, pannable image view with checkerboard alpha and nearest-neighbour magnification, palette swatch grid with index readout, crop/resize/palette conversion, and batch conversion from the CLI. Output is written through Qt image formats; writing back to idTech texture formats is planned. |
| Audio surface | Header metadata, a real min/max envelope waveform painted from decoded PCM, and WAV export. Playback is planned. |
| Model surface | A software `QPainter` model viewport drawing decoded MDL, MD2 and MD3 geometry in wireframe, flat-shaded or textured mode, with a resolved skin, inferred animation playback and single-frame OBJ export, beside the existing metadata, skin and material dependency summaries. MDC, MDR and IQM reach a dedicated no-geometry state instead of a drawing. |
| Level editor | Map inspection, entity property editing, object moves, undo/redo, non-destructive save-as, the interactive viewport, SVG export, health summaries, entity definition loading and entity validation, and compile-plan handoff. Brush/patch geometry persistence and Radiant-class editing are planned. |
| Build pipelines | Chained pipelines with stage ordering, per-stage enable/disable and extra arguments, streamed output, diagnostics, leak detection, file hashes and command manifests: `quake-full`, `quake-fast`, `quake-bsp-only`, `quake3-full`, `quake3-bsp-only`, `doom-zdbsp` and `doom-zokumbsp`. |
| Game launch | Launch plans for Doom, Quake, Quake II and Quake III source ports, plus a pass-through custom profile, built from the selected installation profile and reviewable before anything starts, with a run path that starts the configured executable. |
| Compiler registry | Descriptors over the imported submodules, executable discovery through source trees, known build outputs, extra search paths, user and project overrides and `PATH`, plus short version/help probes and argument presets. |
| Editor profiles | Routed presets for the VibeStudio default and GtkRadiant 1.6.0-, NetRadiant Custom-, TrenchBroom- and QuArK-style workflows, with stable command IDs, layout/camera/selection/grid metadata and shortcut conflict smoke coverage. Full fidelity is planned. |
| idTech3 shader graph | Shader script parsing, stage and dependency graph lines, stage previews, raw text detail, stage directive round-tripping, and validation of texture references against mounted packages. |
| Code/script IDE | Syntax highlighting, diagnostics boundaries, project-wide find/replace, source tree indexing, symbol search, build task hints and launch profile summaries. A full IDE is planned. |
| CLI | Subcommand router with JSON output, quiet/verbose, watch streaming, machine-readable task state and stable exit codes, covering project, install, package (including `package compare`), asset, map (including `map render`), `bsp inspect`, `entity definitions` and `entity validate`, `model inspect` and `model export`, `build list|plan|run`, `launch plan|run`, shader, sprite, code, extension, compiler, localization, diagnostics, AI and credits commands. |
| Accessibility | High-visibility themes, 100–200% text scaling, reduced motion, non-color status cues, accessible control metadata and a release audit gate. Deep per-surface audits are ongoing. |
| OS-backed text to speech | Preference storage only. Qt TextToSpeech is not linked and nothing is spoken. |
| AI connector workflows | Opt-in settings, provider-neutral connector and model registry, redacted credential status, safe tool descriptors, no-write manifests and proposal review surfaces. No provider network call is made; OpenAI is the only connector past the stub stage, and Claude, Gemini, ElevenLabs, Meshy and local runtimes are design stubs. |
| Portable packaging | Windows/macOS/Linux staging scripts, offline guide generation, checksums, license bundles and release asset validation. Qt deployment tooling and signing are planned. |
| Asset index / SQLite search | Planned. Browsing is in-memory today. |

## Imported Compilers

| Tool | Status |
|---|---|
| ericw-tools | Imported as a submodule; `qbsp`, `vis` and `light` are wrapped and chained. |
| q3map2-nrc | q3map2 from NetRadiant Custom, imported as a submodule; BSP, vis and light stages are wrapped and ordered. |
| ZDBSP | Imported as a submodule; wrapped as a Doom node-builder stage. |
| ZokumBSP | Imported as a submodule; wrapped as a Doom node-builder stage. |

VibeStudio does not build the imported compilers by default. Integration detail
lives in [`docs/COMPILER_INTEGRATION.md`](COMPILER_INTEGRATION.md).
