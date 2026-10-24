# Author skeletal animation curves

**Available:** persistent translation, rotation and scale curves for retained
skeletal clips, with graphical and numeric key editing and a shared CLI.
VibeStudio remains pre-alpha and largely untested in real projects. This
milestone does not establish the [modeller production gate](MODELLER_RELEASE.md).

## Capture and edit motion

Open **Mesh Editor → Skeleton → Animation Curves**. Choose a clip and joint.
Create a clip first with **Edit Skeletal Animation** if the skeleton has none.
The GUI and the CLI both handle any clip with poses, including clips outside the
[working set](MODEL_SKELETAL_WORKING_SET.md); the preview skins those one sample
at a time.

1. Choose **Capture Sampled Motion** to stage nine parent-local channels, with
   one key at each existing sample. Capturing preserves the stored poses.
2. Choose a **Channel**. On **Keys**, select a row and set its sample, value,
   interpolation and tangent mode. **Set key** inserts or replaces at the
   entered sample; **Move key** moves the selected key, retaining its properties;
   **Remove key** removes it. Keep at least one key in every channel.
3. On **Curve**, drag a diamond to change its sample and value. Double-click
   inserts a key. Cubic segments expose separate incoming/outgoing tangent
   handles; dragging a handle selects free tangents. Keys snap to whole samples.
   Occupied destinations are refused rather than merged.
4. **Undo draft**, **Redo draft** and **Discard draft** affect only the pending
   joint curves. Clip/joint selection is locked while a draft differs from the
   source. Channel selection remains available.
5. Choose **Review** to rebuild and validate motion in a cancellable worker.
   Compare **Reviewed result** and **Original model** at the same sample and
   camera in **Preview**. **Apply** commits one document undo step. Any further
   key edit requires another review; **Cancel** discards the draft.

In the graph, Left/Right select keys, Up/Down change the value by 0.1, and
Shift increases that step to 1. Control+Left/Right moves a key by one sample.
Delete removes it, Escape cancels a drag, and Home or **Fit curve** fits the
range. The wheel zooms time around the pointer; Control+wheel zooms values.
The native key table and numeric fields provide equivalent editing without
pointer gestures. Time always increases left to right, including in RTL layouts.

## Choose channels and interpolation

Samples are zero-based integer indices inside the selected clip. The clip's
frame rate determines playback time. Channel IDs are stable CLI identifiers:

| Channels | Meaning |
| --- | --- |
| `tx`, `ty`, `tz` | Translation in model units, relative to the parent joint |
| `rx`, `ry`, `rz` | Unwrapped degrees, applied X then Y then Z (`Rz × Ry × Rx`) |
| `sx`, `sy`, `sz` | Positive local scale factors |

Rotation values can exceed a complete turn; 0 to 450 degrees retains that turn
when sampled. Capture chooses nearby equivalent Euler angles to reduce jumps.
Dense imported matrices cannot identify extra complete turns between samples.
Euler channels still have gimbal singularities; capture retains sampled matrices
within the validation tolerance, but sparse interpolation needs visual review.
Local shear, reflection, singular or nonpositive scale cannot be captured as TRS.

Interpolation belongs to the key at the start of each segment. **Step** holds
its value until the next key, **Linear** connects values, and **Cubic** uses a
Hermite segment with both endpoint slopes. **Hold next** (`step-next`) holds the
next key's value strictly between the two keys. Exact key times always return
their own values. Reversing **Step** therefore produces **Hold next**, including
isolated values where two different holds meet. The graph draws jumps at the
actual knot times. Values outside the keyed range hold
the nearest endpoint. Slopes are channel units per sample:

| Tangents | Behaviour |
| --- | --- |
| Auto | Uses adjacent secants, weighted for unequal sample spacing; may overshoot |
| Clamped | Flattens local extrema and limits slopes to avoid overshooting monotone neighbours |
| Free | Stores separate incoming and outgoing numeric slopes |
| Flat | Sets both slopes to zero |

There is no implicit loop-seam smoothing. Every key time must be distinct and
inside the clip. Values and stored slopes are finite and within ±1,000,000.
Analytic cubic extrema validate the continuous curve between keys as well as
the integer samples; scale must remain greater than 0.000001 throughout.
The model permits at most 1,048,576 stored keys across all clips and joints,
subject to existing clip, preview and 64 MiB editable-source limits.

## Keep poses and source data consistent

