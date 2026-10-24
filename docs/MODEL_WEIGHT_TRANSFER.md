# Transfer skin weights

**Available:** bind-space weight transfer and mirroring between surfaces in one
editable skeletal model. VibeStudio remains pre-alpha and largely untested in
real projects. Implementation does not establish the [production gate](MODELLER_RELEASE.md).

## Review the transfer

Open **Mesh Editor → Skeleton → Transfer Skin Weights**. Choose a bound source
surface and a destination. Use the whole destination or the component selection
captured when opening the window. Different topology and vertex counts are
supported; vertices do not need matching indices.

- **Nearest triangle** interpolates source joint weights at the nearest point
  on a source triangle. It supports clothing, armour and other nearby surfaces.
- **Nearest vertex** copies a source vertex's distribution. It is useful for
  matching geometry and explicit vertex seams.
- **Mirror axis** reflects destination queries through a model-space X, Y or Z
  plane in the retained bind shape. Geometry and skeleton transforms stay put.
- **Direction** chooses both directions, positive to negative, or negative to
  positive. One-way mirrors leave destination vertices within **Centre tolerance**
  unchanged and use source primitives wholly on the source side. Triangles that
  cross the centre band are excluded.
- **Maximum distance** bounds correspondence in model units. **Normal angle
  limit** filters source samples against the destination's bind normal, reflected
  for a mirror. An angle of 180 disables normal filtering.
- **Strength** blends the sampled distribution with existing destination weights.
  **Unmatched vertices** either refuses the whole edit or explicitly keeps
  existing weights on unmatched bound vertices.

The source snapshot stays frozen, including when source and destination are the
same surface or both mirror directions are selected. Equidistant samples with
conflicting weights refuse the transfer. Use the normal filter or isolate the
source surface to resolve a seam; there is no implicit UV welding or arbitrary
choice between differently weighted coincident vertices.

## Map and lock joints

**Joint mapping** initially maps each name to itself. Edit exact destination
names, or enter two suffixes and choose **Pair joint suffixes**. For example,
`.L` and `.R` pair existing `arm.L` and `arm.R` joints in both directions.
Suffixes must be nonempty and neither may end in the other; edit overlapping
naming conventions directly in the table. Unpaired joints retain their current
mapping; review the complete table.
Mapping is simultaneous, and several source joints may explicitly map to one
destination joint, combining their weights.

The lock checkbox keeps the mapped destination joint's existing influences
exactly, including their offsets. Unlocked sampled weights are normalised into
the remaining budget, then blended by **Strength**. A sample with no unlocked
influence cannot fill a positive remaining budget. Fully locked vertices remain
unchanged. Locks apply to destination names after mapping.

Choose **Review** to run the cancellable document service. **Preview** shows
the result or original model at any retained frame with the same camera;
**Weights** lists before/after joint weights, with a full detail field for the
selected row. Changing transfer settings invalidates the review. **Apply** commits
one ordinary editor undo step; Cancel leaves the source untouched. Reapplying an
unchanged transfer does not add history.

## Preserve the model

Transfer needs a retained bind frame and complete mapped skeletal poses. Existing
destination weights must be nonnegative and normalised. Distinct imported bind
offsets require explicit [rebinding](MODEL_SKINNING.md) before changing weights.
Existing influence normal offsets are retained; new influences follow the
destination's normal policy. Independent destination geometry or normal edits,
and any result that changes bind geometry or normals, refuse the entire edit.

An unbound destination can receive weights only when every vertex matches at full
strength. Partial component scope, skipped vertices or an incomplete one-way
mirror cannot leave a partially bound surface. More than 64 influences at any
vertex is refused; weight reduction remains an explicit operation.

Only destination weights and their baked skeletal geometry change. Source
surfaces, topology, UV seams, materials, skins, joint transforms, attachments and
collision keep their own data. Clip bounds are invalidated for recomputation.
Save `.mesh.json` as the authoring source; undo, redo and recovery retain the
result. Rebuild MD5/IQM or baked MDL/MD2/MD3 derivatives through their usual export
checks. Refit collision and refresh package/level derivatives explicitly after
reviewing changed deformation. Transfer does not publish packages or compile maps.

## Automate the workflow

```sh
vibestudio --cli model weight-transfer ./models/player.mesh.json --source-surface 0 --surface 0 --vertices all --mirror x --side positive-to-negative --joint-map ./joints.json --output ./models/mirrored.mesh.json --json
vibestudio --cli model weight-transfer ./models/player.mesh.json --source-surface 0 --surface 1 --vertices all --method triangle --distance 2 --normal-angle 45 --output ./models/armour.mesh.json --dry-run --json
```

The input is positional or supplied with `--input`. Required options are
`--source-surface N`, destination `--surface N`, `--vertices all|N,N` and
`--output .mesh.json`. Optional settings are:

| Option | Default and range |
| --- | --- |
| `--method` | `triangle`; also `vertex` |
| `--mirror` | `none`; also `x`, `y`, `z` |
| `--side` | `all`; also `positive-to-negative`, `negative-to-positive`; requires a mirror |
| `--plane` | `0`, within ±1,048,576; requires a mirror |
| `--centre-tolerance` | `0.0001`, from 0 to 1,048,576; requires a mirror |
| `--distance` | `1`, from 0 to 1,048,576 |
| `--normal-angle` | `180`, from 0 to 180 degrees |
| `--strength` | `1`, greater than zero and at most one |
| `--joint-map` | JSON object of exact source names to destination names, at most 64 KiB |
| `--locked-joints` | Comma-separated distinct destination joint names |
| `--skip-unmatched` | Keep unmatched bound vertices; ambiguous matches still fail |

A joint map can contain `{"arm.L":"arm.R","arm.R":"arm.L"}`. Unlisted names
map to themselves. Unknown names, repeated flags, invalid values and duplicate
vertex indices are rejected. `--dry-run` validates the candidate and output
without writing; `--overwrite` permits replacing an existing output through the
guarded writer. The joint map input cannot be replaced, including hard-link
aliases, even with `--overwrite` or `--dry-run`. Source-save preparation and the
64 MiB serialised byte limit are shared with the editor. JSON reports identify the source and destination surfaces,
matching method, mirror direction and write outcome. Use `model skinning` for
resulting influence diagnostics and explicit weight reduction.

## Remaining acceptance

Cross-document/skeleton retargeting, explicit per-vertex correspondence maps and
UV-based transfer remain planned. Maximum-size responsiveness, original-engine
animation fidelity, physical accessibility review, real projects and clean
macOS/Linux packages remain unverified. Source surfaces may be split explicitly
when overlapping layers cannot be distinguished by distance and bind normals.
