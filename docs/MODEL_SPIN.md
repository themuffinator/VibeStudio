# Spin profiles and radial copies

**Available** — **Spin** makes curved extrusions from face regions or open
border edges. **Spin Duplicates** distributes selected faces around an axis.
Both use the shared Mesh Editor document and CLI service. VibeStudio remains
pre-alpha; [release acceptance](MODELLER_RELEASE.md) is still open.

## Make a curved extrusion

1. Select a face region or open border edges. Place the **3D Cursor** at the
   intended centre, then choose **Face → Spin** or **Edge → Spin**.
2. Move around the centre or type an angle in degrees. **X/Y/Z** chooses a
   world axis; pressing the same key again chooses the selection's local axis,
   and a third press returns to the view axis. The default is the view axis.
3. Use the wheel or **Page Up/Down** to change the number of steps. **Ctrl**
   temporarily toggles the existing rotation-grid snap. **Enter/Space** confirms;
   **Esc** discards the preview.
4. **F9** opens **Adjust Last Operation** for angle, steps, centre, axis,
   duplication, full-turn closure and UV scale. Accepted adjustments replace
   the operation as one undo step; refused values restore the accepted result.

Angles range from −360 to 360 degrees and steps from 1 to 256. A connected
extrusion needs less than 180 degrees per step. A closed full turn needs at
least three steps. Zero-angle extrusion makes no change. Topology changes in
every retained pose, regardless of the current frame scope. Proportional and
mirror transforms do not modify Spin.

A partial face extrusion replaces the original selected faces with side bands
and an end cap. On a detached sheet, the starting end remains open. A complete
360-degree face spin closes the bands and omits the end caps when **Close full
turn** is enabled. The profile must be detached at its boundary; a cap attached
to a larger body cannot close a full turn. Profile points on the axis form
triangle fans without collapsed faces. Open-border extrusion retains the source
faces and selects the new leading edges; automatic full-turn closure is not
available for that mode.

Use **Face → Spin Duplicates** for radial copies of a face region or a complete
object. It retains the original geometry and its UVs and skin bindings. With
closure enabled, a 360-degree spin of eight steps produces eight total islands,
including the original. Other angles produce the original plus eight copies.
Disable **Close full turn** only when the coincident endpoint is intentional.

## Retain animation and finish the asset

Geometric adjacency is shared across indexed UV and hard-normal seams only
where vertices coincide in every retained pose. Inconsistent face winding,
non-boundary edge selections, collapsed faces, locally folded bands and
document capacity overflow are refused before committing. These checks do not
prevent distant self-intersections. Review **Health**, face orientation and all
animation poses before export.

End caps and copies keep source UVs and seam marks. Each new border band gets
a separate strip chart, with arc distance and profile length measured in the
reference pose. **Units per texture repeat** defaults to 64. Strips smooth
along angular steps and keep hard boundaries between profile edges. The new
UV islands can overlap: pack or edit them for the target skin layout.

For a skeletal surface, select the bind pose or a retained skeletal pose as
the reference. New vertices keep each source vertex's joints and weights.
Joint-local offset changes reproduce the requested rotation in that reference
pose; other bound poses follow the rig. Independently authored baked frames
rotate their stored geometry directly. They cannot serve as the skeletal
reference. Negative weights, zero total weight, more than 64 influences and
singular reference transforms require repair. Existing independent edits to
bound pose geometry are retained; rebaking the skeleton still replaces those
edits according to the ordinary skeletal bake contract.

Other surfaces, material paths, attachment tags, animation definitions and
collision data remain in the document. Review collision after changing the
shape, refresh native derivatives, check format budgets, then stage the result
for package or level use. MDL/MD2/MD3 skin requirements and skeletal export
limits still apply. Keep `.mesh.json` as the editable source.

## Automate a spin

```sh
vibestudio --cli model tool models/profile.mesh.json --tool spin --faces all --centre 0,0,0 --axis z --angle 360 --steps 16 --output models/ring.mesh.json --dry-run --json
vibestudio --cli model tool models/part.mesh.json --tool spin --faces all --centre 0,0,16 --axis-vector 0,0,1 --angle 360 --steps 8 --duplicate --output models/radial.mesh.json --json
```

Use `--faces` or `--edges` with the existing component syntax. Duplication
requires faces. `--centre` defaults to `0,0,0`, `--axis` to `z`, `--angle` to
90, and `--steps` to 8. Use either `--axis x|y|z` or `--axis-vector x,y,z`,
never both; the vector is normalised. `--no-close` disables full-turn closure.
`--tile-size` controls strip UV scale. `--surface` and `--reference-frame`
default to zero. `--frame` is inapplicable because topology is shared.

Dry runs perform the full edit and output validation without writing. Replacing
an existing output requires `--overwrite`; the shared writer protects inputs.
GUI previews, worker cancellation, undo, recovery and CLI edits use the same
candidate transaction. No dependency, connector or setup step is added.

Automatic overlap limiting, general profile self-intersection prevention,
maximum-size latency, real-project and engine checks, physical accessibility
review and cross-platform acceptance remain open.