The editable source stores curves alongside the dense samples used for preview
and game export. Curves are authoritative: stored parent-local samples must
agree with their evaluated curves. Each matrix entry uses tolerance
`max(0.0001, abs(expected) * 0.000002)` for float storage. Rename remaps the
exact joint name; clip duplication retains curves and clip deletion removes
them. Shrinking a clip refuses to discard keys outside the new range.

Sampled pose, viewport pose, IK, bind or hierarchy edits that would invalidate
stored curves are refused. To switch to those tools, open the joint's unchanged
curves and choose **Stage Bake Joint Curves**, **Review**, then **Apply**. Baking
removes that joint's editable curves while preserving sampled motion; normal
undo restores them. Bake each affected joint/clip explicitly when necessary.

Curve changes propagate through descendants, then use the shared skeletal
rebuild. Independent affected geometry, normals and linked attachment edits
are protected. Unaffected poses, UVs, material bindings, weights, frame metadata,
manual tags and collision tracks remain intact. Changed clip bounds are
invalidated. Refit collision explicitly after changing motion.

`.mesh.json` schema 9 adds `curves` to each skeletal clip. Each group has an exact
`joint` name and a `channels` object containing all nine IDs. A key is the tuple
`[sample, value, interpolation, tangent, incomingSlope, outgoingSlope]`.
Interpolation and tangent strings use the lowercase names in the tables above.
Schema 10 adds the `step-next` interpolation identifier. Readers accept source
versions 1–12. Sources with persistent connectivity use version 11, including
their curves. A source too large for readable JSON uses version 12, whose packed
arrays hold geometry, joint matrices and influences while curves stay readable
(see [Editable Meshes](MODEL_MESH.md#sources-import-and-export)). Otherwise save uses version 10 when a hold-next key is present,
and version 9 for other curves. Models without curves or explicit connectivity
use the earlier appropriate schema (8 for skeletons). Save/reopen, undo/redo and recovery retain
curve metadata. Builds that predate a schema cannot read files using it;
labelling hold-next data as an older schema is refused.

## Automate curve edits

```sh
vibestudio --cli model curves models/player.mesh.json --clip 0 --json
vibestudio --cli model curves models/player.mesh.json --clip 0 --joint upper --operation capture --output models/keyed.mesh.json
vibestudio --cli model curves models/keyed.mesh.json --clip 0 --joint upper --operation set-key --channel rz --frame 8 --value 90 --interpolation cubic --tangent flat --output models/turn.mesh.json --dry-run --json
vibestudio --cli model curves models/keyed.mesh.json --clip 0 --joint upper --operation bake --output models/sampled.mesh.json
```

Input is positional or supplied with `--input`. `--clip` is required. The default
`inspect` operation returns stored groups; `--joint` optionally filters them.
Edits require an exact `--joint` and `--output` ending in `.mesh.json`.

`set-key`, `remove-key` and `move-key` require `--channel` and `--frame`.
Set requires `--value`; optional `--interpolation` and `--tangent` preserve
an existing key's modes when omitted. `--in-slope` and `--out-slope` require free
tangents. Move requires `--new-frame`. `replace --curves joint.json` validates
and replaces a whole nine-channel group; the JSON file contains one group
object in the source representation above. It can also establish a new group.

`--dry-run` validates the edit, source size and output destination without
writing. Existing output requires `--overwrite`. A replacement `--curves` input
cannot become the output, including hard-link aliases, even with `--overwrite`
or `--dry-run`. Unknown, repeated, malformed
and inapplicable options are refused. JSON reports `operation`, `clip`, `joint`,
`dryRun`, `written` and the resulting clip's curve groups. GUI and CLI use the
same `EditAnimationCurves` document operation.

## Edit groups of keys

Open **Skeleton → Edit Animation Keys** after capturing curves. Choose a clip,
then use the joint and channel filters to show one track or several. Changing a
filter clears the selection and review. The **Keys** table supports extended
row selection across joints and channels. **Select all visible keys** selects
everything matching the filters; **Select range** replaces the selection with
keys inside **First sample** and **Last sample**, inclusive. The range controls
only change selection when that button is used.

On **Operation**, choose retiming, duplication, removal or reduction. **Review**
prepares the whole selection on a cancellable worker, updates the hierarchy and
rebuilds the skin once. **Results** lists selected/before/after key counts and
the largest continuous value change per channel, including between integer
samples. Select a result row to inspect its complete values in the detail field.
Compare original/result poses in **Preview**, then **Apply** one undo step.
Changing selection or operation settings invalidates the reviewed result.

### Retime, duplicate or remove

Retiming maps every selected key to:

`newSample = pivot + (oldSample - pivot) * timeScale + sampleOffset`

The time scale must be positive and the pivot inside the clip. Values and
interpolation modes stay with each key; free tangent slopes are divided by the
time scale. Automatic/clamped tangents are recalculated from their neighbours.
A complete affine retiming preserves the normalised cubic shape. Partial
retiming can change neighbouring segments or reorder selected keys past
unselected keys, so inspect the whole motion. Clip frame rate and duration stay
unchanged; resize the clip explicitly before extending motion beyond its end.

**Round to nearest sample** allows fractional results, with halves away from
zero. Without it, mapped times must be within 0.0000001 of an integer. Rounding
can change relative timing and cubic shape. Every resulting sample must be inside
the clip. Collisions between moved keys or with unselected keys refuse the
entire edit; no existing key is implicitly merged or replaced.

Duplication retains the original keys and adds transformed copies. Destination
collisions are refused, including with the selected originals. New neighbours
can alter automatic tangents and intervening motion. Removal deletes exactly
the selected keys and leaves at least one key in every channel. Use explicit
joint-curve baking to remove editable curves altogether.

### Reduce captured keys

Reduction replaces eligible spans with straight segments within independent
**Translation error**, **Rotation error** and **Scale error** limits. Defaults
are 0.001 model units, 0.01 Euler degrees and 0.0001 scale units. These bound
each scalar channel continuously, not world-space joint, attachment or skin
displacement. Review the resulting animation, especially long articulated rigs.

Each contiguous run of selected existing keys keeps its first and last key.
Unselected keys and both anchors of every step segment remain. A candidate
straight segment is checked against every original polynomial piece, including
analytic extrema of their difference and both sides of step discontinuities.
If the bound fails, the span is split and checked again; curved pieces that
cannot lose a key remain intact. Balanced splitting bounds the work by
`O(n log n)` rather than repeatedly scanning progressively shorter curves.
This is a bounded simplification, not a guarantee of the fewest possible keys.

Retained automatic/clamped boundary tangents become free tangents when their
neighbours change, preserving the original slopes on adjacent curved pieces.
Accepted collapsed spans become linear. A final whole-channel comparison checks
the declared tolerance against the original curve, preventing accumulated error.
The usual finite-value, positive-scale, key-count and source-size limits apply.

### Automate key groups

```sh
vibestudio --cli model curve-keys models/keyed.mesh.json --clip 0 --channel ty --json
vibestudio --cli model curve-keys models/keyed.mesh.json --clip 0 --joint upper --joint lower --channel ry --first 0 --last 8 --operation retime --pivot 0 --factor 2 --output models/slower.mesh.json --dry-run --json
vibestudio --cli model curve-keys models/keyed.mesh.json --clip 0 --channel rz --samples 2,4 --operation duplicate --offset 8 --output models/copied.mesh.json --dry-run --json
vibestudio --cli model curve-keys models/keyed.mesh.json --clip 0 --operation reduce --translation-error 0.001 --rotation-error 0.01 --scale-error 0.0001 --output models/reduced.mesh.json --json
```

Input is positional or supplied with `--input`; `--clip` is required. The
default `inspect` operation reports selected key counts without changing data.
Repeat `--joint` and `--channel` for distinct exact names/IDs; omission selects
all stored joints/channels. Choose `--first`/`--last` for an inclusive range
(omitted endpoints cover the clip), or `--samples` for distinct comma-separated
integer indices. Exact samples may match different tracks, but every requested
index must match a selected track. Missing joint curves or an empty selection
are errors.

`--operation retime|duplicate` accepts `--pivot`, `--factor`, `--offset` and
`--snap`. `remove` accepts no timing options. `reduce` accepts the three error
limits above. Every edit requires `--output .mesh.json`; dry-run and overwrite
use the same guarded writer as other model tools. JSON reports `channels`,
including `joint`, `channel`, `selectedKeys`, `beforeKeys`, `afterKeys` and
`maximumChange`, alongside `operation`, `clip`, `dryRun` and `written`.
GUI and CLI share the `EditCurveKeys` document operation, with curve persistence,
undo, recovery, cancellation and the normal skin/attachment protections.

## Transfer or reverse a range

Open **Skeleton → Transfer Animation Range**. Choose **Source clip** and
**Destination clip** from the current model. The source can be the destination;
overlapping copies always read the original motion. Create or resize a clip
with **Edit Skeletal Animation** before transferring beyond its current extent.

1. On **Range**, set the inclusive first/last source samples and first destination
   sample. **Time scale** multiplies sample distances; **Reverse source range**
   maps the last source sample to the first destination sample.
2. **Match clip rates** uses destination fps divided by source fps to retain
   duration in seconds. Both rates must be explicit and positive. Otherwise,
   the scale operates in samples; clip rates and durations stay unchanged.
3. On **Tracks**, select joints and channels. Stored curves retain their shape;
   unkeyed motion is captured as parent-local TRS, with the usual shear,
   reflection and scale protections. No manual capture is required.
4. **Review** crops and maps the curves, replaces destination keys inside the
   resulting interval and rebuilds the destination once. **Results** reports
   transferred keys, including evaluated boundary keys, destination counts and
   the largest continuous scalar change across each whole destination channel.
5. Inspect **Preview** at samples inside and outside the interval. Compare
   **Reviewed result** with **Original destination**, then **Apply** one undo
   step. Settings or selection changes invalidate the review.

Cropping evaluates boundary values and cubic slopes exactly within floating-point
precision. Transferred tangents become free slopes so changing neighbours does
not reshape the copied segments. Reversal swaps incoming/outgoing slopes,
changes their signs and reverses step directions; scaling divides slopes by the
time factor. Every key and boundary must map within 0.0000001 of a distinct
integer sample inside the destination. Incompatible rate conversions or scales
are refused without rounding or silently resampling the motion.

Keys outside the replaced interval retain their values and evaluated slopes.
The copied boundary values can change the adjoining transitions; the report
includes that change. Unselected local channels remain, while selected parent
motion also moves descendants. Previously unkeyed destination joints gain all
nine captured channels to preserve the other components. Other clips, weights,
UVs, materials, independent tags and collision tracks remain intact. Refit
collision and refresh exports after accepting motion changes.

The captured destination baseline and transferred curves must fit the model's
1,048,576-key budget. A short transfer into many unkeyed joints can still require
a dense baseline; capture and reduce those destination curves first if the
budget is exceeded.

This transfers local channel values within one rig. Cross-document exchange,
retargeting, animation layers and blending remain separate planned workflows.

### Automate range transfers

```sh
vibestudio --cli model curve-transfer models/keyed.mesh.json --source-clip 0 --target-clip 1 --first 0 --last 8 --target-first 0 --reverse --output models/reversed.mesh.json --dry-run --json
vibestudio --cli model curve-transfer models/keyed.mesh.json --source-clip 0 --target-clip 1 --first 2 --last 6 --target-first 4 --match-rate --joint upper --joint lower --channel ty --output models/copied.mesh.json --json
```

Input is positional or `--input`. All five clip/range options in the examples
and `--output .mesh.json` are required. Repeat distinct `--joint` and `--channel`
selectors; omission selects all rig joints/channels. Use positive `--factor`
(default 1) or `--match-rate`, with optional `--reverse`. JSON reports source
and target clips/ranges, effective factor, reversal, per-channel counts/change,
`dryRun` and `written`. Dry-run validates the same operation and output limits;
existing output needs `--overwrite`. Transfer outputs must differ from the input,
including aliases of the same file; this also applies to dry runs.
Unknown, repeated, malformed and
inapplicable options are refused. GUI and CLI use `TransferAnimationCurves`.

## Deliver and review game animation

Export through the existing MD5/IQM or baked MDL/MD2/MD3 paths. Native files
receive sampled motion, not VibeStudio curve metadata, and retain their own
limits on names, scales, frame counts and quantisation. Keep `.mesh.json` as
the editable source. Refresh derivatives used by level placement, attachments
and packaging; review collision, animation scripts and engine playback.
Export fixtures do not prove acceptance in the original engines.

Curve clipboard exchange, quaternion tracks, cyclic
extrapolation, layers/blending, persistent constraints, long-chain IK and
retargeting remain planned. Physical keyboard/screen-reader acceptance,
maximum-size performance, real projects and clean cross-platform release
checks remain open. This workflow works offline and adds no dependency.
