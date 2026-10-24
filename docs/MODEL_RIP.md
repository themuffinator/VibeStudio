# Open and fill mesh rips

**Available** — **Rip** opens a connected vertex or edge selection and moves
one adjoining side. **Rip and Fill** adds faces across that gap. Both use the
shared Mesh Editor document and CLI service. VibeStudio is pre-alpha and
largely untested in real projects; [production acceptance](MODELLER_RELEASE.md)
remains open.

## Rip a vertex or edge chain

1. Select a vertex or an unbranched, connected edge chain between faces.
2. Choose **Vertex → Rip** or **Edge → Rip**. **Rip and Fill** starts with the
   gap filled. The VibeStudio and Blender profiles also use **V** and **Alt+V**.
3. The face under the starting pointer chooses the moving side. When the
   pointer misses an incident face, the nearest visible incident face centre
   is used. **B** swaps the side; **F** toggles filling. The starting face stays
   fixed while moving.
4. Move or type a distance. **X/Y/Z** constrains to a world axis; pressing the
   same key again uses the selection's local axis, and a third press clears it.
   **Shift+X/Y/Z** constrains to a plane. **Ctrl** temporarily toggles snapping
   and **Shift** slows pointer movement, following the current control profile.
5. **Enter/Space** confirms; **Esc** cancels the entire candidate. The moved
   vertices or edges stay selected. **F9** adjusts the XYZ offset, side face,
   opposite side, fill, UV scale and quad-pairing angle. Refused adjustments
   restore the accepted result; all adjustments remain one undo step.

**Rip** can be confirmed immediately to split without moving. Its preview
already contains the detached connectivity. Moving the side back onto its
original position, saving, reopening or using another mesh tool preserves
the split. **Rip and Fill** needs a nonzero offset to create gap faces;
switch filling off to keep a zero-distance split. Cancelling either preview
restores the original connectivity.

An interior chain endpoint extends the cut along an adjoining edge. A single
interior vertex chooses two opposing adjoining edges; a boundary vertex needs
one interior edge. The side face and reference pose resolve these directions.
Quad interpretation avoids automatically choosing triangulation diagonals;
an explicitly selected diagonal remains usable. Inspect the preview before
confirming. Face-region detachment uses **Separate Faces** instead.

Topology changes in every retained pose regardless of frame scope. Rip does
not use proportional or mirror-transform settings. Source UV corners stay
separate; every filled cut gets its own strip chart, measured in the reference
pose. **Units per texture repeat** defaults to 64. Pack or edit generated UVs
for the intended skin layout.

## Preserve the rig and asset workflow

Geometric adjacency joins indexed UV and normal copies only when their
positions coincide in every stored pose and their source connectivity
identities match. Rip assigns a separate identity to the moving side,
duplicates the required indexed
corners and keeps their source joints, weights and UV coordinates. On a
skeletal surface, movement is expressed through joint-local offsets using a
bind or retained skeletal reference pose; other bound poses follow the rig.
Independent baked frames move their own stored positions directly, but cannot
serve as the skeletal reference. Existing authored deviations in bound poses
remain; explicitly rebaking still replaces them under the normal bake contract.

Neighbouring normals are rebuilt using the existing hard/smooth splits. Fill
strips have separate normals at the original boundary. Negative or zero-total
weights, more than 64 influences, singular reference transforms, incomplete
clip mappings and diverging seam bindings require repair before ripping.

The transaction retains other surfaces, material references, tags, animation
definitions and collision data. Review **Health**, face orientation and every
pose, update collision as needed, check native format budgets and regenerate
exports before staging the asset for packages or level placement. Keep the
`.mesh.json` source. MDL/MD2/MD3 skin rules and MD5/IQM export limits still apply.
Editable source version 11 retains these identities through mesh, UV, surface,
history and recovery operations. Game formats generally lack this metadata:
reimporting an exported model infers connectivity from its retained positions.
Keep the editable source for subsequent authoring. **Merge** and **Weld**
explicitly reconnect compatible selected vertices; **Join Surfaces** retains
the separate connectivity of its input surfaces.

## Automate a rip

```sh
vibestudio --cli model tool models/prop.mesh.json --tool rip --vertices 4 --side-face 3 --offset 2,0,0 --output models/ripped.mesh.json --dry-run --json
vibestudio --cli model tool models/prop.mesh.json --tool rip --edges 4:7,7:10 --side-face 6 --offset 0,2,0 --fill --output models/filled.mesh.json --json
```

Use actual component indices from the source model. `--side-face` and
`--offset` are required; `--opposite` chooses the other side of that face.
Use `--offset 0,0,0` without `--fill` for a persistent split without movement.
`--fill` enables gap faces, `--tile-size` changes their UV scale and `--angle`
changes quad pairing. `--surface` and `--reference-frame` default to zero.
`--frame` is inapplicable because topology is shared. A dry run performs the
full edit and output validation without writing; replacing an existing output
requires `--overwrite`. The shared writer protects source paths.

## Current limits

Disconnected selections, branched junctions, ambiguous vertex fans,
non-manifold or inconsistently wound neighbourhoods, collapsed or reversed
neighbouring faces, folded fills and document capacity overflow are refused
without changing the source. These checks do not detect every distant
self-intersection.

Richer rip junctions remain planned. Maximum-size
latency, original-engine and real-project checks, physical accessibility
review and clean macOS/Linux acceptance also remain open.

Persistent connectivity uses editable source version 11. It adds no dependency,
connector or first-run setup requirement.
