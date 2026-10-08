# Textures and sprites

The **Textures** page lists the images in a package, decodes them with the
right palette, and opens the Texture Editor for painting, converting and
staging textures for Doom, Quake, Quake II and Quake III projects.

> [!NOTE]
> **Status: Partial.** Decoding the classic idTech image formats, the Texture
> Editor and its game export profiles work and have automated tests, but they
> have not been proven in real projects yet. Sprite files cannot be written,
> and WAD3 export and Doom wall-texture definitions are not built.

## Browse textures

Choose **Textures** on the rail, or press <kbd>Ctrl</kbd>+<kbd>4</kbd>
(<kbd>Cmd</kbd> on macOS). The page lists the images in the open package, so
open one first:

- **File** > **Open Package…** opens a PAK, WAD, ZIP or PK3 file.
- **File** > **Open Folder Package…** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>O</kbd>)
  treats a folder, such as your project's asset folder, as a package.
- **Go to File** (<kbd>Ctrl</kbd>+<kbd>P</kbd>), or dropping a loose image on
  the window, opens the image's folder as a package and selects the image.

Opening a project does not list its images by itself; the project supplies the
palette instead (see [Choose a palette](#choose-a-palette)).

Switch between **Tiles** and **List**, and type in the filter to search paths.
Terms such as `ext=wal`, `size>16kb` or `w>=128 format=wal` test the name,
extension, folder, size and number of uses in the open map, and, once an image
is decoded, its width, height and format. Tick **In open map** to list only the
textures the map open in **Levels** uses.

Select an image to decode it. Decoding runs in the background:
**Cancel Preview** stops it, and **Reload Previews** rereads images and
palettes from the package. In the preview, scroll to zoom, drag to pan and
point at a texel to see its palette index. **Zoom to Fit** (<kbd>F</kbd>),
**Actual Size** (<kbd>0</kbd>), **Zoom In** (<kbd>+</kbd>), **Zoom Out**
(<kbd>-</kbd>), **Pixel grid** and **Transparency** (a checkerboard behind
transparent texels) sit above it. **Mip** shows another mip level and
**Sprite frame** steps through a sprite's frames.

The inspector has three tabs: **Details** (format, size, mip levels, palette
source, flags and raw metadata), **Palette** (all 256 swatches, with the
transparent index marked) and **Sprite Creator**.

## Supported image formats

| Format | Typical use | Notes |
| --- | --- | --- |
| Doom patches, flats, `PLAYPAL`, `COLORMAP` | Doom, Heretic, Hexen WADs | Patches keep their offsets. Flats are recognised by size (4096, 4160 or 16384 bytes) in a flat namespace or folder. |
| Quake `.lmp` pictures, WAD2 miptextures | Quake | Miptextures decode all four mip levels. |
| WAD3 miptextures | Half-Life-style WADs | Each texture carries its own palette. There is no WAD3 texture export. |
| `.wal`, `.m8`, `.m32` | Quake II engine games | WAL surface flags, content flags and animation names are read. |
| PCX, TGA | Quake II skins and pictures, Quake III | 8-bit PCX only. |
| `.spr`, `.sp2` | Quake, Half-Life and Quake II sprites | See [Work with sprites](#work-with-sprites). |
| DDS, FTX, SiN `.swl` | Exchange with other tools | DDS shows its base mip level only. There is no SWL export. |
| PNG, JPEG, BMP, GIF, TIFF, WebP | Quake III and source ports | Read through Qt's image plugins, so they depend on your build. |

Images up to 64 MiB decode; one that fails shows the reason instead of a
partial picture.

## Choose a palette

Indexed formats store palette numbers rather than colours, so how they look
depends on the palette. The **Palette** list on the toolbar decides it:

- **Automatic**, the default, uses the palette your project names in its
  manifest (see [Projects](projects.md)), otherwise the one the open package
  ships: `PLAYPAL` for Doom-family WADs, `gfx/palette.lmp` for Quake and
  `pics/colormap.pcx` for Quake II.
- The other entries name a game family, such as **Quake II (generated)** or
  **Doom (generated)**. VibeStudio still looks for that family's real palette,
  first in the open package and then in the selected game installation.

The line under the preview always says where the palette came from. If no real
palette is found, VibeStudio uses a generated stand-in palette and says so:
indexed colours will not match the game. The stand-ins contain no game data.

## Use a texture in the open map

Right-click an image to use it in the map open in **Levels**. These actions
change the map in memory; save it in **Levels** afterwards (see
[Level editing](levels.md)).

| Action | What it does |
| --- | --- |
| **Select in Open Map** | Selects everything in the map that uses the texture. |
| **Apply to Map Selection** | Puts the texture on every face of the brushes and patches selected in a Quake-family map, as one undoable edit. |
| **Use for Map Painting** | Makes it the paint material and switches **Levels** to its paint tool. |
| **Use in Open Map…** | Replaces a texture the map already uses with this one. |
| **Show in Packages** | Shows the exact package entry. |
| **Show in Materials** | Lists the shaders and materials that read the image on the **Materials** page, drawn the way the game draws them (see [Materials and shaders](materials.md)). |
| **Export PNG…** | Writes the decoded image to a PNG file. |

## Edit a texture

1. Choose **Texture Editor** on the page header to start a new image, or select
   an image and choose **Edit Selected** to edit the mip level or sprite frame
   on screen, keeping its name, WAL flags or patch offsets for export.
   **Open…** in the editor imports a loose image file instead.
2. Pick a section from the editor's inspector list: **Paint**, **Transform**,
   **Selection**, **Palette**, **Layers**, **Package**, **Export** or
   **Recovery**.
3. Edit the image, then choose **Save Project** to keep a layered `.vtexture`
   file.

- **Paint**: **Pencil**, **Brush** (square or round, **Replace RGBA** or
  **Blend over**), **Eraser**, **Line**, **Rectangle**, **Ellipse**, **Fill**
  with a tolerance, and **Eyedropper**. **Wrap painting** lets strokes cross the
  edges, and **Tile Preview** shows a 3 × 3 repeat so seams stand out.
- **Transform**: crop, resample, rotate and flip the canvas, **Offset and Wrap**
  or **Offset Half Size** to bring edge seams into view, and
  **Set Canvas Size** to pad or crop without resampling.
- **Selection**: a rectangle selection limits every edit; copy, cut, paste,
  move, resize or rotate the selected pixels.
- **Palette**: choose the **Game palette**, then **Remap to Palette**
  (optionally dithered) or **Refresh Palette Source**. Existing pixels keep
  their colours until you remap or export.
- **Layers**: visibility, locks, opacity, blend modes, **Import Layer…**,
  **Merge Down** and **Flatten**.

On the canvas, arrow keys move the pixel cursor, <kbd>Space</kbd> applies the
tool or sets a shape's end points, <kbd>Shift</kbd>+arrow keys grow a
selection, <kbd>F</kbd> fits, <kbd>0</kbd> shows actual size and
<kbd>Esc</kbd> cancels. Middle-drag pans. Each stroke is one undo step.

The **Recovery** section keeps background copies of unsaved work; restoring
one opens it as an unsaved draft. Images are 8-bit RGBA or indexed colour, up
to 4,194,304 pixels (for example 2048 × 2048) and 32 layers. Pressure-sensitive
painting is not supported.

## Export and convert

**Export PNG** on the page header writes the image on screen as a separate PNG
file. For game formats, use the editor's **Export** section:

1. Choose an export profile.
2. Choose **Preview and Validate**. The preview shows the encoded colours, the
   palette's source, alpha changes, engine warnings and every mip level.
3. Choose **Export…** and pick where to write the file.

<details>
<summary>Export profiles</summary>

| Profile | CLI name | Notes |
| --- | --- | --- |
| **PNG · RGBA** | `png` | Full colour and alpha. The target engine must read PNG. |
| **PNG · game palette** | `png-indexed` | Palette indices with an embedded palette. |
| **Targa · RGBA** | `tga` | 32-bit with alpha; suits Quake III. |
| **DDS · BGRA8** | `dds` | Uncompressed, one surface, no mip levels. |
| **FTX · RGBA8** | `ftx` | 32-bit colour and alpha. |
| **PCX · opaque palette** | `pcx` | 8-bit with an embedded palette. |
| **Quake · miptexture** | `quake-miptex` | Four mip levels; sides divisible by 16. |
| **Quake · WAD2 texture** | `quake-wad2` | One miptexture in a new WAD2 file. |
| **Quake II · WAL** | `quake2-wal` | Four mip levels with flags and animation name; index 255 is reserved. |
| **Doom · flat** | `doom-flat` | Exactly 64 × 64 pixels. |
| **Doom · patch** | `doom-patch` | Columns with transparency and offsets. |

</details>

Indexed profiles need a real palette; a generated stand-in is used only if you
tick **Allow generated palette**. Choose how transparency is handled with
**Preserve or reject**, **Composite on matte** or **Binary threshold**, and
tick **Use source-port limits** to relax the classic size limits, with
warnings. Exporting never marks the project as saved. To convert many package
images at once, use `asset convert` on the command line.

## Stage a texture into a package or map

1. With the target package open, enter the **Package path** in the editor's
   **Package** section: a path with the profile's extension, such as
   `textures/base/rust.tga`, or a bare lump name for a WAD.
2. Tick **Replace existing entry** if the package already has that file.
3. Choose **Stage Export** to add the texture to the package's staged changes,
   or **Stage and Apply** to also put it on the brushes and patches selected in
   **Levels**.

Quake miptextures go into a WAD2 under the same name as the export. Doom flats
and patches go between `F_START`/`F_END` or `P_START`/`P_END` markers, which
are added if missing. **Stage and Apply** works with PNG or TGA files under
`textures/` in Quake III maps, and with Quake II WAL files or WAD2 miptextures
in Quake-family maps.

Staged textures appear in the browser straight away, but nothing is written
until you save the package in **Packages** and the map in **Levels**,
separately (see [Packages](packages.md)). For Quake, point the map's `wad` key
or the compiler's WAD search path at the saved WAD2. WAD3 output and Doom
`PNAMES`/`TEXTURE1` wall definitions are not available, so a staged Doom patch
still needs a wall definition before a level can use it.

## Work with sprites

- Quake and Half-Life `.spr` sprites, including frame groups, and Quake II
  `.sp2` sprites decode in the browser; choose a frame with **Sprite frame**.
  A `.sp2` file names images stored elsewhere in the package, and up to 256 of
  its frames are loaded. Doom sprite lumps use the patch format and decode like
  patches.
- **Edit Selected** opens the frame on screen in the Texture Editor as an
  ordinary image, which you can export with any profile, such as a Doom patch.
- The **Sprite Creator** tab plans a new sprite: choose the **Engine** (Doom or
  Quake), **Name**, **Frames** and **Rotations**, then **Plan Sprite** to list
  the frame names, palette and package staging lines. It writes no files.

Planned: writing `.spr` and `.sp2` files, staging frames into a WAD's sprite
namespace, and animating sprite frames in the editor.

## Generate a texture (optional)

**Generate** on the page header opens the Texture Generator. It draws tiling
variants in the game's format and palette with an image model you have set up,
or makes one from a picture of your own with no AI at all. AI is off by
default; see [AI assistant](ai.md#generate-levels-textures-and-sounds).

## Command-line equivalents

The `texture`, `asset` and `sprite` command families use the same services as
the page. For example:

```sh
vibestudio --cli texture palette ./mymod/pak0.pak --palette quake --json
vibestudio --cli texture export ./wall.vtexture --profile tga --output ./wall.tga --dry-run --json
vibestudio --cli asset convert ./pak0.pk3 --entry textures/base/wall.png --output ./converted --format png --resize 128x128 --dry-run
```

<details>
<summary>All texture and sprite commands</summary>

| Task | Command |
| --- | --- |
| Decode an image or sprite to PNG | `texture decode` |
| Report which palette decodes indexed art | `texture palette` |
| Create, edit or check a `.vtexture` project | `texture create`, `texture edit`, `texture inspect` |
| List profiles, check an export, or export | `texture profiles`, `texture validate`, `texture export` |
| Stage into a package draft | `texture stage` |
| List or restore recovery copies | `texture recoveries`, `texture recover` |
| Generate a texture, or its source-port companion maps | `texture generate`, `texture derive` |
| Inspect or bulk-convert package images | `asset inspect`, `asset convert` |
| List the formats this build reads | `asset formats --module textures` |
| Plan a sprite | `sprite plan` |
| Put a texture on map objects | `map apply-texture` |

</details>

See [Command line](cli.md) for options and exit codes.

## Learn more

- [Texture Editor reference](../TEXTURE_EDITOR.md): every tool, limit and export rule.
- [Asset formats](../ASSET_FORMATS.md) and the [support matrix](../SUPPORT_MATRIX.md): exact format variants and limits.
- [AI automation](../AI_AUTOMATION.md): how the Texture Generator works.
