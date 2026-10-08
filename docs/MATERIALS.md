# Materials, Shaders And Textures

The Materials page and the `material` CLI family manage the surface
appearance of idTech 1 to 4 games in one place: Doom wall textures and flats
with their animations and switches, Quake WAD textures, Quake II WAL textures,
Quake III shaders and Doom 3 materials. Every material can be browsed, drawn
the way its engine draws it (animated, in real time), checked the way its
engine loads it, and edited either as text or as a node graph.

Status: **Partial**. The module is new, has automated tests on generated
fixtures only, and has not been proven against real game packages.

## Shape Of The Module

| Layer | Files | Role |
|---|---|---|
| Model | `src/core/material_model.*` | One definition type for all five engines: directives with their exact source spans, stages, expressions, image programs, deforms, sky and fog parameters, classic frames, switches, WAL flags, diagnostics and the reason an engine would drop the material |
| Scripts | `src/core/material_script.*` | Quake III and Doom 3 lexers and parsers, the Doom 3 expression grammar, text edits that keep comments and layout, templates, implicit materials |
| Classic | `src/core/material_classic.*` | Doom composites, flats, vanilla and Boom animation and switch tables, SWANTBLS and ANIMDEFS text; Quake texture names; Quake II WAL headers and `.wal_json` |
| Evaluation | `src/core/material_eval.*` | Quake III wave tables and noise, Doom 3 expressions and tables, classic frame timing, Quake light styles |
| Images | `src/core/material_images.*` | Engine lookup order, decoding, image programs, cube maps, Doom composition, palettes and colormaps, a shared byte-bounded cache and a shared per-package index |
| Renderer | `src/core/material_render*.cpp` | A CPU renderer per engine on six preview shapes (wall, floor, cube, sphere, cylinder, room) |
| Library | `src/core/material_library.*` | Scans a package: scripts, implicit shaders, classic textures; which duplicate the game uses |
| Graph | `src/core/material_graph.*` | Node graphs built from definitions, and graph edits turned into text edits |
| CLI | `src/cli/materials.*` | `material list`, `inspect`, `validate`, `render`, `graph`, `edit`, `templates`, `new`, `doom-tables`, `wal` |
| GUI | `src/app/material_*.cpp` | The Materials workbench on the **Materials** page (the `shell.mode.shaders` mode) |

The GUI and the CLI call the same functions; nothing the page shows is worked
out a second way.

## One Text, Two Editors

The text is the single source of a material. The node graph is built from the
parsed definition, and a graph edit (set a property, add or remove a node,
move a stage or tcMod, wrap or unwrap a Doom 3 expression, expand a Doom 3
shorthand) is translated into text edits by `applyMaterialGraphEdit`. The text
is parsed again and the graph rebuilt from it, so the two cannot disagree.
Node ids are paths (`stage/0/tcmod/1`, `stage/0/color/red/e/a`) and survive
rebuilds, so the selection and the positions the user dragged nodes to stay.

Text edits patch only the directive spans they change: comments, indentation
and the rest of the script are kept byte for byte. On the page a graph edit
reaches the editor as one replacement of the changed range, so Undo and Redo
in the text cover graph edits too.

Classic engines have no material scripts, so their editable text is the form
their tools already use:

| Engine | Text | Saved as |
|---|---|---|
| Doom with ANIMDEFS | Hexen/ZDoom `ANIMDEFS` | The `ANIMDEFS` lump or file |
| Doom otherwise | Boom SWANTBLS (`DEFSWANI.DAT` columns: `speed last first`, `episode texture1 texture2`) | Boom `ANIMATED` and `SWITCHES` lumps |
| Quake II | ericw-tools `.wal_json` | The WAL's own header, rewritten without touching its pixels |
| Quake | None: the texture name sets animation, liquids and skies | Read only |

## Engine Rules

