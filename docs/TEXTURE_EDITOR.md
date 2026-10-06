# Texture Editor

Open **Textures → Texture Editor** to create an image, or **Edit Selected** to
edit the mip level or sprite frame currently displayed in the browser. The
editor works offline and needs no AI connector, game installation, or extra Qt
module. **Open…** imports loose images through the existing idTech decoders.

The editor authors layered `.vtexture` projects and exports their visible
composite as PNG, TGA, PCX, Quake miptexture/WAD2, Quake II WAL, or Doom flat/patch.
The working surface is 8-bit RGBA or indexed color; higher
precision raster imports are converted to 8-bit RGBA when opened or imported
as layers. Project saves preserve this working representation losslessly.
Opening a loose native image retains supported names, WAL animation/surface
metadata, and patch offsets as export settings. Its other mip levels are
regenerated from the edited surface; sprite frame authoring is not implemented.
Browser **Edit Selected** retains this metadata and the source package path while
importing the displayed mip or frame. Native package staging uses the selected
export profile. Layered projects, level/model materials, native packages and
external compilers share the verified handoffs described below. Release scope,
evidence and remaining platform/manual limitations are recorded in
[the release plan](plans/texture-editor-release-candidate.md).

**Textures → Generate** opens the Texture Generator: the configured image
model draws variants from a description (or a picture of your own is used
with no AI), and each becomes a tiling, palette-correct texture in the
selected game's format, with optional source-port companion maps. **Open in
Texture Editor** brings a variant here to continue by hand. See
`docs/AI_AUTOMATION.md` and `texture generate` in `docs/CLI_STRATEGY.md`.

## Authoring

- **New** creates a transparent image using the Width and Height fields.
- **Pencil** replaces pixels with the chosen RGBA color using square stamps.
  **Brush** offers square/round hard stamps and Replace RGBA or Blend over.
  Width is 1–128 texels. Blend over applies the chosen alpha once per covered
  pixel in a gesture, including overlapping stamps and retraced segments; a new
  gesture builds up color again. **Eraser** writes transparent black with the
  chosen stamp shape. Each drag is one undo step; Escape restores the entire
  gesture. Transparent blend and clipped no-ops preserve indexed layers.
- **Line**, **Rectangle**, and **Ellipse** preview their bounds until release.
  With the pixel cursor, Space sets the first endpoint; move with arrows and
  press Space again to commit. Escape cancels the preview. Rectangle/ellipse
  outlines lie inside their inclusive endpoint rectangle, with Brush width as
  border thickness; **Filled shapes** paints the interior. Line uses the selected
  brush stamp. Shapes share Paint mode, clipping, wrapping, and a single undo step.
- **Fill** replaces a four-connected region. Fill tolerance (0–255, default 0)
  is the maximum difference in each original red, green, blue, and alpha channel
  from the starting pixel. It does not drift along a gradient or compare newly
  painted pixels. Hidden RGB at zero alpha also participates in matching. Fill
  always replaces RGBA; tolerance 0 retains exact matching.
- **Eyedropper** reads a pixel into the color field. Colors accept a Qt color
  name, `#RRGGBB`, or `#AARRGGBB`; the alpha component comes first.
- **Rectangle Selection** clips painting, fill, paste, clear, palette remap,
  and flips to the active layer's selected pixels. The Selection section offers
  select all, deselect, copy, cut, paste at the pixel cursor, and move by a
  specified pixel offset. Moving outside the canvas is refused to prevent
  accidental clipping. Copy without a selection copies the full active layer.
- **Resize Selected Pixels** and **Rotate Selected Pixels Clockwise** in Selection
  transform only the active layer and update the selection bounds. Choose an
  edge, corner, or center anchor. Vacated pixels become transparent and the
  transformed pixels replace the destination, including its alpha. The result
  must fit inside the canvas; disabled actions explain a missing selection,
  locked/hidden layer, or out-of-bounds result. Width/height affect resizing only.
- **Set Canvas Size** in Transform pads or crops all layers without resampling.
  Width/height and the content anchor control placement; the offset is shown
  before applying. Exposed pixels are transparent and cropped pixels remain
  available through Undo. **Crop Canvas** uses an explicit rectangle completely
  inside the canvas. **Resample Canvas** scales all layers; **Rotate Canvas
  Clockwise** rotates all layers by 90°. These operations clear the selection
  and include hidden/locked layers to keep every layer aligned. Flips affect
  the active layer's selected pixels, or its full surface without a selection.
- **Tile Preview** draws a 3 × 3 repeat to expose edge seams. **Wrap painting**
  wraps brush stamps, shapes, and fill connections across canvas edges and enables
  editing repeated tiles. Strokes retain their unwrapped path across a seam, and
  the selection clips pixels after wrapping. Without wrapping, only the center
  tile accepts edits. The preview does not change saved dimensions.
- **Offset and Wrap** moves active-layer pixels cyclically within the selection
  or full canvas. Negative offsets work; palette indices and hidden RGBA remain
  exact. **Offset Half Size** moves edge seams toward the center for repair
  (odd dimensions round down). Offset has one undo step.
