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

The navigation gizmo in the viewport's top-right corner shows the model axes.
Click an axis to look along it. Hide it under **View > Navigation Gizmo**.

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
| Ctrl+click | Shortest path from the selection |
| **L** / Ctrl+**L** | Linked geometry under the pointer / of the selection |
| Ctrl+= / Ctrl+- | Grow / shrink the selection |
| **A**, Alt+**A**, Ctrl+**I** | All, none, invert |
| Alt+**Z** | X-ray: box, circle and lasso reach hidden components |

The **Select** menu adds Similar (normal, area, coplanar, length, direction,
face angle, seam marking, edge count), Non-Manifold, Loose Vertices, Boundary
Loop, Sharp Edges, Random, Checker Deselect, Mirror Selection and Side of Axis.

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
cancels and leaves the source untouched.

| Key | Tool |
| --- | --- |
| **G** | Move |
| **R** | Rotate (around the view axis unless constrained) |
| **S** | Scale |
| **E** | Extrude faces along their normal, or open border edges |
| **I** | Inset faces (press **I** again for individual faces) |
| Alt+**S** | Shrink/Fatten along vertex normals |
| Shift+**D** | Duplicate faces and move them |
| Ctrl+**R** | Loop cut: point at an edge between quads |
| Ctrl+Shift+**B** | Bevel vertices: move away from the selection to widen |

While a tool runs:

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

A banner over the viewport names the tool, its current value and constraint,
and any reason the preview is invalid. The same text goes to the editor's
status line for screen readers.

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

## Tools

Topology tools cover every animation pose. New vertices follow each pose (for
example, a loop cut point keeps its position along the edge in every frame),
and each side of a UV seam gets its own vertices, so seams survive every edit.
Geometric choices such as convexity, cut positions and inset directions use the
displayed pose. Move, Rotate, Scale, Shrink/Fatten and Smooth honour the
editor's All frames / Current frame scope.

Tools recompute angle-weighted normals for the vertices they change. A vertex
whose seam copies already shared one normal stays smooth across the seam;
copies with different normals keep their hard edge.

| Tool | Behaviour |
| --- | --- |
| Rotate Edge | Turns the diagonal shared by two triangles. The faces must form a convex quad on one UV island. |
| Merge at Center / Cursor | Joins the selection into one point. Vertices connected by an edge share one index and an averaged UV; unconnected seam copies stay separate, as per-corner UVs would. At Cursor places the displayed pose at the cursor and keeps other poses' motion. |
| Collapse | Merges each connected group of selected components to its own centre. |
| Dissolve Vertices | Removes vertices and refills each fan with ear clipping. Seam copies are dissolved per side. |
| Dissolve Faces | Replaces each selected region (one UV island, one border, no holes) with a triangulated polygon. |
| Make Face (**F**) | Builds a triangle or quad from three or four vertices (or two edges), wound to match its neighbours. |
| Poke Faces | Adds a centre vertex to each face, optionally lifted along the face normal. |
| Beautify Faces | Flips diagonals inside the selection to make triangles more even. |
| Extrude Edges | Pulls open border edges out into a new strip. |
| Inset Faces | Insets a region (or each face) by a thickness with optional depth. Even thickness keeps a constant border at corners; a thickness that would fold the border over is refused. |
| Shrink/Fatten | Moves vertices along their normals; even thickness keeps faces parallel. |
| Smooth Vertices | Averages selected vertices toward their neighbours (factor, repeat, keep borders). |
| Shade Flat / Smooth / Auto Smooth | Sets MD3/MD2 vertex normals: flat gives each face its own corners, smooth welds same-position same-UV copies, Auto Smooth keeps edges sharper than its angle (30° by default) hard. |
| Bisect | Cuts the selected faces with a plane and can delete either side. |
| Symmetrize | Replaces one half of the selection with a mirror of the other, joining vertices within the merge distance of the plane. Mirrored halves share their UV layout, the usual way to save texture space. |
| Loop Cut | Adds 1–64 edge loops across a ring of quads; one cut can slide between the sides. Triangles at the ends of an open ring are split too, so no T-junctions remain. |
| Bevel Vertices | Replaces each selected vertex with a cap whose corners sit the bevel width along every edge it had. Bevelling both ends of an edge keeps a point on that edge where it folds. A width that reaches past a neighbouring vertex is refused. |
| Solidify | Gives selected faces a thickness: flipped back faces sit behind along the angle-weighted normals (even thickness by default) and a rim with its own hard-edged corners closes open borders. Single-sided sheets such as capes and banners render from both sides in every idTech renderer. |
| Decimate | Quadric error simplification to a ratio or a triangle budget, measured over up to eight sampled poses. UV seams, borders (unless allowed) and every pose's shape are protected. **Decimate to MD3 Budget** targets Quake III's 2,000 triangles per surface. |

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

