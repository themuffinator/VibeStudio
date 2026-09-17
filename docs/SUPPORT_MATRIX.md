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
- **Planned** — not implemented.

## Engine Families

| Family | Status | What works today |
|---|---|---|
| idTech1 / Doom | Partial | WAD browsing and writing; Doom and Hexen map lumps parse and save back; patches, flats, PLAYPAL and COLORMAP decode to pixels; sector outlines, SVG rendering and node-builder pipelines run. UDMF is detected only. |
| idTech2 / Quake, Quake II | Partial | PAK/WAD2/WAD3/folder browsing; `.map` parsing with classic and Valve 220 faces; miptex, `.lmp`, `.wal`, PCX and `.spr` decode; BSP29/BSP2/IBSP38 inspection; ericw-tools pipelines run. Model support is metadata only. |
| idTech3 / Quake III | Partial | PK3/ZIP browsing and writing with DEFLATE; `.map` parsing with `brushDef`, `brushDef3`, `patchDef2` and `patchDef3`; shader script round-tripping; IBSP46/47 and RBSP inspection; q3map2 pipelines run. Model support is metadata only. |

## Image Formats

All decoders live in `src/core/idtech_image.{h,cpp}` and produce a `QImage`.
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
| PCX | Full | Embedded 256-entry tail palette when present, otherwise the resolved package palette | 8-bit single-plane and 8-bit 3/4-plane only. 1-, 2- and 4-bit PCX headers parse but are rejected by the decoder. |
| Targa | Full | Embedded colour map for colour-mapped images | Types 1, 2, 3, 9, 10 and 11 at 8/15/16/24/32 bits. An all-zero alpha channel is treated as opaque and warned about. |
| Quake `.spr` | Full | Resolved Quake palette | Version 1 only. Single frames and interval groups both decode, with per-frame origins and durations. |
| PNG, JPEG, GIF, BMP, TIFF, WebP | Full | n/a | Handled by Qt's own image plugins, so availability follows the Qt build. |
| Quake II `.m8` / `.m32`, Quake II `.sp2` | Detect only | n/a | Routed to the image preview by extension, but there is no decoder; they fall through to the generic binary summary. |
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
| Doom IWAD / PWAD | Full | Partial | None | 16-byte directory records. The writer preserves the source magic, falling back to `PWAD`. **Single-map WADs only:** the staging model keys entries by name, which a multi-map WAD breaks because every map repeats `THINGS`, `LINEDEFS` and the rest. Writing one is refused with a blocked message rather than producing a WAD with its map lumps interleaved, so any IWAD and most multi-map PWADs cannot currently be saved back. Reading them is unaffected. |
| Quake WAD2 / Half-Life WAD3 | Full | Full | None for written lumps | 32-byte directory records with the type byte preserved. Compressed WAD2/WAD3 lumps are listed with a note but are not decoded on read, and the writer emits uncompressed lumps only. |
| ZIP | Full | Full | Read: stored and DEFLATE. Write: stored or DEFLATE | ZIP64 central directories and ZIP64 extra fields are handled on both sides. CRC-32 is verified on read. Encrypted entries and any method other than 0 or 8 are listed but not decoded. |
| Quake III PK3 | Full | Full | Same as ZIP | Identical container handling, plus layer shadowing when several PK3s are mounted in one session. |
| Nested archives | Partial | n/a | n/a | A package inside a package is flagged and can be mounted as an extra layer. Nested archives are not written back into their host. |

Extraction refuses absolute paths, drive-qualified paths, `..` traversal,
reserved Windows device names, control characters and trailing dots or spaces,
and it resolves symbolic links and NTFS junctions along the output path before
writing, so a crafted entry cannot escape the chosen root.

The written DEFLATE stream uses the in-tree fixed-Huffman encoder. Entries that
do not get smaller are stored verbatim, so an already-compressed asset is never
inflated by re-packing, but PK3s VibeStudio writes are larger than the same
content packed by zlib. See [`docs/STACK.md`](STACK.md) for why that trade was
taken.

