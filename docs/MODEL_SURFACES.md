# Model Surface Authoring

Use **Mesh Editor > Surface > Manage Surfaces** to review a surface operation.
Each accepted operation runs on the document worker, validates every stored pose
and commits one undo step. Cancellation or validation failure leaves the source,
selection and history unchanged. The normal recovery, save, export, package
staging and level handoff paths retain the result.

| Operation | Result |
| --- | --- |
| Rename | Change the active surface's unique name; retain geometry and selection. |
| Separate selected faces | Move faces into a named new surface with the same ordered material slots. Shared boundary vertices are copied with exact attributes. |
| Move selected faces | Append faces to an existing target, preserving its existing vertex and face indices. |
| Duplicate | Copy the entire active surface, including unused vertices, every pose, UVs, normals, seams and material slots. |
| Delete | Remove the active surface; the final remaining surface cannot be deleted. |
| Join | Combine checked surfaces into a checked target, retaining its name and material slots. Target geometry comes first, then other sources in ascending surface-index order. |

Separate and Move require a nonempty face-only selection. They preserve the
original face order, winding, UVs, normals and animation data without welding,
averaging or recalculating attributes. Partitioned vertices remain in source
index order. Seam marks follow actual surviving edges. Vertices used only by
moved faces leave the original surface; originally unused vertices remain there.
If every face is separated, the operation renames the existing surface in place.
If every face is moved, the whole surface (including unused vertices) is appended
to the target and the source surface is removed. Neither operation leaves an
empty surface.

New or moved faces become the active selection for UV projection, material
assignment, finishing or transforms. Duplicate selects all copied faces; Join
selects the resulting whole surface when invoked from Surfaces mode, or all
combined faces when invoked from component mode. Delete clears selection and chooses a
surviving surface. Undo and redo restore the corresponding selection and surface
order. These operations always span every animation pose regardless of the
Geometry inspector's position-transform scope. Tags, collision boxes, animation
clips, embedded skins and MDL settings are model-global and remain unchanged.

## Select and Transform Surfaces

Choose **Surfaces** in the mesh selection-mode control. Clicking a visible face
selects its whole surface; Control-click toggles membership across surfaces.
The table lists names, vertex/face counts and material bindings. Its standard
extended selection, Shift ranges, Control toggles and Select All operate on
surfaces. Moving table focus to a selected row makes it the active inspector
surface. Choosing the surface combo adds that surface and makes it active.
Removing the active member selects the lowest remaining index as the active one.

The document retains the complete selection through edits, undo/redo and recovery.
Selecting alone does not dirty the mesh or create an undo entry. Normal source
save/export stores geometry and metadata; opening a source begins with an empty
selection. Recovery also retains the active surface and displayed frame.

Move, Rotate and Scale handles and **Geometry > Apply Transform** use the same
selected set. Every vertex participates, including unused vertices. **Selection
centre** is the centre of one combined bounding box in the displayed pose, so
separate pieces rotate and scale together. All-frame edits use that fixed pivot
in every pose; current-frame edits affect only the displayed pose. Origin/custom
pivots and translation/angle/scale snapping work as for component transforms.
**Transform axes > Selection** uses the first usable face on the active surface
in the displayed pose. That basis stays fixed for every selected surface and
affected pose. World and Custom axes share the same numeric and viewport path.
Numeric negative scale reverses winding on the selected surfaces and requires
all frames. Handles retain positive scale. Preview cancellation changes neither
the document nor history; each accepted gesture is one atomic, cancellable edit.

UVs, seams, material slots, animation clips, attachment tags and collision boxes
are not transformed with mesh positions. Review tag and collision placement after
moving geometry. Unselected surfaces retain their exact attributes. Component
geometry and UV actions require switching to Faces, Vertices or Edges; they cannot
silently operate on one member of a whole-surface selection. Material controls
explicitly inspect/edit the active surface. Manage Surfaces seeds its Join
checklist from the selected set; other reviewed actions name their active source.
Join remaps the selection to its surviving target. Undo restores the prior set.

```sh
vibestudio --cli model edit parts.mesh.json --operation transform --surfaces 0,2 --offset 0,0,8 --snap-grid 1 --output raised.mesh.json --dry-run --json
vibestudio --cli model edit parts.mesh.json --operation transform --surfaces all --rotate 0,0,90 --pivot-mode selection --pivot-frame 1 --output rotated.mesh.json
```

