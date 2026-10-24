# Bevel mesh edges

**Available** — **Bevel Edges** creates chamfers and rounded bands on selected
edges through the Mesh Editor and `model tool --tool bevel-edges`. VibeStudio
remains pre-alpha and largely untested in real projects; this operation does
not establish the wider [modeller release gate](MODELLER_RELEASE.md).

## Shape an edge

1. Select edges between two faces. Joined corners, separate edges and loops
   can share one operation. Face/vertex selection converts to edges, omitting
   inferred quad diagonals. Explicit edge selectors remain exact.
2. Choose **Edge → Bevel Edges**, use command search, or press **Ctrl+B** in
   the VibeStudio or Blender profile. **Ctrl+Shift+B** retains vertex bevel.
3. Move away from the selection centre or type the width. **Ctrl** snaps the
   pointer width to the translation grid; **Shift** gives finer pointer motion.
   The wheel or **Page Up/Down** changes the segment count from 1 to 32.
4. Confirm with **Enter**, **Space** or a click. **Esc** discards the preview.
   Preparation runs through the cancellable model worker and leaves the source
   and undo history untouched until confirmation.
5. Use **Adjust Last Operation** (**F9** in the VibeStudio/Blender profile) to
   change **Width**, **Segments**, **Roundness**, **Units per texture repeat**
   or **Quad pairing angle**. Accepted adjustments replace the original edit
   as one undo step. Invalid adjustments restore the accepted values.

Starting a new modal preview hides the previous adjustment panel; F9 can reopen
it. A compact refusal banner leaves the model visible, with the full reason in
the banner/status tooltips and accessibility descriptions.

Width is a distance along neighbouring original edges in the reference pose,
rather than a perpendicular face offset. Other poses use the same interpolation
fractions. One segment makes a straight chamfer; additional segments sample a
quadratic profile. Roundness ranges from 0 (straight) to 1 (rounded), with 1 as
the default. It is not an exact circular radius.

Topology changes in every retained pose regardless of the current frame scope.
Axis constraints, proportional editing and mirrored transforms do not alter
a bevel. Zero width makes no change; setting it through F9 removes that last
operation and clears its adjustment controls. The named command is available
in all profiles; 3ds Max and MilkShape keep their existing shortcuts.

## Keep corners and authored data

The existing convex quad interpretation uses a 40-degree pairing limit by
default. Explicitly selected edges and marked UV seams remain triangle sides,
even when they could otherwise become paired diagonals. Selected edges need
two consistently wound faces and a connected manifold fan at each endpoint.
Open sheet borders themselves cannot be bevelled; an interior edge ending at
a sheet border can be, and its new end remains open. Closed fans get junction
caps. Collinear chamfer junctions are stitched without zero-area caps.

Cuts must stop before neighbours. Mitre offsets at a corner must fit inside
its neighbouring edge lengths. Retained faces, bands and caps are checked for
collapse and local winding changes in every stored pose; the result must
preserve closed neighbourhoods. Ambiguous, over-wide or folded results are
refused as a whole. These are local topology checks; use **Health** to review
distant self-intersections. Very small cuts at large coordinates can exceed
the mesh's floating-point precision.

Original face corners keep independently interpolated UVs and normals, including
seam copies. New bands have separate UV strips and caps have projected UVs;
their borders are marked as seams. **Units per texture repeat** defaults to 64.
These islands can overlap existing UVs: use **Pack Around Unselected** or the
ordinary UV workflow for an atlas. New bands are smooth across their segments,
with hard indexed boundaries against retained faces and caps.

New skinned vertices combine source influences and joint-local offsets using
the same interpolation as their geometry. Their motion survives a subsequent
skeletal bake without nearest-vertex reassignment. Negative source weights on
an interpolated corner and combinations above 64 influences are refused for
explicit repair. Original unchanged influences stay intact. New band/cap
normals are rebuilt from their faces on a skeletal rebake.

Other surfaces, materials, tags, animation ranges and collision tracks remain
attached to the document. New bands and caps become the face selection. Review
collision, refresh native derivatives and check format budgets before staging
the result for a package or level. MDL/MD2/MD3 and skeletal exports retain their
existing limits and skin requirements; the authoring source remains `.mesh.json`.

## Automate an edge bevel

```sh
vibestudio --cli model tool models/prop.mesh.json --tool bevel-edges --edges 0:1,1:2 --width 2 --segments 4 --roundness 1 --output models/bevelled.mesh.json --dry-run --json
vibestudio --cli model tool models/prop.mesh.json --tool bevel-edges --edges 0:1,1:2 --width 2 --segments 4 --roundness 0.5 --tile-size 64 --angle 40 --reference-frame 0 --output models/bevelled.mesh.json --json
```

Use existing indexed edge pairs; the example indices are placeholders for your
mesh. `--surface` and `--reference-frame` default to zero. Width defaults to 2,
segments to 1 and roundness to 1. `--frame` is inapplicable because topology is
shared across poses. Repeated, malformed and unrelated options are refused.
JSON reports surface counts and the resulting selection. `--dry-run` writes
nothing; `--overwrite` is required to replace an existing output, and the input
source is protected by the shared writer.

GUI, CLI, undo, source save and recovery use the same document transaction and
existing source schema. No dependency, connector or setup step is added.
[Knife paths](MODEL_KNIFE.md) and [Spin](MODEL_SPIN.md) share these document services.
Richer junction profiles, automatic overlap limiting, global
intersection prevention, maximum-size bevel latency, real-project/engine
acceptance and physical accessibility/cross-platform review remain open.
