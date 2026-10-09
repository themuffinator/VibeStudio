# Models

The **Models** page previews the models in a package and leads to three
editors: the **Mesh Editor** for geometry, UVs, frames, tags and skeletal
models, **Design Prop** for building simple props from primitives, and
**Assemble** for linking models at their tags.

> [!NOTE]
> **Status: Partial.** Models from Doom source ports to the Doom 3 family
> open and preview, and MDL, MD2, MD3, MD5, IQM, ASE and OBJ can be edited and
> exported. Everything has automated tests, but the decoders have only been
> tested against files built by those tests, not real game files, and the
> modeller's [release checklist](../MODELLER_RELEASE.md) is not met yet.

## Preview models

Choose **Models** on the rail, or press <kbd>Ctrl</kbd>+<kbd>3</kbd>
(<kbd>Cmd</kbd> on macOS). The **Models** sidebar lists the models in the open
package, so open a package or a folder first (see [Packages](packages.md));
**Go to File** (<kbd>Ctrl</kbd>+<kbd>P</kbd>) opens a loose model's folder and
selects it. The filter accepts words and terms such as `ext=md5mesh size>64kb`.

Select a model to load it in the background; **Cancel Preview** stops a slow
one. The viewport draws with your graphics card through OpenGL or Vulkan
(**Settings** > **Appearance and Language** > **3D Rendering**; see
[3D views stay empty](troubleshooting.md#3d-views-stay-empty) if it says it
cannot draw):

- Choose **Textured**, **Flat shaded** or **Wireframe**, and tick **Grid**,
  **Axes**, **Edges** or **Cull backfaces** (turn culling off for single-sided
  models such as flags).
- The view orbits, pans and zooms with the controls of your modeller profile
  (see [Choose your controls](#choose-your-controls)); hover over the view to
  see them. With the viewport focused, arrow keys orbit, <kbd>Shift</kbd>+arrow
  keys pan, <kbd>+</kbd> and <kbd>-</kbd> zoom, <kbd>Home</kbd> frames the model
  and <kbd>0</kbd> resets the view.
- Pick an animation from the list, then **Play** or <kbd>Space</kbd>.
  <kbd>Page Up</kbd> and <kbd>Page Down</kbd> step through frames, and the fps
  box sets the preview speed without changing the file.

The sidebar on the other side has four pages: **Summary** lists frames,
surfaces, tags, vertex and triangle counts, animations and skin paths;
**Skin** changes the preview's materials; **Metadata** shows header fields and
raw details; and **Skeleton** lists the joints, skeletal clips and companion
files of skeletal models.

## Supported formats

| Games | Formats | Edit and write |
| --- | --- | --- |
| Doom source ports (GZDoom, Zandronum, Eternity) | KVX voxels, plus MD2 and MD3 through MODELDEF | MD2, MD3 |
| Quake, Hexen II, Quake II, Heretic II | Quake MDL, Hexen II MDL, MD2, Heretic II FM | MDL, MD2 |
| Half-Life and its mods | Half-Life MDL, with its texture and sequence files | Read only |
| Quake III Arena, Team Arena, Elite Force, Darkplaces | MD3, MDR, IQM | MD3, IQM |
| Return to Castle Wolfenstein, Enemy Territory | MDC, MDS, MDM with its MDX | Export as MD3, which both games load |
| Jedi Outcast, Jedi Academy, Soldier of Fortune II | Ghoul 2 GLM with its GLA | Export as MD3 |
| Doom 3, Quake 4, Prey, Quake Wars, The Dark Mod | MD5 mesh and animation, LightWave LWO, ASE | MD5 mesh, MD5 animation, ASE |
| Any modeller | Wavefront OBJ | OBJ (one frame) |

Some models need other files to be complete. VibeStudio looks for them in the
same package, or in the folder of a loose file and the folders above it:

- An MDM needs its MDX, and a GLM its GLA. Without them the model cannot be
  posed and does not open; the message says where it looked.
- An MD5 mesh picks up every `.md5anim` beside it, and the animations a Doom 3
  `.def` file declares for it, under their declared names.
- A Half-Life model reads `<name>T.mdl` for textures and `<name>01.mdl` onwards
  for animation. Missing ones are named in a warning.

Jedi Outcast player models on the Jedi Academy skeleton are refused:
Jedi Academy remaps them with a table VibeStudio cannot include for licensing
reasons, so open them with the Jedi Outcast `_humanoid.gla`.

<details>
<summary>Every format, with what it keeps</summary>

Run `vibestudio --cli model formats` for the same list with its notes. The
[native formats record](../MODEL_FORMATS.md) and the
[support matrix](../SUPPORT_MATRIX.md) have the detail.

| Format | Extension | Kept on reading |
| --- | --- | --- |
| Quake MDL | `.mdl` | Every pose, frame groups and timing, indexed skins and skin groups, header fields |
| Hexen II MDL | `.mdl` | Poses, frame groups, skins, separate texture coordinates, model flags |
| Quake II MD2 | `.md2` | Every pose, skin names and skin size |
| Heretic II FM | `.fm` | Frames, skins, mesh nodes as surfaces |
| Quake III MD3 | `.md3` | Surfaces, shaders, every frame and tag |
| MDC | `.mdc` | Base and compressed frames, tags, shaders |
| MDS | `.mds` | Bones, weights, frames, tags |
| MDM and MDX | `.mdm`, `.mdx` | Surfaces and weights; bones and frames |
| MDR | `.mdr` | Bone matrices for every frame, the first level of detail, tags |
| Ghoul 2 | `.glm`, `.gla` | Surfaces and their hierarchy, bolts as tags, weights; the skeleton and frames |
| Inter-Quake Model | `.iqm` | Meshes, joints, weights, animations |
| MD5 | `.md5mesh`, `.md5anim` | Joints, meshes, weights, shaders; hierarchy, bounds and frames |
| LightWave | `.lwo` | The first layer: points, polygons, UV maps, surface names |
| ASCII Scene Export | `.ase` | Objects, materials, mapping |
| Half-Life MDL | `.mdl` | Bones, sequences, body parts, embedded textures, attachments |
| KVX | `.kvx` | The first mip level, as coloured faces |
| Wavefront OBJ | `.obj` | Polygons, UVs, normals, smoothing groups, `usemtl` paths |

</details>

## Skins in the preview

Skins come from the open package: the path the model names, then the same name
with a `.pcx`, `.tga`, `.jpg`, `.png`, `.wal` or `.lmp` extension. MDL,
Half-Life and KVX models carry their own skins or colours. Indexed skins use
the palette chosen on the **Textures** page (see
[Textures and sprites](textures.md#choose-a-palette)).

The **Skin** page changes the preview only: choose a **Preview surface**,
**Material slot**, **MDL skin** or **Skin member**, apply a package `.skin`
file with **Skin File…**, or **Reset**. To change what a model really uses,
edit its materials in the Mesh Editor.

## Skeletal models

MD5, IQM, MDS, MDM, MDR, Ghoul 2 and Half-Life models animate through joints.
VibeStudio poses every skeletal animation into ordinary frames, so you can
preview, edit and export them like any other model, and the skeleton is kept
alongside:

- The **Skeleton** page lists the joints as a tree and the skeletal clips.
- Edit the bind pose and the vertices you move are re-bound to their joints.
  The bind pose is the frame named `bindpose`, or the first frame when a
  format's first frame is already the bind pose; edits in other frames are not
  re-bound. Added geometry takes its weights from the nearest original vertex.
- **Save** keeps the skeleton in the `.mesh.json` source.
- Export as MD5 or IQM to keep the joints, or as MD3, MD2 or MDL to bake the
  animation into frames.

A model with more than 1,024 frames of animation keeps as many whole clips as
fit for editing, and names the rest; the skeleton keeps them all. Joints and
weights cannot be edited yet.

## Edit a mesh

1. Select a model and choose **Mesh Editor** on the page header. With no model
   selected it starts a new mesh; **Open / Import…** in the editor opens any
   format above, or a `.mesh.json` source.
2. Select faces, vertices or edges in the view or the outliner, and edit them
   with the tools below.
3. Choose **Save** to keep an editable `.mesh.json` source. Game files are
   exported separately (see [Export and build game files](#export-and-build-game-files)).

### Find your way around

The editor is laid out like the Levels page:

- The **outliner** sidebar lists surfaces, frames, tags, collision volumes and
  joints; its **Add** page holds primitives.
- The view sits in the middle, one view or four, with the material row above
  it and the timeline below.
- The property sidebar holds **Item**, **Tool**, **Surface**, **Animation**,
  **Skeleton**, **Collision**, **Quake MDL**, **Export**, **Health** and
  **View**. Choose a tab to open its page and choose it again to close it.

On the **View** page, **Layout** switches between **One view** and **Four
views**, and each of the four panes can show Perspective, Top, Bottom, Front,
Back, Left or Right, named in its corner. Click a pane to make it the one you
work in; it gets an accent border.

### Choose your controls

On the **View** page, **Controls like** sets how the editor answers the mouse
and keys:

| Profile | Navigation | Transforms | Layout |
| --- | --- | --- | --- |
| **VibeStudio (Blender-style)** | Middle-drag orbits, <kbd>Shift</kbd>+middle pans, <kbd>Ctrl</kbd>+middle zooms; <kbd>Alt</kbd>+left drag also orbits | <kbd>G</kbd>, <kbd>R</kbd>, <kbd>S</kbd> start at once | One view |
| **Blender** | As above, without the <kbd>Alt</kbd> drag | As above | One view |
| **3ds Max** | Middle-drag pans, <kbd>Alt</kbd>+middle orbits, <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+middle zooms | <kbd>Q</kbd>, <kbd>W</kbd>, <kbd>E</kbd>, <kbd>R</kbd> pick a tool, then drag | Four views: Top, Front, Left, Perspective |
| **MilkShape 3D** | Left drag rotates the 3D view, <kbd>Ctrl</kbd>+left drag pans, <kbd>Shift</kbd>+left drag zooms | <kbd>F1</kbd> to <kbd>F4</kbd> pick a tool, then drag | Four views: Front, Top, Right, Perspective |

Each profile also renames and reorders the sidebar pages the way its editor
does: Blender says **Material**, **Armature** and **Physics**; 3ds Max has
**Create**, **Modify**, **Hierarchy**, **Motion** and **Utilities**; MilkShape
3D has **Model**, **Groups**, **Materials** and **Joints**. The modeller follows
your level editor profile until you choose one here.

- **Show Every Gesture and Key…** lists the whole profile.
- **Customise Controls…** changes any key, mouse gesture, transform style or
  layout, warns when two collide, and saves only what you changed. **Reset**
  goes back to the profile.
- **Export Controls…** saves your changes as a file to share, and
  **Import Controls…** loads one.

Not every tool of those editors exists here. Each profile says what it leaves
out when you hover over it; the [modeller profiles record](../MODELLER_PROFILES.md#coverage-gaps)
lists the gaps.

<details>
<summary>Selection and tool keys with the Blender-style controls</summary>

| Action | Keys |
| --- | --- |
| Whole surfaces or their components | <kbd>Tab</kbd> |
| Vertex, edge or face select mode | <kbd>1</kbd>, <kbd>2</kbd>, <kbd>3</kbd> |
| Add to or remove from the selection | <kbd>Shift</kbd>+click |
| Box or circle select | Drag on empty space or <kbd>B</kbd>; <kbd>C</kbd> |
| Edge loop, edge ring | <kbd>Alt</kbd>+click, <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+click |
| Linked geometry | <kbd>L</kbd> under the pointer, <kbd>Ctrl</kbd>+<kbd>L</kbd> from the selection |
| Select all, none, invert | <kbd>A</kbd>, <kbd>Alt</kbd>+<kbd>A</kbd>, <kbd>Ctrl</kbd>+<kbd>I</kbd> |
| Hide selected, hide the rest, reveal | <kbd>H</kbd>, <kbd>Shift</kbd>+<kbd>H</kbd>, <kbd>Alt</kbd>+<kbd>H</kbd> |
| X-ray (select hidden parts) | <kbd>Alt</kbd>+<kbd>Z</kbd> |
| Move, rotate, scale | <kbd>G</kbd>, <kbd>R</kbd>, <kbd>S</kbd> |
| Extrude, inset, duplicate | <kbd>E</kbd>, <kbd>I</kbd>, <kbd>Shift</kbd>+<kbd>D</kbd> |
| Loop cut, bevel vertices, make face | <kbd>Ctrl</kbd>+<kbd>R</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>B</kbd>, <kbd>F</kbd> |
| Proportional editing | <kbd>O</kbd> |
| Four views, maximise the view | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Q</kbd>, <kbd>Ctrl</kbd>+<kbd>Space</kbd> |
| Show or hide the sidebars | <kbd>N</kbd> |
| Search Mesh Editor commands | <kbd>F3</kbd> |
| Adjust Last Operation | <kbd>F9</kbd> |

</details>

While a transform runs, <kbd>X</kbd>, <kbd>Y</kbd> or <kbd>Z</kbd> constrains it
to an axis, typing a number sets an exact value, <kbd>Ctrl</kbd> snaps and
<kbd>Shift</kbd> slows the pointer. <kbd>Enter</kbd>, <kbd>Space</kbd> or a
click confirms; <kbd>Esc</kbd> or a right-click cancels and leaves the mesh
untouched. With the 3ds Max and MilkShape 3D controls, pick a tool and drag the
selection instead. Every change is one undo step. Changes to the mesh's
structure, such as extrusions and cuts, apply to every animation frame; moves
can apply to **All frames** or the **Current frame**.

The menus (**View**, **Select**, **Add**, **Mesh**, **Vertex**, **Edge**,
**Face** and **UV**) hold the rest: primitives, merging, dissolving, bisecting,
symmetrising, smoothing, solidify, decimation, normals, UV projections, seams
and packing. The property pages cover the details:

- **Item** and **Tool**: exact transforms, pivots and transform axes, and the
  settings of the active tool.
- **Surface**: UVs, seams, materials, **Manage Surfaces…**,
  **Manage Material Slots…** and **Apply .skin File…**.
- **Animation**: frames, named clips, attachment tags and generated in-between
  frames.
- **Skeleton**: joints and skeletal clips (see [Skeletal models](#skeletal-models)).
- **Collision**: static collision boxes (see [Add collision](#add-collision)).
- **Quake MDL**: indexed skins, frame groups, timing and header flags.
- **Export**: the package path, MD2 skin size and level origin used for export
  and staging.
- **Health**: reports duplicate faces, unused vertices, inconsistent winding,
  open boundaries and intersecting faces, with one-step repairs for several of
  them.

The header's export budget button says whether every surface fits the original
MD3, MD2 and MDL limits, and offers **Decimate to MD3 Budget (2,000 Triangles)**
and **Export Quake III Detail Levels…**. Tick **Keep local recovery copies** to
checkpoint unsaved work; **Recover…** restores a copy as an unsaved draft.

## Design a prop

**Design Prop** builds a static prop from up to 32 boxes, cylinders and planes:

1. Add parts, then set their size, position and rotation in the **Part** tab.
2. In the **Surface** tab, set each part's material path and UV scale, offset
   and rotation. **UV checker** previews tiling without project textures.
3. Choose **Save Design…** to keep the editable `.model.json` source.
4. Choose **Export MD3…** or **Export OBJ…**, **Stage in Package**, or, with a
   Quake III map open, **Stage and Place** to also add an undoable `misc_model`
   entity.

Props have one frame and no animation, tags or collision mesh. MD3 vertices must
stay within -512 to about 512 units of the origin. **Edit as Mesh** turns the
design into an editable mesh for further work.

## Assemble models at tags

**Assemble** links models through their attachment tags, each part with its own
frame range, speed and phase. Choose **Add Part**, pick the model, parent tag
and offset, then **Apply**. Save the links as an `.assembly.json` file. From
there you can bake one pose into the Mesh Editor (**Bake Pose to Mesh**),
export a pose as OBJ, MD2 or MD3 or a sampled animation as MD2 or MD3, write a
Quake III `animation.cfg` (**Native Animation…**), and review and export a
Quake III player PK3 (**Player Package…**). Assemblies link separate models;
they are not skeletal animation, and game-side animation set-up is still up to
you.

## Add collision

The Mesh Editor's **Collision** page creates static, oriented collision boxes:
add or fit a box to the selection, then move, rotate and size it.
**Export Collision Map…** writes the boxes as brushes in a separate `.map`
file, and **Place Collision in Level** adds them to the open Quake, Quake II or
Quake III map as one undoable edit. Placed brushes do not follow later edits to
the prop.

## Export and build game files

| From | How |
| --- | --- |
| Models page | **Export OBJ** writes the current frame of the selected model. |
| Mesh Editor | **Export MD3…**, **Export MD2…**, **Export MDL…** or **Export OBJ Frame…**; **Export Other Format** for **MD5 Mesh…**, **MD5 Animation of Clip…**, **Inter-Quake Model (IQM)…** and **ASE Frame…**; **Stage in Package** or **Stage and Place** for a package and map. |
| Command line | `model build` turns a `.mesh.json` or `.model.json` source into any of those formats; `model lod` writes Quake III detail levels. |

Each export checks the format's limits and reports what it had to leave out:

- MD3 keeps every frame and tag; the original Quake III renderer allows 1,000
  vertices and 2,000 triangles per surface.
- MD2 keeps every frame but needs a single surface (up to 2,048 vertices and
  4,096 triangles), and refuses models with tags.
- MDL keeps indexed skins and frame groups on a single surface (up to 1,024
  vertices and 2,048 triangles), and also refuses tags.
- MD5 mesh and IQM keep the joints and weights; a model without joints gets a
  single origin joint (MD5) or is written as a static mesh (IQM). They animate
  through joints, so edits to frames other than the bind pose do not carry
  over; export MD3 to keep those.
- MD5 animation writes the skeletal clip the current frame belongs to.
- ASE and OBJ write one frame. OBJ has no material library and leaves out
  tags.

MD3 models can be placed in Quake III maps as `misc_model` entities; placing
other models in a level is game-specific and not automated.

## Known limits

- No model format has been tested against real game files in bulk, and no
  MD5, IQM or ASE export has been loaded in the original games yet.
- MDC, MDS, MDM, MDR, Ghoul 2, Half-Life, Hexen II, Heretic II, LightWave and
  KVX models are read only; export them as MD3 or another written format.
- Joints and weights cannot be edited: there is no bone display, pose mode or
  weight painting. Game animation scripts (Jedi Academy `animation.cfg`,
  Wolfenstein scripts, GZDoom MODELDEF) are not read.
- Edge bevel, knife and spin tools and modifiers are not available yet.
- The [modeller release checklist](../MODELLER_RELEASE.md) lists what is still
  open.

## Command-line equivalents

```sh
vibestudio --cli model inspect ./id1/pak0.pak progs/player.mdl --json
vibestudio --cli model import ./prop.obj --output ./prop.mesh.json --dry-run --json
vibestudio --cli model build ./prop.mesh.json --output ./build/prop.md5mesh --dry-run --json
vibestudio --cli model controls --profile 3ds-max --section keys
```

<details>
<summary>All model commands</summary>

| Task | Command |
| --- | --- |
| Inspect a model, or export one frame as OBJ | `model inspect`, `model export` |
| List the formats the studio reads and writes | `model formats` |
| Import a model or design as an editable mesh | `model import` |
| Edit, select or run a mesh tool | `model edit`, `model select`, `model tool` |
| Check topology, UVs, intersections, or repair an import | `model topology`, `model uv`, `model intersections`, `model repair-import` |
| Surfaces, material slots, skins and appearances | `model surfaces`, `model slots`, `model skin`, `model materials` |
| Tags, clips and collision | `model tags`, `model animations`, `model collision` |
| Quake MDL skins and timing | `model mdl` |
| Assemblies | `model assembly` |
| Build game files or detail levels | `model build`, `model lod` |
| Controls profiles, and print, check, share or reset controls | `model profiles`, `model controls` |
| Recovery copies | `model recoveries`, `model recover` |
| Place an MD3 in a Quake III map | `map place-model` |

</details>

See [Command line](cli.md) for options and exit codes.

## Learn more

- [Mesh tools](../MODEL_TOOLS.md) and [editable meshes](../MODEL_MESH.md): every tool, limit and CLI option.
- [Native formats and skeletons](../MODEL_FORMATS.md) and [modeller profiles](../MODELLER_PROFILES.md).
- [Model design](../MODEL_DESIGN.md) and [model assemblies](../MODEL_ASSEMBLY.md): props and linked models.
- [Model collision](../MODEL_COLLISION.md) and [material slots](../MODEL_MATERIAL_SLOTS.md).
- [Modeller release checklist](../MODELLER_RELEASE.md): what has and has not been verified.
