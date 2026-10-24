# Models

The **Models** page previews the models in a package and leads to three
editors: the **Mesh Editor** for geometry, UVs, frames, tags and skeletal
models, **Design Prop** for building simple props from primitives, and
**Assemble** for linking models at their tags.

These editors open as tabs within **Models**. Use **Browser** to return to the
asset list; switching tabs or studio pages keeps your current work open.

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
  re-bound.
- Mesh, UV, surface and frame edits keep each vertex's weights from the vertex
  it came from, never from where it ends up:
  - Cuts such as **Loop Cut**, **Subdivide** and **Bisect** interpolate the
    weights of the edge they split.
  - Copies made by **Extrude**, **Inset** or **Solidify** keep their source's
    joints, even next to another part, and turn with them.
  - **Merge** and **Weld by Distance** give merged vertices the equal blend of
    their weights; with **Preserve Seams** on, differently weighted vertices
    are not welded.
  - Separated or joined surfaces keep their own weights.
  - **Symmetrize** asks for each left and right joint pair (**Detect Pairs**
    fills them from the joint names).
  - **Add Primitive** on a skinned surface asks how the new vertices follow the
    skeleton: rigidly to a joint or to the nearest bones (optionally as a
    surface of its own), or as a new unskinned surface. **Repeat Last** asks
    again.
  - Interpolated vertices can end up with more joints than the surface had;
    the status line counts them, and **Reduce and normalise weights** limits
    them before exporting to a format with a lower limit.

  The status line names interpolated, mirrored, rebound, merged and bound
  weights. An edit that cannot say where a skinned vertex came from is refused.
  See [Skin weights through mesh edits](../MODEL_SKIN_PROVENANCE.md) and the
  [tool-by-tool table](../MODEL_SKINNING.md#weights-through-other-tools).
- While a surface is skinned, **Duplicate Frame**, **Delete Frame** and **Copy
  Full Pose** are disabled on the bind frame and on frames baked from a
  skeletal clip, and **Insert In-between Frames** between two such frames;
  their tooltip says why. Use **Skeleton** > **Edit Skeletal Animation** to
  resize, copy or interpolate clip poses instead. On frames you edited
  independently they work as usual, and the other frames keep their clips.
  Once every surface is unbound they work on every frame.
- **Save** keeps the skeleton in the `.mesh.json` source.
- Export as MD5 or IQM to keep the joints, or as MD3, MD2 or MDL to bake the
  animation into frames.

The Mesh Editor holds at most 1,024 frames and 1,048,576 frame vertices
(vertices times frames). A larger skeletal model opens with a **working set**:
the clips that fit, in file order, baked into editable frames. This applies
whether you open the file with **Open / Import…** or choose **Mesh Editor** for
a model selected in **Models**. The status line says what was baked and what was
left out, and **Skeleton** shows the working set with a budget meter. The
skeleton keeps every clip, and MD5 and IQM exports write them all; MDL, MD2,
MD3 and OBJ exports name the clips they leave out.

To change it, open **Skeleton** > **Working Set…**, check the clips to bake and
watch the meter, then choose **Apply** or press Enter; **Fit Automatically**
and **Bind Pose Only** set the checklist for you, and **Show Details** lists the
full report. A clip too long to bake cannot be checked.
It is one undoable edit, and a clip whose frames you edited on their own stays
until those edits are undone. **Two-Bone Inverse Kinematics**, **Animation
Curves**, **Edit Animation Keys** and **Transfer Animation Range** list clips
outside the working set too and preview them one sample at a time. See
[the skeletal working set](../MODEL_SKELETAL_WORKING_SET.md).

### Edit joints and weights

Open **Skeleton** > **Edit Skeleton** in the Mesh Editor. Add or rename joints,
reparent them while preserving poses, or delete an unused joint without children
or attachments. The first joint binds every vertex of a single-frame model.
Existing vertex animation is not converted implicitly.

To assign weights, select vertices, edges or faces on one skinned surface,
choose **Assign vertex weight**, then choose a joint and a weight from zero
to one. Other influences share the remainder proportionally. Zero removes this
influence; one assigns the vertices entirely to the joint. **Apply** validates
one undoable edit and updates the animation preview.

Weight edits protect independent frame/normal edits and require a retained bind
pose. Validation explains unsupported imported offsets, missing skinning or
changed clip ranges. Save keeps joints/weights in `.mesh.json`; MD5/IQM exports
retain them and MDL/MD2/MD3 exports use the baked frames. Bone overlays and
viewport joint handles and [staged weight painting](../MODEL_WEIGHT_PAINT.md) are available.
See [Skeleton authoring](../MODEL_RIG.md)
for precise limits and `model rig` CLI examples.

### Inspect weights and bind surfaces

Open **Skeleton** > **Skin Weights and Binding…**. **Inspect weights** shows
each vertex's joint weights, sum and issue labels. Choose a weight range or
issue filter and **Select vertices** to return those components to the editor.

For an edit, choose **Bind vertices**, **Rebind existing weights**, **Reduce and
normalise weights** or **Unbind surfaces**, then choose **Review**. Check the
proposed deformation at different preview frames and inspect the **Weights**
table. **Apply** commits one undo step; changed options require another review.

Binding supports one rigid joint or distance weights from nearby joint origins
or bones. First binding and unbinding require complete surfaces. Replacing
existing weights requires its checkbox. Independent frame/normal edits are
protected; attachments and collision keep their own tracks. Rebinding explicitly
reconstructs unusual imported offsets, so review its animation carefully.
The [binding reference](../MODEL_SKINNING.md) explains limits, repairs, export
handoff and the `model skinning` CLI. [Staged weight painting](../MODEL_WEIGHT_PAINT.md) adds brushes and numeric row editing.

### Create skeletal animation

Open **Skeleton** > **Edit Skeletal Animation**. **Position bind joint** moves
a joint's rest position while keeping the model and existing animation in place.
Create a clip with **Add skeletal clip**, choose its pose count and FPS, then
use **Transform joint pose** to move or rotate a joint at the desired poses.
Indices start at zero within that clip. **Move descendant joints** keeps the
branch together; model and local axes are available.

Choose **Interpolate poses** and an inclusive endpoint range to fill the poses
between them. This blends the joint hierarchy along rotation arcs. **Copy whole
pose** copies all joints from another sample or the bind pose. Apply returns to
the editor; choose the corresponding clip in **Animation** to preview it.

The dialog also duplicates, renames, resizes and deletes clips. Extending holds
the last pose; shrinking removes the tail. Existing custom ranges, manual tags,
collision tracks and native MDL timing follow structural edits. Pose transforms
leave manual tags and collision in place; refit collision when required. Edits
that would overwrite independent geometry or attachment changes are refused.

Save preserves all joint samples. MD5 animation and IQM exports use those samples;
MDL, MD2 and MD3 use the baked preview frames. Native format limits still apply.
Viewport bone manipulation, [two-bone IK](../MODEL_IK.md) and
[animation curves](../MODEL_ANIMATION_CURVES.md) are available; persistent
constraints remain planned. The
[skeletal authoring reference](../MODEL_RIG.md#author-skeletal-animation) includes
`model pose` automation and exact limits.

## Edit a mesh

1. Select a model and choose **Mesh Editor** on the page header. With no model
   selected it starts a new mesh; **Model > Open / Import…** in the editor
   opens any format above, or a `.mesh.json` source.
2. Select faces, vertices or edges in the view or the outliner, and edit them
   with the tools below.
3. Choose **Model > Save** (<kbd>Ctrl</kbd>+<kbd>S</kbd>) to keep an editable
   `.mesh.json` source. Game files are exported separately (see
   [Export and build game files](#export-and-build-game-files)).

### Save and review output

The mesh editor checks that a loaded source still has its original contents and
resolved path before saving over it. If another application changes or removes
the file, reload it or save to another path. Saving through a hard-link alias of
the loaded source is also refused; use its original path.

CLI authoring commands use the same checks with `--dry-run`, including complete
serialisation and the 64 MiB editable-source limit. A dry run leaves files,
folders and save locks untouched. It does not reserve the destination or test
write permissions, so a subsequent write can still fail.

### Check source size and undo history

Beside **Keep local recovery copies**, a summary gives the size of the source
against its 64 MiB limit, and how many undo steps are kept and the memory they
hold; hover over it, or read it with a screen reader, for the full account. The
**Undo** and **Redo** tooltips repeat the steps available.

- Sources stay readable JSON while they fit. A larger model, such as a long
  vertex animation or a skeletal model with many clips, saves its geometry and
  joint matrices as packed arrays (source version 12), about half the size, and
  the status line says so after **Save**, since older builds cannot read it.
  Whole-number geometry and simple joint matrices can be smaller as readable
  JSON, so such a model saves readable when only that form fits. `model import`
  and `model edit` report the size and `"encoding"` in `"source"`, and the
  skeletal commands in `"outputSource"`.
- An edit that would take the source over 64 MiB is refused and nothing changes.
  Delete frames, clips, curve keys or embedded skins, or bake fewer clips into
  the [skeletal working set](../MODEL_SKELETAL_WORKING_SET.md). A model opened
  over the limit can be reduced, but it cannot be saved or recovered until it
  fits; recovery copies pause and say why.
- **Model** > **Undo History Limit…** sets the memory undo and redo may keep for
  each open model, 64 MiB by default (16–4,096 MiB); the status strip shows the
  source size and the history in use. A step costs only what it changed, so
  moving vertices in one frame costs that frame. When an edit fills history the
  oldest undo steps are discarded and the editor says so once; the step before
  the current one and every redo step are always kept, so **Undo** and **Redo**
  never lose work. A typed value applies when you press Enter or leave the
  field; a lower limit discards old steps at once and says so.

See [Editable Meshes](../MODEL_MESH.md#sources-import-and-export) for the source
format and how history memory is counted.

Existing CLI outputs require `--overwrite`, except ordinary in-place collision
edits. Most authoring commands can save back to the loaded `.mesh.json` with
that flag; `model tool`, `model curve-transfer` and `model uv-density` require
separate outputs. Imports and native exports protect their sources. Stroke
recipes, joint maps, replacement curve files and knife path files are protected
even with `--overwrite`, including alternate spellings and hard-link aliases.

Package-backed imports and exports also protect their archive, source folder
contents and portable draft files through hard-link aliases. Frame and assembly
exports, player packages and skin imports use the shared package checks. If a
linked destination needs a large folder scan, validation reports progress and
can be cancelled. If source protection cannot be completed, choose a new output
outside the input package, folder or draft.

### Find your way around

The editor is laid out like the Levels page:

- The menu bar starts with **Model**, which holds the document commands:
  **Open / Import…**, **Repair Import…**, **Recover…**, **Save**,
  **Save As…**, the exports, **Stage in Package**, **Stage and Place** and
  **Keep Local Recovery Copies**. The mesh menus follow it.
- The editing header above the view starts with **Undo** and **Redo**, then the
  select modes, **X-ray**, the shading (**Solid**, **Wireframe**, **UV
  Checker**, **Material**), the view preset and **Frame Model**, then
  proportional editing, symmetry, **Delivery targets** and command search.
  When the header is too narrow, the rest fold under the arrow at its end.
- The **outliner** sidebar lists surfaces, frames, tags, collision volumes and
  joints; its **Add** page holds primitives.
- The view sits in the middle, one view or four, with the timeline below
  it. **Customise Controls…** can hide the timeline for a profile.
- The property sidebar holds **Item**, **Tool**, **Surface**, **Animation**,
  **Skeleton**, **Collision**, **Quake MDL**, **Export**, **Health** and
  **View**. Choose a tab to open its page and choose it again to close it.
- The status strip along the bottom shows the last message, the package and
  level the model hands off to, and the recovery state, each on one line. A
  message too long for its space ends in an ellipsis; point at it, or reach it
  with <kbd>Tab</kbd>, to read the whole message. **Export Report…** appears there after an export.
  In **Material** shading the strip also shows the material images: how many
  are ready and how many have a problem, a progress bar and **Cancel** while
  they load, and an **Images** menu with **Reload Images** and **Details…**.

The layout keeps the view in front at large text sizes and on a laptop screen.
Sidebar text, buttons and check boxes wrap rather than widening the sidebar,
and a choice too long for its box shows in full in its list. When the view
would get less than about half the width, the property sidebar folds to its
tabs and the status strip says so; choose a tab to open it again. A sidebar you
open stays open: if both no longer fit beside the view, the other one folds.
The editor's dialogs open within your screen, with their buttons in view; a
dialog with more than fits scrolls.

On the **View** page, **Layout** switches between **One view** and **Four
views**, and each of the four panes can show Perspective, Top, Bottom, Front,
Back, Left or Right, named in its corner. Click a pane to make it the one you
work in, or press <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Page Down</kbd> (**Next
View**) or <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Page Up</kbd> (**Previous
View**); it gets an accent border and keyboard focus.

The navigation gizmo in the view's top corner (top right, or top left in a
right-to-left layout) shows the axes; click one to look along it. It keeps
clear of the corner text, the tool banner and **Adjust Last Operation**, grows
with the text size and hides in a view too small for it. **View > Navigation
Gizmo** turns it off.

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
| Edge loops, edge rings of the selected edges | <kbd>Alt</kbd>+<kbd>L</kbd>, <kbd>Alt</kbd>+<kbd>R</kbd> |
| Linked geometry | <kbd>L</kbd> under the pointer, <kbd>Ctrl</kbd>+<kbd>L</kbd> from the selection |
| Select all, none, invert | <kbd>A</kbd>, <kbd>Alt</kbd>+<kbd>A</kbd>, <kbd>Ctrl</kbd>+<kbd>I</kbd> |
| Hide selected, hide the rest, reveal | <kbd>H</kbd>, <kbd>Shift</kbd>+<kbd>H</kbd>, <kbd>Alt</kbd>+<kbd>H</kbd> |
| X-ray (select hidden parts) | <kbd>Alt</kbd>+<kbd>Z</kbd> |
| Move, rotate, scale | <kbd>G</kbd>, <kbd>R</kbd>, <kbd>S</kbd> |
| Extrude, inset, duplicate | <kbd>E</kbd>, <kbd>I</kbd>, <kbd>Shift</kbd>+<kbd>D</kbd> |
| Loop cut, bevel vertices, make face | <kbd>Ctrl</kbd>+<kbd>R</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>B</kbd>, <kbd>F</kbd> |
| Bevel edges | <kbd>Ctrl</kbd>+<kbd>B</kbd>; wheel or <kbd>Page Up/Down</kbd> changes segments |
| Proportional editing | <kbd>O</kbd> |
| Four views, maximise the view | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Q</kbd>, <kbd>Ctrl</kbd>+<kbd>Space</kbd> |
| Next or previous view in four views (every profile) | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Page Down</kbd>, <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Page Up</kbd> |
| Show or hide the sidebars | <kbd>N</kbd> |
| Search Mesh Editor commands | <kbd>F3</kbd> |
| Adjust Last Operation | <kbd>F9</kbd> |
| Next or previous panel (every profile) | <kbd>Ctrl</kbd>+<kbd>Tab</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> (<kbd>Control</kbd> on a Mac) |

</details>

### Work from the keyboard

Every profile can be used without a mouse:

- <kbd>Ctrl</kbd>+<kbd>Tab</kbd> moves to the next panel and
  <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> to the previous one: the
  sidebars, the editing header, the tool shelf, the view, the timeline and
  the status strip. On a Mac, use
  <kbd>Control</kbd>+<kbd>Tab</kbd> and
  <kbd>Control</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd>, since
  <kbd>Command</kbd>+<kbd>Tab</kbd> switches applications. They are also
  **View > Next Panel** and **Previous Panel**, and no profile or
  customisation can take them. To switch between the **3D** and **UV** tabs,
  click a tab or use the arrow keys on the tab bar.
- The tool shelf is one stop: move between its buttons with the arrow keys,
  <kbd>Home</kbd> and <kbd>End</kbd>, and press <kbd>Enter</kbd> or
  <kbd>Space</kbd> to use one.
- <kbd>Alt</kbd>+<kbd>O</kbd> opens the **Model** menu (open, save, export,
  stage and recovery), and the other menus open with their underlined
  letters. <kbd>F3</kbd> also finds every **Model** menu command by name,
  **Keep Local Recovery Copies** included.
- With the VibeStudio and Blender controls, <kbd>Tab</kbd> switches between
  whole surfaces and their components, and <kbd>Shift</kbd>+<kbd>Tab</kbd>
  toggles snapping, but only while the view has focus. Everywhere else,
  <kbd>Tab</kbd> and <kbd>Shift</kbd>+<kbd>Tab</kbd> move between controls as
  usual. Use <kbd>Ctrl</kbd>+<kbd>Tab</kbd> to leave the view.
- <kbd>F9</kbd> (or **View > Adjust Last Operation** with the 3ds Max and
  MilkShape 3D controls) opens **Adjust Last Operation** with focus on its
  first setting; <kbd>Esc</kbd> takes you back to the view and leaves the
  panel open. While a setting has focus, its own keys work as in any form:
  <kbd>Space</kbd> ticks a check box and typing edits a number, even where
  your controls give those keys to a command.
- While a tool such as **Move** runs, it keeps every key until <kbd>Enter</kbd>
  confirms or <kbd>Esc</kbd> cancels it.
- Pick edges in the outliner's component table with the arrow keys
  (<kbd>Tab</kbd> leaves the table), then use
  **Select Edge Loops of Selection** (<kbd>Alt</kbd>+<kbd>L</kbd>) or **Select
  Edge Rings of Selection** (<kbd>Alt</kbd>+<kbd>R</kbd>) to grow each one; an
  edge without a loop, such as a quad diagonal, stays as it was and the banner
  says why. In vertex mode, the edges between selected vertices grow. A large
  selection shows progress and can be cancelled. MilkShape 3D keeps these in
  the **Select** menu without keys.
- **Loop Cut** (<kbd>Ctrl</kbd>+<kbd>R</kbd>) starts across the selected edge
  when exactly one edge between quads is selected. Set the cuts with
  <kbd>+</kbd>/<kbd>-</kbd>, <kbd>Page Up/Down</kbd> or a number and press
  <kbd>Enter</kbd>. A pointer over another edge takes over until it moves away.
- In four views, <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Page Down</kbd> and
  <kbd>Page Up</kbd> make the next or previous pane active, and the screen
  reader names it.
- In the **UV** view, the arrow keys pan (<kbd>Shift</kbd> for small steps) and
  <kbd>+</kbd> and <kbd>-</kbd> zoom.
- Tooltips on the header buttons and the tool shelf, the tool hints and the
  empty **Adjust Last Operation** panel name the keys your profile gives each
  command, including your own changes, or the **View** menu entry where it
  gives none.

In **Customise Controls…**, the key field records one key. <kbd>Tab</kbd> and
<kbd>Shift</kbd>+<kbd>Tab</kbd> move on and <kbd>Esc</kbd> stops recording, so
pick those keys from **Special key** when you want a command to use them.

### Use a screen reader, high contrast or reduced motion

- The status strip's lines, the sidebar summaries and the dialogs' status
  lines read what they say to a screen reader, under their names (for
  example "Mesh editor status").
- With **Announce results to screen readers** on (Settings, **Alerts and Screen Readers**),
  the editor tells you without moving focus: exports (collision maps and
  controls included), saves, staging, a delivery target moving over its
  limits, a long operation that finished and a tool's prompt as it starts are
  announced; a refused or failed edit, export, placement or tool is announced
  at once, with its reason. A recovery copy that cannot be written is
  announced once, not at every retry. Counts after an edit, values you type
  and a tool's preview while you drag or type are not: a refused **Spin**,
  **Bevel Edges**, **Rip** or **Knife** is announced when you confirm it. With
  speech on, the same messages are read aloud: refusals as failures, a long
  operation's end with **Long tasks that finish**, and other results with
  **Status bar messages**.
- **Delivery targets** describes each format's line, the curve graph names
  the selected key, frame, value, curve and tangents, and the component
  table's **A**, **B** and **C** columns are read as the first, second and
  third vertex index.
- With a high-visibility theme, every preview, including those in the
  skeletal and animation dialogs, uses the high-contrast palette.
- With **Reduce motion** on, no preview plays: **Play / Pause** is off and its
  tooltip says why, and your play key (or **Play or Pause** in command search,
  or <kbd>Space</kbd> in a view) says why in the status strip and to the
  screen reader. <kbd>Page Up</kbd> and <kbd>Page Down</kbd> still step
  frames, and busy bars stand still and name the work instead of moving.
- Numbers in status lines, readouts, tables and the curve graph follow your
  region's format, as the number fields beside them do (in German, a vertex
  at 0.5 reads "0,5" and a clip plays at "12,5 fps"), and counts read
  "1 vertex" or "2 vertices" in each language's plural forms, in English
  until a language's own forms are translated. Copy from the component
  table, saved files and the command line keep "0.5", and so do the Quake MDL
  page's **Eye position** and **Size hint** fields, which hold the file's own
  values: type a full stop for decimals there.

While a transform runs, <kbd>X</kbd>, <kbd>Y</kbd> or <kbd>Z</kbd> constrains it
to an axis, typing a number sets an exact value, <kbd>Ctrl</kbd> snaps and
<kbd>Shift</kbd> slows the pointer. <kbd>Enter</kbd>, <kbd>Space</kbd> or a
click confirms; <kbd>Esc</kbd> or a right-click cancels and leaves the mesh
untouched. With the 3ds Max and MilkShape 3D controls, pick a tool and drag the
selection instead. Every change is one undo step. Changes to the mesh's
structure, such as extrusions and cuts, apply to every animation frame; moves
can apply to **All frames** or the **Current frame**.

The mesh menus (**View**, **Select**, **Add**, **Mesh**, **Vertex**, **Edge**,
**Face** and **UV**) hold the rest: primitives, merging, dissolving, bisecting,
symmetrising, smoothing, solidify, decimation, normals, UV projections, seams
and packing. The property pages cover the details:

- **Item** and **Tool**: exact transforms, pivots and transform axes, and the
  settings of the active tool.
- **Surface**: UVs, seams, materials, **Manage Surfaces…**,
  **Manage Material Slots…**, **Skin Variants…** (see
  [Skin variants](#skin-variants)) and **Apply .skin File…**.
- **Animation**: frames, named clips, attachment tags and generated in-between
  frames.
- **Skeleton**: joints and skeletal clips (see [Skeletal models](#skeletal-models)).
- **Collision**: static collision boxes (see [Add collision](#add-collision)).
- **Quake MDL**: indexed skins, frame groups, timing and header flags.
- **Export**: the package path, MD2 skin size and level origin used for export
  and staging, with **Stage in Package** and **Stage and Place** beside them.
- **Health**: reports duplicate faces, unused vertices, inconsistent winding,
  open boundaries and intersecting faces, with one-step repairs for several of
  them.

The header's **Delivery targets** button says whether the model is ready for
the game you are exporting to, format by format (see
[Delivery targets](#delivery-targets)), and offers **Check Delivery Targets…**,
**Decimate to MD3 Budget (1,999 Triangles)** and **Export Quake III Detail
Levels…**. **Model > Keep Local Recovery Copies** checkpoints unsaved work (the
status strip shows the last copy), and **Model > Recover…** restores a copy as
an unsaved draft.

**Edge → Edge Slide** moves selected chains or loops along surrounding quad
faces. Follow the preview rails or type a signed fraction, then confirm or
cancel. **Adjust Last Operation** also offers a distance in the reference pose.
Current/all-frame scope, seam copies, undo and CLI use the same service. UVs
stay attached to the moved vertices. See [edge sliding](../MODEL_EDGE_SLIDE.md)
for rail direction, selection requirements and limits.

**Edge → Bevel Edges** (**Ctrl+B** in VibeStudio/Blender) creates chamfers or
rounded edge bands. Type a width and change segments with the wheel or
**Page Up/Down**, then confirm or cancel. **Adjust Last Operation** adds
roundness and UV tile size. Closed corners receive caps; new skin weights
follow the cut geometry across all retained poses. Original face UVs stay
separate, while new bands/caps need atlas packing where appropriate. See
[edge bevel](../MODEL_EDGE_BEVEL.md) for width semantics and refusal conditions.

**Mesh → Knife** (**K** in VibeStudio/Blender) cuts a path into the active
surface. Click points, use **N** or right-click for another chain, then
**Enter/Space** to apply or **Esc** to discard. **C** toggles cut-through,
**S** restricts cuts to selected faces, **A** constrains screen angles,
**Shift** snaps midpoints and **Ctrl** ignores geometry snapping. Arrow keys
move the crosshair and **Insert** places points. The preview includes the
hover segment; **Backspace** removes a fixed point. Hidden neighbours retain
their visibility when subdivided. Every pose, UV seam and skin influence uses
the same cut interpolation. F9 adjusts visibility/selection scope. See
[knife cutting](../MODEL_KNIFE.md) for projection, snapping and refusal limits.

**Face → Spin** and **Edge → Spin** make curved extrusions around the **3D
Cursor**. Type degrees, choose an axis with **X/Y/Z**, and set steps with the
wheel or **Page Up/Down**. **Face → Spin Duplicates** makes radial face copies.
**Enter/Space** confirms, **Esc** cancels and **F9** adjusts the centre, axis,
steps, closure, duplication and UV scale. Detached face profiles can close a
full turn. Review every pose, face orientation and UV packing before exporting;
see [Spin](../MODEL_SPIN.md) for skinning behaviour, limits and CLI examples.

**Vertex → Rip** and **Edge → Rip** open a selected vertex or connected edge
chain; **Rip and Fill** bridges the gap. **V/Alt+V** start these in the studio
and Blender profiles. The starting pointer chooses the side; **B** swaps it
and **F** toggles filling. Type a distance, constrain with **X/Y/Z**, confirm
with **Enter**, or cancel the whole preview with **Esc**. **F9** adjusts the
offset, side face, fill and UV scale. Every retained pose is updated through
the shared service. See [Rip](../MODEL_RIP.md) for rig behaviour, CLI examples
and the current unbranched-selection limits. Confirm **Rip** immediately or
enter a zero offset to split without moving; the split survives coincident
positions, other mesh/UV tools and source reopening. **Rip and Fill** requires
movement to create gap faces. **Esc** restores the original connectivity.
Keep the `.mesh.json` source: game-format derivatives generally cannot store
the version-11 connectivity metadata. **Join Surfaces** preserves separate
input islands; **Merge** or **Weld** explicitly connects compatible vertices.

**UV → Texel Density…** (also in the UV toolbar) measures existing islands
against the original texture dimensions. Choose a reference pose, inspect the
per-island results, then preview a target density for each island or for the
combined selection. Apply creates one undo step. Review overlaps and coverage
before packing or exporting; density adjustments keep island centres fixed and
affect UVs across every pose. See [texture density](../MODEL_UV_DENSITY.md) for
material defaults, repair diagnostics, limits and CLI examples.

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

### Open an existing Quake III player

**Open Player…** turns a player of the current package or folder into a new
assembly in one step. Choose the player and one of its complete skins, then
**Open**. The assembly gets the player's three models, with upper on lower's
`tag_torso` and head on upper's `tag_head`, the chosen skin on each part and the
player's `animation.cfg` as its native animation. **Player Package…** is filled in
with the player's ID, icon, other skins and custom sounds, so you can change a skin
or fix a tag and publish the player again. A player missing a model,
`animation.cfg` or a complete skin says why it cannot be opened.

Nothing in the package changes. The new assembly is unsaved: save it beside the
package, not inside it. To bring in only an `animation.cfg`, use **Native
Animation… > Import from Package…**. See
[Opening a Quake III player](../MODEL_ASSEMBLY.md#opening-a-quake-iii-player).

### Publish a Quake III player

**Player Package…** writes one PK3 with the lower, upper and head models, a skin
set and icon for every skin, `animation.cfg`, the materials they use and,
optionally, detail levels and custom sounds. Choose the **Player ID**, **Skin ID**,
**Head part** and icon, then:

- **Team skins (red and blue)** adds the skins team games use. Without them, team
  games show the default team model instead of yours. Give at least one part red
  and blue materials, as a stored [skin variant](#skin-variants) or a `.skin` file.
- **More skins** adds a row per skin. Leave a part's cell empty to use that part's
  stored skin variant of the same name, or its own materials; type a `.skin` path
  (or `package:PATH`) to use a file. An empty **Icon** reuses the package icon.
- **Detail levels** generates one or two lower-detail versions of each part, or
  uses each part's stored [detail levels](../MODEL_LOD.md).
- **Custom sounds** takes a sound for any of the 13 player sounds (`death1` to
  `taunt`). Mono WAV is kept; other audio is converted to the 22,050 Hz mono WAV
  Quake III reads. Sounds you leave empty use the game's default sounds.

**Review Package** checks the package before you can export it: every skin must
cover every surface, and upper must have a `tag_weapon` for the player's weapon.
The review lists every file and notes anything the game will fill in, such as a
missing `tag_flag` or default sounds. See
[Native player packages](../MODEL_ASSEMBLY.md#native-player-packages).

## Skin variants

**Skin Variants…** on the **Surface** page keeps named skins with the model, such
as `red` and `blue`. A variant gives some surfaces another material; the others
keep their own (primary) material.

1. Type a **Name** and choose **Add**.
2. Select surfaces, type a **Material** and choose **Set Material**. **Use
   Primary** puts a surface back to its own material.
3. Use **Show** to see a variant in the viewport without changing the model.
4. Choose **Apply** to keep the variants as one undo step, then save the source.

**Separate selected faces** and **Duplicate surface** give the new surface what
its origin wears in every variant, as Prepare for Target's splits do, so a split
part keeps its team colour.

**Export .skin…** writes the selected variant as a Quake III `.skin`, and
**Export Doom 3 Skin…** as a Doom 3 skin declaration. Doom 3 skins swap materials
rather than surfaces, so the review explains when a variant cannot be written that
way. Player packages use a part's variants for skins of the same name. See
[Skin variants](../MODEL_SKIN_VARIANTS.md) for the rules and the
`model skin-variants` and `model skin-export` commands.

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
| Mesh Editor | In the **Model** menu, **Export MD3…**, **Export MD2…**, **Export MDL…** or **Export OBJ Frame…**; **Export Other Format** for **MD5 Mesh…**, **MD5 Animation of Clip…**, **MD5 Animations of All Clips…**, **Inter-Quake Model (IQM)…** and **ASE Frame…**; **Stage in Package** or **Stage and Place** for a package and map. |
| Command line | `model build` turns a `.mesh.json` or `.model.json` source into any of those formats (`--target` names the game); `model lod` writes Quake III detail levels; `model targets` lists the delivery targets or checks a model against them. |

Each export checks the limits of its delivery target first, and reports what
the format had to leave out:

- MD3 keeps every frame and tag; Quake III Arena and ioquake3 draw at most 999
  vertices and 1,999 triangles per surface.
- MD2 keeps every frame but needs a single surface (Quake II: up to 2,048
  positions, 4,096 triangles and 512 frames), and refuses models with tags.
- MDL keeps indexed skins and frame groups on a single surface (original Quake:
  up to 1,024 vertices and 2,048 triangles; QuakeSpasm: 2,400 and 4,096), and
  also refuses tags.
- MD5 mesh and IQM keep the joints and weights; a model without joints gets a
  single origin joint (MD5) or is written as a static mesh (IQM). They animate
  through joints, so edits to frames other than the bind pose do not carry
  over; the report lists them, and MD3 keeps them.
- MD5 joints cannot scale: the md5mesh carries bind scale in its weights, and an
  md5anim refuses a pose that scales a joint differently from its bind pose.
  IQM keeps joint scale but only four influences per vertex.
- **MD5 Animation of Clip…** asks which clip to write and at what whole frame
  rate, starting at the clip the current frame belongs to, and reviews the
  result first. **MD5 Animations of All Clips…** (**Export All Clips as MD5…** on
  the **Skeleton** page) writes one `<clip>.md5anim` per clip into a folder.
  **Stage in Package** with a `.md5anim` package path opens the same chooser,
  starting at the clip the path names, such as `walk.md5anim`.
- ASE and OBJ write one frame. OBJ has no material library and leaves out
  tags.
- MDL, MD2 and MD3 have no clip table: a game rebuilds clips from the frame
  names (`stand1`, `stand2` and so on). Clips named that way come back; other
  clip names, ranges and rates are left out, and so is a skeleton.

After an export or a package handoff the status line keeps the most important
note in view, and **Export Report…** beside the recovery controls lists every
note, with what the format left out first. To refuse such exports instead, turn
on **Refuse exports that leave data out** on the **Export** page: an export,
stage or set of detail levels that would leave data out then writes nothing,
and **Export Report…** lists what it would have left out. It matches
`model build --strict` and `model lod --strict`, and lasts while the editor is
open. See
[what each export keeps](../MODEL_FORMATS.md#what-each-export-keeps-changes-or-leaves-out)
for the details per format.

MD3 models can be placed in Quake III maps as `misc_model` entities; placing
other models in a level is game-specific and not automated.

### Delivery targets

Engines and source ports accept different amounts of the same format: the
original Quake draws an MDL of 1,024 vertices, QuakeSpasm one of 2,400, Enemy
Territory an MD3 surface of 1,024 vertices where Quake III stops at 999, and
ioquake3 loads at most 128 IQM joints. Choose the game you are making the model
for, and every export, stage and set of detail levels is checked against it
before anything is written.

1. Open the **Delivery targets** button in the Mesh Editor header. It reads
   **ready**, **check** or **over** for MD3, MD2, MDL and, for skeletal models,
   IQM and MD5, with an icon for each.
2. Choose a target in **MD3 Delivery Target**, **MD2 Delivery Target**,
   **MDL Delivery Target** or **IQM Delivery Target**, and for skeletal models
   **MD5 Mesh Delivery Target** or **MD5 Animation Delivery Target** (Q2PRO's
   MD5 replacement models, for example). Each format starts at
   the target saved with the open project, else the one the game installation
   suggests (Q2PRO for `q2pro.exe`, for example), else its original engine (for
   IQM, the format's own limits). The installation is the one the project
   links, else one of the project's game, else the selected installation, as
   for releases and `model build`. The submenu's title says where the choice
   comes from.
3. Read the blockers listed under each format. Choose one to select what it is
   about: the faces past a surface's budget, the vertices outside the skin tile
   or MD3's range, or the surface. Its tooltip gives the fix. Or choose **Check
   Delivery Targets…** to check every limit across every frame and see each
   blocker with its fix and how close each count is to its limit.
4. Fix the blockers yourself, or let the studio prepare the model (below).
5. Export. An export the target refuses writes nothing; the status line says
   how many limits block it and **Export Report…** lists each with its fix.

**check** means a count taken before the writer shares seam vertices is over a
limit, or the model is too large to scan every MD3 frame as you edit; **Check
Delivery Targets…** gives the exact answer. A choice made in the editor lasts
while the editor is open; choose **Save Targets with the Project** to keep each
format's choice, and the **Refuse exports that leave data out** setting, with
the open project, where `model build` and `model lod` use them too. If the
project manifest cannot be read, nothing is saved and the status line names the
file: fix or restore it, then save again. The targets, their limits and where
each limit comes from are listed in [delivery targets](../MODEL_TARGETS.md).

### Prepare a model for its target

1. Open **Delivery targets** and choose **Prepare for MD3 · Quake III Arena…**
   (or the entry under another format with blockers).
2. Review the changes. Depending on the format they split a surface that is over
   its budget into surfaces that fit, join surfaces into the one MDL and MD2
   hold, decimate what cannot be split, move (or, when it is too large, scale)
   a model into MD3's coordinate range with its tags, and reduce the joint
   influences IQM cannot keep. Notes say what to know, such as an origin that
   moved, the skin variants that now cover a split surface's parts, or stored
   hand-made detail levels to rebuild; the end of the list says what would still
   block the target.
3. Clear a kind of change, such as **Decimate**, to leave it out; the review is
   prepared again without it.
4. Choose **Apply Changes**. Everything listed is one undo step, and the
   changed surfaces are selected. **Cancel** leaves the model as it was.

Names, skin sizes, frame counts, joints and features too small for MD3's
precision are left for you to change; the review and **Check Delivery
Targets…** list them with their fixes.

### Detail levels

Quake III loads `name_1.md3` and `name_2.md3` beside `name.md3` and draws one
by distance; Enemy Territory also loads `name_3.md3`, and some games load none.
The source can own its detail levels, so they are reviewed, kept and staged with
the model.

1. Choose **Mesh > Clean Up > Detail Levels…**.
2. Set **Detail levels**, up to what the MD3 delivery target loads, and choose
   how each level is made: **Generated: keep a share of the triangles**,
   **Generated: reduce to a triangle count**, or **Hand-made model** with its
   file (**Browse…**). A hand-made level needs the base model's frames and
   surface names.
3. Choose **Review**. The table lists each surface's triangles per level, and
   the text below lists any problem the game would have with the set, such as
   a level with fewer frames, and the distance from which each level shows.
   **Show** displays a level in the viewport without changing the model.
4. Choose **Store in Source** to keep the levels in the source (one undo step;
   save to keep them), or **Export…** to write the base MD3 and its levels.

**Export Quake III Detail Levels…** writes the stored levels, or two halvings
when the source stores none. **Stage in Package** stages an MD3 with its stored
levels as one package step, and the level dependency review lists the levels a
package holds beside each model, so releases ship them. Every file is checked
against the delivery target before any is written; if one of the files already
exists, for example a hand-made `name_1.md3`, one question names every file
that would be replaced, and **Cancel** writes nothing. Afterwards **Export
Report…** lists each file's notes. To check a set that already exists, use
`model lod --inspect` (see [detail levels](../MODEL_LOD.md)).

Writing or staging an MD3 replaces only the files written. When the folder or
package already holds detail files the game would load with the new model, such
as a `name_2.md3` from an older set, or an `.mdc` that Return to Castle
Wolfenstein and Enemy Territory load first, the status line and **Export
Report…** name them and say whether the game would read them wrongly. Write the
detail levels again, or remove those files. When the MD3 delivery target loads
fewer levels than the source stores, or none, **Stage in Package** stages the
model with the levels the target loads, and the report says which stay in the
source.

## Manipulate joints in the viewport

Choose a joint in **Skeleton**, or select **Joints** in the component table.
Use the move, rotate and scale handles; **Geometry** provides numeric values,
snapping, axes and pivots. G/R/S choose the joint handle tool. **Transform children**
moves descendants with the selected joint; turn it off to leave children in place.

The status line identifies the bind pose or the current clip/sample. Bind changes
preserve the mesh and animation; pose changes affect just the displayed sample.
Use **Edit Skeletal Animation** for a range. Previewing leaves the document
unchanged; release commits one undo step and Escape cancels. Changes to the frame,
joint or descendant option also cancel. Independently edited skin or attachment
poses are protected. Fit collision separately after posing.

**X-ray joints** reveals joints through the mesh. Turn it off to pick only visible
origins and bone segments; wireframe always reveals them. The hierarchy and table
provide keyboard access to hidden or overlapping joints. Named joint selection
survives undo, redo and recovery. See [viewport posing](../MODEL_RIG.md#pose-joints-in-the-viewport)
for the shared CLI matrix option and current limitations.

## Paint skin weights

Open **Skeleton → Paint Skin Weights** after binding a surface. Choose a joint
and replace, add, subtract or smooth weights. Radius, falloff, visible/through
painting and a selected-row mask control coverage. Joint locks preserve existing
weights; an explicit remainder joint handles reducing a sole influence.

Release a stroke to validate and preview its skeletal poses. Escape cancels
pending coverage. **Undo stroke** and **Redo stroke** work inside the review
window; **Apply staged strokes** commits the batch as one editor undo step.
The weight table and **Paint rows** provide exact keyboard-accessible editing.
Cancel discards the staged batch. See [weight painting](../MODEL_WEIGHT_PAINT.md)
for preservation rules, recipe replay and remaining limits.

## Transfer or mirror skin weights

Open **Skeleton → Transfer Skin Weights**. Choose a bound source and a destination
surface, nearest triangle or vertex matching, and a distance limit. For symmetry,
choose a mirror axis and direction; **Joint mapping** pairs exact names or existing
suffix pairs. Destination locks preserve chosen influences. Normal filtering
helps distinguish hard-normal seams and nearby layers; conflicting matches are
refused.

**Review** prepares the candidate without changing the source. Compare original
and resulting poses in **Preview**, inspect exact before/after **Weights**, then
**Apply** one undoable edit. Component selection can restrict an already bound
destination; an unbound destination needs a match for every vertex at full
strength. See [weight transfer](../MODEL_WEIGHT_TRANSFER.md) for preservation,
mirroring, joint maps, export handoff and CLI options.

## Place a hand or foot

Open **Skeleton → Two-Bone Inverse Kinematics**. Choose a clip, an end joint
and an inclusive sample range. Its parent and grandparent form the chain.
Set the target in model units and a pole point on the desired elbow/knee side.
**Require target** refuses unreachable poses; **Nearest reachable** clamps
without stretching and reports the remaining distance. Choose whether the
end keeps its orientation or follows the lower bone.

**Review** prepares a candidate in a cancellable worker. Compare before and
after samples in **Preview**, inspect numeric **Results**, then **Apply** one
undoable edit. Changed settings require another review. IK writes dense poses;
independent affected mesh/tag edits are protected. Refit collision and refresh
exports explicitly. See [inverse kinematics](../MODEL_IK.md) for exact policies,
CLI use and remaining constraints.

## Edit animation curves

Open **Skeleton → Animation Curves**, choose a clip and joint, then **Capture
Sampled Motion**. Each joint has translation, unwrapped rotation and positive
scale channels. The **Keys** table and numeric fields insert, move and remove
keys and set step, hold-next, linear or cubic interpolation with automatic, clamped, free
or flat tangents. The **Curve** graph offers the same edits with keys and handles.

**Undo draft**, **Redo draft** and **Discard draft** act locally; the 256 newest
drafts are kept. **Review**
samples the curves and validates the resulting skin and attachments. Compare
original/result motion in **Preview**, then **Apply** one document undo step.
Save uses source schema 10 for hold-next curves, or schema 9 for other curves;
native game exports use dense samples.
To return to pose or IK tools, explicitly **Stage Bake Joint Curves**, review
and apply: motion remains, and undo restores the editable keys. See
[animation curves](../MODEL_ANIMATION_CURVES.md) for controls, limits and CLI use.

For groups of keys, open **Skeleton → Edit Animation Keys**. Filter joints and
channels, select rows or an inclusive sample range, then choose retime, duplicate,
remove or reduce. Retiming changes sample times around a pivot and refuses key
collisions; reduction checks continuous channel error, including between samples.
**Review** reports key counts and changes, and previews the resulting motion.
**Apply** commits the whole selection as one undo step. See
[key groups](../MODEL_ANIMATION_CURVES.md#edit-groups-of-keys) for tangent,
rounding, selection and tolerance rules, and the `model curve-keys` CLI.

To reuse or reverse motion, open **Skeleton → Transfer Animation Range**.
Choose source/destination clips, inclusive source bounds, destination start,
time scale and optional **Reverse source range** or **Match clip rates**.
Select joints/channels on **Tracks**, then **Review** the counts and whole-channel
changes. Inspect original/result destination poses, including adjoining
transitions, before **Apply**. The destination interval is replaced as one undo
step. Stored curves keep their shape; unkeyed motion is captured automatically.
Every mapped key must land on an integer sample. See
[range transfers](../MODEL_ANIMATION_CURVES.md#transfer-or-reverse-a-range)
for boundary behaviour and the `model curve-transfer` CLI.

## Known limits

- No model format has been tested against real game files in bulk, and no
  MD5, IQM or ASE export has been loaded in the original games yet.
- MDC, MDS, MDM, MDR, Ghoul 2, Half-Life, Hexen II, Heretic II, LightWave and
  KVX models are read only; export them as MD3 or another written format.
- Long-chain IK, persistent constraints, animation layers, retargeting and
  joint modal pointer-following remain planned. Two-bone IK, bone overlays,
  shared viewport handles and numeric joint,
  weight and skeletal pose tools are available.
  Game animation scripts (Jedi Academy `animation.cfg`,
  Wolfenstein scripts, GZDoom MODELDEF) are not read.
- A skeletal clip too long for the editable frame budget on its own cannot
  join the working set: edit it through the skeletal tools, which preview one
  sample at a time. See [the working set limits](../MODEL_SKELETAL_WORKING_SET.md#limits).
- On skeletal models, merged weights are an equal blend rather than weighted by
  distance, position tools make the frames they edit independent of the
  skeleton, and an unskinned surface must be bound before it can join a skinned
  one. See [Skin weights through mesh edits](../MODEL_SKIN_PROVENANCE.md#limits).
- Modifier stacks and branched rip
  junctions remain planned. [Spin](../MODEL_SPIN.md) and [Rip](../MODEL_RIP.md)
  provide bounded shared GUI/CLI workflows. [Knife](../MODEL_KNIFE.md)
  cuts projected paths; [Bevel Edges](../MODEL_EDGE_BEVEL.md) provides chamfers
  and rounded bands with joined corners. Both share GUI/CLI validation.
- The [modeller release checklist](../MODELLER_RELEASE.md) lists what is still
  open.

## Command-line equivalents

```sh
vibestudio --cli model inspect ./id1/pak0.pak progs/player.mdl --json
vibestudio --cli model import ./prop.obj --output ./prop.mesh.json --dry-run --json
vibestudio --cli model build ./prop.mesh.json --output ./build/prop.md5mesh --dry-run --json
vibestudio --cli model build ./monster.mesh.json --all-clips --output-dir ./build/anims --strict --json
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
| Named skin variants and their `.skin` or Doom 3 skin files | `model skin-variants`, `model skin-export` |
| Tags, clips and collision | `model tags`, `model animations`, `model collision` |
| Joints, weights, bind poses, skeletal clips and the working set | `model rig`, `model pose`, `model skinning`, `model weight-paint`, `model weight-transfer`, `model ik` |
| Quake MDL skins and timing | `model mdl` |
| Editable skeletal keys, tangents and baking | `model curves` |
| Retime, duplicate, remove or reduce selected skeletal keys | `model curve-keys` |
| Assemblies, and opening or publishing a Quake III player | `model assembly` |
| Build game files or detail levels | `model build`, `model lod`, `model targets`, `model prepare` |
| Controls profiles, and print, check, share or reset controls | `model profiles`, `model controls` |
| Recovery copies | `model recoveries`, `model recover` |
| Place an MD3 in a Quake III map | `map place-model` |

</details>

See [Command line](cli.md) for options and exit codes.

## Learn more

- [Mesh tools](../MODEL_TOOLS.md) and [editable meshes](../MODEL_MESH.md): every tool, limit and CLI option.
- [Skin weights through mesh edits](../MODEL_SKIN_PROVENANCE.md): how edits keep, interpolate and mirror joint weights.
- [Native formats and skeletons](../MODEL_FORMATS.md) and [modeller profiles](../MODELLER_PROFILES.md).
- [Delivery targets](../MODEL_TARGETS.md) and [detail levels](../MODEL_LOD.md): each engine's limits, Prepare for Target and linked detail levels.
- [Model design](../MODEL_DESIGN.md) and [model assemblies](../MODEL_ASSEMBLY.md): props and linked models.
- [Model collision](../MODEL_COLLISION.md), [material slots](../MODEL_MATERIAL_SLOTS.md) and [skin variants](../MODEL_SKIN_VARIANTS.md).
- [Modeller release checklist](../MODELLER_RELEASE.md): what has and has not been verified.