- The Palette section selects a game palette and resolves it from the open package
  or selected installation, with a labelled generated fallback. Swatches
  select a paint color. Choosing or refreshing a source uses the editor's
  cancellable worker. Cancellation or a changed source revision retains the
  previous palette, choice and saved state. **Remap to Palette** quantizes pixels
  and produces indexed PNG output, optionally with dithering. Painting after
  remapping can introduce other colors; remap again before saving when an
  indexed result is needed. Palette provenance identifies generated stand-ins.
  Quantization reduces alpha to the selected palette's transparency rules;
  palettes without a transparent entry produce opaque output.

The inspector's section selector exposes Paint, Transform, Palette, Package,
Layers, Selection, Recovery, and Export without horizontally clipped tabs at larger text sizes.
The Layers section lists layers top to bottom. Add a transparent layer, import an
image matching the canvas dimensions, duplicate, rename, reorder, hide, lock,
and change opacity or blending. Pixel edits affect the selected layer; hidden
or locked layers reject them. Normal, Multiply, Screen, and Add blend modes
are available. Merge Down requires two visible unlocked Normal layers because
other blend modes can depend on layers beneath them. Flatten preserves the
visible composite and requires all layers unlocked. Both can be undone.

Dimensions are limited to 4096 per side and 4,194,304 pixels per canvas, with
at most 32 layers, 33,554,432 total layer pixels, and 128 MiB of layer pixel
storage. Undo retains up to 64
states with 128 MiB of additional pixel storage; shared layer buffers are
counted once. The budget is enforced after edits, Undo, and Redo, including
canvas size changes. Older history may be dropped to retain the immediate
inverse action within the limit. Undo/redo tracks the last successful project save, and labels
identify the operation. Cancelled strokes and failed recipes leave layers and
history intact.
Imports check dimensions before allocating native or Qt image pixels. Package
imports check both stored and decoded entry sizes before reading or inflating
them and require the complete payload. Each file
is limited to 64 MiB; mip levels and sprite frames share a 33,554,432-pixel decode
budget, and a sprite can store at most 4,096 frames across all groups. External
SP2 frame reads share a 64 MiB work budget, charging the larger of stored and
uncompressed entry size for each read, including repeated references. The texture
editor applies its stricter canvas limit to each imported surface. Indexed layers
must have a palette entry for every pixel index. Invalid imports and malformed
projects retain the current document, history, and source metadata.
Selection bounds alone do not dirty the project because they are session state.
A transform that changes only the selection still has an undo step, with the
same saved-content revision in both directions.

Nearest resampling selects the source pixel containing each destination pixel's
center. Smooth resampling is optional and can introduce new colors. Exact
rotation, nearest resampling, flips, moves, and canvas placement preserve
indexed values (including duplicate palette colors) and hidden RGB under zero
alpha. When a transform exposes transparency, indexed layers reuse a transparent
entry or append one if a palette slot is free. A full opaque palette or RGB32
layer promotes to RGBA when necessary. Center anchoring rounds half-pixel offsets
toward the top left, including when shrinking.

Painting uses crisp pixel masks without antialiasing or pressure sensitivity.
A stamp of width `w` begins at `cursor − floor((w−1)/2)` on each axis, so even
widths extend one extra pixel to the right/bottom. A round stamp includes pixel
centers within its diameter; ellipses use the same pixel-center convention.
These deterministic rules are shared by the GUI and CLI. Brush and view settings
are session controls and are not stored in the native project.

File loading, layer operations, transforms, palette conversion, encoding, and saving run on
a worker using a document snapshot. The UI reports progress and blocks edits
while busy. Fill and stroke loops, palette scanlines, and multi-layer transforms
check cancellation between bounded chunks and report measured progress. Recipes
also check between operations and commit only after the complete recipe
succeeds. Exact transforms check scanlines or small row blocks. Smooth resampling
and Qt image decoding still wait for their bounded library call before
discarding a cancelled result. PNG exports check cancellation at encoded output
blocks and reject output exceeding 64 MiB before the buffer can grow past that
limit. Native project layers use the same bounded, cancellable PNG sink.
Project file reads and checksums check cancellation every 64 KiB.
Native decoders check between rows, packet blocks,
and external frame reads; one bounded archive read finishes before cancellation.
Project saving offers Cancel while inspecting the destination and encoding
layers. Cancelling retains the dirty document and recovery checkpoint and does
not resume a pending close/open action; no project or backup is written. Publication
then runs as a separate non-cancellable phase: it rechecks the original target,
preserves the previous version when required, and commits the file atomically.
The saved state and continuation are acknowledged only after publication succeeds.
Large layer edits and undo/redo prepare their composite on the worker. Interactive
strokes recomposite the affected regions, including both sides of wrapped seams;
the preview is retained when a stroke is committed. A cancelled stroke restores
the original composite. These caches do not change exported pixels or undo data.

The Textures browser decodes the selected image and thumbnails on separate
background workers. Each worker holds one active request and at most one pending
replacement, using an immutable staged-package snapshot. Selecting another
image, changing palette sources, or staging an edit retires old results. The
status strip names the loading phase and shows thumbnail progress; **Cancel
Preview** stops publication and queued thumbnails, and **Reload Previews** retries
the current source. Native decoding checks cancellation between rows/blocks;
opaque Qt codecs finish their bounded call first. **Edit Selected** becomes available when decoding
finishes and reuses that result, including the displayed mip/frame and native
export metadata. The thumbnail worker releases full images after scaling.
Texture and level browsers request visible rows, up to 128 per view. A shared
least-recently-used cache retains at most 512 source thumbnails and 64 MiB of
source pixels, with each thumbnail capped at 256 physical pixels per side.
Eviction also removes the view's reference to those pixels. Changing source
revisions clears old icons immediately. Dimension/format searches examine
offscreen rows in the background but retain only their small metadata; scrolling
or filtering requests the newly visible thumbnails. Hidden tabs do not scan
the whole package.

