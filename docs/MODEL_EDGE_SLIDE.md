# Slide mesh edges

**Available** — edge chains and loops can slide along the surrounding quad
faces through the Mesh Editor and `model tool --tool edge-slide`. VibeStudio
remains pre-alpha; this tool does not establish the wider modeller release gate.

## Shape an existing loop

1. Select a continuous edge chain or loop. Separate chains can be edited together.
2. Choose **Edge → Edge Slide**, or find **Edge Slide** in command search.
3. Follow the displayed rails with the pointer, or type a signed fraction.
   `0.25` moves a quarter of the way towards one rail; `-0.25` uses the other.
   Hold **Ctrl** to snap to tenths and **Shift** for finer pointer movement.
4. Confirm with **Enter**, **Space** or a click; **Esc** cancels the preview.
5. Use **Adjust Last Operation** to change the amount, choose **Distance in
   reference pose**, or adjust the **Quad pairing angle**. An accepted change
   replaces the last operation and remains one undo step.

Invalid adjustments restore the accepted values and leave history untouched.
Setting the amount to zero removes that operation and clears its adjustment
controls; start **Edge Slide** again to make a new edit. Long adjustment forms
scroll inside the viewport, including with enlarged text, while leaving part
of the model view visible. The preview banner shows the current fraction;
viewport/status tooltips and accessibility descriptions carry detailed controls.

The regular **All frames / Current frame** scope applies. Distances are model
units in the displayed reference pose. Other edited poses follow the same
fraction along their corresponding rails, so a two-unit change in a reference
pose can become four units in a pose that is twice as wide. No pose is silently
flattened into the reference geometry.

The tool has a named command in every control profile. It has no assigned
default shortcut; use command search or customise its binding. Axis constraints,
proportional editing and mirrored transforms do not change the rail path.

## Understand the rails

The existing [quad view](MODEL_TOOLS.md#selecting) pairs consistently wound,
convex neighbouring triangles, using a default 40-degree pairing limit. The
selected edges must be sides of these quads. Shared diagonals, branched edges,
selected junctions, ambiguous corners and adjacent unpaired triangles are
refused with a reason. A chain may stop at an existing vertex; surrounding
unselected faces still follow that endpoint and are checked too.

Open chains are oriented from their lowest-index endpoint. Closed loops begin
at their lowest-index geometric vertex towards its lowest-index selected
neighbour. The winding side is positive. A chain beginning on a one-sided
border makes its available side positive. Every adjoining edge must agree on
the rail at their common vertex. A missing side is refused when chosen.
Separate chains use this rule independently; selector insertion order has no
effect. Inspect the preview when sliding several disconnected chains.

Fractions must lie strictly between `-1` and `1`. Fixed distances must stop
before every neighbouring rail vertex. Pointer motion is bounded just inside
the endpoints; typed or numeric values are validated without merging geometry.
The entire edit is refused if any affected face collapses or reverses its
winding relative to its original face in an edited sample. This is a local
face check, not a global self-intersection test; use **Health** for the latter.

## Keep authored data

No vertices or faces are added, removed or reindexed. Coincident seam copies
move together only when their positions agree in every retained pose. UV
coordinates and seam marks remain attached to their existing vertices, so
textures can stretch with the changed shape; automatic UV correction is not
provided. Affected normals are refreshed while preserving existing hard-normal
splits. Other surfaces, tags, materials, skins, collision tracks and animation
metadata remain intact.

Skin influence assignments stay unchanged. Mesh edits remain independently
authored geometry; use the normal [skin rebinding workflow](MODEL_SKINNING.md)
when the bind shape needs to drive subsequent skeletal rebakes. Review
collision and refresh native derivatives, level placements and staged packages
after accepting the new shape. Native exports retain their own format limits.

## Automate an edge slide

```sh
vibestudio --cli model tool models/prop.mesh.json --tool edge-slide --edges 2:7,7:12,12:17 --slide 0.25 --output models/slid.mesh.json --dry-run --json
vibestudio --cli model tool models/prop.mesh.json --tool edge-slide --edges 2:7,7:12,12:17 --distance -2 --reference-frame 0 --frame all --output models/slid.mesh.json --json
```

Use exactly one of `--slide` and `--distance`, with an explicit edge selection.
`--surface` defaults to zero, `--reference-frame` defaults to zero and `--frame`
defaults to `all`; a frame index limits position/normal changes to that sample.
`--angle` changes quad pairing. Repeated, malformed and inapplicable options
are refused. JSON includes the resulting selection and surface counts.

GUI and CLI use the same `SlideEdges` document transaction, validation,
cancellation and guarded source writer. `--dry-run` writes nothing;
`--overwrite` is required to replace an existing output. Saves and recovery
use the existing source schema; no dependency, connector or setup step is added.

Knife and spin tools, UV correction, maximum-size acceptance,
physical accessibility checks and real-project/engine validation remain open.
For chamfers and rounded corners, use [Bevel Edges](MODEL_EDGE_BEVEL.md).
