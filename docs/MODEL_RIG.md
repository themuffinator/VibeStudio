# Skeleton authoring

**Status: Partial.** Joint hierarchy and numeric weight editing use the shared
mesh document, worker, undo/redo, recovery and source persistence. Numeric bind
positioning, clip management and sampled pose authoring are also implemented.
Bone overlays and viewport posing are available, as are [influence inspection,
explicit binding and weight repair](MODEL_SKINNING.md) and [two-bone IK](MODEL_IK.md).
[Animation curves](MODEL_ANIMATION_CURVES.md) and [mirrored and spatial weight
transfer](MODEL_WEIGHT_TRANSFER.md) are available. Persistent constraints,
long-chain IK and retargeting remain planned.
[Staged weight painting](MODEL_WEIGHT_PAINT.md) is available.
These operations do not establish production readiness for the modeller.

## Edit joints and weights

Open **Mesh Editor** > **Skeleton** > **Edit Skeleton**. The panel is called
**Armature**, **Hierarchy** or **Joints** in some control profiles. Choose one
operation and apply it; validation failures leave the source unchanged.

| Operation | Contract |
| --- | --- |
| Add joint | A model-space origin with identity rotation. A child follows its parent's existing animation through a fixed local bind transform. Existing weights stay unchanged. |
| First joint | Only a single-frame model can start a skeleton. Every vertex binds rigidly to this joint without changing its position. Existing vertex animation is never implicitly converted. |
| Rename joint | Preserves hierarchy, matrices, flags, influences and attachment links. External scripts using its old name need updating. |
| Reparent joint | Preserves every model-space bind and animation pose. Reorders joint arrays as required and remaps influences and skeletal tags. Self-parenting and cycles are refused. |
| Delete unused joint | Requires a leaf without influences or skeletal attachments. At least one joint remains. No implicit weight reassignment or child deletion. |
| Assign vertex weight | Select vertices, edges or faces on one skinned surface. Their union receives the requested joint weight; other influences share the remainder proportionally. Zero removes the influence; one replaces all other influences. |

New names use 1–63 ASCII letters, digits, underscores, dots or hyphens, starting
with a letter or underscore; duplicates are checked without case. Existing joint
targets use exact names. The authoring limit is 4,096 joints and 64 influences
per vertex. Native exporters enforce their own narrower limits.