The browser's **Export PNG** captures the displayed frame before opening the
destination picker. Its progress dialog encodes that snapshot on a worker,
checks cancellation while inspecting the destination and writing encoded blocks,
then publishes through the same guarded output service as editor/CLI exports.
Cancellation is disabled during publication, so a committed file is always
reported as saved. Failures remain visible with the destination and a diagnostic.
Browser export accepts the decoder's 16,777,216-pixel limit, independently of
the smaller authoring canvas limit. It does not change an open editor document.

Existing projects use `QSaveFile` atomic replacement; new projects publish a
same-directory temporary file without overwriting a concurrently created target.
Save errors leave edits unsaved.

## Project persistence

**Save Project** (Ctrl+S) writes `.vtexture`; **Save Project As…** writes a copy.
**Open…** accepts projects and loose images. Projects retain layer IDs, order,
names, visibility, pixel locks, opacity, blend modes, pixel data, active layer,
and the package texture path. Indexed palette entries and transparency are
preserved. The selected game palette is embedded with its provenance, so
reopening does not depend on the source package still being available. Palette
and package-path changes count as unsaved project settings. Selection and undo
history are session state and are not stored.

**Export…** opens profile settings and validates the visible composite. **Export
File…** writes the selected profile. Exporting or staging never marks
an edited project clean. Every export captures its destination's path and SHA-256
before encoding and checks them again before publication. A changed or deleted
destination blocks replacement; a newly created file is never overwritten by a
new-file export. Existing destinations and encoded output are capped at 64 MiB.
Export folders must already exist, including for dry runs. Dry runs validate
without creating temporary files or output. Close, New, and Open offer Save, Discard, and Cancel;
Save must finish successfully before the requested action continues. Replacing
an opened project checks its source SHA-256 and resolved path, including a
second check before atomic commit. Outside edits or deletion block the save;
save a copy or reopen after reviewing the conflict.

Before replacing different project bytes, Save keeps the exact previous file
under the destination folder's `.vibestudio/texture-backups/`, named by its
SHA-256. Repeated identical versions reuse a verified backup. Backup failures
block replacement; existing backup corruption never overwrites the source.
The save status and CLI `backupPath` report its location. Dry runs create no
backup files or directories. Backups are retained until explicitly removed;
there is no automatic pruning. They can be opened as projects and saved as a
copy.

## Local recovery

The Recovery section enables local checkpoints by default, every 30 seconds
while there are unsaved edits. The interval is adjustable from 5 to 600 seconds;
**Checkpoint Now** requests the current committed revision. A gesture in progress
finishes before a checkpoint is taken. Background writes retain one active and
one replaceable pending snapshot, without retaining undo buffers. Editing stays
available; the footer reports progress, completion, and errors. Turning recovery
off preserves existing files and allows an already accepted write to finish.

Checkpoints live in the application data folder's `texture-recovery` directory,
or beside an explicit `--settings-file` profile. Each document instance owns a
random UUID filename, so multiple editors do not overwrite each other's drafts.
**Refresh Recovery List** inspects timestamps, dimensions, layer counts, and
checksums. Invalid records have an explicit error. The scan inspects at most
256 files from up to 8,192 matching entries; **Open Recovery File…** can choose an
unlisted record. Scanning and restoration are cancellable. A listed checkpoint's
fingerprint is checked again when restoring it.

**Restore Selected as Draft** restores layers, palette, and package path with a
fresh unsaved state. Source paths inside a checkpoint are informational and are
never followed. The restored draft's first project save must use a new filename;
it cannot replace an existing project. The chosen checkpoint remains available.
A successful project save, document replacement, or approved close retires only
that editor's current checkpoint. Failed opens and saves retain it. A late
background write cannot resurrect a retired draft. Unexpected dialog destruction
flushes the most recent accepted snapshot; a process crash can recover only the
last completed checkpoint. Recovery files and backups have no automatic pruning.

The original recovery envelope uses the eight-byte magic `56 53 54 52 45 43 1a 0a`,
little-endian version 1, a 32-bit JSON length and a 64-bit native payload length.
JSON (at most 64 KiB) records the UUID, UTC timestamp, source fingerprint,
revision, and dimensions. A complete bounded `.vtexture` payload follows; the
final SHA-256 covers the envelope, metadata, and payload. Inspection validates
the envelope; restoration also validates every embedded layer before committing.
Unsupported versions, corrupt payloads, and mismatched dimensions leave the open
document unchanged. Undo history and the clipping selection are not recovered.

## Native project format

The original VibeStudio project format has the eight-byte magic
`56 53 54 45 58 0d 0a 1a`, followed by little-endian 32-bit version (1) and JSON
length. Bounded JSON describes dimensions, active layer, layer properties and
PNG byte lengths, and an extensible metadata object. One lossless PNG per
layer follows, then a SHA-256 digest of every preceding byte. Files are capped
at 192 MiB and JSON at 256 KiB. The reader checks version, digest, dimensions,
layer count, identifiers, payload bounds, PNG dimensions, and known metadata before replacing
an open document. Unknown metadata is preserved; unsupported versions fail.
Embedded palettes must contain all 256 validated colors and their required
properties. The package path must be text of at most 4,096 characters without
null characters. GUI and CLI use these same validation rules.
The optional `indexedPalette` layer array preserves exact table lengths when Qt
encodes an identity grayscale palette as a grayscale PNG. Legacy grayscale
payloads restore a 256-entry identity table; mismatched recorded palettes fail.
The known `export` metadata object uses the versioned settings described below;
invalid fields or unsupported settings versions cannot silently reset on reopen.