`--surfaces` accepts `all` or unique comma-separated indices and applies only to
`model edit --operation transform`. It cannot be combined with `--surface`,
`--vertices`, `--faces` or `--edges`, and cannot be repeated. JSON reports the sorted
`selectedSurfaces` set. Frame, pivot, snapping, dry-run, output and overwrite rules
are the normal [mesh transform rules](MODEL_MESH.md#cli). The CLI's default pivot
remains the origin; choose `--pivot-mode selection` to match the GUI default.

## Materials and Names

Move and Join require identical **ordered material slot lists** by default. A
different alternate slot is a mismatch even when primary materials match. The
explicit **Use target material bindings** checkbox replaces all incoming
bindings with the target's list. Its checked state resets when the operation
changes. Geometry and UVs remain exact; the material reassignment is intentional.
There is no automatic image atlas, UV rebake or shader merge.

The source supports unique names of 1–128 characters without control characters.
Native exports keep their stricter requirements. For example, MD3 uses ASCII
names and rejects names that collide under its engine normalization. Rename
provides a way to resolve those export errors. External `.skin` files and other
name-based references are not rewritten automatically. Review those dependencies
after renaming, separating or deleting surfaces. The editor clears its previous
skin-import receipt after a surface change and reloads material images against
the new surface indices.

Join can prepare a multi-surface source for a single-surface format such as MD2
or MDL. All other export restrictions, including per-format vertex/triangle
limits and supported tag/skin data, still apply; export never silently drops
unsupported data. Keep the editable source for source-only seams, collision and
other metadata. See [editable meshes](MODEL_MESH.md) for native MDL authoring and
material dependency review.

## CLI

The `model surfaces` command uses the same document operations as the GUI.
Indices are zero-based. `--input` can replace the positional source.

```sh
vibestudio --cli model surfaces prop.mesh.json --json
vibestudio --cli model surfaces prop.mesh.json --operation rename --surface 0 --name body --output named.mesh.json
vibestudio --cli model surfaces prop.mesh.json --operation separate --surface 0 --faces 0,2,3 --name panel --output parts.mesh.json
vibestudio --cli model surfaces parts.mesh.json --operation move --surface 1 --faces all --target-surface 0 --output moved.mesh.json
vibestudio --cli model surfaces prop.mesh.json --operation duplicate --surface 0 --name body_copy --output copied.mesh.json
vibestudio --cli model surfaces parts.mesh.json --operation delete --surface 1 --output remaining.mesh.json
vibestudio --cli model surfaces parts.mesh.json --operation join --surfaces 0,1 --target-surface 0 --adopt-target-materials --output joined.mesh.json --dry-run --json
```

Omitting `--operation`, or using `list`, reports each surface's index, name,
vertex/triangle/seam counts and ordered materials without writing. Source edits
require `--surface`; Join instead requires `--surfaces` and `--target-surface`.
Separate and Move require `--faces all` or unique comma-separated face indices.
Move also requires a different `--target-surface`. Rename, Separate and Duplicate
require `--name`. Only Move and Join accept `--adopt-target-materials`.

Edits require an output ending in `.mesh.json`. `--dry-run` validates the candidate
and destination without writing. Existing outputs require `--overwrite`, including
during a dry run. Normal source fingerprint and atomic-write protection still
apply. JSON includes the resulting surfaces, frame count, selected surface and
face count, write status, operation and material-adoption choice. Invalid syntax,
repeated or unrelated options return 2; invalid geometry, selection or material
choices return 4; file read/write failures return 1.

## Limits

The existing document limits apply: 32 surfaces, 65,536 total vertices, 131,072
triangles, 1,024 poses, 1,048,576 stored frame vertices and a 64 MiB serialized
source. Separating or moving shared boundaries can increase the vertex and pose
storage counts. Capacity is checked before copying those vertices. Duplicate
checks vertices, triangles, pose storage and surface capacity. Join retains all
vertices, including coincident and unused ones; explicit welding and cleanup
remain separate edits.

Individual face, edge and vertex selections still belong to one active surface;
Surfaces mode transforms whole surfaces together. The manager does not reorganize
external model files or attachments.
The [Material Slots](MODEL_MATERIAL_SLOTS.md) workflow authors and reorders all
external bindings; Assign Material edits the selected preview slot. The surface
manager preserves those slots or explicitly adopts the target's complete
list. In particular, joining alone cannot convert alternate TGA references into
the PCX paths required by original Quake II MD2 export.