Weight edits require a retained bind pose, complete baked clip ranges and
affected surface geometry/normals that still match the skeleton. Independently added,
rearranged or modified frames are refused instead of overwritten. The frame
tools keep that mapping intact: **Duplicate Frame**, **Delete Frame**,
**Insert In-between Frames** and **Copy Full Pose** are refused on the bind
frame and on clip frames while a surface is skinned, and on independent frames
the other frames keep their clips. Once every surface is unbound they apply to
any frame (see [frame edits on skeletal models](MODEL_SKIN_PROVENANCE.md#frame-edits-on-skeletal-models)). Imported
vertices with distinct per-influence bind positions are also refused. An
unskinned surface in an existing rig can use **Skin Weights and Binding…**.
That workflow also diagnoses and explicitly rebuilds unusual imported offsets
before numeric weight editing; review its deformation before applying.

Only selected vertex positions change; derived surface normals follow the new
geometry. UVs, materials, topology, unselected vertex positions,
frame names/ranges, tags and collision tracks stay in place. Stored skeletal
clip bounds are invalidated after a weight edit; exports derive current bounds.
Joint edits span all retained clips, including clips omitted from the editable
frame budget. Bone motion and collision fitting remain separate operations.

## Automate the workflow

`model rig` reads `.mesh.json` sources and uses the same service as the GUI:

```sh
vibestudio --cli model rig models/prop.mesh.json --json
vibestudio --cli model rig models/prop.mesh.json --operation add --name root --origin 0,0,0 --output models/rig.mesh.json
vibestudio --cli model rig models/rig.mesh.json --operation add --name hand --parent root --origin 16,0,0 --output models/hand.mesh.json
vibestudio --cli model rig models/hand.mesh.json --operation weight --joint hand --surface 0 --vertices 1,2 --weight 0.5 --output models/weighted.mesh.json --json
vibestudio --cli model rig models/weighted.mesh.json --surface 0 --vertices all --json
```

The operations are `list`, `add`, `rename`, `reparent`, `remove` and `weight`.
Use `--parent -` to create a root or remove a parent. Every edit requires
`--output` ending in `.mesh.json`. `--dry-run` validates without writing;
`--overwrite` permits replacing an existing output. Unknown, duplicate,
incomplete and inapplicable options fail. Exit codes follow the shared CLI:
2 for usage, 4 for an invalid edit, 1 for source/output I/O.

Listing returns ordered joint names, indices, parent indices, origins and
influence counts. An explicit surface and vertex selection also returns each
vertex's joint/weight pairs; `all` means every vertex of that surface. JSON
includes whether output was written and the resolved source/output paths.

## Author skeletal animation

Open **Skeleton** > **Edit Skeletal Animation**. The dialog starts with the
selected joint and the current frame's position inside its skeletal clip.
Clip and pose indices start at zero. Every operation uses the same cancellable
document transaction and history as other mesh tools.

| Operation | Contract |
| --- | --- |
| Position bind joint | Translate, rotate or scale a joint's rest transform. Rebase its influences, skeletal attachment offsets and every retained clip to preserve existing skin deformation and attachment poses. Descendants can follow or remain in place. |
| Add skeletal clip | Create a named clip with 1–1,024 copies of the bind pose and a saved FPS. New preview frames inherit manual tags, unskinned geometry, frame origin and collision from the retained bind frame, or the first editable frame when no bind frame exists. |
| Duplicate skeletal clip | Copy its joint samples and baked auxiliary tracks. A retained clip outside the preview budget has no separate auxiliary frames; duplication uses the first editable frame for those tracks. |
| Rename/delete skeletal clip | Keep the matching preview range aligned. Deletion removes that clip's preview frames and remaps following ranges. A custom range that would lose every frame blocks deletion until explicitly removed. |
| Resize skeletal clip | Extend by holding the final joint pose and its auxiliary tracks, or trim the tail. Surviving custom ranges and native MDL group timing are remapped. New MDL samples inherit their source member's duration as individual groups. |
| Set clip timing | Set FPS from 0.001 to 1,000 and a native loop flag. FPS reaches the matching preview range; IQM retains the loop flag. The editor transport still loops independently. |
| Transform joint pose | Apply a translation, XYZ rotation in degrees or positive XYZ scale to an inclusive range. Model-space rotations pivot about the joint's current origin; local transforms follow its axes. Descendants follow by default and can be held in model space. |
| Copy whole pose | Copy all joint matrices from a clip sample, or the bind pose, into an inclusive range. Manual attachment, collision and frame-origin tracks stay at their destination values. |
| Interpolate poses | Keep endpoint samples exact. Fill interior samples by interpolating parent-relative translation/scale and shortest-arc quaternion rotation, then rebuild the hierarchy. Sheared, reflected or singular endpoints are refused. |

The stored clips contain dense pose samples. Interpolation fills existing samples;
it does not create sparse keys, animation curves or inverse-kinematics constraints.
Use [Animation Curves](MODEL_ANIMATION_CURVES.md) for persistent keys and tangent
editing. Stored curves protect their sampled motion: pose, bind, IK or hierarchy
edits that would make them inconsistent require an explicit curve bake first.
**Edit Animation Keys** and `model curve-keys` select across joint channels for
retiming, duplication, removal and bounded reduction; see
[key groups](MODEL_ANIMATION_CURVES.md#edit-groups-of-keys).
Clip structure is retained even when the editable budget leaves some clips out
of the [working set](MODEL_SKELETAL_WORKING_SET.md), the clips baked into
editable frames. Pose, curve, key, transfer and IK edits on a clip outside it
change its poses in the skeleton without baking them, and exports still write
it. New displayed clips (added, duplicated or extended) must fit 1,024 frames
and 1,048,576 frame vertices in total; operations fail atomically rather than
silently leaving out the requested clip, and **Skeleton** > **Working Set…**
makes room. Source saves also enforce their 64 MiB size limit.

Pose edits rebuild affected skinned geometry and skeletal attachments. Independent
edits in unaffected frames remain exact. An affected frame or attachment that no
longer matches the skeleton is protected from replacement. Manual attachments,
collision tracks, UVs, materials and topology retain their ownership; fit collision
explicitly after posing. Bind repositioning checks that its compensation preserves
deformation within the editing precision. Native exporters enforce joint count and
name limits and report what they change: the md5mesh carries bind scale in its
weight offsets, an md5anim refuses poses that scale a joint differently from its
bind pose and needs a whole frame rate, and IQM reports influence reduction and
quantisation. See [what each export keeps](MODEL_FORMATS.md#what-each-export-keeps-changes-or-leaves-out).

`model pose` exposes the same operations with text and JSON inspection:

```sh
vibestudio --cli model pose models/rig.mesh.json --json
vibestudio --cli model pose models/rig.mesh.json --operation add --name turn --frames 24 --fps 24 --output models/clip.mesh.json
vibestudio --cli model pose models/clip.mesh.json --operation transform --clip 0 --joint root --first 23 --last 23 --rotate 0,0,90 --output models/posed.mesh.json
vibestudio --cli model pose models/posed.mesh.json --operation interpolate --clip 0 --first 0 --last 23 --output models/animated.mesh.json
vibestudio --cli model pose models/animated.mesh.json --clip 0 --first 12 --json
```

The example starts with a rig without clips. `list` is the default operation;
`--clip N --first N` inspects one sample's model-space joint matrices. Reports
label their joint inspection with `poseClip` and `poseFrame`; clip -1
means the bind pose. Editing operations are `bind`, `add`, `duplicate`,
`rename`, `remove`, `resize`, `timing`,
`transform`, `copy`, `interpolate` and `working-set`. Help lists each operation's required fields.
`working-set` takes `--clips` with clip names or indexes, or `-` for the bind
pose alone, and fits clips automatically without it; see
[the working set](MODEL_SKELETAL_WORKING_SET.md#use-the-command-line).
Transforms accept `--translate`, `--rotate`, `--scale`, `--space model|local` and
`--children yes|no`. Copy uses `--source-clip N|bind --source-frame N`; bind uses
source frame zero. Add/timing accept `--loop yes|no`. Output, dry-run, overwrite
and strict option checks follow `model rig`.

## Pose joints in the viewport

**Status: Partial.** Select **Joints** in the component selector, choose a row in
the component table, or choose a joint in the **Skeleton** hierarchy. The selected
joint has a square outline; circles and links show joint origins and parents.
**Show joints**, **X-ray joints** and **Transform children** are session controls.
With X-ray off, the rendered mesh occludes joint origins and sampled bone
segments. Wireframe always exposes the skeleton. Clicking a visible link selects
its child; the hierarchy remains available for coincident or hidden joints.

Use the shared move, rotate and scale handles, snapping, world/selection/custom
axes and origin/selection/custom pivots. **Geometry** supplies the same numeric
transforms for keyboard use. G/R/S select the corresponding joint handle tool;
the mesh modal pointer-following and typed-value workflow does not apply to
joints. A joint transform edits the displayed skeletal sample only. At the bind
frame it repositions the rest joint while preserving skin and animation. The
status line identifies this scope, and the mesh **All frames** field is disabled
in Joints mode. Use **Edit Skeletal Animation** for inclusive pose ranges.

The render worker deforms the displayed skin and attachments without changing
the document. Bones and the mesh share a completed camera/pose snapshot in all
four views. Release validates and commits one undo step; Escape, a frame change,
selection change or descendant-scope change cancels. Independent geometry or
attachment edits block any pose transform that would replace them. Collision
stays on its own track and must be fitted explicitly. Joint rename/delete, undo,
redo and recovery retain or remap the named selection.

Automation can use `model pose --operation bind|transform --matrix` with twelve
row-major values, representing a 3×4 model-space affine delta. For example,
`--matrix 1,0,0,3,0,1,0,-2,0,0,1,1` translates by `(3,-2,1)`.
The matrix includes its pivot compensation. It cannot be combined with
`--translate`, `--rotate`, `--scale` or `--space`; `--children` still applies.
Values must be finite and within one million, with a positive determinant and
finite inverse. Native exporters can reject transforms their target cannot retain.

## Integrate with the studio

Save/reopen and recovery use version-8 source skeleton data, version 9 for
editable animation curves, or version 10 when hold-next curves are present.
MD5/IQM export retains joints and weights; MDL/MD2/MD3 use the updated baked
frames. Authored skeletal clips feed MD5 animation and IQM through the normal
export path. The model's material and package paths remain unchanged, so normal
export, staging, dependency review, level placement and compiler workflows
remain the handoff. Exporting a derivative does not update existing placements
or packages automatically. External joint-name references need manual review.

The implementation adds no library, renderer or proprietary asset. Synthetic
core/CLI and owned-widget tests cover the authoring contract; original-engine
and production-project acceptance remain separate gates in
[the modeller plan](plans/modeller-production.md).