The rules below were checked against the GPL source releases listed in
[Credits](CREDITS.md#materials-shaders-and-textures-2026-10-08).

- **Quake III** follows ioquake3. An unknown keyword, noise outside `rgbGen`,
  more than four tcMods or a missing image drops the whole shader, and the
  engine draws its default shader instead; the preview does the same and says
  why. Default `rgbGen` is `identityLighting` for opaque and `GL_ONE`/
  `GL_SRC_ALPHA` source blends, depth writes follow the blend unless
  `depthWrite` is given, and sort defaults follow `polygonOffset`, fog, sky
  and blending. Waves use the engine's 1024-entry tables, noise its seeded
  table, and every tcMod, deform, tcGen and colour generator its formula.
  Overbright lighting halves stages and doubles the display. Within one file
  the first definition wins; across files the alphabetically last script
  wins (ioquake3 reads scripts in reverse order). Vanilla Quake III reads
  them in forward order, which the library does not model.
- **Doom 3** parses expressions right-associatively at every level, as
  `idMaterial` does, with tables appending their first value and the `%`
  operator truncating. An unknown keyword defaults the material. Interactions
  pair bump, diffuse and specular stages as the engine flushes them, light
  colour is twice the light stage, and shading is either the ARB2 interaction
  with the specular table or the BFG `pow(N.H, 10)` form. The first decl of a
  name wins.
- **Doom** composes wall textures from patches, keeps flats as indices, and
  shades through `COLORMAP` by sector light, distance and fake contrast.
  Vanilla wraps rows at 128 (tutti-frutti on short textures); Boom wrapping
  is an option. Animations come from ANIMDEFS, then Boom `ANIMATED`, then the
  vanilla table; switches from Boom `SWITCHES`, then the vanilla list.
- **Quake** gathers `+0`..`+9` and `+a`..`+j` frames at 0.2 s each, warps
  liquids, draws two-layer skies and fullbright colours, and lights through
  styles at 10 Hz, with GLQuake, modern-port and software renderers.
- **Quake II** follows the WAL next-frame chain at 2 Hz, flowing and warping
  surfaces, `trans33`/`trans66`, the `intensity` setting and env sky boxes.

## The Workbench

The page lists every material of the open package (or loose script) as
swatches, filtered by the studio query language over the same properties the
CLI's `--where` reads. Choosing one shows:

- a live preview, playing by itself when the material animates (unless
  reduced motion is on), with play, restart, speed, a time scrubber, the
  preview shape, and the engine's own view options (overbright, lightmap
  brightness and surface type for Quake III; sector light, distance, fake
  contrast, Boom wrapping and switch state for Doom; renderer, light style and
  alternate frames for Quake; intensity for Quake II; interaction shading,
  light orbit, specular, ambient light, light colour and the developer
  `_default` for Doom 3). Dragging orbits, Shift+drag moves the light, the
  wheel zooms; the arrow keys, plus, minus, Home, Space, comma and full stop
  do the same from the keyboard. Frames resize to keep playback near 30
  frames a second;
- the **Nodes** tab: the graph and the selected node's properties, with Add
  Node by category, Remove (Delete), Move Earlier and Later (Alt+Up and
  Alt+Down), operator wrapping for Doom 3 expressions, and Enter to edit;
- the **Text** tab: the script with highlighting and the parser's problems
  underlined;
- **Images**: every image the material reads, where the engine found it, and
  what is missing;
- **Problems**: what the engine would warn about or reject, with the line;
- **Details**: the summary and the other materials sharing its images.

Saving writes a loose script in place, or stages the change into the open
package through the package staging model, so it joins the package's review,
undo and save. **New Material** starts from a template (`material templates`
lists them) in the current script, or in a new `scripts/` or `materials/`
script.

Scans, image decoding and frames run off the GUI thread; a newer request
replaces an older one, and nothing scans until the page is shown.

## Accessibility And Localisation

The graph is exposed to screen readers as a list of nodes, each with its
title, what feeds it and what it feeds; selection changes are announced.
Every control has a name and a description, ports differ by shape as well as
colour, swatch badges use shapes (a triangle for animated materials, an
exclamation for dropped ones), and node text scales with the text size
preference through the graph's zoom. Right to left, the graph's flow and the
arrow keys mirror. All text is translatable.

## Limits And Integration Gaps

- Renders are CPU approximations: no real lightmaps or bump-mapped geometry
  from a map, no Doom 3 GLSL or ARB fragment programs beyond the interaction,
  and no RoQ/CIN video (a moving placeholder stands in).
- Quake III dialects: Enemy Territory's `implicitMap`, `implicitMask` and
  `implicitBlend` are drawn as Enemy Territory draws them, and the other
  Wolfenstein and Jedi Knight keywords (fog, sun, light grid, compression,
  surface sprites, surface materials) are read with a warning that vanilla
  Quake III would drop the shader, but are not drawn. There is no per-game
  dialect setting yet.
- The Doom texture list comes from the open package itself: a PWAD's
  textures are not merged with its IWAD's.
- **Show in Materials** on the Textures page lists the materials reading an
  image, and in the Levels texture list selects the material a map names.
  The other direction is open: the workbench does not list the maps that use
  a material, and the Levels 3D view does not draw material effects.
- Graph edits cannot rename a material; rename it in the text.
