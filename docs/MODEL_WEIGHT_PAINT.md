# Paint skin weights

**Available:** staged vertex weight painting for retained skeletal models.
VibeStudio is pre-alpha and largely untested in real projects. This tool does
not establish production or native-engine acceptance.

Open **Mesh Editor → Skeleton → Paint Skin Weights**. Bind an unskinned
surface with [Skin Weights and Binding](MODEL_SKINNING.md) first. Choose a
surface, painted joint, brush mode, value and strength. Drag with the primary
mouse button to cover vertices; release to validate and update the staged
skeletal poses. The preview inherits the editor's camera controls. Other
navigation gestures remain available between strokes; turn off **Paint with
primary drag** to use that button for navigation too.

## Control the stroke

- **Replace** moves the active weight towards **Weight / amount**.
- **Add** and **Subtract** change it by that amount multiplied by strength
  and coverage, clamped to the available unlocked weight.
- **Smooth** moves it towards the mean active-joint weight of its unique
  triangle-edge neighbours. Every vertex reads the state before this stroke,
  so traversal order does not affect the result. Disconnected vertices stay
  unchanged. Split UV/hard-normal vertices are separate; coincident vertices
  and separate surfaces are not joined implicitly.
- **Radius** uses logical pixels. Smooth, linear and constant falloff use
  distance from the pointer path. Fast pointer moves cover continuous line
  segments. Repeated passes take the maximum coverage for each vertex within
  one stroke; holding still does not accumulate paint. Start another stroke
  to build up more weight.
- **Paint through mesh** includes occluded vertices. With it off, only
  depth-tested visible vertices in the frozen starting projection are eligible.
  Inspect the pose and camera before starting; the projection stays fixed
  until the stroke ends.
- **Limit brush to selected rows** uses the table selection as a mask. An
  empty mask paints nothing. The editor's selected vertices, edges or faces
  initialise the table selection on the active surface.

Vertex rings encode weight by colour and arc length, from an empty ring at
zero to a full ring at one. Pending coverage has a square outline. Hover near
a vertex for its index and numeric weight. The table always exposes exact
weights, influence counts and totals without depending on colour. Select rows
with the keyboard and use **Paint rows** for the same operation at
full coverage, without a pointer gesture. It targets the chosen rows regardless
of viewport visibility; the brush visibility option applies only to dragging.

Check joints in **Lock weights** to preserve their influence values and offsets
exactly. The painted joint must be unlocked. Other unlocked influences keep
their proportions and absorb the remainder so the total remains one. When
reducing the sole unlocked influence, choose an explicit **Remainder joint**;
otherwise the stroke is refused. That joint must be different and unlocked.
It is used only when there are no other positive unlocked influences.

## Review and apply

Pending coverage does not change the model. Escape, a view/frame change,
resizing, loss of viewport focus or changed settings cancels the active stroke.
Release starts a cancellable validation worker and updates the staged model
only after success. Poses deform after that review, not during the drag.

Review relevant animation frames. **Undo stroke** and **Redo stroke** operate
inside this window using the document's bounded history (100 steps, with older
states trimmed beyond the 64 MiB history budget). A stroke which changes
nothing adds no history. **Apply staged strokes** commits the reviewed batch
as one Mesh Editor undo step; **Cancel** discards the entire batch. During an
active stroke, Escape first cancels that stroke. During a worker operation,
closing requests cancellation and waits for the worker to finish.

The editor verifies that its source has not changed while the window was open.
An unsuccessful stroke leaves earlier reviewed strokes intact. A batch may
contain up to 1,024 strokes and 1,048,576 vertex samples; apply it before starting
another batch. No game source or export file is written by the paint window.

## Preserve the source

Painting requires a retained bind frame, existing skinning and complete mapped
skeletal samples. Negative or unnormalised touched weights require explicit
repair. Distinct imported per-influence bind positions require rebinding.
Independent geometry or normal edits on a targeted surface block replacement.
Smoothing also validates the weights of the neighbours it reads.

The shared service preserves bind-frame bytes, reconstructs new joint offsets
from that bind shape, preserves the existing imported normal policy, and
rebakes only affected surfaces. It retains joint hierarchy, clip transforms,
material paths, UVs, tags, collision and unrelated surface data. Clip bounds
are invalidated for recomputation. More than 64 influences at a vertex is
refused; use explicit weight reduction when a game format needs fewer.

Save the resulting `.mesh.json` as the authoring source. Undo, redo and recovery
retain the weights and baked poses. Rebuild MD5/IQM for skeletal targets, or
MDL/MD2/MD3 for baked frame targets, then review the format's diagnostics and
limits. Refit collision separately if the deformed silhouette requires it.
Refresh derived assets, package staging and level placements through their
normal workflows; painting does not silently publish exports or compile maps.

## Use the command line

```sh
vibestudio --cli model weight-paint ./models/rig.mesh.json --surface 0 --joint arm --vertices 1,2,3 --mode replace --value 0.7 --strength 1 --output ./models/painted.mesh.json --json
vibestudio --cli model weight-paint ./models/rig.mesh.json --surface 0 --joint arm --vertices all --mode smooth --strength 0.25 --locked-joints spine --remainder-joint root --output ./models/smoothed.mesh.json --dry-run --json
vibestudio --cli model weight-paint ./models/rig.mesh.json --stroke-file ./paint-strokes.json --output ./models/painted.mesh.json --json
```

Direct painting requires `--surface`, `--joint`, `--vertices all|N,N` and
`--output .mesh.json`. Mode defaults to `replace`, value to `1`, strength to
`0.5`, opacity to `1`. Value and opacity are in `[0,1]`; strength is in `(0,1]`.
Smooth does not accept `--value`. `--locked-joints` takes comma-separated names.
`--remainder-joint` names the explicit fallback. Duplicate targets or unknown,
repeated and inapplicable options are refused. `--overwrite` permits replacing
an existing output. `--dry-run` validates the result and output target without
writing. A stroke recipe cannot be replaced by the output, including hard-link
aliases, even with `--overwrite` or `--dry-run`. Source-save preparation and the
64 MiB serialised byte limit are shared with the editor. `model skinning`
inspects the resulting exact weights.

A recipe uses the same ordered strokes and resolved vertex coverages as the
GUI's document operation; its coordinates are vertex indices, not screen
positions. Brush options cannot be combined with `--stroke-file`.

```json
{
  "version": 1,
  "strokes": [
    {
      "surface": 0,
      "joint": "arm",
      "mode": "replace",
      "value": 0.7,
      "strength": 0.5,
      "lockedJoints": ["spine"],
      "remainderJoint": "root",
      "samples": [[1, 1.0], [2, 0.5], [3, 0.25]]
    }
  ]
}
```

Each sample is `[vertex, opacity]`, with distinct existing indices per stroke.
Only `surface`, `joint` and `samples` are required inside each stroke; other
defaults match direct painting. A smooth recipe omits `value` or leaves it at
its default `1`. Files are bounded to 64 MiB and reject unknown fields, versions
and wrong types. All strokes validate and commit atomically. Targeted poses
are baked once at the end of a batch.

## Remaining work

[Mirrored/spatial transfer](MODEL_WEIGHT_TRANSFER.md) is available with normal
filtering and explicit seam ambiguity refusal. **Planned:** tablet pressure, full shaded weight heatmaps and deformation preview during a drag.
The current display uses per-vertex weight rings and exact values. Large-model
latency, physical screen-reader/input review, real-game animation fidelity and
clean macOS/Linux acceptance remain open in the
[modeller release gate](MODELLER_RELEASE.md).
