# Mesh Tools

The Mesh Editor works the way Blender's edit mode does: a header with menus,
select modes and editing toggles, a tool shelf, keyboard-driven modal tools with
axis constraints and typed values, region and loop selection, a 3D cursor, an
Adjust Last Operation panel and command search. Everything is built for idTech
models: triangle meshes with vertex animation, UV seams, MDL/MD2/MD3 limits and
Quake-scale units.

Every tool runs through the ordinary [editable mesh](MODEL_MESH.md) document
service, so each change is one undo step with recovery, source saves and native
export unchanged. A tool's preview applies the same core edit to a copy of the
mesh, so what you see before confirming is exactly what is committed. The CLI
commands `model tool` and `model select` use the same services.

**Vertex/Edge → Rip** opens vertices or connected edge chains;
**Rip and Fill** adds gap faces. Both support side selection, constrained
movement, typed offsets, worker preview and F9 adjustment. See [Rip](MODEL_RIP.md)
for the shared CLI, retained pose/rig rules and current topology limits.

Mesh tools retain explicit source connectivity through UV/normal corner copies,
generated geometry and vertex compaction. Detached coincident regions remain
separate. **Rip** accepts zero movement; **Rip and Fill** requires a gap.
**Join Surfaces** keeps its input islands separate; **Merge** and **Weld** are
the explicit routes to reconnecting vertices. Sources containing this metadata
use version 11; keep them when producing native game-format derivatives.