## Model Formats

Model support is metadata only. Nothing is tessellated, skinned or drawn; the
studio reports header fields, dependencies and animation names so packaging and
dependency work can proceed.

| Format | Status | What is read |
|---|---|---|
| Quake MDL (`IDPO`) | Metadata only | Skin count, skin width and height, vertex count, triangle count, frame count. |
| Quake II MD2 (`IDP2`) | Metadata only | Skin size, skin count, vertex/triangle/frame counts, up to 8 skin paths and up to 12 frame names. |
| Quake III MD3 (`IDP3`) | Metadata only | Frame, tag, surface and shader counts, plus a bounded surface walk (64 surfaces) that sums vertices and triangles. A walk that runs past the sampled bytes is reported as partial rather than silently truncated. |
| MDC, MDR, IQM | Detect only | Recognised by extension and routed to the model surface, but no header is parsed. |
| Everything else (OBJ, ASE, MD5, glTF, …) | Planned | Assimp remains an optional future path and is not linked. |

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
| Package manager | Folder/PAK/WAD/ZIP/PK3 browsing, tree and list views, text/image/model/audio/script previews, selected and whole-package extraction with dry-run reporting, staged add/replace/rename/delete with conflict reporting, manifests, and save-as PAK/ZIP/PK3/WAD writers. |
| Texture and sprite surfaces | Zoomable, pannable image view with checkerboard alpha and nearest-neighbour magnification, palette swatch grid with index readout, crop/resize/palette conversion, and batch conversion from the CLI. Output is written through Qt image formats; writing back to idTech texture formats is planned. |
| Audio surface | Header metadata, a real min/max envelope waveform painted from decoded PCM, and WAV export. Playback is planned. |
| Model surface | Metadata, skin and material dependencies, animation names and a textual viewport summary. A drawn model viewport is planned. |
| Level editor | Map inspection, entity property editing, object moves, undo/redo, non-destructive save-as, the interactive viewport, SVG export, health summaries, and compile-plan handoff. Brush/patch geometry persistence and Radiant-class editing are planned. |
| Build pipelines | Chained pipelines with stage ordering, per-stage enable/disable and extra arguments, streamed output, diagnostics, leak detection, file hashes and command manifests: `quake-full`, `quake-fast`, `quake-bsp-only`, `quake3-full`, `quake3-bsp-only`, `doom-zdbsp` and `doom-zokumbsp`. |
| Game launch | Launch plans for Doom, Quake, Quake II and Quake III source ports, plus a pass-through custom profile, built from the selected installation profile and reviewable before anything starts, with a run path that starts the configured executable. |
| Compiler registry | Descriptors over the imported submodules, executable discovery through source trees, known build outputs, extra search paths, user and project overrides and `PATH`, plus short version/help probes and argument presets. |
| Editor profiles | Routed presets for the VibeStudio default and GtkRadiant 1.6.0-, NetRadiant Custom-, TrenchBroom- and QuArK-style workflows, with stable command IDs, layout/camera/selection/grid metadata and shortcut conflict smoke coverage. Full fidelity is planned. |
| idTech3 shader graph | Shader script parsing, stage and dependency graph lines, stage previews, raw text detail, stage directive round-tripping, and validation of texture references against mounted packages. |
| Code/script IDE | Syntax highlighting, diagnostics boundaries, project-wide find/replace, source tree indexing, symbol search, build task hints and launch profile summaries. A full IDE is planned. |
| CLI | Subcommand router with JSON output, quiet/verbose, watch streaming, machine-readable task state and stable exit codes, covering project, install, package, asset, map (including `map render`), `bsp inspect`, `build list|plan|run`, `launch plan|run`, shader, sprite, code, extension, compiler, localization, diagnostics, AI and credits commands. |
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