## Export Budget And Detail Levels

The header's export budget reports whether the model fits the original
renderers' limits: MD3 allows 1,000 vertices and 2,000 triangles per surface,
MD2 one surface of 2,048 vertices and 4,096 triangles, and MDL one surface of
1,024 vertices and 2,048 triangles (each with its frame and surface limits). The
button reads "fits" or "over" for each format, with an icon, and opens the
details per surface with **Decimate to MD3 Budget** and **Export Quake III
Detail Levels**. It updates after every edit.

### Quake III Detail Levels

Quake III loads `name_1.md3` and `name_2.md3` beside `name.md3` as lower detail
models, chosen by distance and `r_lodbias`. **Mesh > Clean Up > Export Quake III
Detail Levels** writes the base model and two levels, each keeping half of the
previous level's triangles per surface. The levels use the same decimation as
the tool, so UV seams and every animation pose are kept; the first level also
keeps borders. A surface that cannot be reduced further keeps its previous
detail and is reported. The editable source is not changed.

## CLI

`model tool` runs one tool on an editable source and writes a new
`.mesh.json`; `model select` prints the indices an operator selects, ready to
paste into `--faces`, `--vertices` or `--edges`.

```sh
vibestudio --cli model select ./prop.mesh.json --select loop --mode edges --edge 4:5 --json
vibestudio --cli model tool ./prop.mesh.json --tool loop-cut --edges 4:5 --cuts 2 --output ./cut.mesh.json
vibestudio --cli model tool ./prop.mesh.json --tool inset --faces 0,1 --thickness 2 --depth 1 --output ./inset.mesh.json --dry-run --json
vibestudio --cli model tool ./prop.mesh.json --tool add --primitive cylinder --at 0,0,16 --size 16,16,32 --segments 12 --output ./added.mesh.json
vibestudio --cli model tool ./prop.mesh.json --tool decimate --faces all --target-triangles 2000 --output ./md3-budget.mesh.json
vibestudio --cli model lod ./prop.mesh.json --output ./out/prop.md3 --levels 2 --ratio 0.5 --json
```

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
| `symmetrize` | `--faces`, `--axis x\|y\|z`, `--direction positive\|negative`, `--threshold` |
| `loop-cut` | one `--edges` pair, `--cuts`, `--slide`, `--angle` |
| `add` | `--primitive`, `--at`, `--size`, `--segments`, `--rings`, `--up` |
| `decimate` | `--faces`, `--ratio` or `--target-triangles`, `--allow-boundary` |
| `bevel-vertices` | selection, `--width` |
| `solidify` | `--faces`, `--thickness` (negative grows outward), `--no-even` |
| `uv-cube` | `--faces`, `--tile-size` (units per repeat, default 64), `--fit` |
| `uv-view` | `--faces`, `--u-axis` and `--v-axis` (default 1,0,0 and 0,0,1, the front view), `--tile-size`, `--fit` |
| `uv-cylinder`, `uv-sphere` | `--faces`, `--axis` (default z), `--fit` |

All tools accept `--surface N` (default 0) and `--reference-frame N` (the pose
used for geometric choices, default 0), and require `--output <new.mesh.json>`.
`--dry-run` validates without writing; an existing output needs
`--overwrite`. A tool refuses options it does not take. JSON reports the
surface's vertex and triangle counts and the resulting selection.

`model lod <source> --output <name.md3>` writes the base MD3 and its detail
levels beside it, `name_1.md3` up to `name_3.md3` (`--levels 1-3`, default 2;
`--ratio 0.05-0.95`, default 0.5). The source may be an editable `.mesh.json` or
an MDL, MD2, MD3 or OBJ. `--dry-run` prepares every file without writing, and
existing files need `--overwrite`. JSON lists each file's level, triangle count,
size and notes.

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

Exit codes match `model edit`: 0 success, 1 read or write failure, 2 usage
error, 4 an edit or selection the mesh refuses.

## Limits

- Edge bevel, knife and spin tools, non-destructive modifiers, hiding geometry
  and armature-driven animation are not available yet. Use vertex bevel,
  extrude, inset, loop cut and the existing frame tools instead.
- Loops and loop cuts need the quad view; meshes triangulated without quad
  pairs (or with large folds between neighbours) stop rings at those triangles.
- Decimation keeps seam vertices and, by default, borders, so heavily seamed
  imports reduce less than a seamless mesh would.
- Proportional distances and mirror counterparts are measured in the displayed
  pose; other poses follow the same weights.