Interaction design follows Blender's documented edit mode and default keymap;
no Blender code is used. See [Credits](CREDITS.md#blender-style-mesh-editing-2026-10-07).

## Navigating

The editor starts with Blender navigation. Turn it off under **View > Blender
Navigation** to return to the studio's orbit controls (left-drag orbits).

| Action | Blender navigation |
| --- | --- |
| Orbit | Middle-drag, or Alt+left-drag (three-button emulation) |
| Pan | Shift+middle-drag |
| Zoom | Mouse wheel |
| Front, right, top | Numpad 1, 3, 7 |
| Back, left, bottom | Ctrl+Numpad 1, 3, 7 |
| Opposite view | Numpad 9 |
| Orbit in 15° steps | Numpad 4, 6, 8, 2 |
| Perspective/orthographic | Numpad 5 |
| Frame all / selected | Home / Numpad . |

The navigation gizmo shows the model axes. Click an axis to look along it.
Hide it under **View > Navigation Gizmo**. It sits in the view's trailing top
corner (top right, or top left in a right-to-left layout), below the heads-up
tags and the tool banner and clear of **Adjust Last Operation**, which keeps to
the bottom leading corner. Its size follows the heads-up text, so it grows at
larger text sizes; each axis bubble is a click target at least 24 px across.
In a view too small for that it shrinks, then hides. The numpad view keys
above do the same without a pointer.

## Selecting

**1**, **2** and **3** switch between vertex, edge and face select modes, as do the
first three header buttons. Switching converts the selection as Blender does:
faces become their corners or edges, and vertices or edges become the faces
they fully cover.

| Gesture | Effect |
| --- | --- |
| Click | Select the component under the pointer |
| Shift+click | Add or remove it |
| Drag on empty space, or **B** then drag | Box select (Shift extends, Ctrl subtracts) |
| **C** | Circle select: drag to add, Shift+drag to remove, wheel resizes |
| Ctrl+right-drag | Lasso select (Shift+Ctrl subtracts) |
| Alt+click | Edge loop (face loop in face mode); Shift+Alt extends |
| Ctrl+Alt+click | Edge ring |
| Alt+**L** / Alt+**R** | Edge loops / rings of the selected edges, without the pointer |
| Ctrl+click | Shortest path from the selection |
| **L** / Ctrl+**L** | Linked geometry under the pointer / of the selection |
| Ctrl+= / Ctrl+- | Grow / shrink the selection |
| **A**, Alt+**A**, Ctrl+**I** | All, none, invert |
| Alt+**Z** | X-ray: box, circle and lasso reach hidden components |

The **Select** menu adds Similar (normal, area, coplanar, length, direction,
face angle, seam marking, edge count), Non-Manifold, Loose Vertices, Boundary
Loop, Sharp Edges, Random, Checker Deselect, Mirror Selection and Side of Axis.

**Select Edge Loops of Selection** (Alt+**L**) and **Select Edge Rings of
Selection** (Alt+**R**), in the **Select** and **Edge** menus, grow every
selected edge to its loop or ring and keep what was selected; in vertex mode
the edges between selected vertices count, leaving out quad diagonals. An
edge without one, such as a selected quad diagonal, stays as it was, and the
banner says how many did not grow, with each edge and its reason in its
details. Edges picked in the component table with the arrow keys grow the
same way, so loops need no pointer. The quad view is built once for the whole
selection, so thousands of edges take a moment, and the work runs on the
editor's worker, with progress and **Cancel** when it takes longer. 3ds Max
binds the same keys; MilkShape 3D has no loops and keeps them in the menus.

Loops, rings and linked selection follow the mesh's geometric connectivity, so
they cross UV seams. Indexed vertices that coincide in every animation pose
(seam and hard-normal copies) act as one vertex. **Select Linked UV Island**
stops at seams instead.

Triangle meshes have loops through a *quad view*: neighbouring triangles that
share an edge with consistent winding, form a convex quad and lie within 40° of
each other pair up, preferring flat, rectangular quads whose shared edge is the
longest side of both. Loops cross vertices where exactly four quads meet;
rings cross opposite sides of quads and stop at triangles and borders.

## Modal Tools

Modal tools start from the keyboard, the tool shelf or the menus, follow the
pointer, and finish with Enter, Space or a click. Escape or a right-click
cancels and leaves the source untouched. Knife uses clicks to add points and
right-click to start another path; **Enter/Space** confirms and **Esc** cancels.

| Key | Tool |
| --- | --- |
| **G** | Move |
| **R** | Rotate (around the view axis unless constrained) |
| **S** | Scale |
| **E** | Extrude faces along their normal, or open border edges |
| **I** | Inset faces (press **I** again for individual faces) |
| Alt+**S** | Shrink/Fatten along vertex normals |
| Shift+**D** | Duplicate faces and move them |
| Ctrl+**R** | Loop cut: across the one selected edge between quads, or the edge under the pointer |
| Ctrl+Shift+**B** | Bevel vertices: move away from the selection to widen |
| Ctrl+**B** | Bevel edges: type a width; wheel or Page Up/Down changes segments |
| **K** | Knife: add path points, then confirm the complete preview |

While a transform-style tool runs:

- **X**, **Y** or **Z** constrains to a world axis. Press it again for the
  selection's own axis, and a third time to clear it. Shift+**X**/**Y**/**Z**
  constrains to the plane without that axis.
- Typing a number sets the value exactly: a distance, an angle in degrees, a
  scale factor, an inset thickness or (for a loop cut) the number of cuts.
  **-** flips the sign and Backspace edits.
- Holding Ctrl toggles snapping, using the Move, Rotate and Scale steps of the
  header's Snap control. Holding Shift slows the pointer to a tenth.
- The wheel, Page Up/Down or +/- change the loop cut count or the
  proportional radius. **O** toggles proportional editing.

Loop Cut starts across the selected edge when the selection names exactly one
edge between quads (in any select mode, after setting aside quad diagonals);
otherwise it starts on the edge under the pointer. Moving the pointer onto
another edge picks that edge instead; moving it away from every edge returns
the cut to the selected one. The banner names the edge, such as
*across the selected edge 4:5*, so the cut can be made entirely from the
keyboard: pick the edge in the component table, press Ctrl+**R**, set the
cuts with +/- or a number, and press Enter.

A banner over the viewport names the tool, its current value and constraint,
and any reason the preview is invalid. The same text goes to the editor's
status line, whose accessible description carries it for screen readers. A
tool's prompt and keys are announced once as it starts, and a refusal
(*Rip refused*, *Bevel refused*, a transform with nothing selected) is
announced with its reason when the tool starts or is confirmed; the readouts
that follow the pointer and typed values are not announced, and nor is the
live preview that Rip, Spin, Bevel Edges and Knife run at every move or digit,
even when it is refused. Every other document operation that is refused or
fails is announced the same way, and one that takes 1.5 seconds or more says
it finished. See
[Modeller Status And Announcements](ACCESSIBILITY_LOCALIZATION.md#modeller-status-and-announcements).

Readouts, the status line, the component table and the curve graph write
numbers the way the user's region does ("0,5" in German), as the spin boxes
beside them do; a position reads "0,5; 1; 2" there. Copy from the component
table keeps the C locale ("0.5"), as files and the CLI do. Counts take each
language's plural forms: the status line's vertex, triangle, frame, face and
edge counts are separate messages.

### Proportional editing and symmetry

**O** or the header's proportional button makes Move, Rotate and Scale affect
nearby vertices with a falloff: Smooth, Sphere, Root, Inverse Square, Sharp,
Linear or Constant (Shift+**O** cycles them). The radius is in model units;
**Connected** measures distance along the mesh instead of straight through
space. A dotted circle shows the radius during the tool.

The header's **Symmetry X/Y/Z** buttons mirror edits across the model's axes:
moving a vertex also moves its counterpart on the other side, and vertices on
the mirror plane stay on it. Counterparts are matched by position in the
displayed pose.

## The 3D Cursor

Shift+right-click places the 3D cursor on the surface under the pointer (or in
the view plane at the cursor's depth). **Add** places new primitives there, and
**Merge at Cursor**, **Bisect Through 3D Cursor** and **Pivot at 3D Cursor** use
it. **Shift+S** opens the snap menu: Cursor to Selected, Cursor to World Origin
(also Shift+**C**), Cursor to Grid and Selection to Cursor.

## Menus And Panels

The menu bar starts with **Model**, the document's own menu (open, repair,
recover, save, export, stage and **Keep Local Recovery Copies**), as Blender's
image and text editors start with theirs; the mesh menus follow. **Undo** and
**Redo** lead the editing header, and the shading and view-preset choices and
**Frame Model** follow the select modes there. How the chrome, the sidebars
and the dialogs fit a laptop screen at 200% text is in the
[modeller layout record](MODELLER_PROFILES.md#large-text-and-small-screens).

Right-click opens the vertex, edge or face context menu for the current select
mode. Ctrl+**V**, Ctrl+**E** and Ctrl+**F** open them from the keyboard; **X**
or Delete opens Delete, **M** Merge, Shift+**A** Add, Alt+**N** Normals,
Shift+**G** Select Similar, **Z** Shading and **U** UV Mapping.

**F3** searches every Mesh Editor command by name and shows its shortcut.

**F9** shows the **Adjust Last Operation** panel. Changing a setting there
redoes the last tool with the new value in place of the old result, still as one
undo step. The panel only edits a result that is still current; after another
edit it shows the earlier settings without applying them. Shift+**R** repeats
the last tool on the current selection.

## Keyboard Focus

The Mesh Editor works without a pointer in every controls profile, and no key
traps focus (WCAG 2.1 success criterion 2.1.2, No Keyboard Trap):

- **Ctrl+Tab** moves focus to the first control of the next panel in reading
  order: the leading sidebar, the editing header (with the shading and view
  controls), the tool shelf, the views (the active view itself on the **3D**
  tab), the animation transport, the trailing sidebar, then everything else
  in the editor, the status strip (and its material images' state) included. The
  document commands are the **Model** menu's (**Alt+O**, or **F3** by name).
  **Ctrl+Shift+Tab** moves to
  the previous panel. On macOS they are **Control+Tab** and
  **Control+Shift+Tab**, since Qt's Ctrl is the Command key there and the
  system keeps Command+Tab for switching applications. Both are in **View**
  as **Next Panel** and **Previous Panel**, work while a worker runs, and are
  kept by every profile: `modelEditorFocusKeys()` names them, the controls
  reference and `model controls` list them, **Customise Controls** refuses
  them and an override or controls file that gives them to a command is
  skipped for that key with a warning naming the command. Ctrl+Tab no longer
  switches between the **3D** and **UV** tabs; click a tab, or use the arrow
  keys on the focused tab bar.
- A command whose keys include **Tab** or **Shift+Tab** (**Toggle Object/Edit
  Mode** and **Toggle Snapping** in the VibeStudio and Blender profiles) is a
  widget shortcut on the active view: it runs only while the view itself has
  focus. On the editing header and in **Adjust Last Operation**, Tab and
  Shift+Tab move focus as usual. The 3ds Max and MilkShape 3D profiles give
  Tab to no command.
- The tool shelf is one Tab stop, as tool bars are elsewhere: the arrow keys,
  **Home** and **End** move between its buttons, and **Enter** or **Space**
  presses the focused one. The shelf remembers the button used last. Clicking
  a button leaves keyboard focus where it was.
- The component table, the controls reference and the **Customise
  Controls** command list move between rows with the arrow keys; Tab leaves
  them in one step rather than visiting, and wrapping round, every cell.
- **F9** shows **Adjust Last Operation** with focus on its first setting
  (the 3ds Max and MilkShape 3D profiles give it no key; use **View > Adjust
  Last Operation**). **Esc** in a setting returns focus to the view and keeps
  the panel open; **F9** from the view hides it. A setting that redoes the operation keeps
  focus on the same setting while the panel rebuilds. A focused setting keeps
  the keys it uses: **Space** toggles a check box rather than running
  Blender's Play, letters choose from a list and typing edits a number.
  Keys with Ctrl, Alt or Meta and the function keys still run commands, and a
  key the setting does not use stops at the panel instead of reaching the
  view.
- A running modal tool keeps every key, Ctrl+Tab included, until **Enter**
  confirms or **Esc** cancels it.
- In four views, **Next View** (Ctrl+Alt+Page Down) and **Previous View**
  (Ctrl+Alt+Page Up) make the next or previous pane in reading order active,
  as a click does, with keyboard focus on it; the status line and the screen
  reader name the view. Every profile binds these keys (**Customise
  Controls** can change them) and **View** lists the commands. In one view
  they say how to open four. Page keys have no
  direction to mirror right to left, and Linux desktops take Ctrl+Alt with
  the arrow keys.
- The **UV** view pans with the arrow keys (24 px; Shift for 4 px steps) and
  zooms about its centre with + and -. The arrows are not mirrored right to
  left. **F** and **Home** frame as before.
- The view's accessible description names these keys after the profile's own
  summary.

Key hints follow the active profile and the user's changes. The header's
select-mode, **X-ray** and **Proportional editing** buttons, the tool shelf
and the command search button put the command's current keys in their
tooltips and accessible descriptions; a command without keys says so in its
description. Accessible names carry no keys. The empty **Adjust Last
Operation** panel, the **Spin** and **Bevel Edges** hints and the view's
description after **View > Blender Navigation** name the profile's keys for
Adjust Last Operation, Search Commands, Move, Rotate and Scale, or the **View**
menu entry where the profile has none (3ds Max and MilkShape 3D give Adjust
Last Operation no key, and F3 is another command there). Status messages
about saving, exporting and importing controls name the profile by its
display name, and a skipped focus key names the command by its title.

In **Customise Controls** the key field records one key. Tab and Shift+Tab
move focus on and Esc stops recording, so the field never holds focus.
**Special key** puts Tab, Shift+Tab or Esc into the field for **Assign** or
**Add** when a command should use them.

## Tools

Topology tools cover every animation pose. New vertices follow each pose (for
example, a loop cut point keeps its position along the edge in every frame),
and each side of a UV seam gets its own vertices, so seams survive every edit.
Geometric choices such as convexity, cut positions and inset directions use the
displayed pose. Move, Rotate, Scale, Shrink/Fatten, Smooth and Edge Slide honour the
editor's All frames / Current frame scope.

Shape-changing tools recompute angle-weighted normals for the vertices they change. A vertex
whose seam copies already shared one normal stays smooth across the seam;
copies with different normals keep their hard edge. Knife cuts interpolate the
original authored normals along with UVs and pose positions.

On skeletal models every tool keeps joint influences from where each vertex
came from: copies keep their source's weights (moved copies are rebound where
they land), cut points and pokes interpolate the weights of the vertices they
split, merged vertices take the equal blend, and mirrored halves map left and
right joints. **Add Primitive** on a skinned surface asks for a binding first.
The status line names interpolated, mirrored, rebound, merged and bound
weights. See [skin weights through mesh edits](MODEL_SKIN_PROVENANCE.md) and the
[tool-by-tool table](MODEL_SKINNING.md#weights-through-other-tools).

| Tool | Behaviour |
| --- | --- |
| Rotate Edge | Turns the diagonal shared by two triangles. The faces must form a convex quad on one UV island. |
| Edge Slide | Moves selected chains or loops along their surrounding quad rails by a signed fraction or reference-pose distance. UVs remain attached; invalid branches, collapsed faces and ambiguous rails refuse the edit. See [edge sliding](MODEL_EDGE_SLIDE.md). |
| Merge at Center / Cursor | Joins the selection into one point. Vertices connected by an edge share one index and an averaged UV; unconnected seam copies stay separate, as per-corner UVs would. At Cursor places the displayed pose at the cursor and keeps other poses' motion. |
| Collapse | Merges each connected group of selected components to its own centre. |
| Dissolve Vertices | Removes vertices and refills each fan with ear clipping. Seam copies are dissolved per side. |
| Dissolve Faces | Replaces each selected region (one UV island, one border, no holes) with a triangulated polygon. |
| Make Face (**F**) | Builds a triangle or quad from three or four vertices (or two edges), wound to match its neighbours. |
| Poke Faces | Adds a centre vertex to each face, optionally lifted along the face normal. |
| Beautify Faces | Flips diagonals inside the selection to make triangles more even. |
| Extrude Edges | Pulls open border edges out into a new strip. |
| Spin / Spin Duplicates | Curved face/open-border extrusion or radial face copies around the 3D cursor. Type degrees, choose X/Y/Z, adjust steps with Page Up/Down, then confirm or cancel. F9 exposes centre, axis, closure, copies and UV scale. See [Spin](MODEL_SPIN.md) for all-pose and skinning semantics. |
| Inset Faces | Insets a region (or each face) by a thickness with optional depth. Even thickness keeps a constant border at corners; a thickness that would fold the border over is refused. |
| Shrink/Fatten | Moves vertices along their normals; even thickness keeps faces parallel. |
| Smooth Vertices | Averages selected vertices toward their neighbours (factor, repeat, keep borders). |
| Shade Flat / Smooth / Auto Smooth | Sets MD3/MD2 vertex normals: flat gives each face its own corners, smooth welds same-position same-UV copies, Auto Smooth keeps edges sharper than its angle (30° by default) hard. |
| Bisect | Cuts the selected faces with a plane and can delete either side. |
| Symmetrize | Replaces one half of the selection with a mirror of the other, joining vertices within the merge distance of the plane. Mirrored halves share their UV layout, the usual way to save texture space. On a skinned surface, **Symmetrize Skin Weights** first confirms each off-centre joint's mirror partner on the other side of the plane (**Detect Pairs** reads left and right names), or keeps the joint names; joints on the plane always keep theirs. **Adjust Last Operation** asks again for a new axis, and **Repeat Last** for its target surface. |
| Loop Cut | Adds 1–64 edge loops across a ring of quads; one cut can slide between the sides. Triangles at the ends of an open ring are split too, so no T-junctions remain. |
| Bevel Vertices | Replaces each selected vertex with a cap whose corners sit the bevel width along every edge it had. Bevelling both ends of an edge keeps a point on that edge where it folds. A width that reaches past a neighbouring vertex is refused. |
| Bevel Edges | Builds chamfer/rounded bands and joined corner caps with width, 1–32 segments and roundness. Retains face-side UVs, interpolates skin influences and checks every stored pose. See [edge bevel](MODEL_EDGE_BEVEL.md). |
| Knife | Cuts finite, crossing, closed or disconnected paths, with visible/through and selected-face scope. Interpolates every pose, UVs and skinning, preserves hidden child faces and selects path edges. See [knife controls and CLI](MODEL_KNIFE.md). |
| Solidify | Gives selected faces a thickness: flipped back faces sit behind along the angle-weighted normals (even thickness by default) and a rim with its own hard-edged corners closes open borders. Single-sided sheets such as capes and banners render from both sides in every idTech renderer. |
| Decimate | Quadric error simplification to a ratio or a triangle budget, measured over up to eight sampled poses. UV seams, borders (unless allowed) and every pose's shape are protected. **Decimate to MD3 Budget** targets Quake III's working limit of 1,999 triangles per surface. |

### Adding primitives

**Add** (Shift+**A**) inserts a plane, cube, circle, grid, cylinder, cone, UV
sphere, ico sphere or torus at the 3D cursor into the active surface, with UVs
and normals ready for texturing: the cube uses a 3×2 atlas, cylinders and cones
wrap their sides with caps below, spheres use latitude/longitude or spherical
mapping, and the torus wraps both ways. Primitives start at 32 units, a common
idTech scale. Size, segments, rings (subdivisions for the ico sphere),
location and up axis can be changed in the Adjust Last Operation panel.

### UV mapping

**UV** (**U**) projects texture coordinates onto the selected faces, as
Blender's UV menu does. Corners are split only where the mapping changes, at the
selection's border or between cube sides, so faces outside the selection keep
their mapping and vertex counts stay within MD3, MD2 and MDL budgets.

| Projection | Result |
| --- | --- |
| Cube Projection | Each face takes the side of a world-aligned box it faces most, seen upright from outside. The texture repeats every 64 units by default. |
| Cylinder Projection | U wraps once around the up axis (Z by default) and V runs down the sides with square texels. Faces across the wrap get their own corners. |
| Sphere Projection | U wraps around the axis and V runs from pole to pole. Each face at a pole gets its own corner, as in Blender. |
| Project From View | Projects along the current view, upright on screen. **(Bounds)** scales the result to fill the skin. |

The Adjust Last Operation panel changes the projection, units per texture
repeat or axis, and **Scale to bounds**, which fits the result into the 0–1
skin keeping its aspect. **Unwrap and Pack** and **Pack Islands** use the UV
panel's atlas settings, and **Mark Seam** and **Clear Seam** set the seams that
unwrapping cuts along. Seam marks on projected faces follow their new corners.

**Texel Density…** measures existing islands using original material dimensions
and a chosen reference pose. Preview either matching every island to a target
or scaling the selection while retaining relative density. Apply uses one undo
step and preserves every pose and exact skin influences. See
[texture density](MODEL_UV_DENSITY.md) for scope, precision, packing and CLI limits.

### Skin variants

**Skin Variants…** (Surface page) keeps named skins such as red and blue with the
source: each gives some surfaces another material and leaves the rest on their
primary material. The working copy previews in the viewport, applies as one undo
step and writes Quake III `.skin` files or Doom 3 skin declarations; player
packages use a part's variants for skins of the same name. See
[skin variants](MODEL_SKIN_VARIANTS.md).

## Delivery Targets And Detail Levels

The header's **Delivery targets** button summarises the model against the
delivery target chosen for each format: the engine or source port the export is
meant for. The defaults are the original engines (Quake, Quake II, Quake III
Arena) and, for IQM, the format's own limits, unless the open project saves
another or its game installation suggests one; the button's menu offers the
source ports and games whose limits differ, such as QuakeSpasm, FTE and
DarkPlaces for MDL, Q2PRO for MD2, MD3 and MD5, ioquake3, Return to Castle
Wolfenstein, Enemy Territory and GZDoom for MD3, and ioquake3 and GZDoom for
IQM. The table, each limit and its source are in
[delivery targets](MODEL_TARGETS.md).

The button reads "ready", "check" or "over" for each format, with an icon. It
checks as you edit: counts for every format, and MD3's full frame scan
(coordinate range and 1/64-unit precision) unless the model holds more than
500,000 vertex positions across its frames. "Check" means a count taken before
the writer shares seam or duplicate vertices exceeds a limit, or the model was
too large to scan, which **Check Delivery Targets…** settles exactly. The menu
names every blocker with its surface; choosing a blocker selects exactly what it
names (the faces past a surface's budget, the vertices outside the tile or
MD3's range, or the surface), with the fix in its tooltip. Under a format with
blockers, **Prepare for FORMAT · Target…** reviews the splits, joins,
reductions and moves that fit the model and applies them as one undo step (see
[Prepare For Target](MODEL_TARGETS.md#prepare-for-target)). The menu also
lists the largest value for each limit, a submenu per format to choose the
target (saying when the choice is the project's or the installation's),
**Save Targets with the Project**, **Check Delivery Targets…** (every limit, on
the worker, listed with a fix for each), **Decimate to MD3 Budget (1,999
Triangles)**, **Detail Levels…** and **Export Quake III Detail Levels**.
Exports, staging and detail levels all use the chosen target, and an export it
refuses writes nothing and lists every blocker under **Export Report**.

Quake III's working limit is 999 vertices and 1,999 triangles per surface:
`R_LoadMD3` accepts 1,000 and 2,000, but `RB_CheckOverflow` drops the game when
one surface reaches either, and ioquake3 refuses them at load.

### Quake III Detail Levels

Quake III loads `name_1.md3` and `name_2.md3` beside `name.md3` as lower detail
models, chosen by distance and `r_lodbias`; `MD3_MAX_LODS` is 3, so a third
detail file would never load and is refused (Enemy Territory loads a third).
How many levels a game loads is part of its MD3 delivery target. **Mesh > Clean
Up > Detail Levels…** reviews, stores and exports the levels the source owns,
generated or hand-made; see [detail levels](MODEL_LOD.md). **Mesh > Clean Up >
Export Quake III Detail Levels** writes the base model and the stored levels or,
when the source stores none, two levels, each keeping half of the previous
level's triangles per surface. The levels use the same decimation as the tool,
so UV seams and every animation pose are kept; the first level also keeps
borders. A surface that cannot be reduced further keeps its previous detail and
is reported. Exporting does not change the editable source.

The whole set is prepared before anything is written: every file's bytes, the
delivery target's preflight for each level, and a review of every destination.
A level that would land on the editable source or the file it was opened from
is refused. When a file of the set already exists, for example a hand-made
`prop_1.md3`, one confirmation names every file that would be replaced, and
**Cancel** writes nothing. Publication then rechecks each reviewed destination,
writes new files without replacement and existing ones atomically, and a
failure part way says which files were already written. **Export Report…** then
lists each file's decimation notes and what its MD3 leaves out, prefixed with
the file's name; with **Refuse exports that leave data out** on, a set whose
files would leave data out is refused before anything is written.

## CLI

`model tool` runs one tool on an editable source and writes a new
`.mesh.json`; `model select` prints the indices an operator selects, ready to
paste into `--faces`, `--vertices` or `--edges`.

```sh
vibestudio --cli model select ./prop.mesh.json --select loop --mode edges --edge 4:5 --json
vibestudio --cli model select ./prop.mesh.json --select ring --mode edges --edges 4:5,9:10 --json
vibestudio --cli model tool ./prop.mesh.json --tool loop-cut --edges 4:5 --cuts 2 --output ./cut.mesh.json
vibestudio --cli model tool ./prop.mesh.json --tool inset --faces 0,1 --thickness 2 --depth 1 --output ./inset.mesh.json --dry-run --json
vibestudio --cli model tool ./prop.mesh.json --tool add --primitive cylinder --at 0,0,16 --size 16,16,32 --segments 12 --output ./added.mesh.json
vibestudio --cli model tool ./prop.mesh.json --tool decimate --faces all --target-triangles 1999 --output ./md3-budget.mesh.json
vibestudio --cli model lod ./prop.mesh.json --output ./out/prop.md3 --levels 2 --ratio 0.5 --json
vibestudio --cli model tool ./player.mesh.json --tool symmetrize --faces all --axis x --joint-map detect --output ./mirrored.mesh.json --json
vibestudio --cli model tool ./player.mesh.json --tool add --primitive cube --at 0,0,64 --size 12,12,12 --bind-joint head --output ./helmet.mesh.json --json
vibestudio --cli model targets ./prop.mesh.json --format md3 --target ioquake3 --json
```

On skeletal models, `--json` output adds `skinProvenance`: per skinned surface,
how many vertices kept, interpolated, mirrored, rebound, merged or bound their
weights. Text output prints one line per skinned surface. `add` also reports
its `binding`.

| Tool | Options |
| --- | --- |
| `rotate-edges` | `--edges` |
| `merge` | selection, `--merge centre\|point\|collapse`, `--point x,y,z` |
| `dissolve-vertices`, `dissolve-faces` | selection |
| `poke` | `--faces`, `--distance` |
| `beautify` | `--faces`, `--angle` |
| `make-face` | `--vertices` or `--edges` |
| `extrude-edges` | `--edges`, `--offset x,y,z` |
| `inset` | `--faces`, `--thickness`, `--depth`, `--individual`, `--no-even` |
| `shrink-fatten` | selection, `--distance`, `--no-even`, `--frame all\|N` |
| `smooth` | selection, `--factor`, `--iterations`, `--pin-boundary`, `--frame` |
| `transform` | selection, `--offset`/`--rotate`/`--scale`, `--pivot-mode`, `--pivot`, `--proportional-radius`, `--falloff`, `--connected`, `--mirror x\|y\|z\|xy…`, `--threshold`, `--frame` |
| `shade-flat`, `shade-smooth` | `--faces` |
| `auto-smooth` | `--faces`, `--angle` (default 30) |
| `bisect` | `--faces`, `--plane-point`, `--plane-normal`, `--keep both\|front\|back` |
| `symmetrize` | `--faces`, `--axis x\|y\|z`, `--direction positive\|negative`, `--threshold`, `--joint-map detect\|left=right,…`, `--keep-joint-names` (skinned surfaces; see [joint maps](MODEL_SKIN_PROVENANCE.md#symmetrize-skinned-surfaces)) |
| `loop-cut` | one `--edges` pair, `--cuts`, `--slide`, `--angle` |
| `edge-slide` | `--edges`, exactly one of `--slide` or `--distance`, `--reference-frame`, `--frame all\|N`, `--angle` |
| `add` | `--primitive`, `--at`, `--size`, `--segments`, `--rings`, `--up`, `--new-surface NAME`; on skeletal models `--bind-joint NAME` or `--bind nearest-bones` (a skinned surface needs one; see [binding new geometry](MODEL_SKIN_PROVENANCE.md#bind-new-geometry)) |
| `decimate` | `--faces`, `--ratio` or `--target-triangles`, `--allow-boundary` |
| `bevel-vertices` | selection, `--width` |
| `bevel-edges` | `--edges`, `--width`, `--segments` (1–32), `--roundness` (0–1), `--tile-size`, `--angle`, `--reference-frame` |
| `knife` | exactly one `--path`/`--path-file`, `--view xy\|xz\|yz`, `--opposite`, `--through`, `--faces`, `--camera-origin`, `--perspective`, optional full camera basis; see [projection coordinates](MODEL_KNIFE.md#automate-a-knife-cut) |
| `solidify` | `--faces`, `--thickness` (negative grows outward), `--no-even` |
| `uv-cube` | `--faces`, `--tile-size` (units per repeat, default 64), `--fit` |
| `uv-view` | `--faces`, `--u-axis` and `--v-axis` (default 1,0,0 and 0,0,1, the front view), `--tile-size`, `--fit` |
| `uv-cylinder`, `uv-sphere` | `--faces`, `--axis` (default z), `--fit` |

All tools accept `--surface N` (default 0) and `--reference-frame N` (the pose
used for geometric choices, default 0), and require `--output <new.mesh.json>`.
`--dry-run` validates without writing; an existing output needs
`--overwrite`. Outputs must be separate from the input source, including aliases
and dry runs. A tool refuses options it does not take. JSON reports the
surface's vertex and triangle counts and the resulting selection.

`model lod <source> --output <name.md3>` writes the base MD3 and its detail
levels beside it, `name_1.md3` and `name_2.md3` (`--levels`, default 2;
`--ratio 0.05-0.95`, default 0.5; or `--target-triangles` and `--level-source`,
or the source's stored levels; see [detail levels](MODEL_LOD.md#cli)).
`--target` names the MD3 delivery target (default: the project's, else
`quake3`), which sets how many levels the game loads and the limits every file
is checked against; more levels than it loads exit 2. The source may be an
editable `.mesh.json` or an MDL, MD2, MD3 or OBJ. Every file is prepared and
checked before any is written; `--dry-run` stops there, and existing files need
`--overwrite`. JSON lists each file's level, triangle count, size, whether it
existed and its commit state (`written`, `failed` or `pending`), its
decimation `notes`, and its MD3 export's `exportNotes` and `exportOmissions`,
with `existing`, `committed` and every omission once (`exportOmissions`) for the
set; a publication that fails part way exits 1 with the same report. `--strict`
refuses (exit 4) a set whose files would leave data out, writing nothing.

`model targets [<source>]` lists the delivery targets with their limits and
sources, or checks a model against them, and `model prepare` applies the fixes
they suggest to a new source; see [delivery targets](MODEL_TARGETS.md#cli).

### Selection

`model select <source> --select <operator>` accepts `all`, `none`, `invert`,
`linked`, `more`, `less`, `loop`, `ring`, `path`, `similar`, `non-manifold`,
`loose`, `boundary`, `sharp`, `random`, `checker`, `side`, `facing` and
`mirror`, with `--mode faces|vertices|edges` (default faces). Give the current
selection with `--faces`, `--vertices` or `--edges`; a loop or ring seed with
`--edge a:b`; path endpoints with `--from` and `--to`; similarity with
`--similar normal|area|coplanar|length|direction|face-angle|seam|valence` and
`--threshold` (degrees, or percent for area and length); `--ratio` and
`--seed` for random; `--nth` and `--offset` for checker; `--axis x|y|z` and
`--negative` for side, facing and mirror. `--delimit-seams` keeps linked
selection on one UV island, `--extend` adds to the given selection and
`--frame N` chooses the pose. It never writes files.

Without `--edge`, `--select loop` and `--select ring` grow every edge in
`--edges` (without `--edges`, every edge between two `--vertices`, leaving
out quad diagonals) to its loop or ring and join the results, as **Select
Edge Loops of Selection** and **Select Edge Rings of Selection** do in the
editor. Both call `selectModelEdgeLoops`, which takes its seeds from the
selection the same way. The editor keeps the selection it started from, so
add `--extend` for its result; without it a refused edge is left out of
`selection`. JSON adds `seeds` (the edges grown from) and `refused` (each
edge without a loop or ring, with its `reason`); the text lists them as
`Edge a:b: reason`. When no edge grows, the command exits 4 with every
reason; with no seed at all it exits 4 and names `--edge`, `--edges` and
`--vertices`.

Exit codes match `model edit`: 0 success, 1 read or write failure, 2 usage
error, 4 an edit or selection the mesh refuses.

## Limits

- Non-destructive modifiers remain planned. [Spin](MODEL_SPIN.md), [Rip](MODEL_RIP.md), vertex and
  [edge bevel](MODEL_EDGE_BEVEL.md), extrusion, inset, loop cuts and [edge sliding](MODEL_EDGE_SLIDE.md) are
  available. Hiding geometry uses the existing selection controls;
  [skeletal authoring](MODEL_RIG.md), [animation curves](MODEL_ANIMATION_CURVES.md)
  and the [skeletal working set](MODEL_SKELETAL_WORKING_SET.md) have their own
  documented workflows and limits.
- Loops and loop cuts need the quad view; meshes triangulated without quad
  pairs (or with large folds between neighbours) stop rings at those triangles.
- Decimation keeps seam vertices and, by default, borders, so heavily seamed
  imports reduce less than a seamless mesh would.
- Proportional distances and mirror counterparts are measured in the displayed
  pose; other poses follow the same weights.
