# Skin weights and binding

**Status: Partial.** Influence inspection, vertex selection, rigid and distance
binding, explicit rebinding, weight reduction and unbinding are available through
the shared mesh document and CLI. [Staged weight painting](MODEL_WEIGHT_PAINT.md)
and [mirrored/spatial transfer](MODEL_WEIGHT_TRANSFER.md) are available; retargeting remains planned. Mesh, UV and surface
edits, and frame edits, keep influences through [per-vertex edit provenance](MODEL_SKIN_PROVENANCE.md); see
[Weights through other tools](#weights-through-other-tools). VibeStudio is pre-alpha and largely
untested in real projects.

## Inspect and select influences

Open **Mesh Editor** > **Skeleton** > **Skin Weights and Binding…**. The same
page is called **Armature**, **Hierarchy** or **Joints** in other modeller
profiles. Choose **Inspect weights**, a surface and a scope, then **Review**.
The initial inspection starts automatically.

The **Weights** table shows surface and vertex indices, exact joint weights,
their sum and issue labels. Duplicate entries for one joint are summed for
display and selection. The influence count measures distinct joints with a
positive combined weight, not the number of stored entries.

Select a table row to read or copy its complete weights in the detail field.
This remains available when columns are narrow or text is enlarged. Issues include:

| Filter | Meaning |
| --- | --- |
| Joint weight range | Inclusive raw combined weight for the selected joint; absent joints count as zero. |
| Unbound | No positive combined joint weight, including an unskinned vertex. |
| Not normalised | Raw weight sum differs from one by more than 0.00001. |
| Negative weights | At least one stored influence is negative. |
| Duplicate joints | A joint occurs more than once among the vertex's stored influences. |
| Distinct bind offsets | A positive influence transforms to a different position from the retained bind vertex, beyond 0.0001 units per axis. Requires a retained bind frame. |
| Over influence limit | More positive combined joint weights than **Maximum influences**. |

**Select vertices** returns matching components to the editor without editing
the model. Use the normal transform, selection and numeric weight tools on that
selection. Selection is limited to the inspected scope on one surface; inspect
several surfaces together for diagnosis, then choose one to select components.
Changing the model invalidates the report.

## Review a binding change

1. Choose **Whole active surface**, captured **Selected components**, or
   captured **Selected surfaces**. Face and edge selections expand to their
   vertices. Binding an unskinned surface for the first time requires all of
   its vertices. Unbinding always requires complete surfaces.
2. Choose an operation and its applicable options.
3. Choose **Review**. A cancellable worker validates a private copy. Inspect
   the **Preview**, change its frame to review deformation, and check **Weights**.
4. Choose **Apply** to commit one undoable edit. Changing a binding option
   disables Apply until another review. Closing the dialog leaves the source
   unchanged; closing during review cancels the worker.

| Operation | Behaviour |
| --- | --- |
| Bind vertices — Rigid joint | Assign weight one to the exact chosen joint. |
| Bind vertices — Nearest joints | Use distances from bind vertices to eligible joint origins. |
| Bind vertices — Nearest bones | Use distances to each eligible joint's parent-to-child segment; a root uses its origin. The segment belongs to its child joint. |
| Rebind existing weights | Keep positive joint assignments, merge duplicates, normalise, and rebuild joint-space position/normal offsets from the retained bind geometry. This deliberately changes unusual imported deformation. |
| Reduce and normalise weights | Merge duplicate entries, retain the strongest permitted influences above the normalised threshold, then normalise. At least one strongest influence remains. Requires canonical bind positions. |
| Unbind surfaces | Remove skinning and retain every baked frame, including normals. Does not remove the skeleton or convert its clips. |

Automatic binding considers all joints unless an explicit eligible pool is
selected. **Maximum influences** is 1–64; **Distance falloff** is 0.1–16, with
default 2 for inverse-square weighting. Higher values concentrate weights near
the closest candidates. Exact hits share weight among the retained coincident
candidates. Ties use joint index, so repeated operations are deterministic.
This is a distance heuristic; it does not infer anatomy or joint constraints.

Already skinned surfaces require **Replace existing weights** for a new binding.
This also permits replacement of negative weights. Rebinding and reduction
refuse negative weights; vertices with no positive assignments need binding.
Reduction preserves each retained joint's imported normal policy, including
zero offsets that request geometric normals. Rebinding explicitly reconstructs
normal offsets from the bind geometry.

## Preserve authored work

Binding, rebinding and reduction require joints, a retained bind frame and an
unchanged mapping of displayed frames to complete skeletal clip ranges. Affected
skinned surfaces must still match the current skeleton at every displayed frame;
unskinned surfaces must have identical geometry and normals across those frames.
Independent frame or normal edits block replacement. Unbinding does not need
the bind frame or clip mapping.

The bind geometry remains unchanged. Other retained poses on affected surfaces
are rebuilt from the new weights; clip joint matrices remain unchanged, including
clips outside the [working set](MODEL_SKELETAL_WORKING_SET.md). Derived skeletal clip bounds are
invalidated so exports calculate new bounds. Materials, UVs, topology, surface
ownership, frame names/origins, custom ranges, manual and skeletal attachment
tracks, collision tracks and native MDL timing are retained. Collision does not
automatically follow a changed skin; refit it separately.

All edits use the normal document validation, cancellation, undo/redo, recovery
and guarded source-save path. Failed or cancelled operations do not publish a
partial result, including multi-surface edits. The tools work offline without
AI or additional setup.

Save `.mesh.json` to retain authored weights. MD5/IQM exporters consume the
retained skinning and enforce their own narrower limits. MDL/MD2/MD3 use baked
frames and their existing budgets. Unbinding preserves vertex animation in the
source and baked exports; it does not make that animation representable in
skeletal exports. A formerly animated unbound surface cannot be automatically
rebound while its frames differ. Review native export diagnostics before staging.
Package, material and level handoffs continue through the usual model services.

## Weights through other tools

Every other edit on a skeletal model derives each vertex's influences from the
vertex it came from, never from its position; see
[skin weights through mesh edits](MODEL_SKIN_PROVENANCE.md) for how. Retained
skeletal poses are the bind frame and frames baked from a skeletal clip.

| Tool | Weights | Retained skeletal poses |
| --- | --- | --- |
| **Transform**, **Shrink/Fatten**, **Smooth Vertices**, weighted transforms, **Edge Slide** | Kept by index, bit for bit. | The tool's geometry in the frames you edit, as an independent edit. |
| **Flip Faces**, **Rotate Edges**, **Beautify Faces**, **Make Face**, **Fill Boundary Loops**, **Bridge Boundary Loops**, UV seams, materials and material slots | Kept: these change faces, not vertices. | Unchanged. |
| **Recalculate Normals**, **Shade Flat**, **Shade Smooth**, **Shade Auto Smooth** | Kept. Hard-edge copies keep their source's weights; recomputed normals are rebound from the bind pose. | Normals follow the skeleton. |
| **Detach UV Faces**, **Transform UVs** and **Project UVs** (every pivot mode), **Unwrap**, **Pack UVs**, **Pack Around Unselected**, **Texel Density…**, the UV projections | Kept. UV split copies keep their source's weights. | Unchanged. |
| **Delete Faces**, **Dissolve Vertices**, **Dissolve Faces**, **Decimate**, the topology repairs | Surviving vertices and split copies keep their own weights, coincident seam copies included. | Unchanged. |
| **Subdivide**, **Split Edges**, **Loop Cut**, **Bisect**, **Bevel Vertices**, **Poke Faces** | Originals kept; each new vertex interpolates the weights of the edge or face it splits, as its position does. A lifted poke centre is rebound where it rose. | New vertices follow the skeleton. |
| **Extrude**, **Duplicate Faces**, **Extrude Edges**, **Inset**, **Solidify** | Each copy takes its source's joints and weights, even when it lands next to or on another part; a moved copy is rebound at its new bind position. | Moved copies turn with their joints. |
| **Merge Vertices** (centre, point, collapse) | Every merged vertex takes one equal blend of the merged vertices' weights, including seam copies that stay separate. | Merged vertices follow the skeleton. |
| **Weld by Distance** | With **Preserve Seams**, vertices with different weights are not welded. Without it, the surviving vertex takes the equal blend. | Welded vertices follow the skeleton. |
| **Symmetrize** | The source half is kept; the mirrored half takes its sources' weights through the joint mirror map. Joints on the mirror plane keep their names; a mapped pair must sit on opposite sides of it. Off-centre joints without a partner are refused unless joint names are kept. | The mirrored half follows the mapped joints. |
| **Bevel Edges**, **Knife**, **Spin**, **Rip** | Written by the tool, interpolated from the vertices it cuts. | Posed by the tool; on vertices it made or rewrote, a normal that differs from the skin's takes the skin's. Vertices it left alone keep their normals. |
| **Manage Surfaces…**: **Separate selected faces**, **Move selected faces**, **Join surfaces**, **Duplicate surface**, **Delete surface**, **Rename surface** | Copied unchanged from the surface each vertex came from. A surface that would mix skinned and unskinned vertices is refused. | Unchanged. |
| **Add Primitive** | Bound rigidly to a chosen joint or to the nearest bones (into the surface or as a surface of its own), or added as a new unskinned surface. Adding to a skinned surface without a binding is refused. | New vertices follow the skeleton. |
| **Duplicate Frame**, **Delete Frame**, **Insert In-between Frames**, **Copy Full Pose** | Kept. | While a surface is skinned, refused on the bind frame and clip frames; on independent frames every other frame keeps its clip mapping. Once every surface is unbound they apply everywhere, and a clip that loses or overwrites a frame stops claiming its other frames. |
| **Skin Weights and Binding…**, **Paint Skin Weights**, **Transfer Skin Weights**, joint and clip edits | Written directly. | Rebuilt by the tool. |
| Attachment tags, collision boxes, clip ranges, MDL settings, `.skin` bindings | Untouched: no vertex data changes. | Unchanged. |

Each edit reports how many vertices kept, interpolated, mirrored, rebound,
merged or bound their weights, in the status line and in the CLI's
`skinProvenance` output. It also counts interpolated or merged vertices that
now have more joint influences than the surface had: MD5 keeps them all, but a
format with a lower limit keeps only the strongest, so limit them with **Reduce
and normalise weights** (`model skinning --operation prune --max-influences N`)
before exporting. Nothing is approximated silently: an edit that cannot derive a
skinned vertex's weights is refused and leaves the model unchanged.

## Automate the workflow

`model skinning` accepts an editable source positionally or with `--input`:

```sh
vibestudio --cli model skinning models/rig.mesh.json --surface 0 --max-influences 4 --json
vibestudio --cli model skinning models/rig.mesh.json --operation select --surface 0 --filter weighted --joint hand --minimum 0.25 --maximum 1 --json
vibestudio --cli model skinning models/rig.mesh.json --operation bind --surface 1 --method rigid --joint hand --output models/bound.mesh.json --dry-run --json
vibestudio --cli model skinning models/rig.mesh.json --operation bind --surfaces 0,1 --method bones --joints root,hand --max-influences 4 --falloff 2 --replace-weights --output models/bound.mesh.json
vibestudio --cli model skinning models/bound.mesh.json --operation rebind --surface 0 --output models/rebound.mesh.json
vibestudio --cli model skinning models/rebound.mesh.json --operation prune --surface 0 --max-influences 4 --threshold 0.001 --output models/reduced.mesh.json
vibestudio --cli model skinning models/reduced.mesh.json --operation unbind --surface 0 --output models/baked.mesh.json
```

The default operation is `inspect`, on surface zero. All edits require an
explicit `--surface N` or `--surfaces N,N` and `--output` ending in `.mesh.json`.
Single-surface operations optionally accept `--vertices all|N,N`. Automatic
methods are `joints` and `bones` (default); rigid binding requires `--joint`.
Omitting `--joints` considers all joints. Existing weights require
`--replace-weights` for binding. Names match exactly.

Selection requires `--filter weighted|unbound|unnormalised|negative|duplicate|noncanonical|over-limit`.
Only `weighted` accepts and requires `--joint`, with optional `--minimum`
(default 0.001) and `--maximum` (default 1). Inspection and selection accept
`--max-influences` as a diagnostic threshold. Selection emits sorted vertex
indices; inspection includes per-vertex weights, sums and issue flags. Both
leave files unchanged. JSON reports include `hasBindFrame`, so a missing bind
frame cannot be mistaken for a successful offset check.

`--dry-run` validates the edit, source-size limit and output target without
writing. Existing outputs need `--overwrite`; normal source identity checks
still apply. Unknown, repeated and operation-inapplicable options are refused,
including explicitly supplied defaults. Exit codes are 0 for success, 1 for
input/output failures, 2 for invalid arguments and 4 for model validation errors.

See [Skeleton authoring](MODEL_RIG.md), [native formats](MODEL_FORMATS.md) and
the [release gate](MODELLER_RELEASE.md) for the wider workflow and acceptance gaps.

After binding, use [Paint Skin Weights](MODEL_WEIGHT_PAINT.md) for brush or
selected-row authoring with joint locks, smoothing and reviewed animation poses.
