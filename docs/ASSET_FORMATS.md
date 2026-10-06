# Shared Asset Formats

`core/asset_formats` is the capability catalog shared by filename routing,
project file classification, texture/package import filters, package composition
summaries and the CLI. Content
decoders remain authoritative: a suffix identifies a candidate, not a valid
payload. Package namespaces still resolve extensionless WAD assets and exact
directory occurrences. Longest suffix matching distinguishes `.mesh.json`,
`.model.json` and `.assembly.json` from ordinary code JSON.

```sh
vibestudio --cli asset formats --json
vibestudio --cli asset formats --module textures --json
vibestudio --cli asset route textures/wall.dds --json
```

Each descriptor reports its owning module, suffixes, preview family, read
capability, native export profiles, conversion profiles, availability and limitations. `metadata`
means inspection only; it never implies decoded geometry or sample editing.
Qt image codec availability is checked in the running application. Native
editor documents report `document`; their payloads are not raster/audio previews.
Code extensions continue through the shared text-language classifier. Unknown
assets can still be extracted as original bytes by the package manager.

Recorded `.vstake` files report an Audio document capability. Local Open Audio
routes them to verified take review; selected ranges export to `.vsaudio` or WAV
and enter ordinary session/waveform workflows. They are not game-ready sample
previews or an implicit microphone action. See [Recording](AUDIO_EDITOR.md#recording-and-recorded-takes).

## Image exchange

The shared decoder now supports these additional formats in package previews,
the texture browser/editor, explicit model-skin references and CLI imports:

| Format | Import | Native export | Limits |
|---|---|---|---|
| DDS | 2D base surface: BC1/2/3, BC4/5 UNORM/SNORM, RXGB, supported RGB/luminance masks and DX10 variants | Uncompressed BGRA8 with alpha | Base mip only; cube, array and volume textures refused; no BC6/7 or compressed export. |
| FTX | RGBA8 with the header's alpha flag | RGBA8 with alpha | Exact payload and positive dimensions required. |
| SiN SWL | Indexed image, embedded palette, four mip levels | Convert pixels through another export profile | Index 255 transparency; non-overlapping, ordered in-bounds mip payloads; no SWL writer. |

DDS data is presented as an 8-bit preview: signed channels are remapped to the
visible range, BC4 is grayscale, and BC5 reconstructs the blue normal component
from red/green. Import does not preserve compressed blocks, HDR data or color-space
metadata. Extract the original package bytes when byte-for-byte fidelity is needed.

These use the same 64 MiB input, per-surface and aggregate pixel limits,
cancellation checks and smaller texture-authoring limits as existing imports.
DDS/FTX exports are selectable in the texture editor and in `texture export`,
`texture validate` and `asset convert`, and stage through the existing package service.
No original Quake/Quake II/Quake III compatibility is implied for DDS/FTX;
their level-placement options remain disabled where the target cannot use them.
SWL imports can export PNG, TGA, DDS, FTX or the existing validated indexed game
profiles; native SWL metadata is not round-tripped.

```sh
vibestudio --cli texture export ./wall.vtexture --profile dds --output ./wall.dds
vibestudio --cli texture export ./wall.swl --profile ftx --output ./wall.ftx
vibestudio --cli texture validate ./wall.dds --profile quake2-wal --palette-file ./palette.lmp
```

Existing native image codecs include Doom patches/flats/palettes/colormaps,
Quake LMP and WAD2/WAD3 miptex, WAL, M8/M32, PCX, TGA, SPR and SP2. Qt supplies
PNG/JPEG/BMP/GIF/TIFF/WebP where its plugins are available. TIFF now reaches the
same image classifier and import filter as these other raster formats.
See [the support matrix](SUPPORT_MATRIX.md) for exact variants and limits.

Synthetic `extra-image-smoke` fixtures assert decoded pixels, RGBA export
round trips, block formats, embedded palette/mip behavior, cancellation,
truncation and oversized input rejection. No commercial game assets are used.

## Module handoffs

The project remains the source of installation, palette, compiler and output
choices. Package readers provide shared asset paths and palettes; native editor
documents retain authoring state. Export profiles turn those documents into
engine assets. Existing staging and placement services validate exports before
they enter a package/map, dependency review feeds prepared builds, and verified
build outputs feed publication and launch. A [portable workspace](WORKSPACES.md)
restores the saved references around that workflow without duplicating them.

The catalog deliberately does not bypass a parser, dirty-document decision,
staging transaction, target-engine check or build validation. The GUI's map/WAD
content probes still choose between Levels and Packages. Native model design
and assembly sources can be opened from their editor; the catalog reports their
owner without claiming every native document has a generic preview.

## PakFu comparison and remaining work

Reviewed against the local [PakFu support matrix](https://github.com/themuffinator/PakFu/blob/13111e4c07513548a29fd7eb74fc9d004c44aa59/docs/SUPPORT_MATRIX.md)
on 2026-10-05. This is a capability comparison, not a claim of complete parity.

| Area | VibeStudio today | Remaining PakFu breadth |
|---|---|---|
| Packages | Folder, PAK, ZIP/PK3/PK4/PKZ, WAD2/WAD3 and Doom WAD readers; supported writers/drafts | SiN SPAK, BFG `.resources`, Quake Live encrypted PK3 and broader nested-container workflows. |
| Images | Existing idTech codecs plus DDS/FTX/SWL import and DDS/FTX export | SWL native writing and wider conversion combinations; fidelity and target-engine fixtures before claiming parity. |
| Models | OBJ, MDL, MD2, MD3 geometry and authoring; MDC/MDR/IQM metadata | FM, MDC/MDR/IQM geometry, MD4, SKB/SKD/MDM/GLM, MD5, TAN, ASE, LWO/BLWO. |
| Audio | PCM/float WAV, digital DMX, native MP3/FLAC/Vorbis decoding and WAV/DMX delivery | IDWAV; Opus remains metadata/backend playback rather than native sample import. |
| Maps/cinematics | Source-map authoring and BSP inspection | `.proc` geometry, best-effort BSP decompilation, CIN/ROQ decoding/export and video workflows. |
| Other inspectors | Existing code/shader/script tooling | Demo/navigation/font and additional animation/binary inspectors. |

Prioritize format additions that complete authoring → placement → dependency
review → compile/package workflows. Keep inspectors read-only until an actual
encoder and round-trip fixtures exist. General model import would need its own
dependency/license decision, material and animation loss reporting, and native
editable-mesh validation; it is not enabled by listing an extension.
