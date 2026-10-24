# Cut mesh paths

**Available** — **Knife** cuts drawn paths into the active mesh surface, through
the Mesh Editor and `model tool --tool knife`. VibeStudio remains pre-alpha and
largely untested in real projects. This operation does not establish the wider
[modeller release gate](MODELLER_RELEASE.md).

## Draw a cut

1. Choose **Mesh → Knife**, the tool shelf or command search. VibeStudio and
   Blender profiles assign **K**; every profile exposes the named command.
2. Click to place the first point, then further points. The preview includes
   the segment from the last fixed point to the crosshair. Points may lie
   inside faces or outside the mesh. Paths may cross or close on themselves.
3. **N** or right-click finishes the current chain and starts another within
   the same draft. **Backspace** or **Ctrl+Z** removes the last fixed point.
4. **Enter** or **Space** applies the complete preview as one undo step.
   **Esc** discards it. A refused confirmation retains the editable draft.
5. **Adjust Last Operation** (**F9** in VibeStudio/Blender) changes **Cut through**
   and **Selected faces only**, replaying the saved path as one undo step.

**Repeat Last** reuses the recorded path, projection and reference pose with
the current selection and hidden faces. Start a new knife draft to draw from
a different view or pose.

The crosshair can also move with the arrow keys: ten logical pixels per step,
or one with **Alt**. **Insert** places a point. These controls use the same
draft and preview as pointer authoring. Full controls and refusal details are
available from the status/banner tooltip and accessibility description.

| Control | Behaviour |
| --- | --- |
| **C** | Toggle **Cut through**. The default cuts the nearest visible face along each part of the path. |
| **S** | Toggle **Selected faces only**. The default cuts any eligible face of the active surface, even with an empty selection. |
| **A** | Toggle screen-plane angle snapping, initially 45°. Type an angle from 1° to 180°; the wheel changes it in 5° steps. |
| **Shift** | Snap to nearby edge midpoints. |
| **Ctrl** | Ignore geometry/path snapping; an enabled angle constraint still applies. |
| No modifier | Snap within ten logical pixels to visible vertices and earlier path points, then to edge points when no anchor is nearby. |

Angle constraints take precedence over a nearby off-angle snap target. A snap
marker, anchors, line style and written status complement colour. Geometry
snapping uses the current rendered view; a depth-checked one-pixel neighbourhood
handles silhouette corners, and it waits for fresh picking data. Transform
handles are suppressed while the knife draft is active.

Changing the displayed pose, editing or replacing the mesh, changing hidden
faces, rotating the view or moving a perspective camera requires cancelling
and restarting the path. Confirmation checks this context even when the pointer
has not moved; cancellation preserves the current model and visibility.
Orthographic pan, zoom and widget
resize retain the path's model coordinates and refresh snap projections.
Transform axis locks, symmetry and proportional editing do not alter a knife.

## Preserve the model

Visible cuts account for other surfaces as occluders, including where the
nearest face changes inside a single stroke. **Cut through** reaches all
eligible faces of the active surface. Other surfaces are never cut. Hidden
faces neither receive path cuts nor occlude them. An unselected visible face
can still occlude a selected face.

Boundary points subdivide every adjacent geometric edge copy, including hidden
and unselected neighbours, to avoid cracks. Child faces retain their original
hidden state in previews, commits and F9 replays. This is a display state;
ordinary undo/redo follows the editor's existing topology visibility reset.

The cut is planned in the reference pose and applied with the same interpolation
to every retained pose. New vertices interpolate UVs, normals, skin influences
and joint-local offsets. Indexed UV and hard-normal copies remain separate;
explicit seam marks follow their replacement edges. A skeletal rebake therefore
retains the new vertices' motion. Negative contributing weights and combinations
above 64 influences require repair before cutting.

The result selects the actual path edges. Extra triangulation edges may be
needed around interior endpoints and intersecting paths. Material slots, other
surfaces, tags, animation ranges and collision tracks remain attached. Save the
authoring `.mesh.json`, check target budgets, review collision and refresh native
derivatives before staging for a package or placing in a level.

Paths are limited to 256 segments. Planning accepts up to 262,144 face spans;
a single face permits at most 512 spans and 1,024 local intersection points.
Close, degenerate or over-complex intersections and any new face collapse or
winding reversal in a retained pose refuse the transaction. Use a simpler path
or move points away from nearly coincident edges. The existing document limits
still apply. Preparation is cancellable and leaves source/history untouched.

## Automate a knife cut

```sh
vibestudio --cli model tool models/prop.mesh.json --tool knife --view xy --path '-40,3;40,3' --output models/cut.mesh.json --dry-run --json
vibestudio --cli model tool models/prop.mesh.json --tool knife --view xy --path '-40,3;40,3|0,-40;0,40' --through --output models/cut.mesh.json --json
vibestudio --cli model tool models/prop.mesh.json --tool knife --path-file cuts.json --perspective --camera-origin 0,0,128 --output models/cut.mesh.json --json
```

Supply exactly one of `--path` and `--path-file`. Inline paths use `u,v` points,
semicolons between points and `|` between disconnected paths. Quote the entire
value in the shell. JSON files use an array of paths, for example:

```json
[[[-0.3, 0], [0.3, 0]], [[0, -0.3], [0, 0.3]]]
```

Both inputs are bounded to 64 KiB. Coordinates must be finite and within
±1,000,000,000. Orthographic path coordinates are model units relative to
`--camera-origin` (default `0,0,0`) along the view's right/up axes. Perspective
coordinates are right/depth and up/depth on a unit focal plane; they are not
pixels. `--perspective` requires an explicit camera origin. Geometry is clipped
at depth 0.0001 for planning.

| Option | Meaning |
| --- | --- |
| `--view xy` | Default: right +X, up +Y, look towards −Z. |
| `--view xz` | Right +X, up +Z, look towards +Y. |
| `--view yz` | Right +Y, up +Z, look towards −X. |
| `--opposite` | Reverse the look direction without changing right/up coordinates. |
| `--camera-right`, `--camera-up`, `--camera-forward` | Three comma-separated, perpendicular unit vectors; provide all three together, without `--view` or `--opposite`. |
| `--through` | Cut all intersected eligible active-surface faces. |
| `--faces` | Comma-separated face indices; restrict path cuts to those faces. |
| `--surface`, `--reference-frame` | Active surface and pose, both default zero. |

Camera coordinates are bounded to ±1,000,000. Topology always spans all retained
poses, so `--frame` is inapplicable. Repeated, malformed and unrelated options
are refused. JSON output reports the resulting selection and surface counts.
`--dry-run` writes nothing; existing outputs require `--overwrite`, and the
input source is protected by the shared writer. A `--path-file` input is also
protected, including hard-link aliases, even with `--overwrite` or `--dry-run`.
Dry runs share complete source-save preparation and the serialised byte limit.

No dependency, connector or setup step is added. Curved strokes, saved editable
knife objects, automatic repair of near-coincident cuts and maximum-size latency
acceptance remain open. Real-project/engine, physical accessibility and clean
cross-platform acceptance remain part of the wider release gate.
