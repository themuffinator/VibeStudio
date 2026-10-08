# Support Matrix

Session metering supports stereo pre/post track, bus and master sample peaks,
RMS, held maxima, per-channel over-range counts and phase correlation. The GUI
shows live playback readings; **Analyze Range** and `asset audio-session meters`
work without devices and compensate tap latency over exact frame bounds.
Loop playback keeps effects/routing/modulation continuous, with wrapped authored
automation and one compensation prime using repeated future context. Meter
history counts every cycle on unwrapped signal time. Finite exports remain one
pass. Metering is read-only and does not change rendered samples or native v7 files.
These meters do not provide live true-peak or LUFS measurements. See
[Session Meters](AUDIO_EDITOR.md#session-meters) for response and timing semantics.

Audio sessions expose source usage and external file availability, reviewed
bit-identical relinking, replacement across every referencing clip, source
renaming and unused embedded-source removal through GUI and CLI. Replacement
preserves channel count and clip descriptors, requires explicit rate conversion,
and uses normal undo/recovery and native v7 persistence. Missing references leave
embedded playback intact. Source files are never modified or deleted. Bulk
relink discovery, automatic waveform source round trips and disk streaming
remain open in the [DAW plan](plans/audio-daw.md).

Audio sessions support scoped clear, ripple-delete, insert-silence and repeat
range edits through shared GUI/CLI services. Optional automation following
retains cut linear/smooth domains and splice values; master following is a
separate all-track choice. Clip/fade windows, independent repeated groups,
undo/recovery and native v7 persistence share rendering with mixdown/stems and
waveform handoff. Tempo/meter markers remain unchanged. Musical anchoring,
tempo ramps, automation recording and the remaining [DAW gates](plans/audio-daw.md)
are still open.

Recording review persistence: native Open Saved Review, Save and Save As share
bounded v1/v2/v3 JSON with CLI `asset audio-recording save-review`. Reopening
restores every ordered selection, including non-comp multi-pass queues. Relative
recording paths, source verification and output revision guards preserve portable
recipes and external edits. This does not add native take lanes or automatic
revision of previously imported clips. See [saved recording reviews](AUDIO_DUPLEX.md#save-and-reopen-recording-reviews).

## Shared formats and workspaces

The [format catalog](ASSET_FORMATS.md) separates pixel/sample/geometry import
from metadata inspection, native export and runtime Qt availability. `asset
formats --json` and `asset route <path> --json` expose it. Texture import filters,
image routing and project file classification share this catalog. PK4/PKZ
suffixes now use the ZIP reader, with its existing compression/validation limits.

DDS imports a 2D base surface (BC1–BC5, RXGB, supported RGB/luminance masks and
DX10 variants); FTX imports RGBA; SWL imports its palette and four mips. DDS/FTX
export is uncompressed 32-bit color/alpha through the Texture Editor, `texture
export` and `asset convert`, with normal staging and validation. DDS cubes,
volumes, arrays and BC6/7, compressed DDS writing and native SWL output are
unsupported. `extra-image-smoke` checks pixel oracles, round trips and bounds.

[Portable workspaces](WORKSPACES.md) save/load project, package/draft, map, code
tabs, selected assets and active module using version-1 `.vibeworkspace` JSON.
`workspace-smoke` and `workspace-ui-smoke` cover relocation, invalid inputs,
stale saves and GUI integration. Unsaved authoring payloads, asset-editor
dialogs and window/camera layouts are outside workspace v1.


Modeller collision supports up to 64 static oriented boxes, source schema 5,
selection-aware undo/recovery and explicit Quake-family clip-brush map export
or level placement. Native mesh formats do not retain these volumes. Quake III
requires a project clip shader; availability and engine behavior are unverified.
Arbitrary convex/animated collision and linked prop updates remain open. See
[Model Collision](MODEL_COLLISION.md) for exact limits and target differences.

## Level material painting

| Workflow | Status | Current boundary |
| --- | --- | --- |
| Camera paint/sample | Implemented, acceptance partial | Exact brush-face, patch, Doom wall, floor and ceiling targets; one undo per stroke; pending/cancel state; package/staging image refresh; model source materials stay in Models. No interpolation between pointer samples. |
| Explicit targets and CLI | Implemented | Shared atomic validation and persistence; four text face dialects, Doom/Hexen upper/lower/middle walls and floor/ceiling flats. UDMF uses its dedicated property editor. |
| Doom camera and assets | Implemented, acceptance partial | Concave/holed/disconnected sectors, floors/ceilings, both masked middle sides, offsets/pegging, PNAMES/TEXTURE1/TEXTURE2 composition, PLAYPAL and exact staged namespaces. Invalid boundaries/assets produce diagnostics. Engine lighting, sky effects, animation, sprites, advanced UDMF effects, Strife tables, ZDoom TEXTURES and automatic multi-package merging remain open. |
| Doom dependency review | Implemented, export partial | Shared material inputs retain namespace and directory occurrence. Asset subset export is disabled until texture-table and namespace closure can be preserved or rewritten. Complete package save remains available. |
| Preview acceptance | Partial | Full engine shader effects, native input, screen readers, other operating systems and large-map latency remain unverified. |

## Level navigation

| Workflow | Status | Current boundary |
| --- | --- | --- |
| Four-view workspace | Implemented, acceptance partial | Camera/top/front/side share selection, editing, visibility, leak trails and package-backed assets. Planes retain independent pan/zoom and reuse solved geometry. Layout overrides and splitter sizes persist, with `editor layout` CLI parity. Persistent groups/layers, linked pan/zoom and native/production-scale acceptance remain open. |
| Saved level views | Implemented, acceptance partial | Named camera/plan/layout bookmarks, per-map and per-WAD-marker storage, management, portable files, Save As handoff and shared `editor bookmarks` CLI. Atomic writes detect stale editors. Recovery records and package-entry identities do not yet carry views; native and cross-platform acceptance remain open. |

See [Four-View Workspace](LEVEL_EDITOR.md#four-view-workspace).
See [Saved Level Views](LEVEL_EDITOR.md#saved-level-views).

## Reusable level assets

| Workflow | Status | Current boundary |
| --- | --- | --- |
| `.vprefab` capture and placement | Partial | Versioned Quake/Quake II/Quake III assemblies contain brushes, patches and entity properties, retain material/model/sound paths, promote complete brush owners, namespace standard internal targets, and insert through one map undo. Worker previews share package/staged assets and Models rendering. GUI and export/inspect/insert CLI support file or package sources. Same-family/same-dialect placement only; no Doom, linked instances, automatic asset bundling or implicit dialect conversion. |

See [Reusable Prefabs](LEVEL_EDITOR.md#reusable-prefabs) for limits and verification.

## Texture authoring

| Workflow | Status | Current boundary |
| --- | --- | --- |
| Raster editing | Implemented, bounded | Layers, opacity/blending, clipping selection, copy/cut/paste/move, square/round brush, explicit replace/blend alpha, pencil, eraser, line/rectangle/ellipse, tolerant connected fill, eyedropper, crop, canvas sizing, whole-canvas resampling/rotation, selected-pixel resize/rotation with nine anchors, flip, indexed palette remap, wrapped painting/fill, cyclic offset, anchored zoom, tile preview, and bounded undo. The working representation is 8-bit RGBA or indexed color, limited to 4,194,304 canvas pixels and 33,554,432 aggregate layer pixels. Pressure-sensitive painting and sprite animation authoring remain unavailable. |
| `.vtexture` authoring projects | Implemented, bounded | Versioned bounded layered persistence, exact indexed tables including grayscale, embedded palette provenance and export settings, cancellable save preparation and chunked reads/checksums, atomic publication, verified backups and external-change conflicts. Image export never cleans a project. Background local recovery, inspector/CLI restoration to a new file, and deferred close continuation are verified. Projects retain up to 32 layers within the aggregate pixel budget; undo history and session brush/view settings are not persisted. |
| PNG output | Full | Atomic save, explicit overwrite, destination fingerprints during encoding, guarded new-file publication, write-free dry runs, and generated-byte package staging. Browser export snapshots the displayed frame and runs cancellable encoding before separate publication, accepting up to 16,777,216 decoded pixels. Existing destinations/encoded output are bounded to 64 MiB, including while the PNG sink is writing. Target engines must support PNG. |
| Native texture output | Partial | Nine GUI/CLI profiles: RGBA/indexed PNG, TGA, PCX, Quake miptexture/single-texture WAD2, Quake II WAL, Doom flat/patch. Palette consent/provenance, alpha rules, reserved/fullbright indices, four generated/previewable mips, native names/flags/offsets, engine limits, cancellation and guarded publication. Fixed native fixtures and independent PNG/TGA/PCX readers verify bytes. WAD2/Doom native staging preserves lump types, namespaces and grouped draft undo. Sprite/WAD3 encoding, Doom wall-definition composition and live target-engine acceptance remain outside the implemented boundary. |
| Authoring handoff | Implemented with explicit limits | Layered projects feed staged images, brush/patch references, shader images, model materials and dependency review. Stage and Apply validates Quake III PNG/TGA, Quake II WAL and WAD2 miptexture destinations atomically; restaging pixels preserves unchanged map history. Native WAD namespace/type rules and palette/preview invalidation are tested. Generated assets pass ericw qbsp (Quake/Quake II) and q3map2 BSP/VIS/LIGHT acceptance. Saves remain separate. Shader rewrites and Doom wall composition remain unavailable; target-engine rendering has not been verified. |

See [Texture Editor](TEXTURE_EDITOR.md) for limits, commands, and controls.

The bounded texture workflow has release-candidate evidence from 42 relevant
suites on Windows and Linux/WSL, independent raster readers, native format
fixtures and a 22-step compiler/package handoff. Windows runtime deployment
is verified locally with Qt absent from PATH. Physical input, live clipboard,
screen readers, native windows, macOS/ARM, clean-machine deployment and game
rendering remain unverified. Linux checks use Qt 6.4.2 on a WSL-mounted Windows
filesystem, with source-language fallback and audio playback disabled. See
the [requirement audit](plans/texture-editor-release-candidate.md#current-requirement-audit)
for evidence and platform limits; this is not acceptance of the whole suite.

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
| idTech1 / Doom | Partial | WAD browsing and writing, including multi-map WADs; Doom and Hexen map lumps parse and save back; patches, flats, PLAYPAL and COLORMAP decode to pixels; sector outlines, SVG rendering and node-builder pipelines run. KVX voxel models decode and draw, and source-port MD2/MD3 models edit and export (see [Native model formats](MODEL_FORMATS.md)). UDMF supports lossless properties, native transforms and common-field previews. |
| idTech2 / Quake, Quake II | Partial | PAK/WAD2/WAD3/folder browsing; `.map` parsing with classic and Valve 220 faces; miptex, `.lmp`, `.wal`, `.m8`, `.m32`, `.sp2`, PCX and `.spr` decode; Quake MDL, Hexen II MDL, MD2, Heretic II FM and Half-Life MDL models decode and draw, and MDL/MD2 edit and export; Radiant `.def` and Valve `.fgd` entity definitions parse and validate a map; BSP29/BSP2/IBSP38 inspection; ericw-tools pipelines run. |
| idTech3 / Quake III | Partial | PK3/ZIP browsing and writing with DEFLATE; `.map` parsing with `brushDef`, `brushDef3`, `patchDef2` and `patchDef3`; shader script round-tripping; MD3, MDC, MDS, MDM/MDX, MDR, Ghoul 2 GLM/GLA and IQM models decode and draw, with skeletons posed into frames, and MD3/IQM edit and export; `.ent` entity lists parse; IBSP46/47 and RBSP inspection; q3map2 pipelines run. |
| idTech4 / Doom 3 | Partial | MD5 mesh and animation (with `.def` model declarations), LightWave LWO and ASE models decode and draw; MD5, ASE and IQM export with joints and weights; `brushDef3` map faces parse (see Map Source Formats). Model decoders are tested on synthetic files only. |

## Image Formats

Decoders use `src/core/idtech_image.{h,cpp}` and its `extra_image`/DDS helpers and produce a `QImage`,
except the `.sp2` container, which carries no pixels of its own and produces a
list of frames naming images stored elsewhere in the package.
"Palette source" is what colours the indexed formats: VibeStudio first looks for
a real palette inside the open package, and only falls back to a generated,
license-clean ramp when none is found. The fallback is always reported as
generated, never presented as a game palette.

Image payloads are limited to 64 MiB, each decoded surface to 16,777,216 pixels,
and all retained mip levels or sprite frames to 33,554,432 pixels. Sprite groups
share a 4,096-frame cap; external `.sp2` frame reads share the same pixel budget
and a 64 MiB aggregate input-work budget, including repeated references.
Qt format detection reads headers without decoding pixels. Qt surfaces above
32 bits per pixel are normalized to 8-bit RGBA, with a 128 MiB source decode buffer
check. Texture editor imports additionally enforce 4096 pixels per side and
4,194,304 pixels per surface. Doom patch column references also share a 64 MiB
decode work limit, including repeated offsets. Truncated columns fail the entire
decode. Package image and palette reads check directory sizes before inflation
and reject incomplete payloads. Rejected decodes expose no partial images.
Native decoders check cancellation between rows/blocks and external-frame reads;
opaque Qt codec calls remain bounded but cannot be interrupted internally.
Browser source thumbnails use a shared 64 MiB / 512-entry cache with 256-pixel
physical tiles and visible-row demand. Property searches scan offscreen metadata
without retaining full images or thumbnail pixels for every match.

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

Each on-disk archive/folder open admits at most 250,000 records (including skipped
records and implied folders), 128 path components, 64 MiB of logical index metadata
and 64 MiB of chunk fingerprints. Folder fingerprints share that budget. GUI/CLI
refuse over-limit sources without partial listings; ZIP directory records stream.
Combined sessions share these bounds across at most 64 layers, including
shadowed source records and mount-prefix folders. Multi-folder map texture audits
use the same admission and report incomplete sources through JSON and exit 4.
Staged documents cap retained generated bytes at 256 MiB and payload hashes at
128 MiB, including base and history; draft objects share the hash allowance.
Retained metadata admits 1,000,000 logical records and 128 MiB of index/text
across base, operations and history. Undo/redo slots remain reserved; drafts
check metadata before reading payloads. General reader snapshots and staged
folder projections also admit 250,000 records, 64 MiB of metadata text and 128
path components, including diagnostic records and implied parents. Refusal
preserves the document/history and reports a reason. Individual edits and
outermost edit groups admit their final plan and browser view before history
commit. Legacy history remains recoverable through Undo/Redo. WAD/helper
allocation audits and aggregate independent snapshots remain open. See
[Opening limits](PACKAGE_MANAGER.md#opening-limits),
[Retained document content](PACKAGE_MANAGER.md#retained-document-content) and
[Retained document metadata](PACKAGE_MANAGER.md#retained-document-metadata) and
[Reader snapshot limits](PACKAGE_MANAGER.md#reader-snapshot-limits).

Readers live in `src/core/package_archive.{h,cpp}`, writers in
`src/core/package_staging.{h,cpp}`, and the DEFLATE codec both sides use is
`src/core/deflate.{h,cpp}`.

| Format | Read | Write | Compression | Limits |
|---|---|---|---|---|
| Folder | Full | Not supported | n/a | Recursive listing, byte reads, and use as an extraction source. Writing a package back out as a folder tree is not implemented. |
| Quake PAK | Full | Full | None — PAK stores everything uncompressed | Deterministic writer. Every entry is readable because the container has no compression. |
| Doom IWAD / PWAD | Full | Full | None | 16-byte directory records. Existing WADs retain their IWAD/PWAD magic, exact source lump order and ordinal-based payload identity, including repeated map names, named UDMF maps and sidecars. New WAD documents assemble binary/GL runs in the planned view, with ambiguous ownership blocked. Drafts and subsets retain that reviewed order, including UDMF sidecars and native texture anchors. Conversion from non-WAD sources uses canonical binary-map ordering; ambiguous loose multi-map plans are refused. Names must be nonempty Latin-1 without folder separators and at most 8 characters; lump sizes and directory offsets remain within the 32-bit format bound. Subset export expands complete binary/UDMF map and matching GL groups, namespace boundaries and local texture name tables; incomplete/ambiguous groups or skipped records block export. This is grouping preservation, not complete game dependency closure. Reviewed map/GL renames and complete map, namespace or texture-table deletion share GUI/CLI staging and undo. |
| Quake WAD2 / Half-Life WAD3 | Full | Full | None for written lumps | 32-byte directory records with the type byte preserved. Compressed WAD2/WAD3 lumps are listed with a note but are not decoded on read, and the writer emits uncompressed lumps only. |
| ZIP | Full | Full | Read: stored and DEFLATE. Write: stored or DEFLATE | ZIP64 end records, local size fields and signed/unsigned data descriptors are checked. Names support strict UTF-8, CP437 and checksum-matched Unicode Path fields. CRC-32 is verified on read. Multi-disk archives are refused; encrypted entries, unsupported processing flags and methods other than 0 or 8 are listed but not decoded. Digital signatures are recognized structurally, without authentication. |
| Quake III PK3 | Full | Full | Same as ZIP | Identical container handling, plus layer shadowing when several PK3s are mounted in one session. |
| Nested archives | Partial | n/a | n/a | A package inside a package is flagged and can be mounted as an extra layer. Nested archives are not written back into their host. |

A Doom WAD's namespace markers type the lumps between them: `F_START`/`F_END`
(and `FF_`, `F1_` to `F3_`) hold flats, `S_`/`SS_` sprites, `P_`/`PP_`/`P1_`
to `P3_` wall patches, and ZDoom's `TX_` textures, reported as the `wad-flat`,
`wad-sprite`, `wad-patch`, and `wad-texture` type hints, and the markers
themselves as `wad-marker`. Namespaces nest: an `_END` closes the innermost one
of its kind, and the lumps after it are back in the one around it. A lump
outside them whose first 8 bytes are a DMX sound header (format 3, a rate from
4000 to 48000 Hz, a count that fills the lump) is `wad-sound`, whatever its
name, which is how Heretic's and Hexen's sounds reach the Audio page; map lumps
are never taken for sounds. Every other lump is `wad-lump`. The Textures page
lists the namespaced lumps, every WAD2/WAD3 lump, and the graphics an IWAD
keeps outside any namespace by their well-known names (`PLAYPAL`, `COLORMAP`,
`TITLEPIC`, the `M_` menu, `ST` status bar, and `WI` intermission patches); a
flat is decoded as a flat because of its namespace, whatever its bytes
resemble, and a bare 320x200 lump, Heretic's and Hexen's full-screen picture,
decodes as one. Graphics a PWAD keeps outside any namespace under other names
are not listed.

Extraction refuses absolute paths, drive-qualified paths, `..` traversal,
reserved Windows device names, control characters and trailing dots or spaces,
and rejects symbolic links and NTFS junctions along the output path. Selection
preflight rejects repeated/case-folded/Unicode-normalized output names,
file/directory conflicts, and writes to source archives, imported staging files
or inside a source folder. Stored and DEFLATE payloads stream with within-file
cancellation, and each completed file is published atomically. Current portable
path checks cannot eliminate a foreign filesystem mutation in the final
check/commit window. The GUI Extract actions use an owned worker; drag copies
and the unified staged browser remain separate integration work.

The written DEFLATE stream comes from the in-tree encoder in
`src/core/deflate.{h,cpp}`. Every level except `store` cuts the input into
blocks of at most 65535 bytes and measures all three RFC 1951 block types —
stored, fixed Huffman and dynamic Huffman — keeping the smallest, so a block is
never larger than storing its bytes would be. The four levels (`store`, `fast`,
`default`, `best`) differ only in how hard the LZ77 hash-chain match search
works. `PackageWriteRequest::compression` defaults to `default` and is applied
to ZIP/PK3 only; PAK and WAD output is always stored. CLI archive output accepts
all four levels through `--compression`. Entries that do not get smaller are stored
verbatim, so an already-compressed asset is never inflated by re-packing. See
[`docs/STACK.md`](STACK.md) for why an in-tree codec was taken over zlib.
Archive writers now stream verified chunks; incremental DEFLATE retains one
block plus its dictionary. ZIP measures then writes, checks both passes and
keeps the stored fallback. Per-file progress/cancellation covers reads,
compression and manifest hashes. Directory/plan metadata remains proportional
to entries; native-platform and full-scale memory acceptance remain open.

Saved `.vibepackage` storage supports byte/file save limits (32 GiB/200,000 files
by default), full content/history review and explicit unused-object reclamation
through the GUI and CLI. Participating document and worker readers block cleanup;
read-only review creates no lock file. Quotas include unused files and peak
metadata writes. Corrupt/incomplete drafts and foreign layouts are refused.
Windows reader and process-crash fixtures are available; native macOS/Linux,
network, disk-full and power-loss acceptance remain open. See
[Saved draft storage](PACKAGE_MANAGER.md#saved-draft-storage).

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
| `comparePackageToPlan(left, plan, request)` | Full | The staged plan is the right-hand side, so `Added` means the plan adds it. Both sides resolve repeated WAD names positionally. Exposed through **Review Changes** and CLI `package compare <source> --staged`. |
| Status categories | Full | `Identical`, `Added`, `Removed`, `Changed`, `CaseOnly`, and `Uncompared`. Case-only spelling takes precedence over `Changed`, with any byte difference carried in `contentChanged`. Unchecked content prevents a content match. |
| Content test | Full | Reported per entry as `NotCompared`, `SizeOnly`, or `Sha256`; `Crc32` is retained as a legacy report enum. Differing sizes decide on their own; equal-size payloads are read, verified and hashed. Directories, explicit metadata-only mode, unreadable entries and entries above `maxEntryBytes` (default 256 MiB) remain unchecked with a reason. Archive and planned reads stream with cancellation within a file and explicit side/path/byte progress. Malformed lengths or unsupported stream readers remain unchecked; the budget limits I/O rather than allocation. |
| Reporting | Full | `packageCompareLines` / `packageCompareText` skip identical rows and print case-only rows as `(left -> right)`; `packageCompareJson` is `schemaVersion` 1 and `packageCompareJsonBytes` is indented UTF-8, byte for byte reproducible. |
| CLI `package compare` | Full | `vibestudio --cli package compare <left> <right>`, with the right side also accepted as `--against`, plus `--metadata-only`, `--include-directories`, `--max-entry-bytes <n>` and `--json`. Identical exits 0; **any difference exits 4 (`validation-failed`)** so a release script can gate on "these two packages match". A package that will not open exits 3. |
| Shell comparison | Full | **Compare** compares source archives; **Review Changes** compares the current plan. Worker dialogs expose searchable results, hashes, unchecked entries, side/path/byte progress, cancellation, and JSON export. Partial results include only completed rows. |
| Directory records | Partial | Excluded unless `includeDirectories` is set, and records the reader invented (`storageMethod == "synthetic"`) are always skipped. |

### Package Integrity Validation

**Packages > Validate** and `package validate` share `package_validation.*`.
Validation streams every file with bounded payload buffers, checks complete
sizes, ZIP CRCs and DEFLATE boundaries, and records SHA-256 hashes. Repeated WAD
lumps are separate entries identified by physical ordinal. Local ZIP headers
are checked against the central directory; unsupported methods/encryption and
loader warnings prevent a pass. Optional byte budgets leave oversized files
unchecked. This validates container payloads, not each asset's format or game
semantics. Directory indexing still buffers metadata, and some older preview,
drag-export, and writer paths still buffer individual files. Archive and imported
file identities contain whole-file and 64 KiB chunk SHA-256 hashes. Reads check
chunks before returning bytes; saves check complete sources before writing and
publication. Equal-size/equal-time changes are detected, and folder file
membership is checked. Accepted file imports keep independent temporary copies
through undo/redo and worker readers; draft objects provide persistent storage.
Fingerprints on original archive/folder sources remain source-change checks, not a
filesystem snapshot. Opening and file staging use cancellable workers. Path-based
edits of repeated entries are blocked; explicit occurrence editing is supported,
while semantic WAD map-group edits remain open.

## Model Formats

Editable `.mesh.json` sources now support MD2/MD3 import, baked primitives,
component transforms, face topology edits, per-selection UV edits, frame
duplication/deletion/renaming, animated MD2/MD3 export, and OBJ frame export.
Schema 3 retains UV seam marks and MD2 skin dimensions, with version-1/2 reads.
MD2 export reports precision loss, preserves ordered skin slots and validates
all poses against original-renderer limits. Mesh handoff prepares exports on
the cancellable document worker; MD2 automatic map placement remains unavailable.
Materials and tags are retained across edits, with strict validation and
atomic source saves with reviewed destination checks. Checksummed local recovery
runs alongside cancellable document workers for import/edit/save/export and
restores as an unsaved draft; catalog and restore commands are available in the
CLI. MDL authoring retains every indexed skin member, native group/timing and
header value in schema-4 sources. The Quake MDL inspector and `model mdl` CLI
edit them; native export and package staging apply original Quake limits.
The studio previews stored or original-GLQuake group/skin timing. Indexed PNG,
PCX, LMP/miptexture, WAL and M8 skins can be copied from loose files or exact
staged package entries, with shared GUI/CLI validation and undo. Original-engine
acceptance remains open. See [Editable Meshes](MODEL_MESH.md)
for exact limits and the remaining [release gates](MODELLER_RELEASE.md).

Authoring is available for static box/cylinder/plane designs in schema-versioned
`.model.json`, with a 1 MiB input limit and 32 parts. The shared design service
writes one-frame MD3 version 15 or OBJ, including per-part material references,
X/Y/Z rotation, and UV scale/offset/rotation. Sources save as schema 2, with
schema 1 still readable. Solid, wireframe, and UV checker previews share these
transforms; project material rendering and vertex-level UV editing are deferred.
The service does not rewrite imported models or generate animation, tags, or MTL files.
MD3 range and quantization failures are explicit. Generated props can be staged
into non-WAD packages, placed in Quake III maps, and audited with their material
dependencies. See [Model Design](MODEL_DESIGN.md).

Linked model assemblies use separate schema-1 `.assembly.json` sources, bounded
to 1 MiB and 32 parts. File or current-package references retain independent
frame ranges, rate/phase, nested attachment tags and local transforms. Preview
and explicit static OBJ/MD2/MD3 baking share the mesh services. Bounded assembly
recovery retains the linked recipe, selection and time as a separate unsaved draft.
This does not provide skeletal animation, native assembly export or game timing
configuration. See [Model Assemblies](MODEL_ASSEMBLY.md) for combined
geometry limits, package context and source-protection rules.

Detection and the format catalogue live in `src/core/model_mesh.{h,cpp}`; each
decoder has its own `src/core/model_format_*.cpp` behind
`src/core/model_formats_p.h`, and skeletons are in `src/core/model_skeleton.{h,cpp}`.
The widget that draws models is `src/app/model_viewport.{h,cpp}`; the header
summaries that packaging and dependency work use are in `src/core/asset_tools.{h,cpp}`.
`detectModelMeshFormat` checks magic first and falls back to the extension so a
damaged file still reports which decoder was expected. Each decoder accepts
exactly the versions its game writes; any other version is rejected with a
named error rather than guessed at. Every decoder outside MDL, MD2, MD3 and OBJ
is new and tested on synthetic files only; see
[Native Model Formats](MODEL_FORMATS.md) for conventions and companion files.

| Format | Status | What decodes, and what does not |
|---|---|---|
| Quake MDL (`IDPO` version 6) | Full geometry/native-data decode | One surface, `scale * byte + translate` positions, published 162-entry normals, texel-centre UVs and back-facing seam copies. Geometry is flattened into poses while native group boundaries and cumulative times remain intact. All indexed skin members, group times, flags, eye position, synchronization and size hint are retained. The first member provides the default preview; the editor can preview any selected member. The editor previews stored software-Quake timing or original-GLQuake schedules with deterministic seeking and entity phase, using exact poses and cached skin members. The CLI exposes the same sampler; original-engine acceptance remains open. Every decoded pose reports the header radius; export recomputes bounds/radius. Version 6 only; Half-Life IDST is unsupported. |
| Quake II MD2 (`IDP2` version 8) | Partial | Indexed geometry: position/ST pairs become combined corners, STs use texel-centre sampling at the retained skin dimensions, the 162-entry normal table supplies per-frame normals, and ordered external skin slots retain duplicates. Preview uses indexed triangles; editable import audits matching GL strips/fans and refuses differing or malformed streams and invalid normal indices. Source-only GL geometry is not editable. Original-renderer MD2 export supports all poses with target-limit, collapse/reversal and quantization diagnostics; see [Editable Meshes](MODEL_MESH.md). |
| Quake III MD3 (`IDP3` version 15) | Full | Decodes: per-frame mins, maxs, local origin, radius and name; `numTags x numFrames` tags with name, origin and a 3x3 axis; and a surface chain walked by each surface's own `ofsEnd` with every hop bounds-checked, giving per-surface shader names, float STs, `int32` triangles and `int16` XYZ at 1/64 unit steps, with normals unpacked from the 16-bit lat/long pair. Does not: nothing assembles a multi-part model from the tags — there is no tag following and no head/upper/lower attachment; shader names are treated as texture paths only, with no `.shader` script parsed and `shaderIndex` unused. Surfaces with fewer frames than the model have their last frame repeated, with a warning. If no surface survives the walk, geometry is unavailable and "No MD3 surface could be decoded." is recorded. |
| MDC (`IDPC` version 2) | Partial | Base and compressed frames (`MDC_DIST_SCALE` offsets with the 256-entry normal table), shaders, STs, triangles and tags with compressed angles. Read only; export MD3, which RTCW and ET also load. |
| MDR (`RDM5` version 2) | Partial | Per-frame model-space bone matrices, including compressed frames, weighted vertices of the first level of detail, and tags. The file has no hierarchy or bind pose: joints are roots and the first frame is the bind pose. Read only. |
| IQM (`INTERQUAKEMODEL` version 2) | Partial | Meshes, positions, UVs, normals, blend indices and weights, joints, poses, animations with their rates, and bounds; static meshes too. Written with joints, weights and every skeletal clip. |
| Hexen II MDL (`RAPO` version 50) | Partial | Quake MDL with separate texture-coordinate indices: poses, frame groups, indexed skins and model flags. Read only. |
| Heretic II FM (`header` chunks) | Partial | Frames, skins and GL-command texture coordinates (without the half-texel offset); each mesh node becomes a surface. Read only. |
| MDS (`MDSW` version 4) | Partial | Bones with absolute rotations and parent distances, weights, frames, bone and named tags, at the full level of detail. Torso blending is a run-time effect and is not applied. Read only. |
| MDM (`MDMW` version 3) and MDX (`MDXW` version 2) | Partial | MDM surfaces and weights skinned against the MDX found beside the mesh (or `animations/human/base/body.mdx`); an MDX alone opens as a skeleton with frames. An MDM without its MDX is refused. Read only. |
| Ghoul 2 GLM (`2LGM`) and GLA (`2LGA`), version 6 | Partial | GLM surfaces of the first level of detail, the surface hierarchy, off and bolt surfaces (bolts as tags) and up to four weights per vertex, against the GLA the mesh names; a GLA alone opens as a skeleton. A GLM without its GLA, or a Jedi Outcast mesh with only a Jedi Academy skeleton, is refused. Read only. |
| MD5 (`MD5Version 10`) | Partial | `.md5mesh` joints, meshes, weights and shaders; `.md5anim` hierarchy, base frame, bounds and frames at their rate; every animation beside the mesh and those named by Doom 3 `.def` model declarations. Both written back. |
| LightWave LWO (`LWO2`, `LWOB`) | Partial | The first layer: points, polygons, UV maps, smoothing angles and surface names as materials, as Doom 3 and Quake 4 load them. Read only. |
| ASCII Scene Export (`*3DSMAX_ASCIIEXPORT`) | Partial | Objects, materials, mapping and normals; the first mesh of each object; sub-materials split into surfaces as q3map2 does. Written as one static frame. |
| KVX voxels | Partial | The first mip level, meshed into faces the way GZDoom meshes slabs, coloured from the file's palette and placed as VOXELDEF places them. Read only. |
| Half-Life MDL (`IDST`/`IDSQ` version 10) | Partial | Bones, run-length animation values, sequences (with `<name>01.mdl` sequence groups), body parts and their first model, embedded 8-bit textures (from `<name>T.mdl` when external) and attachments as tags. Chrome texture coordinates are a run-time effect and are not generated. Read only. |
| OBJ (`.obj`) | Polygon geometry | Shared GUI/CLI import with bounded concave planar triangulation, separate UV/normal indices, smoothing groups, direct package material paths and one pose. Package previews use cancellable verified streaming and per-surface material resolution. MTL libraries, free-form geometry, vertex colours, loose vertices and unsupported records fail explicitly. See [OBJ interchange](MODEL_MESH.md#obj-polygon-interchange). |
| glTF, FBX, Collada and other modern formats | Planned | `detectModelMeshFormat` returns `Unknown` and the decode fails with "The file is not a recognised idTech model." Assimp remains an optional future path and is not linked. |

| Model capability | Status | Notes |
|---|---|---|
| Software viewport | Full | `ModelViewport` is a `QPainter` widget with no OpenGL dependency: orthographic/perspective projection, a bounded per-pixel depth buffer, perspective-correct repeating bilinear textures, near-plane clipping that retains UVs, alpha compositing and nearest-surface picking, built around the idTech Z-up convention. Selection hatches and edges respect occlusion; wireframe intentionally shows the whole mesh. Wireframe, flat-shaded and textured modes; orbit, pan and zoom by mouse and keyboard; per-triangle hover reporting. Backface culling, grid, axes and edge overlays exist on the widget but are not wired to shell controls. |
| Skin resolution | Partial | `resolveModelSkin` collects the mesh and surface skin paths, normalises them, and tries the literal path plus the stem with each of `pcx`, `tga`, `jpg`, `png`, `wal` and `lmp`, matching package entries exactly and then case-insensitively. The first that decodes wins. If nothing resolves it falls back to the first embedded skin, which only MDL has. There is no directory scanning, fuzzy matching or lookup outside the open package. |
| Animations | Partial | Native model previews infer default ranges from consecutive frame-name stems; editable sources retain indexed clips. The mesh editor and CLI add/rename/range/delete clips, copy full poses and generate validated in-between frames across every surface and attachment. Session-only Smooth preview blends positions, normal directions and rigid tags, including the selected clip's end-to-start loop, at an elapsed-time FPS rate. Pause/step restores exact poses and reduced motion disables playback. Game-specific timing/configuration, attached-model assembly and original-engine acceptance remain open. See [Mesh Editing](MODEL_MESH.md). |
| Skeletal animation | Partial | MD5, IQM, MDS, MDM/MDX, MDR, Ghoul 2 and Half-Life skeletons keep their joints, weights, clips and skeletal tags, and are posed into frames for preview, editing and frame-based export (up to 8,192 frames). Bind-pose edits re-bind the moved vertices; topology edits transfer weights from the nearest original vertex; `.mesh.json` version 8 saves the skeleton. Joint editing, weight painting and pose mode are planned. |
| Export | Partial | `exportModelFrameObj` writes one OBJ frame with groups, UVs and normals; editable export adds primary material assignments but no MTL companion. Shared authoring writes MDL, MD2 and MD3 with target limits and quantization checks, MD5 mesh and animation and IQM with joints and weights, and ASE as one static frame. MDL retains indexed skins, native groups/times and header metadata, with exact all-pose seam sharing. Its palette remains external. Schema-4 MDL and schema-3 ordinary sources/recovery retain authoring metadata; native output receives resolved UVs. MD3 retains tags; MDL/MD2 reject tags and MD2/MD3 reject embedded skins. Original-engine MDL acceptance remains open. |
| CLI | Full | `model inspect` prints `modelMeshSummaryText` or JSON for a loose file or a package entry, with `--palette` for MDL's indexed skins. `model export` takes `--frame`, `--material`, `--output`, `--overwrite` and `--dry-run`, and exits 5 (`unavailable`) when geometry is unavailable, which is what animation-only MDX, GLA and `.md5anim` files return. `model formats` lists every format with its engines, games and companions. |
| Hardening | Full | Every read is range-checked. Caps: 8192 frames, 2048 surfaces, 1048576 vertices and triangles per surface, 1024 skins, 8192 tags, 1024 skin-group frames, and roughly 4M vertex slots per model. Non-finite scale or translate is an error; a file past any cap is rejected rather than decoded. |

## Audio Formats

Audio analysis lives in `src/core/asset_tools.{h,cpp}`.

The [Audio Editor](AUDIO_EDITOR.md) adds sample editing for complete PCM/float WAV
and digital DMX inputs. It supports new empty/silent documents, copy/cut/paste,
additive mixing, silence insertion, trim, delete, silence, fades, reverse, gain,
normalization, mono/stereo conversion, polarity/DC correction, undo/redo, and
PCM8/16/24/32 or exact float32 WAV file export with optional integer TPDF dither,
and WAV/DMX game delivery presets shared by GUI staging and `asset audio-export`.
**Stage & Place in Level** and `map place-sound` support original Quake II/III
`target_speaker` entities, validated `sound/` WAV paths, looping/triggered modes,
coordinates and target names. GUI conversion stages mono 22050 Hz PCM16 and an
undoable map entity together; both documents retain independent save/undo.
Stale map or package context blocks handoff. CLI placement requires a compatible
sound already in an archive/folder/draft and writes a separate map. Unmarked
legacy maps need an explicit Quake II choice. Stock Quake/Doom and custom mod
classes retain package delivery and their game-specific entity workflows.
Read-only `asset audio-analyze` and the editor's Analyze command report exact-range
per-channel sample peak, unweighted RMS including DC, DC offset, and full-scale
counts/runs, plus true peak and BS.1770 integrated loudness at 8–384 kHz.
Surround loudness needs explicitly reviewed speaker roles; mono/stereo have
defined defaults. Short, below-gate and unavailable measurements have explicit
status. Sample-only statistics remain available below 8 kHz. See
[analysis conventions and limits](AUDIO_EDITOR.md#analysis).
Whole-document resampling uses anti-alias
filtering, retains float headroom, and maps selection by time; undo restores the
prior samples and rate. Paste/mix require matching rate/channels.
Lossless `.vsaudio` documents preserve float32 samples and editor metadata, with
hash-guarded atomic saves and local recovery. Export and staging do not save the
editable document. `asset audio-project` shares inspection/import/recovery/export
services with the GUI; `asset audio-edit` can preserve native precision between edits.
Processing and take review also work without Qt Multimedia; auditioning and device capture require it.
Native `.vssession` versions 1/2/3/4/5/6/7 support mono/stereo multitrack sources, region edits,
persistent clip groups, batch edits and fade-preserving splits (v6),
gain/pan envelopes and a stereo mix. Version 5 adds stepped tempo and bar-boundary
meter maps, a musical ruler, grid snapping and position navigation through
GUI/CLI; earlier versions retain constant timing. Audio stays sample-anchored. It embeds lossless `.vsaudio` snapshots and
uses digest-guarded atomic saves. Initial limits: 64 tracks, 128 sources, 4,096
clips and 256 MiB aggregate source samples, with the existing per-source limits.
The CLI and GUI share block rendering and a frame transport. Optional Qt Multimedia
streams stereo float audition to an explicitly selected compatible output, with
pause/seek/loop and reported buffering/dropouts. Sources remain in memory;
low-latency recording/monitoring is not established. Device-free CLI transport
diagnostics use the same mixer. PCM8/16/24/32 or float32 WAV export streams up to
the classic RIFF size boundary, with optional integer TPDF dither. Session export
does not synthesize a cue/loop timeline. The waveform handoff supplies game
delivery and marker authoring for bounded ranges. Synchronized overdubbing, MIDI,
plugins, long-media streaming and surround/sidechain routing remain open; see
[Multitrack Sessions](AUDIO_EDITOR.md#multitrack-sessions).
Version 2 adds up to 32 stereo buses within the 64-strip limit, eight pre/post
sends per strip, polarity/swap and validated solo paths. Version 1 imports as
direct-master audio strips. Cycles and missing destinations are refused, even
for disabled edges. GUI and CLI routing share the same renderer and persistence.
Version 3 adds up to eight ordered effects per track, bus and master: gain,
filters/EQ, linked compressor/gate/sample limiter, fractional stereo delay and
soft saturation, stereo reverb, chorus, flanger, tremolo and phaser. Saves/recovery retain bypass, parameters and a 0…60-second
tail. Versions 1/2 load with empty chains. Prepared state is limited to 128 MiB;
playback/export share processing and fresh-state seek/loop/range behavior.
Nine factory presets and portable version-1 `.vsfx` recipes share GUI/CLI
validation, fresh IDs, compatible-rate checks and atomic output guards. File
loading/saving is asynchronous in the staged effects inspector. Preset libraries
and generic asset-browser opening remain gaps. True-peak limiting, oversampled
saturation and external plugin hosting remain open. A native lookahead sample-peak
limiter reports fixed latency; main outputs, sends, buses, master and strip taps
are automatically aligned in playback/export. Lookahead is a staged structural
parameter, while ceiling/attack/release are automatable. Compensation and effect
state share the 128 MiB bound. Version 4 adds numeric
effect lanes on tracks/buses/master and linear/step/smooth gain/pan/effect
curves, shared by graphical/native point controls, CLI, playback, export and
recovery. Live write/touch/latch recording remains open. See
[Session Effects](AUDIO_EDITOR.md#session-effects).
Initial `.vstake` capture selects an explicit device, rate and 1–8 stored channels
from 1–32 device channels. A sixteen-block queue feeds checksummed disk records;
reported input/dropout/storage faults stop capture. GUI review and `asset audio-take`
share prefix validation, exact ranges and channel selection; session import uses
its existing memory limits. Take journals cap at 64 GiB/33,177,600,000 frames and
remain separate from document checkpoints. Monitoring, synchronized overdubbing,
punch/loop/comping and native-device/power-loss acceptance are not established.
macOS recording additionally requires Qt 6.5+ permission support and a declared,
properly deployed microphone-enabled bundle; incomplete packaging fails clearly.

Version-1 `.vsrecord` folders group up to eight mono/stereo `.vstake` arms with
checksummed, new-only plan/final-result JSON records. The internal duplex worker
coordinates permission/playback acknowledgement, sample-aligned punch capture,
preparation of every journal before input, bounded disk/telemetry queues and a progress
watchdog. `asset audio-recording inspect` reports actual per-arm prefixes even
when a final receipt is absent or interrupted. No schema change is made to
sessions or individual takes. Native Record Tracks/Review controls support
explicit devices, punch ranges, monitoring and one-step grouped import. The
Meters tab adds mapped dry-input and pre-clamp output sample peak/RMS, held
maximum/headroom and over-range counts with callback-owned reset. Readings
are transient and do not alter capture or the durable receipt. Finite loop
recording repeats the punch range with continuous DSP and one device clock;
version-2 plans persist the pass count and version-2 imports select pass-local
ranges. Journals concatenate passes and retain partial final passes. The
CLI shares hash/range/channel/target checks and guarded session writes. Physical
device, power-loss, open-ended loop and dedicated comp-lane acceptance remain
open. Review can assemble repeated-pass sections with validated after-cut linear
crossfades, one-step undo and CLI v3 parity; originals stay in their journals.
Review also auditions current sections or the whole comp with optional backing,
pause/seek/repeat and acknowledged playback shutdown. CLI preview exports the
same span to guarded float32 WAV without a device. Alternate lanes remain open; see
[Duplex recording integration](AUDIO_DUPLEX.md).

Audio recovery includes bounded metadata-only startup discovery, a deferred
review notice, File/command-palette access, and separate checkpoint/startup
preferences in Getting Started. Full verification happens in the manager;
restoration opens an unsaved draft and preserves the original and retained copy.
Recovery storage follows an explicit settings profile unless overridden.
Session `.vssession-recovery` envelopes embed the native arrangement and media,
with a separate checksum and original-path/timestamp metadata. They share the
32-copy/512-MiB inventory with waveform recoveries; GUI/CLI restore requires the
reviewed digest and preserves the input and original session. The manager and
`asset audio-session recover` are the entry points for these envelopes. The byte
budget covers committed copies; atomic replacement temporarily also holds the
new file until publication. Undo history and recording streams are not checkpointed.
Limits are 128 MiB input, 16,777,216 interleaved samples, 1–8 channels, and
1–384000 Hz; resampling also bounds padded processing work as documented in the
[Audio Editor guide](AUDIO_EDITOR.md#resampling). Doom DMX writing uses 11025 Hz
mono PCM8, at least 17 playable frames, and 16-byte endpoint padding. Doom
IWAD/PWAD staging requires a valid DS sound name. Quake-family presets emit
legacy mono PCM8/PCM16 WAV. The editor supports 256 named cues and one forward
infinite loop through edits, native version 2 documents, recovery, and supported
WAV delivery. Quake/II legacy loop lengths are supported; cue-only game delivery
omits cues to avoid unintended looping. DMX omits markers, and original Quake III
ignores embedded loop instructions. Unsupported WAV loop modes/text encodings
fail explicitly; see the marker contract in the editor guide. MP3, native FLAC
and single-stream Ogg Vorbis also import through bounded bundled decoders,
independently of Qt Multimedia. Pending additions/replacements appear immediately in Audio;
preview, playback, browser export, and editor reopening share a staging snapshot.
Package writes still require saving the reviewed plan in Packages.

| Format | Header metadata | Waveform | Export to WAV | Limits |
|---|---|---|---|---|
| RIFF/WAVE | Full | Full | Full | Waveform and export cover PCM at 8/16/24/32 bits and IEEE float at 32/64 bits, including `WAVE_FORMAT_EXTENSIBLE`. A-law, mu-law and every other codec tag are reported but produce neither peaks nor an export. Audio browser export writes PCM16. The Audio Editor additionally exports PCM8/24/32 and float32 with optional integer dither; see the editor format contract above. |
| Ogg Vorbis | Full | Editor | Full | Single complete logical stream, 1–8 channels; page CRCs and sample count checked. Header-only browser preview; decoded editor waveform. See the compressed import contract in [Audio Editor](AUDIO_EDITOR.md#formats-and-limits). |
| Ogg Opus | Full | None | None | Channels, original input rate, and the fixed 48 kHz decode rate from `OpusHead`. |
| FLAC (native `fLaC`) | Full | Editor | Full | Bounded native FLAC, 1–8 channels, 4–32-bit PCM; declared frames and nonzero MD5 checked. Float32 may round source precision above 24 bits. Browser preview uses STREAMINFO. |
| FLAC-in-Ogg | Partial | None | None | Recognised and named; the mapped `STREAMINFO` is not parsed. |
| MP3 | Full | Editor | Full | MPEG-1/2/2.5 Layer III standard CBR/VBR, mono/stereo; complete frames, stable format, checked gapless timing. Browser duration remains a header estimate; editor uses decoded frames. Free-format and Layer I/II are unsupported. |
| Doom DMX digital sound (format 3) | Full | Full | Full | 8-bit unsigned mono at the header's rate. The header's count includes 16 pad bytes at each end, which are left out of the waveform, playback, and export, as the DMX library skips them; a sound of 48 samples or fewer is flagged, because DMX does not play it. Recognised by its header; the Audio page lists a WAD's `DS*` lumps and a PK3's bare or `.lmp` entries under `sounds/`. Export widens each sample by a byte shift, so nothing is lost. |
| Doom PC speaker sound (format 0) | Full | None | None | Tone count and duration at 140 tones a second, from lumps named `DP*`: two zero bytes open too many lumps to trust the header alone. There are no samples to draw, play, or export. |
| Doom `MUS`, MIDI | Planned | Planned | Planned | No parser exists. |

**Playback.** The Audio page plays the selected sound through Qt Multimedia
when the build links it (the `audio_playback` Meson feature, on when the module
is found). `assetAudioPlaybackSource` decides what the player is handed: original
WAV bytes (including float precision and codecs the host may support beyond the
editor's import contract), a DMX sound as the PCM16 WAV
it exports to, and Ogg, MP3, and FLAC as they are, for Qt's own decoders; a
sound the player cannot open reports the player's error and is let go, so the
next Play tries it afresh rather than waiting on a dead source; opening a
package, the same one reopened included, lets go of whatever was loaded. A PC
speaker sound is never offered. A build without Qt Multimedia shows the same
transport, disabled, with a tooltip saying why.

Browser preview and audition preparation run on bounded workers. Preview reads
up to 64 MiB; larger entries sample a 64 KiB header without a partial waveform.
Audition allows 128 MiB of input/prepared media. Stop cancels queued preparation;
selection and package changes discard superseded results. Exact entry indexes
preserve repeated WAD names in preview, audition, Edit Sound and WAV export.
The shared editor/browser transport handles finite loading/buffering timeouts,
device errors, stale signals and retry. Compressed browser duration can change
when the backend reports its measured duration; it remains a millisecond clock.

MP3, FLAC and Vorbis decode through [dr_libs and Xiph](DEPENDENCIES.md#dr_libs-and-xiph-vorbis)
for editing and PCM16 browser export, with cancellation, 128 MiB input,
16,777,216 output samples and a 16 MiB decoder allocation budget. Compressed tags,
artwork and embedded marker conventions are omitted with a warning; native saves
retain that warning and decoded float samples. Browser export remains separate
from source files and uses the shared atomic writer. Opus, Ogg FLAC, chained Ogg,
MUS/MIDI, and compressed encoding remain unsupported.

## Map Source Formats

Parsing, editing and save-back live in `src/core/level_map.{h,cpp}`; geometry
reconstruction in `src/core/map_geometry.{h,cpp}`; headless drawing in
`src/core/map_render.{h,cpp}`.

| Format or dialect | Parse | Save-back | Limits |
|---|---|---|---|
| Doom binary map lumps | Full | Full | `THINGS`, `LINEDEFS`, `SIDEDEFS`, `VERTEXES` and `SECTORS` are rewritten in place inside a copy of the source WAD; every other lump is carried through byte for byte. |
| Hexen binary map lumps | Full | Full | The 16-byte linedef and 20-byte thing strides are read and written, including the thing id, action special and five arguments. Detected by the presence of a `BEHAVIOR` lump. |
| UDMF (`TEXTMAP`) | Common-field preview, lossless properties/transforms and scene organization | Edited spans only; unknown fields/blocks and other WAD records preserved | Shared GUI/CLI authoring supports fractional coordinates, scalar properties, move/rotate/mirror/snap/resize, exact undo and recovery. Geometry and raw properties invalidate nodes; ordinary-thing transforms retain them; ZNODES structural validation feeds compiler and launch checks. Native topology tools and advanced namespace rendering remain open. |
| Quake / Quake II `.map`, classic faces | Full | Partial | Three-point planes with shift/rotate/scale texture parameters. |
| Valve 220 faces | Full | Partial | Explicit `[ x y z offset ]` texture axes are parsed and kept. |
| Quake III `brushDef` | Full | Partial | Three-point planes with the 2x3 texture matrix. |
| idTech4 / Radiant `brushDef3` | Full | Partial | Plane-plus-distance faces with the 2x3 texture matrix; a basis is built from the plane so the brush still solves. |
| Quake III `patchDef2` | Full | Authoring | Plane/cylinder/cone creation; point/UV editing, subdivision, facing, boundary stitching, undo and save/recovery. Stitching refines unequal grids exactly and offers UV/tangent matching. Changed blocks retain comments and header metadata. Planar caps close loops/open arches with one undo step and source ownership preserved. Non-planar covers and propagation across multiple seams remain unavailable. |
| Quake III `patchDef3` | Full | Authoring of existing grids | Same control-grid editing, with explicit per-axis subdivision counts retained and used in previews. Presets create `patchDef2`. |

"Partial" save-back for `.map` files means this: the writer is line-preserving,
so everything not edited stays byte-identical, including indentation, line
endings, comments and compact lines. An edited value is replaced inside its
quotes; a removed key takes only its own pair off its line; moved, turned or
flipped brushes rewrite their face points or planes (and, when turned or
flipped, their Valve 220 texture axes and offsets); moved patches rewrite their
control rows; a replaced texture rewrites only its name. A deleted entity,
brush or patch drops the lines it owns, heading comment included. Added
entities are appended after the last one, and added, copied or pasted brushes
and patches go inside their entity, ahead of its closing brace, written in the
format of the brush they came from. What is refused rather than risk the file:
deleting or copying an object whose brace shares a line with other text,
replacing a texture on a line that holds more than one face, and texture names
the tokenizer would split. A key whose value is written on the next line keeps
its old value on save.

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
| 3D viewport | Preview | The Levels **3D** preview draws brushes, Quake III patches, and Doom walls with the Models surface's `QPainter` renderer: orthographic, flat shaded or wireframe, orbit and zoom. It shares the selection, lit and hatched, and a click selects the brush, patch, or linedef under the pointer; editing stays in the 2D views. Brush faces carry outward normals so back faces cull and the painter's sort stays honest; very large maps are cut off at a triangle limit, with a note. No GPU backend is linked. |
| Undo/redo | Full | Every edit below is one undo command, with a saved-depth marker so "modified" versus "saved" is reported rather than guessed; the Levels History tab jumps to any step. |
| Add and delete | Full for Quake-family maps; things only for Doom | Point entities and box brushes (in the map's own face format) are added; entities, brushes and patches are deleted, an entity taking its brushes. Doom and Hexen things are added, duplicated and deleted; vertices, linedefs and sectors are not. `worldspawn` is never deleted. |
| Duplicate and clipboard | Full for Quake-family maps | Copies keep their source text as a template. The clipboard carries plain `.map` text: entity blocks and bare brush blocks, as TrenchBroom and Radiant exchange. |
| Move, rotate and flip | Shared brush texture policy | Numeric/viewport moves, numeric/quick rotation and flips use exact classic/Valve/primitive texture lock, with explicit map-wide Valve conversion when needed. Texture Lock defaults on and persists; CLI defaults match. Numeric and quarter-turn rotations share model-style entity orientation. Binary Doom/Hexen rotation handles shared vertices, integer rounding, collapse checks and node invalidation. Quake mirrors preserve winding; Connected Doom/Hexen/UDMF mirroring retains sidedef ownership. UDMF transforms preserve fractional coordinates and untouched source spans through cancellable workers. Per-object snap and duplicate/paste offsets need a separate texture-policy audit. |
| Brush components | Bounded authoring | Vertex, edge and face selection/movement in a dedicated draft editor and CLI. Convex reconstruction splits bent faces; collapse requires an explicit option. Classic/Valve 220/brushDef/brushDef3 retain source material syntax, flags and comments. Limits: 128 faces, 256 vertices, ±32768 units. Focused geometry/GUI checks and synthetic q3map2-to-PK3 validation passed. Deformed-face texture lock and physical interaction acceptance remain. See [Level Editor](LEVEL_EDITOR.md). |
| Clip | Full for Quake-family brushes | A plane cuts the selected brushes, keeping one side or both as two brushes. Each piece is the brush's own face lines, less those the cut leaves without an edge, plus a face on the plane written in the brush's format (classic, Valve 220 with paraxial axes, `brushDef`, or `brushDef3`), with the texture and Quake II/III flags of its most used face. Brushes the plane misses stay as they are; brushes written on shared lines are refused. |
| Doom linedefs | Split and flip | A split puts a vertex, rounded to whole units, at the linedef's middle and gives the second half its own copies of the sides, with texture offsets carried on so each half shows what it showed before (a back side another line shares is copied rather than changed); a flip swaps the ends, and the sides of a two-sided line, as Doom Builder's Flip Linedefs does. New records are appended, so every index the lumps use stays put. |
| Doom fields | Full for Doom and Hexen maps | The Levels Inspector edits a selected thing (type, angle, position, and its skill, ambush, and multiplayer flags as named boxes), sector (heights, flats, light, special, tag), linedef (special, tag or Hexen arguments, flags as named boxes, and both sides' sector, textures, and offsets), or vertex (position); `map edit --select sector:N`, `linedef:N` (with `front.` and `back.` side fields), `sidedef:N`, or `thing:N` does the same from the CLI. A side packed sidedefs share is copied before a linedef edit changes it. A thing's fields take whole numbers in the range the WAD holds (type 1 to 65535, positions and angle as 16-bit values), a Hexen line's `arg0` is also its tag, and a key a thing has not got is refused rather than kept in memory and lost on save. The same holds however a thing is placed: an `origin` edit, a move, or a turn or flip, which lands things on whole units; a Doom-format thing has no height. |
| Doom geometry | Draw, delete, and merge | Draw Sector (D, and Add Sector… for typed corners, and `map draw-sector`) closes a shape into a sector: corners join vertices or split the linedefs they land on, edges along linedefs share them, and new lines are two-sided inside a sector (which the new one copies) or walls in the void; lines the shape surrounds turn to face it. Crossing a linedef or itself, or covering more than one area (a sector on one edge's inside, the void on another's), is refused; corners join vertices exactly, off the whole-unit grid too, and vertices no linedef uses (the ones node builders add) are passed over. Delete takes vertices (one between two linedefs joins them), linedefs, and sectors (lines left with one side become walls); Merge Vertices stitches lines left over each other into two-sided lines; Join Sectors and Merge Sectors (`map join-sectors`, `map merge-sectors`) make sectors one, merging also taking away the lines between them unless they carry a special or tag. A click inside a room selects its sector. Raise and lower (Page Up and Page Down for floors, with Ctrl for ceilings, by 8 or with Shift by 1, on the map view; Brighten and Darken by 16; `map shift-sectors`) and Gradient Floors, Ceilings, and Brightness over three or more sectors (`map gradient-sectors`) keep heights within 16 bits and light within 0 to 255, and, moving no line, leave the nodes current. Make Door (Levels and `map make-door`) makes the selected sectors doors as Doom Builder does: the ceiling comes down to the floor, each line to another room faces out and opens the door when used (special 1; on Hexen maps Door_Raise, 12, with speed 16 and delay 150, repeatable on use) with the door texture above it, and the door's own walls become lower-unpegged tracks; a line between two of the new doors is left alone. These edits drop records nothing uses and renumber the rest, as one undo step each that keeps only what changed, so undo history stays small on large maps. UDMF geometry fields can be edited in the property editor; these native topology tools remain unavailable for UDMF. |
| Carve | Full for Quake-family brushes | CSG subtraction: the selected brushes are carved out of every brush they overlap, which gives way to its parts outside them, split along their faces; the faces the carve makes take the carving faces' textures, and a brush wholly inside goes, with its brush entity when that was its last brush. Brushes that only meet a carver across an edge stay whole, and Levels leaves hidden brushes alone. The carving brushes stay. |
| Hollow | Full for Quake-family brushes | Each selected brush becomes one wall per face, the face pulled in by the thickness and turned to face the inside, textured like the face it grew from; walls meet at the edges as Radiant's Make Hollow leaves them. A brush too thin for two walls is refused. |
| Resize | Shared brush texture policy | Bounds resize maps brush planes, patch controls, origins and things. Source texture parameters stay fixed by default. Texture Scale Lock (GUI) or `--texture-lock on` stretches UVs with the geometry in all four brush dialects, refusing unrepresentable classic mappings unless map-wide Valve conversion is enabled. Numeric controls and viewport handles share the preference and atomic undo service. |
| Snap to grid | Full | Each selected entity, brush, patch, thing or vertex moves by its own amount. |
| Face alignment | Full for classic and Valve 220 faces | A click on a face in the Levels 3D preview opens it in the Inspector, which lists a selected brush's faces by the way they face, each with its texture, shift, rotation, and scale editable in place, and `map edit --select brush:N --set face2.shiftx=16` does the same. Only the edited face's line is rewritten, and a face whose line another brush's face shares, as in a compact file, refuses the edit; a Valve 220 face shifts through its axis offsets and turns its axes about the face. A brushDef face takes a new texture, but its texture matrix is shown, not edited. A face whose numbers are not all after its name on its line (too few, or carried to the next line) refuses a number edit rather than dropping it on save; comments by the numbers are stepped over. Numbers are written to six places, so a scale too small to survive that, or any value past a million, is refused. A brush that writes two faces on one line keeps them: it refuses moves, turns, flips, resizes, and copies, as it already refused clip, hollow, and carve, because save-back would write one face over the other. |
| Apply texture | Full for Quake-family maps | One texture on every face of the selected brushes (a brush entity's included) and on the selected patches, each name rewritten in place. Doom walls and flats take Replace Texture instead. |
| Replace texture | Full | Brush faces, patches, and Doom wall textures and flats, across the map or the selection. |
| Target links | Full | `target`, `killtarget`, `pathtarget`, `combattarget` and `deathtarget` to `targetname`, drawn in the viewport and in `map render --links`. On Doom-format maps the links are tags: a line with a special and a tag points at each sector carrying the tag (`levelMapTagLinks`), drawn as Doom Builder's association arrows, and Select Targets and Select Sources follow them; Hexen lines are not read as tag links yet. Connect Entities (Levels and `map connect`) makes the selected entities target the one picked last. That entity keeps its `targetname`; without one it takes the name a source already targets, so that link keeps firing (Radiant's "prioritize existing target key"), or else `t<N>`. Select Targets and Select Sources follow the links. |

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
| Shell surface | Full | An **Inspector** tab on the Levels page shows the selected entity's class summary, its keys with per-key help, and its spawnflags as `[x]`/`[ ]` bit rows — a text marker, not a colour. A definition path row with Browse and Load sits above it, and validation issues are folded into the Levels **Health** tab with selectors that navigate to the entity. |
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

Package inspector previews use immutable planned entries and exact occurrence
indexes. Text/audio/model metadata runs on a coalescing worker with streamed
sampling and Cancel/Retry; texture decoding retains its palette-aware worker.
Full samples verify final size/checksum results, while truncated samples leave
the unread tail unverified. Temporary copy preparation also streams verified
planned entries on a cancellable worker. Native accessibility, other studio
preview handoffs, native drag gestures and broader shutdown cleanup remain
acceptance work. Temporary Package Copies now exposes per-window
logical reservations and configurable byte/file/entry/batch limits; the CLI
`package copy-limits` shares that policy. Existing copies survive lower limits.
Managed copy sessions now expose actual logical usage, native live-owner
protection and explicit fingerprint-reviewed orphan discard through Review
Retained Copies and `package copy-sessions` / `package copy-discard`. Normal
shutdown drains their dedicated cleanup pool; old unregistered folders remain
outside the managed review. Shared Storage Limits and `package copy-store-limits`
now cap initial byte/file/entry/batch reservations across cooperating processes
using the same physical store. Checksummed pending/crash/failed-cleanup charges
remain until verified release; legacy or invalid records block new admission.
Actual usage is reviewed separately; later consumer growth is not enforced.
Package-derived maps use Save As with a durable directory suggestion. Code
Save As and map saves refuse this window's temporary-copy storage, including
directory aliases, and preserve edits after cancellation or refusal. Code Save
As outside that storage creates an independent editable file. Saved maps are
independent files; adding them or compiled output to the package remains explicit.

| Workflow | Status |
|---|---|
| Application shell | Q_OBJECT/moc-backed `QMainWindow` with a mode rail driving a `QStackedWidget` across Workspace, Levels, Models, Textures, Audio, Packages, Code, Materials (the Shaders mode), Build and Settings; menu bar, toolbar, status chips, drag-and-drop file opening, and confirmation prompts for destructive actions. |
| Command palette | Interactive type-to-filter dialog over the `QAction` registry plus the documented palette entries, with shortcut and enablement state. |
| Runtime localization | Active. `.ts` catalogs for 20 targets plus pseudo-localization are compiled to `.qm` by `lrelease` at build time and installed with `QTranslator` at run time, with locale, base-language and source-language fallback, layout direction applied per locale, and an overridable catalog directory. |
| Localization tooling | Shared target registry, pseudo-localization, Arabic/Urdu RTL smoke, `QLocale` formatting and pluralization samples, expansion stress and layout-budget checks, stale/untranslated reporting, and dry-run `lupdate` extraction validation. Fully translated catalogs are still seeds, not finished translations. |
| Syntax highlighting | `QSyntaxHighlighter` with data-driven rules for plain text, config, idTech3 shader scripts, QuakeC, `.map` source, entity definitions, INI-style key-value files and JSON, themed from the active studio theme including high-contrast. |
| Package manager | Folder/PAK/WAD/ZIP/PK3 browsing, tree and list views, text/image/model/audio/script previews, selected and whole-package extraction with dry-run reporting, staged add/replace/rename/delete with conflict reporting (files and folders dropped on an open package's entry list or folder tree are staged into the folder under the pointer, folders keeping their layout and a Doom WAD taking 8-character lump names, with one conflict question per drop; entries and folders dragged out of the entry list prepare exact planned copies on a cancellable worker, preserving empty folders and using a fresh owned batch; limits are 2,000 files, 10,000 entries and 512 MiB, with integrity failure/cancellation preventing any handoff), manifests, entry-by-entry comparison against another archive, and save-as PAK/ZIP/PK3/WAD writers. GUI replacement requires confirmation; CLI source replacement requires `--in-place`. Atomic publication verifies the replacement and an independent original copy, keeps the original present until commit, and retains a `.bak` backup. Recovery journals support bounded GUI/CLI folder discovery and cancellable verification/finishing of an already-committed save. Reviewed checksums reject changed journals; external backups require explicit path selection. Pre-commit replacements and changed outputs remain manual-review cases. Live imported/replaced files retain verified independent temporary copies through undo and background readers; changes to originals do not alter accepted imports. Read-only CLI plans retain no-write source checks. Managed working sessions now reserve bytes and file slots before copying, retain live-reader leases, and expose configurable limits, bounded usage review and explicit crash-orphan discard through shared GUI/CLI services. Reviewed lock recovery checks native owner exclusion, including empty interrupted locks; normal application exit drains the dedicated cleanup queue. Native macOS/Linux and remote-filesystem acceptance remain open. Portable `.vibepackage` drafts preserve payloads and grouped undo/redo. Browsing, exact-row previews, validation and extraction read the planned content; saved drafts use the same CLI read services. Repeated names are labelled; Replace/Rename/Delete address exact occurrences and draft version 2 preserves their identity. GUI output-path review and CLI index mappings extract repeated names separately. GUI/CLI new documents need no source; folder creation, rename and deletion are atomic and undoable. ZIP/PK3 preserve explicit empty folders; PAK output blocks rather than dropping them. WAD remains flat. Draft dry runs validate content/history without writing. Automatic local checkpoints preserve full history; File > Recover Packages and CLI inventory/restore/discard share metadata bounds, digest checks, editor leases and independent restored drafts. Checkpoint retirement and unreachable-object reclamation run on workers. Recovery has configurable logical-byte/copy limits (8 GiB/32 defaults), bounded usage scans and reviewed incomplete-copy discard in the GUI/CLI; limits preserve existing copies. Saved drafts have separate byte/file save limits and reviewed unused-object maintenance, blocked while participating document/history/worker readers remain. Archive subsets preserve exact occurrences and expand required WAD map/GL groups, namespace boundaries and local texture name tables through a shared GUI/CLI review. New and opened Doom WADs support reviewed map/GL renames and complete map, namespace or local texture-table deletion through the shared GUI/CLI staging service. Source-free WADs retain their reviewed positional order through saving, draft history and subset export. Metadata/script reference rewriting and complete game-asset dependency closure remain pending. |
| Texture and sprite surfaces | Zoomable, pannable image view with checkerboard alpha and nearest-neighbour magnification, palette swatch grid with index readout, crop/resize/palette conversion, and batch conversion from the CLI. The **Automatic** palette, the default, takes the project's palette, else the one the open package ships (`PLAYPAL`, `gfx/palette.lmp`, or `pics/colormap.pcx`), else, image by image, the palette its own format implies (Doom's for a patch or flat, Quake's for a lump), read from the selected game installation's base packages when the installation is of that family (a PWAD's colours are its IWAD's `PLAYPAL`), and generated otherwise, as the Packages preview, `asset inspect`, and `texture decode` without `--palette` do for the open package alone. Browser conversion uses Qt image formats. The layered Texture Editor separately exports PNG/TGA/PCX, Quake miptexture/WAD2, WAL and Doom flat/patch through explicit profiles with mip previews and metadata; WAD2/Doom staging and texture stage CLI drafts share namespace/type preservation and staged palette resolution. |
| Audio surface | Package metadata/waveform previews and optional Qt Multimedia playback. The [Audio Editor](AUDIO_EDITOR.md) opens local or package WAV, MP3, native FLAC, Ogg Vorbis and digital DMX audio, and reopens lossless `.vsaudio` projects. Selection edits, bounded undo/redo, cues/loops, resampling, analysis, recovery, float32 audition, WAV/game delivery and package/level handoff share documented GUI/CLI services. Source files stay intact; native saves and delivery remain separate. A resource WAD with no maps opens as a package so its Doom sounds reach this page. |
| Model surface | A software `QPainter` model viewport drawing every decoded model format in wireframe, flat-shaded or textured mode, with a resolved skin, inferred animation playback and single-frame OBJ export, beside the existing metadata, skin and material dependency summaries. Skeletal models list their joints and clips on a Skeleton page; animation-only files reach a dedicated no-geometry state instead of a drawing. The Mesh Editor offers Blender, 3ds Max and MilkShape 3D controls profiles with matching layouts (see [Modeller Profiles](MODELLER_PROFILES.md)). |
| Level editor | Map inspection, entity property editing, object moves, undo/redo, non-destructive save-as, interactive viewports, SVG export, health summaries, entity definitions, validation and compile-plan handoff. Brush component and patch authoring share document persistence, recovery and dependency/package services. Broader production acceptance gaps are tracked in [Level Editor](LEVEL_EDITOR.md). |
| Build pipelines | Chained pipelines with stage ordering, per-stage enable/disable and extra arguments, streamed output, diagnostics, leak detection, file hashes and command manifests: `quake-full`, `quake-fast`, `quake-bsp-only`, `quake3-full`, `quake3-bsp-only`, `doom-zdbsp` and `doom-zokumbsp`. |
| Game launch | Launch plans for Doom, Quake, Quake II and Quake III source ports, plus a pass-through custom profile, built from the selected installation profile and reviewable before anything starts, with a run path that starts the configured executable. |
| Compiler registry | Descriptors over the imported submodules, executable discovery through source trees, known build outputs, extra search paths, user and project overrides and `PATH`, plus short version/help probes and argument presets. |
| Editor profiles | Routed presets for the VibeStudio default and GtkRadiant 1.6.0-, NetRadiant Custom-, TrenchBroom- and QuArK-style workflows, with stable command IDs, layout/camera/selection/grid metadata and shortcut conflict smoke coverage. Full fidelity is planned. |
| idTech3 shader graph | Shader script parsing, stage and dependency graph lines, stage previews, raw text detail, stage directive round-tripping, and validation of texture references against mounted packages. |
| Materials workbench | Partial: every material of idTech 1 to 4 (Doom walls, flats, ANIMATED/SWITCHES/ANIMDEFS animations and switches; Quake WAD2/WAD3 textures; Quake II WALs and `.wal_json`; Quake III shaders; Doom 3 materials and tables) in one library with engine lookup and shadowing rules, a live CPU preview per engine, validation of the rules each engine applies, text and node-graph editing over one text, and saving into the package's staging. Shared with the `material` CLI. Tested on generated fixtures only; see [Materials](MATERIALS.md). |
| Code/script IDE | Syntax highlighting, diagnostics boundaries, project-wide find/replace, source tree indexing, symbol search, build task hints and launch profile summaries. A full IDE is planned. |
| CLI | Subcommand router with JSON output, quiet/verbose, watch streaming, machine-readable task state and stable exit codes, covering project, install, package (including `package compare`), asset, map (including `map render`), `bsp inspect`, `entity definitions` and `entity validate`, `model inspect`, `model export`, `model formats`, `model profiles` and `model controls`, `build list|plan|run`, `launch plan|run`, shader, sprite, code, extension, compiler, localization, diagnostics, AI and credits commands. |
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

## Map Document Lifecycle

New empty/starter-room maps support Quake, Quake II, Quake III, binary Doom and
binary Hexen. GUI/CLI saves share atomic writes, backups, SHA-256 source
conflicts, and owned WAD snapshots. Local automatic recovery preserves map
bytes and stale-node state, with fresh undo history after restore. Maps are
limited to 512 MiB; UDMF TEXTMAP is limited to 64 MiB and supports lossless property
authoring, preview, scene organization and recovery. See [Level Editor](LEVEL_EDITOR.md)
for the full editor acceptance gaps.

Doom node readiness covers classic SEGS/SSECTORS/NODES, XNOD/ZNOD and
XGLN/ZGLN/XGL2/ZGL2/XGL3/ZGL3, with bounded decompression, tree/reference
checks and BLOCKMAP/REJECT structure checks. GUI and CLI geometry saves clear
obsolete products; compiler wrappers and Doom WAD launch plans validate the
selected map, including UDMF ZNODES. DeePBSP and separate GL cache formats remain unvalidated
and explicitly warn. See [node readiness](LEVEL_EDITOR.md#doom-node-readiness).


Audio session delivery supports aligned track/bus pre/post WAV stems and an
optional master, common frame ranges, integer TPDF dither, stable naming and
version-1 JSON delivery manifests through GUI/CLI. Output is bounded streaming
RIFF stereo at the session rate. Per-file atomicity, digest/source guards and
partial completion reports apply. RF64, surround stems, editable interchange,
automatic loudness matching and cross-file transactional rollback are unsupported.

## Native Quake III Player Packages

The modeller reviews and publishes non-team lower/upper/head player PK3s with
complete native animation poses/tags, primary `.skin` assignments, configuration,
a converted TGA icon and captured material dependencies. GUI and CLI share
validation, cancellation, input protection and deterministic atomic output.
Exactly three native roles, a single-pose head and an open asset context are
required. Team variants, custom sounds, LOD generation, shader-language/video
certification and gameplay acceptance remain open. See the precise
[target limits](MODEL_ASSEMBLY.md#native-player-packages).
