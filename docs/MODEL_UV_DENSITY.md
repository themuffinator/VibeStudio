# Match texture density

**Available**, with the limits below. VibeStudio is pre-alpha and largely
untested in real projects. Texel density does not complete the
[modeller production gate](MODELLER_RELEASE.md).

## Measure existing islands

Open **UV → Texel Density…** in the Mesh Editor, or use **Texel Density…**
in the UV toolbar. Choose **Selected islands** or **All surface islands**.
Selected vertices, edges or faces expand to complete indexed UV islands;
this includes hidden faces belonging to those islands.

Set the original texture width and height, then choose the reference pose.
To resolve package images, choose material rendering in the modeller, wait for
image loading to finish, then open **Texel Density…**.
Resolved package materials supply their original image dimensions, including
rectangular images. Resized preview images and the fallback checker never
supply measurement dimensions. If no material dimensions are available, enter
the intended size; the initial value is explicitly 512 × 512 pixels.
**Use material size** restores the dimensions captured when opening the dialog.
Reopen it after resolving or replacing a material.

**Inspect** reports each island's density, model area, pixel area, per-face
density range, collapsed geometry/UV counts and mirrored-face count. Select a
result row to inspect or copy its complete measurements. Inspection does not
change the model.

Density is the square root of total pixel area divided by total model area.
Pixel area uses unsigned UV triangle area multiplied by texture width and
height. A 10 × 10 model-unit plane covering a 100 × 100 texture therefore has
10 pixels per model unit. The combined result uses summed areas; it is not an
arithmetic average of island densities. Overlapping and mirrored triangles
each contribute their area. This measures area density, not unique texture
occupancy or directional stretching.

## Review and apply

Set **Target px/unit**, or choose **Use measured density** to use the
selection's combined measurement. Choose one of two modes:

| Mode | Result |
| --- | --- |
| Match each island | Every chosen island reaches the target. |
| Preserve relative densities | One scale factor brings the combined selection to the target while retaining density ratios between islands. |

**Preview** prepares the candidate on a cancellable worker. Review the results
and switch between original and proposed UVs in **UV preview**, then **Apply**
as one undo step. Changing an option invalidates the reviewed result. Closing
during preparation cancels and joins the worker. A changed source document
requires a fresh review.

Each island scales uniformly around its UV bounding-box centre. Shape,
orientation and mirroring are preserved. UVs remain shared across all animation
poses; the reference pose controls measurement only. Shared corners split where
necessary, retaining pose positions, normals, seam marks and exact skin
influences. Unchanged islands retain their existing corners. Other surfaces
are unaffected.

Scaling can introduce overlaps or move islands outside the base tile. Review
coverage, then pack or reposition as appropriate. Packing or later geometry
edits may change density; inspect again afterwards. Density does not bake,
resize or modify a texture, change material paths, or automatically normalise
different surfaces with different materials.

## Automate density adjustment

```text
vibestudio --cli model uv-density actor.mesh.json --texture-size 1024x512 --json
vibestudio --cli model uv-density actor.mesh.json --texture-size 512 --faces 0,4 --target 8 --mode individual --output actor-density.mesh.json --dry-run
vibestudio --cli model uv-density actor.mesh.json --texture-size 512 --reference-frame 3 --target 8 --mode group --output actor-density.mesh.json
```

The CLI uses the same inspection, adjustment, document validation and source
writer as the GUI. `--surface` and `--reference-frame` default to 0;
`--faces` defaults to `all`. `--texture-size` is required: a square side or
`WIDTHxHEIGHT`. Face indices expand to complete islands. Without `--target`,
the command is read-only. Adjustment requires a separate `.mesh.json` output;
`--overwrite` permits an existing output but never the input or an alias of it.
Dry runs validate the candidate and output policy without writing.

JSON includes `before`, optional `after`, original dimensions, reference pose,
combined and per-island `pixelsPerUnit`, face indices, areas, ranges and repair
counts. Existing material, export, package and level-placement services consume
the resulting source normally. Export MDL/MD2/MD3/MD5/IQM through their existing
writers and review format limits before staging a package. Native UV
quantisation may change measured density, so inspect the imported export when
exact density matters. Engine acceptance remains a separate release gate.

## Understand refusals and limits

- Texture dimensions must each be 1–65,536 pixels; targets must be
  0.000001–1,000,000 pixels per model unit.
- Collapsed model or UV triangles are reported by inspection and refuse
  adjustment. Repair or unwrap them first. Mirrored islands are supported.
- New corners must fit the shared document vertex and animation storage limits.
  UV coordinates must remain within the supported range, ±1,000,000.
- Stored float UVs must reproduce the requested density to 0.01% relative
  tolerance. Very small islands far from the origin can lose precision;
  move them nearer the origin or use a larger target.
- Inspection and adjustment are atomic on failure or cancellation. Source
  metadata, native export budgets and save limits still apply.
- Texture editing/baking, multi-material batch matching, anisotropic stretch
  correction, maximum-size performance, real-engine delivery and native
  accessibility acceptance remain open work.

The original implementation follows the familiar island-scale workflow;
[credits](CREDITS.md#blender-style-mesh-editing-2026-10-07) identify the behavioural
reference. No external implementation, manual prose or assets were imported.