## Export profiles and previews

Choose **Export** in the inspector, then **Preview and Validate**. It uses a
worker snapshot and shows the actual encoded colors, alpha, palette provenance,
byte size, changed-pixel counts, warnings, and a selector for all four generated
mip levels. Editing pixels, changing the palette, or changing options invalidates
the preview. Export settings persist in projects and recovery checkpoints;
preview bytes and undo history do not. Encoding can be cancelled; the short,
guarded publication step finishes atomically and reports its result.

| Profile ID | Output | Constraints and behavior |
| --- | --- | --- |
| `png` | `.png` | Lossless 8-bit RGBA, including partial alpha and hidden RGB. |
| `png-indexed` | `.png` | Explicit 8-bit index plane and embedded 256-color palette, including grayscale palettes. Per-entry alpha must represent the requested pixels exactly. |
| `tga` | `.tga` | Uncompressed 32-bit BGRA, bottom-left origin, explicit 8-bit alpha. Suitable for original Quake III image references. |
| `pcx` | `.pcx` | Opaque 8-bit RLE, embedded palette, even row padding. Warns about original Quake II's even-width, 640 × 480 and 131,072-pixel limits; that engine uses its global palette and treats index 255 as transparent. |
| `quake-miptex` | `.mip` | Four opaque indexed mips, dimensions divisible by 16, 1–15-character ASCII name. Classic `sky` names require 256 × 128; the classic compiler's entire texture lump is 2 MiB. |
| `quake-wad2` | `.wad` | One miptexture in a new WAD2, native type `0x44`. Same limits as miptexture; this does not edit an existing WAD. |
| `quake2-wal` | `.wal` | Four opaque indexed mips, dimensions divisible by 16, safe relative names up to 31 ASCII characters, next-animation name, unsigned 32-bit surface/content flags and signed surface value. Reserves index 255; classic output is limited to 131,072 pixels. |
| `doom-flat` | `.lmp` | Exactly 64 × 64 opaque raw indices, including index 255. Requires a flat namespace when added to a Doom WAD. |
| `doom-patch` | `.lmp` | Column posts with binary alpha and signed 16-bit left/top offsets. Classic height is at most 255; explicit source-port limits allow tall posts. Large column offsets and narrower port recognition limits produce warnings. |

Palette profiles require a resolved 256-color palette. Generated stand-ins require
**Allow generated palette** and remain visibly labelled. Matching source indices
are retained, including duplicate colors, unless alpha changes or reserved-index
rules require conversion. Other pixels use nearest-palette conversion with optional
dithering. Game formats carry raw palette RGB values, without color-profile conversion.

**Preserve or reject** keeps representable alpha and fails on loss. **Composite on
matte** explicitly removes alpha against an opaque color. **Binary threshold**
sets alpha below the chosen threshold to zero and the remainder to 255; opaque
profiles refuse it. Doom patches do not reserve a transparent palette index:
transparent pixels are absent posts. WAL does reserve index 255.

Mips use 2 × 2 area averaging or nearest pixel-center sampling. Quake's 224–255
fullbright band has three choices: preserve matching authored indices (new RGBA
colors avoid the band), exclude it, or allow conversion into it. Area filtering
keeps any authored fullbright coverage in the next mip by averaging only the
bright samples for that output pixel. **Use source-port limits** relaxes classic
size restrictions, with warnings; it is not a claim that every port accepts the file.

