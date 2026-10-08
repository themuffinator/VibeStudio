# Materials and shaders

The **Materials** page shows every texture, shader and material in a package
the way its game draws it, animated in real time, and lets you edit each one
as text or as a node graph. It covers Doom wall textures and flats, Quake WAD
textures, Quake II WAL textures, Quake III shaders and Doom 3 materials.

> [!NOTE]
> **Status: Partial.** Browsing, the live preview, checking, text and node
> editing and saving work and have automated tests on generated files, but
> they have not been proven on real game packages yet. The preview is drawn
> by VibeStudio itself and approximates each engine: see
> [What the preview leaves out](#what-the-preview-leaves-out).

## Open materials

Choose **Materials** on the rail, or press <kbd>Ctrl</kbd>+<kbd>8</kbd>
(<kbd>Cmd</kbd> on macOS), with a package open:

- **File** > **Open Package…** opens a PAK, WAD, ZIP or PK3 file, and
  **File** > **Open Folder Package…** a game or mod folder.
- **Open Script** on the page, or dropping a `.shader` or `.mtr` file on the
  window, opens a loose script. Its images and Doom 3 tables come from the
  open package, so open the game's or mod's package first.

You can also jump here from other pages: right-click an image on
**Textures**, or a texture in the **Levels** texture list, and choose **Show
in Materials**. From **Textures** the library lists the materials that read
the image; from **Levels** it selects the material the map uses (Quake III
maps leave out the `textures/` folder, which is added for you).

The page reads the package the first time you show it, with progress in the
strip at the top. The library on the left shows each material as a swatch:
a triangle marks animated materials, and an exclamation mark ones the game
would refuse to draw. **Show as List** switches to a compact list, and
**Animate Swatches** plays animated swatches while they are on screen.

Type in the filter to search names. Terms test what the material is, as in
the command line's `--where`:

| Term | Keeps |
| --- | --- |
| `animated=yes` | Materials that change over time |
| `engine=doom3` | One engine's materials (`doom`, `quake`, `quake2`, `quake3`, `doom3`) |
| `kind=implicit` | Quake III and Doom 3 images no script names, which the game draws with its default material |
| `errors>0`, `rejected=yes` | Materials with problems, or ones the game would refuse |
| `surfaceparm=nolightmap`, `sky=yes` | Surface parameters and traits |
| `shadowed=yes` | Definitions another one with the same name overrides |

## Preview a material

Choose a material to see it drawn by its engine's rules: Quake III stages,
blending, waves, scrolling and deforms; Doom 3 lighting with a moving light;
Doom sector light and the colormap; Quake and Quake II liquids, skies and
animations. A material that animates plays by itself, unless **Reduce
motion** is on in Accessibility settings.

- **Play** and **Pause** (<kbd>Space</kbd> in the preview), **Restart**, the
  speed menu and the time bar control the clock. <kbd>,</kbd> and
  <kbd>.</kbd> step a tenth of a second.
- The shape menu draws the material on a wall, floor, cube, sphere,
  cylinder or the inside of a room (used for skies and fog).
- Drag to turn the shape, Shift+drag (or drag with the right button) to move
  the light, and scroll to zoom. The arrow keys, <kbd>+</kbd>, <kbd>-</kbd>
  and <kbd>Home</kbd> do the same from the keyboard; double-click resets.
- **Engine View** holds the game's own drawing choices: overbright and
  lightmap brightness for Quake III, sector light, distance shading, fake
  contrast and Boom wrapping for Doom, the renderer and light style for
  Quake, intensity for Quake II, and the interaction style, light and
  ambient light for Doom 3.

When the game would refuse a material, the preview shows what it draws
instead (Quake III's default shader, for example) and says why under the
picture. Turn off **Show What the Engine Draws Instead** to see the stages
anyway.

## Edit as nodes

The **Nodes** tab shows the material as a graph: images and coordinate,
colour and geometry nodes feed stages, which feed the material in draw order.
Select a node to edit its properties beside the graph.

- **Add Node** adds a stage, a coordinate change, a colour or alpha
  generator, a deform or a surface setting. Some need a stage: select the
  stage, or a node inside it, first.
- <kbd>Delete</kbd> removes the selected node, and
  <kbd>Alt</kbd>+<kbd>Up</kbd> or <kbd>Alt</kbd>+<kbd>Down</kbd> moves a stage
  or texture modifier earlier or later.
- The arrow keys follow the wires from node to node; <kbd>Enter</kbd> moves to
  the node's properties. Right-click for more, such as wrapping a Doom 3
  expression in an operator or expanding a `diffusemap` shorthand into a
  full stage.

Every node edit is a change to the text, so <kbd>Ctrl</kbd>+<kbd>Z</kbd>
undoes it from either tab.

## Edit as text

The **Text** tab shows the whole script with highlighting. As you type, the
preview, the graph, **Images** and **Problems** follow, and problems are
underlined where they are. For games without material scripts the text is
the form their tools use:

| Game | Text | Saved as |
| --- | --- | --- |
| Doom (Boom) | Animation and switch tables in SWANTBLS form | `ANIMATED` and `SWITCHES` lumps |
| Doom (ZDoom ports) | `ANIMDEFS` | The `ANIMDEFS` lump or file |
| Quake II | The WAL header as `.wal_json` | The WAL's header, pixels untouched |
| Quake | None: the name sets animation (`+0`), liquids (`*`) and skies | Read only |

**New Material** starts a Quake III shader or Doom 3 material from a
template, such as a pulsing glow, rippling liquid, sky or flickering light.

## Check and save

**Problems** lists what the game would warn about or refuse, with the line;
**Images** lists every image the material reads and where it was found; and
**Details** sums the material up with the other materials that share its
images.

**Save** (<kbd>Ctrl</kbd>+<kbd>S</kbd> on the page) writes a loose script in
place. For a script, lump or WAL inside the open package it stages the
change, which you review and save with the package on the **Packages** page.
**Revert** goes back to the saved text; Undo brings your edits back.

## What the preview leaves out

- It draws a single shape without a map: no real lightmaps, no bump-mapped
  map geometry, and Doom 3 materials only with the default interaction.
- RoQ and CIN videos show a moving placeholder.
- The rules follow Quake III Arena (ioquake3). Enemy Territory's
  `implicitMap`, `implicitMask` and `implicitBlend` are drawn as that game
  draws them; other Return to Castle Wolfenstein, Enemy Territory and Jedi
  Knight keywords are read with a warning but not drawn.
- A Doom PWAD's textures are not merged with its IWAD's.

## Command-line equivalents

```sh
vibestudio --cli material list ./baseq3 --where "animated=yes" --json
vibestudio --cli material validate ./mymod --base ./baseq3
vibestudio --cli material render ./baseq3 --material textures/sfx/fire_ctfblue --frames 8 --fps 10 --output fire.png
```

<details>
<summary>All material commands</summary>

| Task | Command |
| --- | --- |
| List the materials in a package or script | `material list` |
| Show one material, its images and problems | `material inspect` |
| Check materials the way the game loads them | `material validate` |
| Draw a material, or an animation sheet, to PNG | `material render` |
| Print the node graph, or apply node edits | `material graph` |
| Apply text edits to a script | `material edit` |
| List templates, start a material from one | `material templates`, `material new` |
| Compile Doom animation and switch tables | `material doom-tables` |
| Show or rewrite a Quake II WAL header | `material wal` |

</details>

See the [command-line reference](cli.md) for the shared options and exit
codes.