The profile rules follow the linked engine/specification references in
[Credits](CREDITS.md#texture-export-formats). Fixture and independent-reader tests
check encoded bytes; live engine/compiler acceptance still requires the target
project's integration tests.

## Project and package handoff

Export inside the project's asset roots, or choose a package-relative filename
matching the export profile and use **Stage Export**. Generated bytes are owned
by the normal package staging service; there are no temporary asset-file
dependencies. Replacing an existing entry requires **Replace existing entry**.
Encoding is cancellable, and a changed package revision or map selection rejects
the pending handoff before it can change either document.

For WAD2, choose **Quake miptexture** and use the same bare name in Export and
Package. The directory records type `0x44`; unrelated lump types are preserved.
For IWAD/PWAD, choose **Doom flat** or **Doom patch** and an eight-character lump
name. New lumps enter a matching flat/patch namespace, creating `F_START/F_END`
or `P_START/P_END` if absent. The texture and generated markers form one package
undo step. Replacements retain their existing namespace and directory position;
repeated names, unbalanced markers, and names in another namespace are refused.
Existing global Doom pictures such as `TITLEPIC` can be replaced in place after
validating that their current bytes are a patch; they are not moved into `P_`.
Later marker edits that strand an authored texture block publication. WAD3
encoding, sprite namespace authoring, and automatic `PNAMES/TEXTURE1` wall
composition are unavailable; a staged Doom patch still needs the target game's
wall-texture definitions before level use.

The Textures browser and thumbnails read the staged plan, so a new or replaced
texture can be inspected and reopened before saving the package. Package
conflicts leave the last on-disk package available for browsing. Unstage uses
the existing Packages controls. Palette resolution reads staged `PLAYPAL`,
`gfx/palette.lmp`, and `pics/colormap.pcx`; package changes invalidate dependent
browser palettes and thumbnails. **Refresh Palette Source** reloads the editor's
chosen palette from that plan or the selected installation. Document pixels keep
their authored colors until explicitly remapped or exported. Package changes
also invalidate the export preview; project palette metadata remains editable.

**Stage and Apply** stages the encoded pixels and applies their reference to
selected brushes or patches through one shared core transaction:

- PNG/indexed PNG/TGA in file packages apply to Quake III maps. The package path
  must begin with `textures/` and end with its lower-case profile suffix.
- Quake II WAL in file packages applies to Quake-family maps, using
  `textures/<name>.wal` as the package path.
- Quake miptexture lumps in WAD2 apply to Quake-family maps. The lump name and
  embedded texture name must match. The WAD2 container export is a separate file,
  not a texture lump.

File-based map tokens omit `textures/` and the extension because the compiler
supplies them. Paths outside that root disable Apply and show the reason in its
tooltip. Failed validation leaves both documents unchanged. Replacing pixels
already used by the selection succeeds without adding a redundant map undo step.
Map undo and package unstage remain separate operations; save each document
explicitly. For Quake, configure the map's `wad` property/compiler WAD search
path to the saved WAD2. Quake II/III package contents must be mounted under the
target game's asset root. PNG support depends on the target engine/source port.

Model materials and shader image references can point at the same package
texture path. This editor does not rewrite shader definitions, rename existing
material references, or create Doom wall-texture definitions. Package refreshes
notify level and model material views. Dependency review remains the place to inspect
these references before compiling and packaging.

`texture stage` uses the same encoders and native staging service, saving an
undoable `.vibepackage` draft. `--target-package` accepts an archive, folder, or
existing draft; `--target-entry` is a filename or native lump name. `--replace-texture`
permits entry replacement and `--overwrite` separately permits replacing a draft.
Unless an explicit palette file/root is supplied, a real palette in the target
plan takes precedence for encoding. `--dry-run` validates without writing.
The JSON report distinguishes `written` (draft) from `packageWritten` (always
false); publish the reviewed draft with the normal package writer.

For headless placement, follow staging with `map apply-texture` using the token
above and explicit object selectors, then publish the package draft and map
outputs separately. Updating pixels under an existing map reference only needs
restaging and package publication; the map is already connected.

```sh
vibestudio --cli texture stage flat.vtexture --profile doom-flat --target-package mod.wad --target-entry NEWFLAT --output mod.vibepackage --dry-run --json
```

## Keyboard and accessibility

The canvas is focusable: arrow keys move the pixel cursor, Space applies the
current tool or sets shape endpoints, and Shift plus arrows grows a clipping selection. Plus/minus zoom,
F fits, 0 shows actual size, and Escape cancels the current stroke/selection.
Middle-drag pans. Mouse-wheel zoom retains the fractional pixel under the pointer;
toolbar and keyboard zoom retain the view center. The pixel readout includes
coordinates and RGBA color, or pending shape endpoints. Eyedropper, Eraser, and
Selection remain usable when the typed paint color is invalid.
Toolbar actions and properties can be reached through keyboard focus; Undo,
Redo, Open, New, Save, and Save As have their standard shortcuts. If the platform
theme supplies no Save As binding, the editor provides Ctrl+Shift+S.

The editor follows the studio palette, font scaling, and layout direction.
Scroll areas keep the inspector available at large text sizes, and focus and
selection use contrasting outlines rather than color alone. Status, fields,
tools, and canvas instructions are translatable and accessible by name.

## CLI

```sh
vibestudio --cli texture create --size 64x64 --color transparent --output texture.png --json
vibestudio --cli texture edit texture.png --operations edits.json --output edited.png --json
vibestudio --cli texture create --size 128x128 --operations layers.json --output wall.vtexture --json
vibestudio --cli texture inspect wall.vtexture --json
vibestudio --cli texture profiles --json
vibestudio --cli texture validate wall.vtexture --profile tga --json
vibestudio --cli texture export wall.vtexture --profile quake2-wal --export-options wal.json --output wall.wal --dry-run --json
vibestudio --cli texture recoveries ./texture-recovery --json
vibestudio --cli texture recover ./draft.vtrecovery --output ./recovered.vtexture --dry-run --json
vibestudio --cli texture edit wall.vtexture --operations edits.json --output wall.vtexture --overwrite --json
vibestudio --cli texture edit --package assets.pk3 --entry textures/wall.png --operations edits.json --output wall.png --dry-run --json
```

`texture profiles` lists profile requirements and complete version-1 defaults.
`texture validate` performs encoding without an output path or writes.
`texture export` accepts a loose image, `.vtexture`, or `--package` with `--entry`;
the output extension must match the profile. Options begin with saved project
settings or imported native metadata. A bounded `--export-options <file.json>`
object overrides individual fields; `--profile` takes precedence over that file.
Unknown fields, invalid enums, fractional/out-of-range integers, and non-boolean
flags fail. `--allow-generated-palette` explicitly accepts fallback colors.
`--palette-file` reads a raw RGB/PLAYPAL palette or a PCX palette (maximum 4 MiB).
Alternatively `--palette-root` searches a game/content directory. These explicit
sources take precedence over a saved project palette. `--palette` selects the
game palette ID; without it, saved project metadata is preferred, otherwise the
native package format, selected output profile or common loose suffix supplies a
default. Specify it for ambiguous lumps. `texture edit` also retains supported
native names, flags and offsets when its output is a `.vtexture` project.
The legacy `texture export <package> <entry>` alias still runs `texture decode`
unless `--profile` or `--export-options` selects the authoring exporter.

For example, `wal.json` can contain:

```json
{"version":1,"name":"custom/wall","animationNext":"custom/wall2","surfaceFlags":0,"contentFlags":0,"surfaceValue":0,"alpha":"matte","matte":"#ff202020","mipFilter":"box"}
```

Other fields are `dither`, `allowGeneratedPalette`, `extendedLimits`, `fullbright`
(`preserve`, `exclude`, `allow`), `alphaThreshold` (1–255), `leftOffset`, and
`topOffset`. JSON flags use decimal integers; GUI WAL flags also accept `0x` hex.
JSON export reports include options, dimensions, bytes, mip dimensions, changed
pixel counts, warnings, output SHA-256, palette source and RGB SHA-256, and a
separate `written` flag. A valid encoding does not imply successful publication.

Create/edit require an output ending in `.png` or `.vtexture`. Existing files require
`--overwrite`; output directories must already exist. `--dry-run` validates
and encodes without writing. `--palette <id>` selects a palette; `--package`
can supply a real one. `texture edit` requires a recipe, while `texture create`
optionally accepts `--operations`. Loose paletted files default to the generated
Quake palette; supply the correct palette ID for other game families. Projects
reuse their saved palette unless explicitly overridden. Use an empty `[]`
recipe to export a project composite without editing it. `texture inspect`
validates the project and reports dimensions, layer properties, metadata and
source SHA-256. Native project updates share the GUI's conflict-aware writer.

`texture recoveries <directory>` reports checkpoint metadata, envelope validity,
individual errors, and scan truncation. `texture recover <checkpoint> --output
<new.vtexture>` validates and restores to a new native project. It accepts
`--dry-run` and `--json`, refuses `--overwrite`, and leaves the checkpoint and its
recorded source unchanged. Output publication cannot replace a file that appears
at the destination during saving.

A recipe is a JSON array, at most 1 MiB and 256 operations. Operations run in
array order; an invalid operation aborts the entire recipe before output is
written. Coordinates and dimensions must be integers. Supported operations:

```json
[
  {"op": "stroke", "points": [[2, 2], [50, 50]], "color": "#ff8040", "width": 2},
  {"op": "stroke", "points": [[-1, 0], [3, 0]], "color": "#80ff8040", "width": 5,
   "brush": "round", "mode": "source-over", "wrap": true},
  {"op": "rectangle", "x": 8, "y": 8, "x2": 24, "y2": 24, "color": "blue", "filled": false},
  {"op": "ellipse", "x": 10, "y": 10, "x2": 22, "y2": 22, "color": "white", "filled": true},
  {"op": "line", "x": 2, "y": 30, "x2": 20, "y2": 30, "color": "red", "width": 2},
  {"op": "fill", "x": 0, "y": 0, "color": "#ff202830", "tolerance": 10, "wrap": true},
  {"op": "offset", "x": 16, "y": -16},
  {"op": "crop", "x": 0, "y": 0, "width": 32, "height": 32},
  {"op": "resize", "width": 64, "height": 64, "smooth": false},
  {"op": "flip", "axis": "horizontal"},
  {"op": "rotate"},
  {"op": "palette", "dither": false}
]
```

`stroke` accepts 1–65,536 points; `width` defaults to 1, `brush` to `square`,
`mode` to `replace`, and `wrap` to false. Brushes are `square`/`round`; paint
modes are `replace`/`source-over`. `line`, `rectangle`, and `ellipse` use inclusive
`x,y` and `x2,y2` endpoints and the same brush fields. `filled` defaults to false;
it affects rectangles/ellipses only. Points must lie inside the canvas, or inside
the 3 × 3 repeat when wrapping (`−width ≤ x < 2×width` and similarly for height).
`fill` accepts optional `tolerance` (0–255) and `wrap`. `offset` takes signed
`x,y` offsets, cyclic within the selection or canvas. `flip` accepts
`horizontal` or `vertical`; `rotate` is one clockwise quarter turn. `smooth`
and `dither` default to false. JSON reports include dimensions, operation
count, output path, dry-run/write state, and palette provenance. Existing
package staging and map texture CLI commands provide the subsequent handoff.
One stroke is limited to 268,435,456 estimated pixel writes, preventing very
long retraced wide strokes from monopolizing a worker. Split larger paths into
shorter strokes. Palette conversion caches up to 65,536 exact RGB matches in
non-dithered mode without changing the color-distance result.

Layer and selection operations use the same ordered recipe:

```json
[
  {"op": "layer-add", "name": "Paint"},
  {"op": "select", "x": 8, "y": 8, "width": 24, "height": 24},
  {"op": "fill", "x": 8, "y": 8, "color": "#80ff8040"},
  {"op": "layer-properties", "opacity": 75, "blend": "multiply"},
  {"op": "select-none"}
]
```

`layer-select` and `layer-move` take a zero-based `index`, ordered bottom to
top. `layer-properties` accepts optional `name`, `visible`, `locked`, `opacity`
(0–100), and `blend` (`normal`, `multiply`, `screen`, `add`). Structural commands
are `layer-duplicate`, `layer-remove`, `merge-down`, and `flatten`. `clear`
clears selected pixels or the full active layer. `move-pixels` takes integer
`x` and `y` offsets. JSON booleans must be booleans, not strings. A later error
rolls back the entire recipe, including layer structure and undo history.

`canvas-size` takes `width` and `height`, plus an optional `anchor` (default
`center`), or explicit `x` and `y` offsets for the old canvas inside the new one.
Both offsets are required together; they cannot be combined with `anchor`.
The old canvas must intersect the new canvas. `resize-selection` takes `width`,
`height`, optional `smooth` (false), and optional `anchor` (center).
`rotate-selection` takes optional `anchor` and rotates clockwise by 90°.
Both require a rectangular selection and a visible, unlocked active layer.
Anchors are `top-left`, `top`, `top-right`, `left`, `center`, `right`,
`bottom-left`, `bottom`, and `bottom-right` in image coordinates, independent
of UI reading direction. Whole-canvas resampling and rotation remain `resize`
and `rotate`.

```json
[
  {"op": "canvas-size", "width": 128, "height": 128, "anchor": "center"},
  {"op": "select", "x": 32, "y": 32, "width": 16, "height": 24},
  {"op": "resize-selection", "width": 32, "height": 24, "anchor": "top-left"},
  {"op": "rotate-selection", "anchor": "center"}
]
```

## Verification

`texture-export-smoke` covers fixed native headers and a complete Doom patch
fixture, all mip offsets, tall posts, alpha/matte behavior, reserved/fullbright
indices, exact duplicate-color indices, engine limits, cancellation, conflicts,
and actual CLI validation/export/dry-run for all nine profiles. Synthetic PNG,
TGA and PCX fixtures are also checked with an independent Pillow reader.
To reproduce that optional check, run the export smoke suite with
`VIBESTUDIO_TEST_CAPTURE_ROOT` pointing at an existing project-local evidence
directory, then run `python src/tests/texture_export_readers.py <directory>`
with Pillow installed. `--report <file.json>` saves the results; this adds no
runtime or required build dependency.
`texture-export-ui-smoke` exercises every profile, mip selection, metadata
persistence/import, invalid flags, stale previews, two-phase publication and
cancellation through direct widget commands. Widget renders and accessible-state
checks cover dark and both high-contrast themes, 100%/200% text, RTL and expanded
translations; no OS capture or input injection is used.
`texture-png-export-ui-smoke` covers the browser output dialog, immutable pixel
snapshots, worker heartbeat, accessible cancellation, overwrite refusal,
cancelled inspection/encoding, changed-target retention, owner destruction,
and the separate browser/export size limits.

`texture-document-smoke` checks pixels, alpha, connected fill, stroke grouping,
history limits, save points, invalid-operation rollback, overwrite protection,
PNG/package round trips, and the executable's create/edit commands.
`texture-paint-smoke` compares optimized brush spans against a pixel-stamp
reference across directions, widths, wrapping and selections. It also checks
blend alpha, shape masks, tolerant and wrapped fill, exact indexed offsets,
cancellation, malformed recipes, and a representative broad-stroke timing.
`texture-transform-smoke` checks all nine anchors, independent pixel mappings,
exact indices and hidden RGBA, palette capacity, layer alignment, selection-only
history, bounds, cancellation after partial work, native persistence, recipe
rollback, and representative 2048 × 2048 transform timings. Executable CLI tests
cover selected transforms, dry runs and failed-save retention.
`texture-project-smoke` covers layered composition, clipping and move semantics,
structural history, exact native round trips, indexed transparency, bounded
malformed projects, source conflicts, and failed-save retention.
`texture-resource-smoke` checks native/Qt allocation limits, aggregate sprite/mip
budgets, cumulative external-frame reads, early/in-progress decoder cancellation,
malformed indexed projects, import rollback, exact cached composites,
and fill results against an independent connected-region definition.
`texture-performance-smoke` measures the limits of 4,194,304 pixels per canvas
and 33,554,432 pixels across layers, undo saturation, cancellation rollback, project encoding/decoding, native
mip export, and high-entropy PNG/PCX conversion and decoding. Its JSON output reports elapsed
time and the longest interval between cooperative progress checkpoints. Use a
Meson release build for performance conclusions; debug timings are diagnostic.
`texture-preview-worker-smoke` exercises queue coalescing, cancellation during
reads, staged-palette revisions, native mip metadata, duplicate occurrences,
thumbnail retention and a 16,777,216-pixel browser decode with event-loop
heartbeat measurements.
`texture-palette-ui-smoke` verifies source snapshots, worker reads, cancellation,
stale-revision rejection, choice rollback and palette saved-state behavior.
`texture-thumbnail-cache-smoke` visits 10,000 entries, checks byte/count eviction,
and exercises visible-row demand at both ends of a large list and in filtered RTL tiles.
Optional browser layout QA sets `VIBESTUDIO_TEST_BROWSER_LAYOUT=1` for the
editor UI suite, with `VIBESTUDIO_TEST_CAPTURE_ROOT` pointing inside the project.
This adds three whole-shell theme/scaling renders; allow a Meson timeout
multiplier of at least 2 for that extra work. Whole-shell styling on a busy debug
host can exceed that allowance; it is tracked separately from texture worker latency.
Ordinary editor regressions keep their
normal timeout and do not run these additional whole-shell renders.
`texture-editor-ui-smoke` calls widget commands directly, exercises asynchronous
edits/cancellation/save/staging and the shell's texture browser, and checks
normal and enlarged high-contrast/RTL layouts without injecting user input.
With `VIBESTUDIO_TEST_CAPTURE_ROOT` set, it also writes
`texture-ui-timings.json` containing shell construction and metadata-search
duration, heartbeat count and maximum event-loop gap. For phase details, enable
`QT_LOGGING_RULES=vibestudio.startup.info=true`; on Windows,
`QT_FORCE_STDERR_LOGGING=1` directs Qt logging to the captured standard error.
These opt-in logs contain fixed phase names and elapsed milliseconds.
`texture-editor-latency-smoke` measures dispatch, completion and UI heartbeats
for a noisy 2048 × 2048 image with eight layers: authoring, direct canvas paint,
undo, project save/reopen, cancellation, native preview and PNG publication.
It verifies pixels and saved-state behavior and reports JSON timings. Use an
optimized build for release latency evidence; timing values are measurements,
not hardware-dependent test thresholds.
`texture-accessibility-smoke` audits every inspector section and both toolbars
for accessible names/roles, Tab focus and focus-chain membership. It checks
document shortcut scope and changing pixel/shape readouts in dark and both
high-contrast themes, including 200% RTL/expanded translations. Toolbar overflow
remains reachable through named buttons. This direct-command and metadata audit
does not claim physical keyboard, live clipboard or OS screen-reader acceptance.
`texture-handoff-smoke` follows a layered project through direct brush, shader
and model references, package undo/redo, dependency review, draft reopening and
saved-package previews. It also checks atomic rejection of invalid destinations,
replacement controls, unchanged-map restaging and native WAD2/WAL placement.
The optional `src/tests/texture_compiler_workflow.py` accepts `--binary`,
`--qbsp`, `--q3map2` and an `--output-root` inside `.agents/tmp`. It authors
synthetic textures through the CLI, publishes native/package outputs, invokes
the installed compilers and independently inspects BSP texture payloads and
references. This verifies compiler handoff, not target-engine rendering; it
uses no game assets or game launch. Compiler versions and complete logs belong
with the resulting acceptance report.
Direct gesture commands cover seam crossings, shape preview/commit/cancel,
two-endpoint cursor authoring, fill/offset controls, anchored zoom, canvas sizing,
selected transforms, disabled-state explanations and their enlarged layouts.
`texture-recovery-smoke` exercises checksums, corrupt metadata, cancellation,
stale checkpoint fingerprints, background queue coalescing/retirement, and the
CLI's read-only listing and new-file restoration. `texture-recovery-ui-smoke`
exercises the actual timer, profile isolation, metadata-only checkpoints,
unexpected destruction, first-save protection, Save/Discard/Cancel across
document transitions, self-recovery, and the main window's save continuation.

### Platform and manual acceptance limits

The bounded texture workflow has completed its release-candidate checklist.
All 42 relevant suites have passing results on both available platforms;
independent raster readers and a 22-step generated-asset compiler/package
workflow supplement those tests. Exact runs, build identities and limitations
are recorded in the [requirement audit](plans/texture-editor-release-candidate.md#current-requirement-audit).

The release audit uses optimized Meson/Ninja builds and offscreen widget
commands on Windows (Qt 6.10.1, clang-cl 20) and Ubuntu 24.04.3 under WSL
(Qt 6.4.2, GCC 13.3). The Linux checkout and test outputs are on `/mnt/e`,
so these checks do not establish native Linux filesystem durability. Linux
audio playback is disabled in this configuration; texture authoring remains
available. The Linux build uses source-language fallback because `lrelease`
is unavailable there. Windows packaging includes the 21 compiled catalogs;
catalog presence does not imply completed translations.

For WSL verification, use a process-local Linux `PATH`, for example
`/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin`.
Inherited Windows search paths caused shared shell compiler discovery to stall:
the same binary's ready phase measured 29.17 seconds with those paths and
1.80 seconds with the Linux-only path. This is a known shell/discovery issue,
not a texture operation timing. No host environment setting needs to change.

Physical keyboard and mouse interaction, live clipboard transfer, native
screen readers, native-window behavior, macOS/ARM builds, clean-machine runtime
installation, signing and target-game rendering remain unverified. Automated
accessibility evidence covers direct commands, control metadata, focus chains,
scaling, RTL, expanded text and widget render targets. The local Windows package
is unsigned and has not been published. The complete evidence and current
gate decisions are in the [release audit](plans/texture-editor-release-candidate.md).

## Additional exchange formats

DDS and FTX are native import/export profiles in the same preview, validation,
staging and CLI workflow. DDS writes uncompressed BGRA8 and FTX writes RGBA8;
SWL imports its embedded palette and four mips and can export through other
profiles. Import filters and `asset formats` share the runtime capability catalog,
including TIFF where Qt provides it. See [Asset Formats](ASSET_FORMATS.md) for
variants and target-engine limits. `.vtexture` remains the editable source;
[workspaces](WORKSPACES.md) restore surrounding project/package context.
