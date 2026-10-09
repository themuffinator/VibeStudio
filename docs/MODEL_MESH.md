# Editable Meshes

Front faces use counter-clockwise winding in editable meshes and OBJ. Native
MDL/MD2/MD3 boundaries convert to/from the engines' clockwise order, preserving
UV/normal corner identities. Both MD2 render streams use the same conversion.
Older editable native imports require orientation review; their stored triangles
are retained. See [engine acceptance and compatibility](MODEL_ENGINE_ACCEPTANCE.md)
for the independent checks, source-port findings and reproduction steps.

Collision component mode selects static or animated boxes through viewport edges, the table
or the Collision inspector. Existing transform handles and Geometry fields move
and rotate in the chosen transform axes and scale in each box's local axes, with shared pivots,
snapping, validated previews and one undo step per gesture. Collision remains
independent of render mesh transforms, follows frame copy/insertion/deletion,
and exports a selected stored pose to a separate static map; see
[Model Collision](MODEL_COLLISION.md) for authoring and level/CLI handoff.

**Models > Mesh Editor** opens the selected model, in any format listed in
[Native Model Formats](MODEL_FORMATS.md), as an editable copy, or
starts with a cube when no model is selected. **Design Prop > Edit as Mesh**
bakes the current primitive geometry into this editor. The primitive design
remains a separate parametric source. An already open mesh document is kept
when another model is selected; close it before opening another baked design.

**Models > Assemble** links separate models through named tags, with independent
frame ranges and playback settings. Its `.assembly.json` recipe stays separate
from mesh sources; **Bake Pose to Mesh** enters this editor with one composed pose
and **Bake Animation…** creates a sampled sequence with saved clip timing.
See [Model Assemblies](MODEL_ASSEMBLY.md) for source protection,
package context and current limits.

The Models browser loads every decoded format on a cancellable worker.
Animation-only MDX, GLA and `.md5anim` files provide a skeleton and frames
without geometry.
**Cancel Preview** stops the current request; select the model again to retry.
Changing the selection or staged package revision retires the old result.
Returning to Models or invoking a model command keeps the selected preview's
warnings and cancellation state until the source, selection or palette changes.
Mesh Editor waits for decoding to finish and requires geometry for a selected
model. Export OBJ is unavailable while loading or when the format has no
decoded geometry. Frames, attachment poses,
MDL indexed skins and native groups remain in the decoded copy; the mesh
editor provides native MDL playback controls. The browser resolves each
surface's primary material, including alternate image extensions, and retains
the native raw-header drawer alongside geometry and material diagnostics.

Model package reads verify one unambiguous entry in full, with a 64 MiB limit
on both stored and expanded payloads. Palette and material reads are bounded
and cancellable too. MDL's 4,194,304 frame-vertex limit applies after native
groups expand; MD3 shares that limit across all surfaces, including padded
frames. Cancellation discards partial geometry, frames, tags and skins. The
maximum editable grid has viewport and component-authoring latency coverage,
including all-face, all-vertex and all-edge selection and pose changes. A Windows
release audit also exercises source save/reopen, dense-selection recovery,
one-frame OBJ export/reimport and cancelled source serialization through the
production worker services. It checks complete source identity, recovery context
and preservation of the saved file. The geometry limits and 64 MiB serialized
source limit are separate constraints. Dense translucent overlap, varied
materials/surfaces, full editor-to-package handoff and cross-platform performance
remain open; see the [release evidence](MODELLER_RELEASE.md).

## Editing

Blender-style edit mode (menus, modal G/R/S/E/I tools, region and loop
selection, the 3D cursor, inset, loop cut, merge, symmetrize, decimate and
more) is described in [Mesh Tools](MODEL_TOOLS.md).

**Repair Import…** prepares a reviewed copy of a damaged `.mesh.json`, MDL, MD2
or MD3 before normal editable-model admission. The original Open / Import path
remains strict. The repair review shows exact zero-based source surface, face,
pose, vertex and seam indices alongside a preview of the prepared copy. Selecting
a change with a pose moves the preview to that pose. **Save and Open Copy**
requires a new `.mesh.json` destination, checks the reviewed input's identity
before saving and again immediately before publishing,
then opens the saved copy for ordinary editing, undo, recovery, native export
and project/package handoff. Cancel leaves the current editor and files intact.

Repair removes faces with out-of-range indices, repeated vertex indices or a
collapse in any stored pose. Each such face is removed across every pose; the
report identifies the first collapsing pose. Remaining face order, every vertex
(including unused vertices), positions, UVs and usable normals are retained.
Only unusable normals are rebuilt from area-weighted adjacent surviving faces
in that pose. If those faces cannot supply a direction, the proposal explicitly
reports a +Z fallback. Seam marks whose edges no longer exist are removed.
Materials, skins, MDL indexed groups/timing, tags, frame origins, saved clips
and collision boxes retain their existing values. This is an import baseline,
not an undo step that restores invalid geometry into the editor.

Preparation rejects incomplete native decodes or decode warnings, missing
coordinate arrays, invalid positions/UVs or metadata, and any repair that would
remove every face of a surface. It never guesses lost data or silently discards
surfaces or poses. The source parser's finite coordinate and structural limits
remain enforced. The complete operation is bounded by normal editable-model
limits and 16,777,216 face poses, and runs with progress and cancellation. This
does not repair malformed OBJ polygons, truncated native records, intersections,
winding, duplicate faces or intentional overlapping parts; use the relevant
authoring and Health workflows after admission.

`model repair-import <source> --json` prepares the same plan without writing.
`--output <new.mesh.json>` saves the repaired copy; `--dry-run` with an output
also validates serialization and the destination without publishing. Overwrite
is deliberately unavailable. JSON includes the input SHA-256, total changes,
and per-surface `removedFaces` (original index, reason, first invalid pose),
`rebuiltNormals` (pose and exact vertex/fallback indices), and `removedSeams`
(original endpoint pairs). Pose `-1` means a topology-wide index fault. Invalid
arguments return 2, source/destination IO failures 1, unsupported or unrepairable
data 4, and complete preparation/save 0. Positional and `--input` source forms
work; duplicate, unknown and unrelated editing flags are rejected.

**Surface > Manage Surfaces** provides rename, face separation/movement,
duplication, joining and deletion across every animation pose. It preserves exact
geometry attributes and ordered material slots, with explicit consent for
incoming material replacement when moving or joining. Resulting faces stay
selected for UV and geometry finishing. See [Surface Authoring](MODEL_SURFACES.md)
for partition rules, native-name repair, limits and the shared `model surfaces` CLI.

Choose **Surfaces** to retain a selection across multiple surfaces and transform
them with one common pivot. Numeric and viewport transforms include unused
vertices, share current/all-frame scope, and retain the set through undo and
recovery. The table, viewport and active material inspector stay synchronized.
See [whole-surface selection](MODEL_SURFACES.md#select-and-transform-surfaces) for
selection rules, CLI selectors and tag/collision integration boundaries.

Choose a surface and select faces, vertices, or edges in the component table. The table
supports extended selection and Select All without creating a widget for every
component. Clicking the preview selects a face; Control-click toggles it. In
Vertices mode, click a point to select that exact index on the active surface;
Control-click toggles it. Selected points use square markers. Filled views pick
only visible points; **X-ray Vertices** explicitly enables hidden points, shown
with hollow markers. Wireframe permits selection through the mesh. Coincident
points resolve to the lowest index; use the table to select a particular seam
copy. Edges mode lists each indexed
endpoint pair, its length in the current frame, and its incident-face count.
Click near an edge on a visible face to select it; Control-click toggles it.
Selected edges use dashed lines in both the 3D and UV views. Filled views respect
occlusion. Endpoint identities persist through transforms, undo, and recovery;
topology edits remap them explicitly. Coincident UV seams remain separate edges.

Selection and frame changes retain the component table and compact selected-row
ranges. Edge order and incidence are prepared with the document, shared with the
inspector, and restored with undo/redo. Select All in edge mode reuses prepared
edge and connected-vertex sets; unused vertices are excluded. Initial column
sizing samples a bounded
number of rows; columns remain resizable and retain chosen widths across pose
updates. Font, style and language changes size them again. Full cell values
remain available to accessibility and tooltips.
Vertex projection, occlusion, indexing and marker images run on the same
cancellable worker as the mesh. Markers stay aligned with its displayed pose;
picks wait for current geometry. A screen-space index preserves exact distance
and lowest-index ties, including X-ray selection. Marker centres align to physical
pixels, with white/black visible points, hollow dotted hidden points and outlined
filled selection squares. Selection-only marker changes reuse the mesh image.
Oblique silhouette points use coverage from their own connected faces within
the marker footprint; unrelated foreground faces cannot reveal a hidden point.

The Geometry inspector provides numeric translation, X/Y/Z rotation, nonuniform
scale, and a pivot. Position transforms apply to all frames or the current frame.
Mirroring requires the entire surface and all frames so winding stays consistent.
Extrude and Duplicate Faces use the Offset controls. Subdivision splits touched
edges in neighbouring triangles too, avoiding new T-junctions within indexed
topology. Extrude, subdivide, duplicate, delete, and flip operate on every frame.
Recalculate Normals uses area-weighted normals at shared indices; existing UV and
hard-normal seams remain separate indices. Collapsed triangles and invalid
coordinates cause the whole edit to fail without changing the document.

Geometry's **Transform axes** selects **World** (default), **Selection**, or
**Custom** for numeric edits and the gizmo. Selection uses the first nondegenerate
selected face by index, or the first face touching a selected edge/vertex. X
follows that triangle's first edge, Z its winding normal, and Y completes the
orthogonal basis. Whole-surface selection uses the active surface. Tags use their
native orientation; collision boxes use their own rotation. The displayed pose
supplies fixed axes for all affected frames. The basis is chosen again for each
new operation; no object hierarchy or persistent local coordinate system is added.
Isolated vertices and wholly degenerate selections require World or Custom axes.
Custom reveals three XYZ orientation angles in degrees. Pivots remain in model
coordinates. Extrude and Duplicate Faces also interpret Offset in the chosen axes.

**Gizmo** provides Move, Rotate, and Scale tools in those axes. Move uses labelled
X/Y/Z arrows and a centre square for view-plane movement. Rotate uses labelled
rings; an edge-on ring follows the tangent at the grabbed point. Scale uses axis
boxes and a centre box for uniform scaling; dragging the centre right or up
increases the scale. Scale gestures remain positive; use numeric transforms for
explicit mirroring. Move/scale axes too closely aligned with the view are hidden.
Geometry's **Pivot** selects the model origin, selection bounds centre (the GUI
default), or custom coordinates. The displayed pose supplies the selection pivot,
which stays fixed for every affected frame. Numeric transforms scale about that
pivot, rotate in X/Y/Z order, then translate, all in the chosen basis. Normals use
the corresponding inverse transpose. Collision sizing always uses each box's
intrinsic axes to keep boxes rectangular; its movement and rotation follow the
chosen transform axes. A viewport label identifies non-world axes.

Rotate also provides **Free** trackball rotation: drag the round centre handle
or empty space inside the dashed circle. The axis rings retain priority outside
the centre handle. The circle stays 172 logical pixels across in orthographic
and perspective views; dragging beyond it follows the rim. A drag captures its
starting view, pivot and transform basis. Each update measures from that same
starting point, so sampling frequency does not accumulate drift and returning
to the start is neutral. Opposite rim points produce a screen-plane half turn.
Meshes, whole surfaces, tags and static/animated collision use the same control.
Free rotation changes authored geometry or poses; it adds no persistent object
transform or hierarchy.

The preview does not modify the document. Release validates and commits one undo
step using the Geometry inspector's frame scope. Escape, focus loss, resizing, or
changing the selection, frame, camera, tool, axes, pivot, snap settings, or frame scope
cancels the gesture. Playback pauses when a drag begins. An invalid drag position
keeps the last valid preview and shows an explanation; releasing there cancels.
Returning to a valid position allows the gesture to continue.

**Snap** applies to the active gizmo and Apply Transform. Each tool has its own
step: Move accepts 0.000001–1,000,000 model units, Rotate 0.000001–180 degrees, and
Scale 0.000001–10,000. Translation rounds the delta in the chosen axes rather than final positions.
Scale rounds the distance from 1, preserving neutral axes; a step of 0.25 gives
factors such as 0.75, 1, 1.25, and 1.5. Half-step ties round away from zero (away
from 1 for scale). Zero-scale results fail. Numeric Apply Transform uses all three
configured steps when Snap is enabled. Extrusion and duplication do not use Snap.
Free rotation preserves its screen-space rotation axis and snaps the total
shortest-arc angle to the nearest step multiple within 0–180 degrees. The XYZ
preview reports the equivalent rotation in the chosen transform axes; its
components are not snapped a second time. To reproduce that result numerically
or through the CLI, use those XYZ angles, axes and pivot with angle snapping off.
The view selector provides exact Top (XY), Front (XZ), and Side (YZ)
orthographic planes, an orbit view, and perspective. Numeric transforms and the
component table provide a keyboard alternative to the gizmo.

Split Edges inserts one midpoint per selected indexed edge, interpolating its UVs,
positions, and normalized normals in every frame. Every incident triangle is
split, including unselected neighbours. The two child edges stay selected.

**Fill Boundary Loops** in Geometry closes explicitly selected holes. In Edges
mode, select at least one indexed boundary edge per intended hole; each seed
expands to its complete loop. A boundary edge belongs to exactly one face.
The displayed pose chooses the triangles, and the same cap must be valid in
every stored animation pose. The transform frame scope does not restrict this
topology operation. All selected loops commit together as one undo step; new
faces become the selection for UV projection, detachment, seam editing or normal
recalculation. Existing positions, normals, UVs, seam marks, materials, tags,
collision volumes and other surfaces remain intact. A cap initially reuses its
boundary UVs and normals; finish those attributes explicitly before export.

Loops may be concave, contain collinear corners or be nonplanar. The service
tries axis projections in descending projected-area order and uses deterministic
ear clipping without adding vertices. At least one simple axis projection must
support the chosen triangles in each pose; strongly folded loops can therefore
be refused even when another 3D triangulation exists. It does not search every
possible cap or optimize triangle quality. Branching or inconsistently directed
boundaries, crossings, collapsed cap faces, existing interior edges and new
intersections with the active surface are refused. Coplanar area overlaps also
fail, preventing a filled sheet or UV island from being covered a second time.
Isolated point contact and common geometric boundary edges are allowed,
including distinct seam indices. Other surfaces and pre-existing intersections
are outside this check. Floating-point checks use scale-relative tolerances;
they are not exact geometric predicates or a whole-model collision certificate.

Each loop is limited to 1,024 vertices and an operation to 16,777,216 work checks,
in addition to the document's ordinary triangle/storage limits. Long work runs
with progress and Cancel; failure leaves every loop and pose unchanged. Undo,
recovery, editable-source saves, native export and package/level handoff use the
existing document paths. This operation adds no dependency, source schema or
setup preference.

`model edit <source> --operation fill-boundary-loops --edges a:b,c:d --output <new.mesh.json>`
uses the same service. `--surface N` defaults to 0; optional `--source-frame N`
chooses the reference pose and defaults to 0. Omit `--frame` or use `--frame all`.
Faces/vertices and single-frame scope are rejected. Dry-run and output/overwrite
protections apply; geometry failures return 4, usage errors 2, and write failures
1. Use `model topology --json` to inspect boundary endpoint indices first.

**Bridge Boundary Loops** joins two open boundaries on the active surface. In
Edges mode, select at least one boundary edge on each intended loop. Both loops
must be closed, disjoint and consistently wound; they may have different vertex
counts. Use Manage Surfaces > Join first when the boundaries belong to separate
surfaces, reviewing material adoption there. The bridge consumes both complete
loops and adds exactly `first count + second count` triangles without adding or
moving vertices. New faces become the selection for further editing.

The displayed pose is the reference. Canonical loop ordering and the closest
available vertex pair determine the initial alignment. **Bridge twist** advances
the second anchor around its aligned loop; positive and negative offsets wrap
by that loop's vertex count. The accepted range is -1,023 to 1,023. For this fixed
alignment, a bounded dynamic program chooses the lowest sum of squared cross-edge
lengths within each of two seam orientations. The cheaper strip is tried first,
then the other if validation fails. This is a deterministic choice among those
two candidates, not an exhaustive search for every possible valid bridge.
Adjust the twist, reference pose or geometry when neither candidate works.

Bridging spans every stored pose regardless of the transform scope. Collapsed
triangles, new strip self-intersections and intersections with existing faces
on the active surface are refused atomically. Shared geometric edges and isolated
point contacts are allowed. Existing intersections, other surfaces and the motion
between stored poses are outside this check. It shares filling's floating-point
tolerances, 1,024-vertex loop limit and 16,777,216-work-check budget; it is not a
whole-model collision certificate. Cancel or failure changes no pose or history.

Original positions, UVs, normals, seam marks and material slots remain exact;
tags, collision, clip timing and other surfaces are retained. The new faces reuse
boundary attributes. Detach/Unwrap, UV projection and Recalculate Normals provide
the finishing handoff. No intermediate rings are inserted; use Subdivide as a
separate edit when needed. Undo, recovery, native export and package/level handoff
use the ordinary document services, with no new dependency or setup preference.

`model edit <source> --operation bridge-boundary-loops --edges 0:1,4:5 --bridge-twist 1 --source-frame 0 --output <new.mesh.json>`
uses the same service. Twist and reference frame default to 0. `--surface N`
chooses the active surface; `--frame all` is the only supported frame scope.
Faces/vertices, repeated twist/reference options and malformed values are usage
errors. `--bridge-twist` is exclusive to this operation. Dry-run and output
protection follow filling's rules and exit codes. Existing outputs require
`--overwrite`; explicitly saving back to the loaded editable source is supported
through its ordinary fingerprint check. Native exports protect their inputs.

Weld by Distance considers selected vertices and the endpoints/corners of selected
edges/faces. It uses the lowest-index compatible vertex as a fixed anchor and
keeps that anchor's positions, UVs, and normals. Every frame must satisfy the
distance; nearby chains cannot pull a vertex farther from its anchor. Zero means
exact positions; nonzero distances range from 0.000001 to 1,000,000 model units.
Preserve Seams is enabled by default and requires matching UVs and normals in
every frame. Turning it off explicitly permits those attributes to merge.
Welding removes faces whose indices collapse and compacts unused vertices. It
rejects removing every face, new duplicate faces, new nonmanifold edges or
inconsistent shared-edge winding, and geometric collapse in any pose. It does
not repair existing topology; use the Health inspector to inspect it. Dense searches
stop after 4,194,304 candidate/frame comparisons with an actionable error.

The **Health** inspector scans the active surface on the document worker, with
progress and Cancel. Choose a finding category and **Select Findings** to show
its indexed faces, vertices or edges in the component table and previews.
Counts distinguish duplicate faces, unused vertices, disconnected vertex fans,
inconsistent shared-edge winding, nonmanifold edges and boundary edges. A face
component connects through indexed edges; point-only contact does not join it.
Coincident UV and normal seam copies stay distinct. Boundaries are informational:
open props and split seams need not be closed. Reports become stale after edits
or a surface change and require another inspection.

Repairs cover the **entire active surface in every pose**, independent of the
current component selection. Each repair commits one undo step and remaps the
selection, source seam marks and recovery data:

- **Remove Duplicate Faces** keeps the lowest face index for each set of three
  vertex indices, regardless of winding. Review deliberately two-sided copies
  before applying it. Other unused vertices remain until explicitly removed.
- **Remove Unused Vertices** removes only samples referenced by no face, retaining
  the exact UVs, positions and normals of surviving vertices in every pose.
- **Split Disconnected Fans** gives each face fan touching only at a vertex its
  own vertex copy. The fan with the lowest face index retains the original;
  selected vertices expand to their surviving copies. No point moves.
- **Split Nonmanifold Edges** separates connections where more than two faces
  share an indexed edge. At its endpoints, face corners remain connected through
  existing two-face edges; disconnected groups receive exact vertex copies.
  The group with the lowest face index retains the original index. This keeps
  a coherent shell together when an extra fin touches its edge, without choosing
  face pairs by angle or changing the choice across poses. Every face and its
  winding, UVs and normals stay intact. Selected endpoints and edges expand to
  their actual copies, and seam marks follow face corners. Disconnected fans at
  vertices away from branching edges stay untouched. Splitting can create open
  boundaries; it does not choose which faces to delete, make a closed solid,
  orient faces, or repair geometric intersections. Welding copies together can
  restore the original problem. Inspect the result before continuing authoring
  or native export, and retain the editable source alongside game derivatives.
- **Orient Faces** makes shared-edge winding consistent, keeping each connected
  component's lowest-index face as the orientation seed. It does not infer
  outside direction or change authored normals. Use Flip Faces for the opposite
  orientation and Recalculate Normals when desired. Duplicate faces, branching
  edges and nonorientable components must be resolved before orientation succeeds.

Repairs preserve materials, tags and all animated attributes. Storage limits,
invalid results or cancellation leave the entire candidate uncommitted. There
is no automatic closure of all boundaries. Repair Import can prepare specific
index/collapse/normal repairs before admission, as described above. Use Fill Boundary Loops for explicitly chosen holes; its
intersection checks cover only the proposed caps on the active surface. Inspect
and manually select the intended faces for deletion or detachment in other
workflows; topology health is not a watertightness or collision certificate.

`model topology <source> --surface N --json` retains its edge/incident-face list
and adds `health`: duplicate-face groups, unused vertex indices, disconnected
fans with face groups, boundary/nonmanifold/winding edge pairs, and the face
component count. Inspection succeeds even when findings exist. `model edit`
accepts `--operation remove-duplicate-faces|remove-unused-vertices|split-disconnected-fans|split-nonmanifold-edges|orient-faces`
with the usual output, dry-run and overwrite protections. These surface repairs
reject component selectors and a single-frame scope to avoid ambiguous intent.
The default surface is 0 and scope is all frames. An unrepairable candidate
returns 4; invalid arguments return 2 and output/write failures return 1.
Use separate outputs to review successive repairs.

**Health > Geometry intersections > Inspect Intersections** scans geometric
face crossings and coplanar area overlaps within and between every surface.
Choose **All poses** (default) or **Current pose**. Each finding identifies one
stored pose, both surface and face indices, and its contact type. Indices are
zero-based; surface names appear in the detail. **Show First Face** and
**Show Second Face** pause playback, show that pose and select the exact face
for ordinary transforms, deletion, detachment or other authoring. Findings
are review information: intentionally overlapping model parts can be valid.
Inspection itself changes no geometry, selection, source, undo or recovery.

Reports use the current document revision. Geometry edits invalidate them;
changing the displayed pose also invalidates a Current pose report. Selection
and camera changes do not invalidate an All poses report. Scanning runs on the
document worker with progress and Cancel. Failure or cancellation preserves
the last complete report and source. Findings are formatted on demand, avoiding
a widget for every pair. The report is session state, with no new source schema
or setup preference. Inspect again after repair and before export or package
handoff; native export and level placement remain explicit operations.

The shared contact predicate also validates boundary fills and bridges. It
reports a positive-length contact entering either triangle's interior and
positive coplanar overlap area, including opposite winding and separate seam
indices. Shared geometric boundary segments and isolated point contacts are
allowed. Bounds and a spatial hierarchy skip separated faces; every requested
stored pose is checked independently. Motion between poses, enclosed solids
with no surface crossing, point-only contacts, and collision-volume behavior
are outside this scan. Floating-point predicates use scale-relative tolerances,
so the report is not an exact geometric or watertightness certificate.

An inspection is bounded to 4,194,304 face poses, 67,108,864 candidate face pairs,
268,435,456 spatial-node visits and 65,536 findings. A finding is one pair in one
pose; a persistent overlap can appear in several poses. Exceeding a limit
fails explicitly without adopting a partial report. Choose one pose, restrict
the CLI surface scope, or reduce the geometry when the corresponding workload
limit is reached. These diagnostic limits supplement normal document limits.

`model intersections <source> [--frame all|N] [--surface all|N] --json` uses the
same service; both selectors default to `all`. A surface filter includes every
pair involving that surface, including pairs with another surface. JSON retains
exact `firstSurface`, `firstFace`, `secondSurface`, `secondFace`, `frame` and
`kind` (`crossing` or `coplanar-overlap`) per finding, plus `complete`,
`framesScanned`, `facePoses`, `candidatePairs` and `nodeChecks`. Scope values of
`-1` in the report mean all. Findings do not make a completed inspection fail.
Usage errors return 2, read errors 1, invalid sources or exhausted scan limits 4,
and complete reports 0. Positional and `--input` sources, spaced and inline
selector forms work; duplicate, unknown and write options are rejected.

The Surface inspector edits ordered material slots and the selected preview binding, transforms
selected UVs, or projects them onto XY, XZ, or YZ. UV projection uses model units;
a scale of `0.015625` maps 64 units to one tile. Face UV operations split corners
shared with unselected faces, preserving the latter's mapping in every frame.
Explicitly selected vertices or edge endpoints take precedence over this
face-local isolation and update all faces using those indices.
The UV tab supports the same face, vertex, and indexed-edge selection. **Pick
Islands** changes face picking to select a complete chart; Control-click toggles
the chart. **Select Islands** expands the current components to all touched
charts and changes the table to Faces. Islands connect through shared indexed
edges; marked seams, split indices, boundaries, and nonmanifold edges stop that
connection. Point-only contact does not join charts. A seam must cut all paths
between regions to separate them; marking a single edge on a closed surface
does not necessarily create another island.

In Surface, **Mark UV Seams** and **Clear UV Seams** edit selected edges. Dotted
lines show marked seams, dashed lines show selected edges, and hatching shows
selected faces. Ordinary wires draw first, seams above them, selected edges
above seams, and selected square markers last. Selected markers remain visible
at coincident UV corners. Hatching covers the union of selected faces once,
including reversed or overlapping UV faces; unselected holes stay clear.
**Detach UV Faces** splits selected faces from unselected
neighbours without moving UVs, poses, or normals. Clearing marks does not weld
already split indices. These operations share document undo, source saves, and
recovery. Splits, subdivision, duplication, deletion, and welding remap marks.

Middle-drag pans the UV view, the wheel zooms around the pointer, **F** frames
the selection, and **Home** frames all UVs. Toolbar actions provide both framing
commands. The texture repeats beyond the unit tile and retains its aspect ratio.
Drag the centre square with **Move** enabled to preview a UV offset; release
validates one undo step. Escape, focus loss, resize, and selection/source changes
cancel it. Numeric UV Offset is the keyboard alternative. **Snap UV Offset**
rounds either offset to multiples of UV Grid, where one unit is one texture tile.
It starts disabled with a step of 0.125. Half-step ties round away from zero.

UV preparation and drawing stay on the cancellable preview worker. The image
has a 4,194,304-pixel ceiling, including extreme aspect ratios. Selection fills
cancel exactly shared internal borders and simplify exactly collinear contour
segments. The separate wire and pick geometry retains every indexed edge and
face. Wires use the studio's antialiased 2D line painter (`app/wire_lines`); opaque tile reuse skips
only pixels already proven unchanged by another stroke of the same color.
This changes neither mesh data nor GUI/CLI authoring, history, recovery or export.

**UV Pivot** chooses the origin (default), selection bounding-box centre, or
custom U/V coordinates. Scale and rotation act around this pivot before adding
the offset. Projection calculates the pivot in projected model coordinates.
The selection centre covers all selected UVs together. **Individual Islands**
instead scales and rotates each selected chart around its own bounding-box
centre, then adds the same snapped offset. Select complete islands first using
Pick Islands or Select Islands. Partial islands and mixed vertex/edge selections
are rejected with an actionable message; the operation never silently expands
the selection. Projection calculates each chart's centre in projected coordinates
from the displayed pose. Negative UV scales mirror the mapping without changing
mesh winding.

Independent transforms split vertex indices shared across selected islands or
with unselected faces, including marked seams and point-only contacts. Every
pose's positions and authored normals are copied exactly. Unselected UVs, unused
vertices, materials, tags and other surfaces stay intact. The complete edit
checks global vertex/animation storage capacity and commits one undo step;
cancellation or invalid coordinates leave the source and selection unchanged.
Source saves, recovery, native export and package/level handoff retain these
resolved splits through the existing document services. Pivot choice is an
editor operation setting; it does not change the source schema. Dragging the
move handle still uses the ordinary face/component offset operation.

**Unwrap and Pack Faces** generates charts from selected faces in the displayed
animation pose, respecting marked seams and existing indexed splits. It can add
cuts to flatten curved surfaces; isolated interior seam tips may need adjacent
cuts. Positions and normals are copied exactly across every animation pose when
corners split. **Pack Selected UVs** keeps existing island shapes, orientation and
relative UV scale while arranging them in one tile. Select Islands first to pack
whole islands. These actions require a face selection and commit one undo step.

**Atlas Width** defaults to 512 pixels. **Same as width** keeps **Atlas Height**
equal to it by default; turn this off for a rectangular texture. Both dimensions
accept 32–4096 pixels, including non-power-of-two sizes such as 320×200. Match the
intended material image: packing preserves flattened chart proportions in
texture pixels, with a common density target for unwrapped charts. Repacking
preserves existing orientation, shape and relative UV scale for those dimensions.
Packing uses independent axis limits
inside xatlas, rather than stretching a square result. A bounded density search
fits one atlas; it does not guarantee optimal occupancy.

**Atlas Padding** defaults to 4 pixels, accepts 0–64, and must remain below one
eighth of the smaller dimension. It reserves space around charts and the atlas
border, with bilinear filtering space included. UVs remain normalized; no image
is created, resized or repainted and material assignments stay intact. Atlas
dimensions are session operation controls, not saved model metadata. Set MD2
skin dimensions separately when exporting; MDL skins retain their actual image
dimensions. With Unwrap and Pack Faces or Pack Selected UVs, unselected UVs stay
fixed and may overlap the selected atlas.

**Pack Around Unselected** packs complete selected islands into remaining space.
Use Select Islands first. Unselected faces on the active surface stay fixed, as
do faces on other surfaces sharing any nonempty material slot. Slot paths match
case-insensitively with slash normalization, including alternate slots. Shader
aliases or different paths resolving to the same image are not inferred; review
those bindings before packing. Other materials do not occupy this atlas.

**Fixed-region packing** offers **Fit uniformly** (default), which finds one
shared scale for the moving islands, and **Keep current UV scale**, which keeps
their texel density and refuses if they do not fit. Both preserve island shape,
orientation and relative density. Atlas dimensions and padding apply. Fixed UVs
must lie within the 0–1 tile; repeating or multi-tile regions are refused.
Existing overlap between fixed regions is allowed and remains untouched.
Selected islands must have noncollapsed, nonoverlapping faces internally.

Packing uses conservative texel masks and a deterministic bounded search, so
it can leave unused space and may refuse a layout that a different arrangement
could fit. It does not rotate islands. The raster workspace is preflighted
against the allocation budget (at most 6 MiB for three 4096-square bitmaps), and
raster/search work stops at 250 million units. Mesh/topology storage and overlap
verification retain their existing document limits. Progress and Cancel use the
normal document worker. Failure leaves the model, selection and history intact;
success is one undo step, preserving all pose positions/normals and remapping
seams if shared corners split. Save, recovery, native export and package/level
handoffs use the resulting ordinary UVs. Images are not repainted. Persistent
per-corner pins, image rebaking and multi-tile packing remain integration work.

The [xatlas service](DEPENDENCIES.md#xatlas) behind Unwrap and Pack Faces and Pack
Selected UVs runs offline on the document worker with progress and Cancel.
It retains indexed boundaries, preserves chart
shape during packing, caps library allocation at 256 MiB and validates generated
faces before adoption. Degenerate or internally overlapping islands, excessive
corner splits, memory exhaustion and cancelled work leave the source unchanged.
Overlap verification stops after eight million broad-phase comparisons with a
message to reduce the selection. Texture-painting and package handoffs continue
to use the document's material paths and normal export/staging services.

UV chart analysis and drawing run on a cancellable worker with one active job
and a replaceable pending request. The view reports when it is updating and
waits for the displayed result before accepting picks. Its image is capped at
4,194,304 pixels with uniformly reduced resolution for larger views; paths are
clipped before drawing. No animation poses are copied into the UV worker.

Material mode resolves each surface's selected preview slot (primary by default) from an immutable snapshot of the
open package and its staged edits. Replacement, deletion, package undo, reload,
and palette changes refresh both views without reopening the mesh. Geometry-only
edits reuse the resolved images. Missing or ambiguous images use the checker.
Show Material opens the related texture workflow. **Preview slot** is a transient
per-surface choice; **Assign Material** edits that selected binding. **Manage
Material Slots** reviews additions, replacement, removal, reordering and clearing
as one undoable all-pose edit. See [Material Slots](MODEL_MATERIAL_SLOTS.md) for
limits, embedded MDL handling, export semantics and the matching `model slots` CLI.

Material images load on a separate worker with progress, Cancel, Reload Images,
and per-surface Details. A newer source or material binding retires older work;
Cancel stays cancelled until Reload Images or a binding change. The shared level
resolver follows Quake III `qer_editorimage`, then the first usable shader-stage
image. These are static images; shader lighting, blending, animation, deformation,
and texture-coordinate effects are not simulated. Unsafe paths, duplicate names,
unreliable shader lookup, and read/decode limits produce diagnostics.

The modeller resolves at most 32 surfaces, reads at most 16 MiB per entry and
64 MiB overall, and retains at most 64 MiB of preview images. Qt image headers
are checked against a 16-megapixel decode limit; previews are downsampled to
1,024 pixels per side. Shader scanning has its own bounded file/count budget.
Cancellation is checked around each bounded read/decode. Reload Images reuses
the current package snapshot; reload the package to accept external edits to base
assets. Imported replacements retain independent bytes; stage the file again to
accept changes to its original. Selected-package exports protect original import
paths even after flattening the plan into a dependency subset.
Relative skin paths use package-relative model provenance only, never arbitrary
filesystem directories.

The Animation inspector duplicates, deletes, and renames frames. Duplicating a
frame carries its attachment tags; insertions and deletions adjust stored clip
ranges. **Animation clips** adds, renames, changes, and deletes saved inclusive
frame ranges. Clips may overlap or contain one pose. New and renamed clip names
must be unique, use 1–128 characters, and have no surrounding spaces or control
characters. Imported duplicate names remain distinguishable by their clip indices.
Deleting a clip retains all its frames. A source supports up to 1,024 clips.

Choose a clip to preview only its range, or **All frames** for the full model.
Stepping wraps within the chosen range; selecting a frame outside it restores
All frames. **Preview FPS** (0.001–1,000) adjusts this session's speed. Selecting
a clip uses its saved FPS when specified. **Saved clip FPS** and **Apply Clip FPS**
retain a rate in the editable source in one undo step; zero means unspecified.
Add Clip also uses that field. FPS edits do not resample geometry or change
native MDL group timing. Native game animation timing needs separate configuration.
Reduced motion
disables automatic playback while retaining explicit frame selection. Clip editing,
Copy Full Pose and insertion are disabled during playback.

**Smooth preview** starts enabled in the mesh editor and is also session-only.
It blends positions and normal directions between saved poses and uses the same
rigid attachment interpolation as Insert In-between Frames. The last pose blends
back to the first pose **of the selected clip**. Playback follows elapsed time,
so delayed display updates skip ahead instead of slowing the animation. Turn the
option off to inspect discrete stored frames. Pausing or stepping returns to the
current stored pose; smooth playback hides transform handles until paused.
No generated geometry, clip timing or preference is written by this control.

The preview omits collapsed transient triangles and uses geometric face normals
when normal directions cancel. Incompatible attachment poses are hidden between
frames; mismatched surface vertex counts retain the stored surface pose. Text and
accessible status identify surface incompatibilities and, when tags are shown,
attachment incompatibilities. Exact stored poses remain
inspectable. Creating permanent in-between frames still requires full validation
and refuses invalid geometry or ambiguous normals.

**Copy Full Pose** copies positions, normals, frame origin and every attachment
from **Copy from frame** into the displayed frame, across all surfaces. It
retains the destination frame name, UVs, materials, clip ranges and other poses.
The source must be a different existing frame.

**Insert In-between Frames** generates evenly spaced poses between the displayed
frame and its next adjacent frame. Positions and origins blend linearly;
endpoint normals are normalized, blended and normalized again. Rigid attachment
orientations use shortest-arc quaternion interpolation. Reflected attachment
bases retain their handedness when both endpoints agree. Opposite handedness,
cancelling normals, or a collapsed triangle in any generated pose rejects the
entire edit. Original positions, normals and attachment bases remain exact.
All surfaces participate, independently of component selection or Geometry scope.
The operation checks the total-frame and frame-vertex limits before allocation,
runs on the cancellable document worker, and commits one undo step.

Clips containing both endpoint frames expand to include the new poses. A clip
ending at the first endpoint stays unchanged; clips starting at the next frame
or later shift forward. Generated names use a prefix (default `blend`, at most
123 characters) followed by `_001`, `_002`, and so on. Native export still
enforces its shorter name limits. Linked parts use the separate
[assembly editor](MODEL_ASSEMBLY.md); game timing configuration remains release work.

### Quake III skin assignments

In **Surface**, **Apply .skin File…** imports a local Quake III surface-to-shader
file. **Apply Package .skin…** selects an exact `.skin` occurrence from the open
staged package. The picker reads metadata only; reading, validation and applying
the assignments use the normal cancellable document worker. A package revision
change invalidates an open picker. **Last Skin Import Details…** shows the last
successful import's before/after material paths, exact package occurrence,
unused bindings and ignored attachment markers. Opening another source clears
this receipt. Reapplying matching assignments reports that no undo step was added.

Every model surface must have a binding before anything changes. Matching follows
Quake III: surface names are lowercase and a final underscore plus one character
is removed from the model's name (`BODY_1` matches `body`). Binding names are
case-insensitive. A single binding may cover several surfaces with the same
engine name. Duplicate binding names are rejected; extra bindings are reported.
Empty `tag_` attachment records are ignored using the original renderer's
case-sensitive substring convention. This operation does not alter attachment
poses. Missing bindings fail instead of assigning a default shader.

The bounded parser accepts ASCII text, quoted names, line/block comments and an
optional UTF-8 BOM. Files must be 1 byte–64 KiB, contain at most 256 records, and
use names and shader paths of at most 63 bytes. Each record requires a comma;
shader paths must be safe package-relative paths with forward slashes. Wildcard
assignments and source-port-specific hiding directives are not interpreted.
Malformed, ambiguous, oversized and cancelled imports leave the source and
history unchanged. Package reads verify the complete entry before applying it.

Applying a skin replaces only each surface's primary material in one undo step.
Alternate material slots, geometry, poses, UVs, normals, tags and collision remain
intact. Material/UV previews refresh through the shared package resolver; ordinary
save, recovery, export, staging and dependency review retain the assigned paths.
This is an explicit authoring import, with no live link to the `.skin` file and
no automatic runtime skin selection in the browser or level view. Export the
modified model and review its materials for the target game. Embedded MDL skins
use the Quake MDL inspector instead.
For a reusable override that preserves the model source, use an
[assembly's linked skin](MODEL_ASSEMBLY.md#linked-skins). Its file or exact package
reference drives preview and bakes through the same parser and material resolver.

The shared CLI accepts local files, archives/folders and saved package drafts:

```sh
vibestudio --cli model skin ./prop.mesh.json --file ./default.skin --output ./prop-skinned.mesh.json --dry-run --json
vibestudio --cli model skin ./prop.mesh.json --package ./assets.vibepackage --entry models/prop/default.skin --output ./prop-skinned.mesh.json --json
```

Use `--entry-index N` for an exact zero-based occurrence; also passing `--entry`
guards its path. JSON includes input byte count/SHA-256, the selected index,
before/after mappings, ignored markers and unused bindings. `--dry-run` validates
the full edit and destination without writing. Existing destinations require
`--overwrite`; package/draft inputs and the skin file are protected. Output is
an editable `.mesh.json`; `model build` creates the native derivative. Exit codes
are 0 for success, 1 for source/package/write failure, 2 for usage or protected
input paths, and 4 for invalid skin data or edits.

### Attachment Tags

Animation > Attachment tags creates, duplicates, renames, and deletes named
attachments across every frame. Tags are model-wide; choose **Tags** in the
component table to select one attachment by name. **Show Tags** draws its origin
as a diamond with labelled local X/Y/Z axes through the model. Tag mode keeps
these markers visible. Picking an origin and selecting its table row share the
same selection, undo and recovery state.

Enter a unique name of 1–63 printable ASCII characters without surrounding
spaces. Creation adds an identity orientation at the entered origin in every
pose. **Use Selection Centre** fills that origin from selected mesh components
in the displayed pose before creation. A model supports 16 tags per frame.

**Set Origin**, **Reset Orientation**, **Copy Tag Pose**, numeric transforms and
the move/rotate gizmos use the all/current-frame scope shared with Geometry.
Copying retains the source pose before changing destinations. Geometry's
selection pivot is the displayed tag origin; an all-frame edit uses that same
fixed point in every pose. World-axis movement/angle snapping matches the CLI.
Tags are rigid frames: scale is unavailable, and imported orientation and basis
handedness are preserved until explicitly changed. Rotation applies a delta to
the stored basis without an Euler-angle round trip. Reset Orientation explicitly
aligns the axes with model X/Y/Z. Playback disables the tag pose form.

MD3 export and package/level handoff retain all tag poses. MD2 refuses tagged
models; OBJ frame export reports omitted attachments and records that limitation
in a file comment. Smooth preview blends attachment poses as described above.
The [assembly editor](MODEL_ASSEMBLY.md) consumes these named tags. Original-engine
acceptance and the remaining assembly release work stay on the release gate.

Undo/redo restores mesh data and component selection. History retains at most
100 edits and trims older states once estimated geometry, topology indexes and selection storage exceeds 64 MiB, keeping
one preceding state even when that single state exceeds the target. The count
includes positions, normals, triangles, UVs, seam marks, frame/clip metadata and names,
tag poses/names, collision boxes/names, embedded image bytes, and a per-entry
selection estimate plus cached edge/incidence and complete-selection storage; it is not
a measurement of the process's total memory. Returning to the saved state clears
the modified marker.

Internal revision fingerprints stream ordered geometry into SHA-256 with an
8 KiB buffer and retain source metadata identity. This avoids building a full
JSON geometry tree merely to detect changes. Source saves, recovery payloads and
external-change checks continue to use their existing serialized formats and
file checksums; internal revisions are not interchangeable with file hashes.

## Sources, Import, And Export

Save writes an atomic `.mesh.json` authoring document. It stores every surface's
triangles, UVs, positions and normals for every frame, material references,
frame names and origins, animation ranges, and attachment tags. Schema identifier
is `vibestudio.mesh`. Sources with a skeleton use version `8`, which adds a
`skeleton` object: joints with their bind matrices, per-surface skinning, clips
of model-space joint matrices and skeletal tags (see
[skeletons](MODEL_FORMATS.md#skeletons)). Animated collision tracks use version `7`. Otherwise,
sources with a positive saved clip FPS use version `6`, with
optional MDL and collision fields. Clip entries optionally contain a numeric
`framesPerSecond` from 0.001 to 1,000; absence means unspecified. That field is
rejected in older versions. Sources without saved rates, native MDL or collision use version `3`.
Native MDL metadata uses version `4`; sources with collision use version `5`,
with optional retained MDL metadata. See [Collision Authoring](MODEL_COLLISION.md).
Each surface includes `uvSeams`, an array of
unique existing indexed edge pairs in ascending endpoint order. New saves use
model-level `md2SkinSize: [width, height]` in versions 3–7. Versions 1 and 2
remain readable, defaulting to 256 by 256 pixels; version 1 has no seam marks. Older studio
versions may not read the new schema. Geometry bounds and counts are derived on load.
The original game file is not adopted as the authoring save target. Saving back
to a loaded source checks its content fingerprint and refuses external changes;
use Save As or reload explicitly. A failed load or edit leaves the current
document intact. Export does not mark the source as saved.

The shared model file service reads and stages output in cancellable 256 KiB
blocks. It records the reviewed destination's resolved path and SHA-256, then
checks both again immediately before publication. New files use an atomic rename
that cannot replace an existing destination. Existing files use an atomic save
and a lock shared by cooperating model writers. An unrelated application can
still change an existing file after the final check; that narrow filesystem race
is not claimed to be eliminated.

Import, editing, saving, and export run on a document worker. Longer operations
show their phase, progress, and Cancel control. Editing controls and shortcuts
are disabled until the worker returns; the displayed document stays intact on
failure or cancellation. Closing requests cancellation and defers closing until
the worker finishes. Cancellation after a successful file commit remains a
successful save or export. Geometry, validation, editable JSON conversion, and
MDL/MD2/MD3/OBJ writing check cancellation within their loops. Native decoder calls,
Qt JSON parsing/encoding, and PNG codecs check it before and after each bounded
call. Mesh package handoff prepares native bytes on that worker, then validates
the current package destination before staging. Package-plan publication and
parts of the view refresh still need a responsiveness audit at authoring limits.

Sources are limited to 64 MiB, 32 surfaces, 65,536 vertices, 131,072 triangles,
1,024 frames, 1,024 clips, and 1,048,576 total frame vertices. All frames share topology and
UV indices. Imported UV seams are not welded implicitly. Native models with
decoder errors or warnings cannot be imported as editable sources.
Topology expansion checks vertex and frame-storage limits before appending
poses; this also bounds temporary working geometry. A dense edit may need a
smaller selection even when later compaction would reduce its final vertex count.

MDL, MD2 and MD3 geometry import uses the shared native decoders. MDL retains
every indexed skin member, native frame grouping, cumulative group times, flags,
sync type, eye position and size hint. Every other decoded format imports through
the same decoders. Skeletal models keep their skeleton and are re-baked to at
most 1,024 frames when their animation is longer; see
[Native Model Formats](MODEL_FORMATS.md).

### OBJ polygon interchange

Open `.obj` in the mesh editor, select an OBJ in the package Models browser, or
run `model import prop.obj --output prop.mesh.json`. All routes share the same
bounded decoder. Save the editable source, assign or review package materials,
then export/stage the appropriate game format. Automatic level placement still
uses MD3 and a Quake III map.

Import retains positions, face winding, separate position/UV/normal indices,
hard normal and UV splits, and direct `usemtl` package paths. Object/group/material
combinations become uniquely named surfaces. Concave planar polygons are
triangulated without removing their corners; self-intersecting, repeated-corner,
collapsed and non-planar polygons fail with a line diagnostic. Supplied normals
are normalized and take precedence over smoothing groups. Missing normals use
area-weighted smoothing groups, or one flat normal per polygon when smoothing is
off. Missing UVs start at zero. UV V is converted from OBJ's bottom origin to the
studio's top origin once. No implicit axis, unit, scale, winding or welding
conversion is performed.

Positive and relative negative indices, UTF-8 names/BOM, comments, CRLF and
backslash continuations are accepted. Faces must reference already-declared
entries and use a consistent index form. Limits are 64 MiB of source, 64 KiB per
logical line, 1,024 corners per polygon, 65,536 entries in each attribute list,
32 surface combinations and the ordinary authoring limits after seam splitting.
Intersection and triangulation work has a shared 16-million-check budget;
triangulate complex source polygons before importing when it is exceeded.
Cancellation is polled inside parsing, triangulation and assembly.

Wavefront `mtllib` shading libraries are **not yet supported**. Export without
materials in the source modeller, then assign game/package materials here.
Without `mtllib`, `usemtl` names are treated as direct package-relative material
references; an empty assignment clears the material. OBJ export explicitly
clears unassigned surfaces so they cannot inherit a preceding surface's material.
Free-form curves/surfaces, lines, points, loose vertices, vertex colours,
non-unit position weights, 3D UVs, unknown records and external commands fail
instead of being discarded. Empty object/group declarations carry no geometry.
The imported result is one pose; OBJ does not provide the studio's animation,
attachment or collision authoring data.

The package browser streams and verifies the selected OBJ on a worker, then
resolves each surface's package material independently. Loading, Cancel Preview,
failure and material diagnostics remain visible. A newer selection or package
snapshot retires old work; cancelled/failed models can be selected again to retry.
Ambiguous package paths, oversized payloads, short reads and late checksum
failures cannot produce an editable model. Staged OBJ bytes and textures use the
same immutable package view as other studio surfaces. Native model previews
share this worker, with their frame, skin, palette and tag data preserved.

### Quake MDL

The **Quake MDL** inspector edits native skin slots, members, groups and header
settings. Add Skin accepts opaque 8-bit indexed PNG with all 256 palette entries,
single-plane PCX, Quake LMP/miptextures, WAL or M8. Native mip images contribute
their base level; the complete stored mip chain must validate. The first skin
prepares an ordinary mesh for MDL export. Later skins must match its dimensions and palette exactly; there
is no implicit RGB quantization or palette remapping. Prepare colour conversion
explicitly in the texture workflow. Equal RGB colours still retain distinct pixel
indices, including player-colour and fullbright ranges. Index 255 remains opaque.

**Import Package Texture…** reads the current package snapshot, including texture
edits staged by the Texture Editor. Filter paths, select the exact zero-based
entry, then add a skin or replace/append the inspector's selected member. Entry
numbers distinguish repeated WAD names. A changed package invalidates an open
picker; an accepted import copies the chosen snapshot into one model undo step.
It does not save the package or keep a live texture binding.

Embedded image palettes are authoritative. For images using an external palette,
the active palette family selects candidate paths in the package; the first
existing candidate must be unique, readable and valid. Missing candidates use
the model's palette, including its generated-preview status. Corrupt/ambiguous
candidates stop the import instead of falling back. Existing model dimensions
and RGB palette bytes must match exactly. PNG/WAD3 transparency, truncated mips,
unverified streams and cancelled work cannot change source or history. Reads and
decoding run on the normal cancellable document worker with a 64 MiB input limit
and 16 megapixel base-level limit. These rules also apply to the CLI.

Replace Member changes its pixels without changing timing. Append Member adds
the chosen hold duration; promoting a single image assigns that duration to both
images. Apply Member Duration shifts later cumulative end times. Remove Member
retains a timed group of one; Remove Skin removes its slot and shifts later skin
numbers. Source skins allow 256 slots, 256 members per group and 16 megapixels
across all members; native export has narrower limits. Load Palette accepts
exactly 768 RGB bytes and rebuilds previews without changing indices. Loose MDL
imports initially use a generated preview palette; package imports can carry a
resolved palette. The generated/imported distinction stays visible in the source.

Preview Member loads the selected member on a worker and displays it on the
model and UV view. It is session-only and resets when the source or skin selection
changes. Normal material preview uses the first embedded image.

Native preview selects a native frame and skin slot independently. **Stored
timing (software Quake)** loops each group's cumulative intervals; a single native
frame can still have an animated skin. **Original GLQuake timing** uses the first
pose interval uniformly and cycles four skin slots at 10 Hz, ignoring skin times.
Groups shorter than four repeat; longer groups use the last member assigned to
each modulo-four slot. Both modes display exact poses; clip FPS and Smooth
preview are disabled while native timing is active. This samples the published
schedules with bounded double arithmetic, not the original engines' numerical
overflow or rendering effects. Original-engine acceptance remains required.

Preview Native Timing prepares the selected skin's images on a cancellable worker
and starts at Seek time. Play / Pause retains the native time; Seek Time samples
without automatic motion. Reduced motion prevents play but permits seeking.
Entity phase is an explicit 0–1 second offset for random-sync software models;
synchronized models and original GLQuake ignore it. The default phase is zero,
so every preview is reproducible. Native preview does not advance between native
frames; game code controls those transitions. Selecting an ordinary pose/clip or
Use Clip Timing returns to the normal transport. Document changes retire cached
images. Playback, seek and phase never change source, history, recovery or export.
Prepared images total at most 64 MiB, share pixel storage on ticks, and update
both model and UV views through coalesced raster work. The UI seek range is one
day; the CLI sampler also accepts larger finite nonnegative times.

The studio preview does not emulate Quake player-colour translations, indexed
fullbright lighting or model-flag effects. Those indices and flag bits are
preserved for the game; current previews show their palette colours with the
studio's material lighting.

GLQuake-style renderers also flood-fill the colour connected to the top-left
texel of the first skin as background to reduce mipmap halos. If that connected
region includes used UVs, its colours can change in game. Keep background padding
separate from used UV regions and check the target renderer. The studio preserves
every exported index; member and native-timing previews do not emulate this
texture preprocessing. The optional [engine rendering check](MODEL_ENGINE_ACCEPTANCE.md#offscreen-native-rendering)
uses a generated palette/guard texel and a deliberate failing control to expose
the difference. Index 255 is not an authoring transparency option.

Group Range assigns uniform hold durations to an inclusive pose range. Ungroup
Range creates individual native frames. Untouched portions of existing groups
retain their individual durations. Apply Current Pose Duration changes the pose
selected above the viewport. Native groups and game frame numbers are separate
from editor clips. Changing grouping can require updating game code. Duplicating
a grouped pose copies its hold time; deleting removes it. In-betweens inside a
group divide the preceding pose's hold time without extending the group; insertion
between native frames creates single frames. All operations use normal document
validation, undo, recovery and cancellation.

MDL sources use schema 4 for native settings and exact Base64 indexed arrays,
schema 5 with static collision, schema 6 with saved clip FPS, or schema 7 with
animated collision. Sources without these features remain schema 3;
schemas 1–7 continue to open. Preview images
are derived from indices and the source palette. The palette itself is external
to the exported MDL, so game colours depend on the target installation.

Export MDL targets original Quake's renderers: one surface, 256 total poses,
1,024 stored vertices, 2,048 triangles, 32 skin slots, no attachment tags, and
15-character printable ASCII pose names. Skin width must be divisible by four,
height at most 480, and each image at most 307,200 pixels. UVs must stay within
0–1 and round to texel centres. Exact all-pose position/normal duplicates share
storage; compatible half-width UV seams use the native seam bit and per-triangle
front/back flag. Mixed seam directions are split deterministically. Packing
preserves coordinates but does not guarantee a global minimum vertex count.

All poses share one byte-coordinate scale. Export reports maximum position,
normal-angle and UV errors and refuses collapsed or reversed triangles. Bounds and radius
are recomputed; flags, eye position, sync, size hint, groups and indexed images
are retained. Source-only clip metadata and external material paths are reported.
OBJ explicitly reports omitted native data. Stage in Package accepts `.mdl` paths,
typically `progs/name.mdl`; automatic level placement still requires MD3 and a
Quake III map. Save the authoring source alongside native derivatives.

MD2 export includes every pose, frame name, and ordered external skin slot,
including repeated paths. **Handoff > MD2 skin width/height > Apply Skin Size**
changes the dimensions through undo/history and recovery. Imported MD2 dimensions
are retained; new meshes and older source schemas default to 256 by 256. These
settings describe the matching PCX skin; changing them does not resize an image.

The target is the original Quake II renderers: one surface, 512 frames, 2,048
position/normal vertices, 4,096 triangles, 32 skin slots, and skins at most
640 by 480 pixels. Skin paths must be safe package-relative PCX names of at most
63 printable ASCII characters; frame names allow 15. Attachment tags, embedded
images, and multi-surface output are refused. Exact position/normal duplicates
across **every** pose can share XYZ indices while retaining separate UV indices.
No pose-dependent weld or normal averaging occurs.

Each pose uses byte coordinates within its own bounds. Normals select the nearest
of the format's 162 directions. UVs select the nearest skin texel centre, clamping
boundary coordinates 0 and 1 to the first and last texels; UVs outside that tile
are refused because the original software renderer does not wrap them safely.
Software triangles and GL strip commands use the same winding, XYZ indices and
texel coordinates. Every pose is checked for quantization collapse or reversal;
failures identify the face/frame and produce no output. Export/staging diagnostics
report maximum Euclidean position, angular normal, and per-axis UV errors.

MD2 cannot store surface names, explicit frame origins, custom animation ranges,
or seam marks; these remain in the authoring source. The browser infers native
animation groups from frame names. Keep the source alongside game derivatives.
The decoder now samples MD2 UV texel centres consistently with the original GL
convention; existing `.mesh.json` UV values are not migrated or shifted.
Skin-image dimensions and PCX contents still need package/material review; the
writer does not load or convert images. Skin filenames must use the lowercase
`.pcx` suffix understood by the original engine. Editable MD2 import audits GL
strips/fans against the indexed faces and texel coordinates, refusing malformed
or disagreeing streams instead of discarding renderer-specific data. Software-only
files with no GL stream can still be edited; export creates a complete stream.
Source-port-specific limits, optimized output strips, and preservation of differing
GL-only geometry remain open.

MD3 export includes all frames, materials, and tags. It targets original Quake
III renderer limits of 1,000 vertices and 2,000 triangles per surface, rather
than the looser nominal file-format limits. Model coordinates must quantize to
1/64 units within -512 through 511.984375; triangles that collapse at that
precision are rejected in every frame. Frame names must fit 15 printable ASCII
characters and surface, material, and tag names 63. No fields are silently
truncated. Surface names must also remain unique after Quake III lowercases them
and strips a trailing underscore-plus-character. MD3 cannot retain embedded images or explicit clip-range metadata;
the latter stays in the authoring source and is identified in export notes.
The browser infers animation groups from frame names; configure ranges and
timing separately for the target game.

OBJ export writes the selected frame's geometry, UVs, normals, groups, and primary
material assignments. It has no animation, attachment tags, or MTL companion.
Tagged exports report omitted attachments in export notes and an OBJ comment;
export notes also identify omitted animation poses and clips. The editable source retains them.
Names that cannot be represented unambiguously as OBJ tokens are refused.

The package browser's **Export OBJ** and CLI `model export` retain their separate
one-frame geometry interchange contract. The CLI's optional `--material` assigns
one material name to all surfaces; use the editable mesh export above to retain
per-surface assignments. Neither route writes textures or an MTL companion.
Omission notes identify other poses, attachments and native skin data. JSON puts
them in `notes`; raw OBJ stdout stays unchanged, with notes on stderr.

Both browser and CLI now use `core/model_frame_export` and the guarded model
file service. Review precedes serialization; changed destinations, links,
directories and competing writers fail without truncating an existing file.
Explicit overwrite cannot replace a loose model source, the input archive, its
source folder contents, a portable draft or retained staged payload/history
storage. Choose an output outside that storage, then use package staging for
intentional package changes. Output and loose input are bounded to 64 MiB.
Dry run prepares the same result and checks the same protections without creating
files, directories or locks. As with other guarded model saves, a noncooperating
external writer can still race an existing-file replacement after its final check.

Browser export captures the displayed mesh/frame and immutable package reader,
then runs serialization and writing on the existing document worker. Progress
and cancellation remain available; changing selection cannot change the captured
output, and nested exports are blocked. Studio close requests cancellation and
resumes after the worker stops. Cancellation before publication preserves any
existing output; cancellation after a completed commit reports success.

Seam marks are authoring metadata stored only in `.mesh.json` and recovery.
MD2, MD3 and OBJ preserve resolved UV corners and split indices, but do not store
the marks or split geometry merely because an edge is marked. Move or detach
selected faces/islands to create independent UV corners when needed by export.

Stage in Package and Stage and Place share the primitive designer's package/map
service. The package-path extension selects MD2 or MD3. Automatic placement
requires MD3 and adds an undoable Quake III `misc_model`; MD2 can be staged in a
folder, PAK, ZIP or PK3, but requires game-specific placement/code integration. Package replacement remains explicit. Save the editable source,
package plan, and map separately. Dependency review and compiler handoff follow
[Model Design And Level Handoff](MODEL_DESIGN.md).

## Recovery

Closing the studio during a mesh operation first cancels the operation and waits
for it to finish. The editor then resolves unsaved changes and continues the
studio close. Cancel in the unsaved prompt keeps both open and withdraws that
close request. An in-progress move preview is cancelled before closing.

Keep local recovery copies is enabled by default. Every five seconds the editor
checks for unsaved changes and sends the latest mesh, selection, and frame to a
background writer. Copies live in the application data folder's `model-recovery`
directory, shown by Recover. An explicit settings file keeps them beside that
file. Disabling the preference stops pending writes and retains committed copies.
Save, Discard on close, or undo back to the clean state retires the current copy.

Recover lists up to 128 small headers, including time and source provenance.
Restore as Draft verifies the SHA-256 payload and complete mesh before replacing
the editor document. Reading and validation run on a cancellable worker. A
restored document is modified and has no save-path binding; Save asks for a mesh
source destination. The selected recovery copy remains available. Copies owned
by a running studio cannot be discarded in the chooser.

The version-1 `.vsmeshrecovery` envelope contains a header of at most 64 KiB and
an editable JSON payload of at most 64 MiB. Checkpoints replace atomically;
failed or cancelled writes retain the preceding copy. The last completed
checkpoint bounds recovery after an interruption; unsaved work after it may be
lost. Recovery is local and independent of AI connectors, packages, and game files.
Edge selections are stored as canonical endpoint pairs in the payload. Older
version-1 copies without that optional field restore with an empty edge selection.
The optional `tag` selection field stores one existing attachment name without
mesh component indices. Older copies without it retain their component selection.

## CLI

`model animations` reports `framesPerSecond` for each clip (zero means
unspecified). Set it with `model edit --operation set-clip-fps --clip N
--clip-fps 23.976 --output ./timed.mesh.json`; zero clears the rate. `add-clip`
also accepts `--clip-fps`. Positive rates must be 0.001–1,000; repeated or
inapplicable rate options are rejected. Mesh timing changes share validation,
undo and recovery with the GUI and never alter native MDL schedules.

```sh
vibestudio --cli model import ./samples/models/angled-panel.model.json --output ./out/panel.mesh.json --dry-run --json
vibestudio --cli model import ./samples/models/angled-panel.model.json --output ./out/panel.mesh.json
vibestudio --cli model edit ./out/panel.mesh.json --operation transform --surface 0 --faces all --offset 0,0,8 --output ./out/raised.mesh.json
vibestudio --cli model edit ./out/raised.mesh.json --operation duplicate-frame --frame 0 --name pose02 --output ./out/animated.mesh.json
vibestudio --cli model build ./out/animated.mesh.json --output ./out/panel.md3 --dry-run --json
vibestudio --cli model materials ./out/panel.mesh.json --package ./assets --json
vibestudio --cli model topology ./out/panel.mesh.json --surface 0 --json
vibestudio --cli model edit ./out/animated.mesh.json --operation add-tag --name tag_weapon --tag-origin 0,0,24 --output ./out/tagged.mesh.json
vibestudio --cli model tags ./out/tagged.mesh.json --frame all --json
vibestudio --cli model edit ./out/tagged.mesh.json --operation transform-tag --tag tag_weapon --frame 1 --rotate 0,0,31 --snap-angle 15 --pivot-mode selection --output ./out/posed.mesh.json --dry-run --json
vibestudio --cli model edit ./out/panel.mesh.json --operation transform --faces all --rotate 0,0,31 --snap-angle 15 --scale 1.27,1,1 --snap-scale 0.25 --pivot-mode selection --output ./out/turned.mesh.json --dry-run --json
vibestudio --cli model uv ./out/panel.mesh.json --surface 0 --json
vibestudio --cli model edit ./out/panel.mesh.json --operation uv-unwrap --faces all --uv-atlas-size 512 --uv-padding 4 --output ./out/atlas.mesh.json --dry-run --json
vibestudio --cli model edit ./out/atlas.mesh.json --operation uv-pack --faces all --uv-atlas-size 512x128 --uv-padding 4 --output ./out/packed.mesh.json
vibestudio --cli model edit ./out/atlas.mesh.json --operation uv-pack-around --faces 0,1 --uv-islands --uv-atlas-size 512x128 --uv-padding 4 --uv-pack-scale preserve --output ./out/around-painted.mesh.json --dry-run --json
vibestudio --cli model edit ./out/panel.mesh.json --operation uv-mark-seams --edges 0:2 --output ./out/seamed.mesh.json
vibestudio --cli model edit ./out/seamed.mesh.json --operation uv-transform --faces 0 --uv-islands --uv-pivot-mode selection --uv-rotation 90 --uv-offset 0.07,0 --uv-grid 0.125 --output ./out/mapped.mesh.json --dry-run --json
vibestudio --cli model edit ./out/seamed.mesh.json --operation uv-transform --faces all --uv-pivot-mode islands --uv-scale 0.75,0.75 --uv-rotation 90 --output ./out/individual.mesh.json --dry-run --json
vibestudio --cli model edit ./out/panel.mesh.json --operation split-edges --edges 0:2 --output ./out/split.mesh.json --dry-run --json
vibestudio --cli model edit ./out/panel.mesh.json --operation weld --vertices all --weld-distance 0.001 --output ./out/welded.mesh.json --dry-run --json
vibestudio --cli model recoveries --json
vibestudio --cli model recover ./copy.vsmeshrecovery --output ./out/recovered.mesh.json --dry-run --json
```

`model import` accepts an MD2, MD3, primitive `.model.json`, or existing mesh
source. `model edit` accepts a mesh source and one operation: `transform`,
`extrude`, `subdivide`, `split-edges`, `weld`, `fill-boundary-loops`, `bridge-boundary-loops`,
`duplicate-faces`, `delete-faces`, `flip-faces`, `normals`,
`remove-duplicate-faces`, `remove-unused-vertices`, `split-disconnected-fans`, `split-nonmanifold-edges`, `orient-faces`,
`uv-transform`, `uv-project`, `uv-mark-seams`, `uv-clear-seams`, `uv-detach`, `uv-unwrap`, `uv-pack`, `uv-pack-around`,
`material`, `md2-skin-size`, `duplicate-frame`, `delete-frame`, `rename-frame`, or
the tag operations below. `--surface` defaults to zero. `--faces` and `--vertices` accept
`all` or comma-separated zero-based indices. Component operations require an
explicit selection. `--frame all|N` defaults to all; frame operations require N.
`--edges` accepts `all` or comma-separated endpoint pairs such as `0:2,2:3`;
either endpoint order is accepted and canonicalized. Split requires edge selection.
Weld uses `--weld-distance` (default 0.001) and preserves seams unless
`--merge-seams` is explicitly supplied. Both operations always span every frame.

Transforms use `--offset x,y,z`, `--rotate x,y,z`, `--scale x,y,z`, and
`--pivot x,y,z`. `--pivot-mode origin|selection|custom` selects the pivot; the CLI
default preserves a custom pivot at 0,0,0. Non-custom modes reject `--pivot`.
For all-frame selection-centred transforms, `--pivot-frame N` selects the pose
whose bounds supply the fixed pivot (default 0). Frame-local transforms use the
edited pose and reject `--pivot-frame`. Optional `--snap-grid N`, `--snap-angle N`,
and `--snap-scale N` use the GUI's translation, degree, and factor snapping rules;
zero disables each step. Positive ranges match the GUI controls above. Pivot and
snap flags require `transform` or `transform-tag`; tags reject scale and scale snapping.
UV operations use `--uv-scale u,v`, `--uv-offset u,v`,
`--uv-rotation degrees`, and `--projection xy|xz|yz`. `--uv-pivot-mode origin|selection|islands`
chooses a pivot; `--uv-pivot u,v` chooses a custom pivot (optionally with
`--uv-pivot-mode custom`). The default is origin. `--uv-grid N` snaps the offset,
with the same zero/off and positive range as translation snapping, in UV units.
Pivot/grid options require `uv-transform` or `uv-project`. `--uv-islands` expands
the selected components to chart faces before a UV transform, projection,
detach, unwrap or pack. Seam marking/clearing requires `--edges`; detaching requires faces or
island expansion. A selected seam edge touches both of its incident islands.
`islands` uses a separate bounding-box pivot for every complete selected chart.
It rejects partial islands and explicit vertex/edge selections unless
`--uv-islands` expands them first. Projection uses `--frame N` (pose 0 for
`all`); splitting copies geometry and authored normals in all poses. Each pivot
mode flag may appear only once; custom coordinates cannot accompany `islands`.
`uv-unwrap`, `uv-pack` and `uv-pack-around` require faces or island expansion. `--uv-atlas-size N`
sets a square atlas; `--uv-atlas-size WIDTHxHEIGHT` sets independent dimensions
(for example, `512x128`). Both forms and `--uv-padding N` use the editor's
dimension/padding defaults and bounds above. Repeated size or padding flags,
malformed dimensions, and padding too large for the shorter axis are rejected.
Unwrap uses `--frame N` as the geometry reference, or pose 0 for `all`; corner
splits always preserve every pose. Existing UV islands are uniformly scaled
and translated by packing. `uv-pack-around` requires complete islands and treats
unselected/shared-material faces as fixed obstacles as described above.
`--uv-pack-scale fit|preserve` chooses uniform fitting or unchanged UV scale for
that operation only, defaults to `fit`, and cannot be repeated. The other two
atlas operations do not treat unselected UVs as obstacles. Images are not
repainted. Atlas-size/padding flags are refused on unrelated operations.
Atlas size, padding and packing scale accept both separated and `--flag=value`
syntax; duplicates are rejected across both forms.
Material assignment uses
`--material path`; frame naming uses `--name`. Authoring write commands support JSON and
dry-run. Existing outputs require `--overwrite`; source fingerprint checks
still apply when editing a loaded source in place. `model build` accepts either
source type, infers MD2/MD3/OBJ from the output extension (or `--format`), and uses
`--frame N` for OBJ. Its JSON reports `authoringSeams` and
`seamMarksExported: false`; text output explains source-only marks when present.
MD2 JSON adds `md2SkinSize`, ordered `skinSlots`, `storedVertices`, `maxPositionError`, `maxUvError`,
`maxNormalAngleDegrees`, and `exportNotes`. `model edit --operation md2-skin-size
--skin-size 320,200` saves target dimensions; integer values from 1 through 8192
can be stored, while MD2 export enforces its narrower renderer limits. The flag
is exclusive to that operation. `model build --dry-run --json` reports export
limits and quantization before writing. Assign a PCX material with `--operation
material --material models/panel/skin.pcx` and review it with `model materials`.
That legacy operation edits only the primary slot. Use `model slots` to author
all external skins in order, including alternates that must also satisfy MD2's
PCX constraints. Preview one alternate with `model materials --surface N
--material-slot N`; preview choices do not change source or export order.

`model animations <source> [--clip N]` lists stored clips by zero-based index,
name, inclusive `firstFrame`/`lastFrame` and `frameCount`. JSON also reports the
model's total `frameCount`. It accepts the same editable/native inputs as
`model topology`, supports text/JSON, and writes nothing. Empty clip lists are
valid. Invalid `--clip` indices return usage error 2.

Animation editing uses the same `model edit` output, dry-run and overwrite rules:

| Operation | Required options | Scope |
| --- | --- | --- |
| `add-clip` | `--name text --first-frame N --last-frame N` | Inclusive range; default `--frame all` |
| `rename-clip` | `--clip N --name text` | Existing clip by index; all-frame scope |
| `set-clip-range` | `--clip N --first-frame N --last-frame N` | Inclusive range; all-frame scope |
| `delete-clip` | `--clip N` | Clip metadata only; all-frame scope |
| `insert-inbetweens` | `--frame N` | Between N and N+1 across every surface/tag |
| `copy-frame-pose` | `--frame N --source-frame M` | Copy M into N across every surface/tag |

Insertion accepts `--insert-count N` (default 1, 1–1022 within total storage
limits) and `--name prefix` (default `blend`). Clip edits reject component and
surface selectors; pose operations also always span all surfaces. Range, clip,
source-frame and insert-count options are restricted to their relevant edits.
Invalid generated geometry or names return validation failure 4 without writing.
Preview speed is not a CLI authoring property. For example:

```sh
vibestudio --cli model animations ./out/animated.mesh.json --json
vibestudio --cli model edit ./out/animated.mesh.json --operation add-clip --name attack --first-frame 0 --last-frame 1 --output ./out/clipped.mesh.json
vibestudio --cli model edit ./out/clipped.mesh.json --operation insert-inbetweens --frame 0 --insert-count 3 --name attack --output ./out/blended.mesh.json --dry-run --json
```

`model recoveries [--directory <folder>]` reports headers; `payloadVerified` is
false until restoration. `model recover <copy> --output <new.mesh.json>` verifies
the payload and writes a new editable source. It supports dry-run and JSON, and
always protects existing files, the original source, and the recovery copy;
`--overwrite` is rejected. Invalid recovery contents return exit code 4,
destination/read/write failures 1, usage errors 2, and success 0.

`model materials <source> --package <archive-folder-or-draft> [--palette <id>]`
uses the same bounded image resolver as the editor. It accepts editable mesh,
primitive design, OBJ, MDL, MD2, and MD3 sources, and reports each surface's image/shader
paths, dimensions, source layer, warnings, and preview limitations. It supports
text and JSON and writes nothing. Success returns 0, source-read failure 1,
usage errors 2, package-open failure 3, and invalid source or material problems 4.
Choose external slots with `--surface N --material-slot N`, embedded MDL pixels
with `--skin N --member N`, or exact package `.skin` bindings with `--entry`
and/or `--entry-index`. These separate preview modes share **Models > Skin**
and leave authored bindings unchanged. See
[model browser appearances](MODEL_MATERIAL_SLOTS.md#model-browser-appearances)
for reset, palette, cancellation, receipts and export handoff behavior.

`model topology <source> [--surface N]` lists sorted indexed edges and incident
face indices and topology-health findings for one surface, defaulting to zero.
Its JSON `health` field includes the exact finding indices described above. It accepts the same source
formats as `model materials`, supports text/JSON, and writes nothing. Success
returns 0, read failure 1, usage errors 2, and invalid source 4. Use its endpoints
for `model edit --edges`; table row numbers are display order, not edge identifiers.

`model uv <source> [--surface N]` reports islands in deterministic face order,
their face/vertex indices, UV bounds, and marked seam pairs. It has the same
source formats and exit codes as `model topology`, supports text/JSON, and
writes nothing. Disconnected but coincident indexed corners stay separate.

`model tags <source> [--frame all|N] [--tag name]` reports each named origin and
its nine orientation components (three local basis vectors in model coordinates).
It accepts the same inputs as `model topology`, supports text/JSON and writes
nothing. JSON includes `tagCount` per frame, total `frames`, and the filtered
`tags` array. Unknown names and invalid frame indices return usage status 2.

Tag edits use `--operation add-tag|duplicate-tag|rename-tag|delete-tag|set-tag-origin|reset-tag-orientation|transform-tag|copy-tag-pose`.
All except `add-tag` require an existing `--tag name`; mesh component selectors
and scale options are refused. Add/duplicate/rename use `--name` for the new
identity. Identity operations require `--frame all` (the default). `add-tag`
accepts an optional `--tag-origin x,y,z` (default zero) and initial `--rotate x,y,z`.
`set-tag-origin` requires the absolute origin. `copy-tag-pose` requires
`--source-frame N` and copies that pose into the selected all/current-frame scope.
`reset-tag-orientation` explicitly writes identity axes in that scope.
`transform-tag` shares `--offset`, `--rotate`, `--snap-grid`, `--snap-angle`,
`--pivot`, `--pivot-mode` and `--pivot-frame` semantics with mesh transforms;
selection-centred pivots use the tag origin in the reference pose. Dry runs,
guarded source publication, validation and exit statuses match other mesh edits.

### Native MDL CLI

`model mdl <source.mesh.json>` inspects native settings, skin members and group
times in text or JSON. JSON includes a palette SHA-256 rather than image payloads.
Use `--input` instead of the positional source if preferred. Mutations require
`--operation` and `--output <source.mesh.json>`; `--dry-run` validates without
writing, and existing targets require `--overwrite`. Normal guarded source save
and history rules apply.

Read-only timing inspection adds `--time <seconds>`, with optional
`--native-frame N --skin N --timing stored|glquake --sync-phase <seconds>`.
Frame/skin default to zero, timing to `stored`, and phase to zero. The JSON
`sample` gives the zero-based pose/member and each cycle length (zero when no
cycle is used). These options require `--time` and cannot be combined
with mutations, output paths, dry-run or overwrite. For example:

```sh
vibestudio --cli model mdl ./models/prop.mesh.json --time 0.23 --native-frame 0 --skin 0 --timing stored --json
vibestudio --cli model mdl ./models/prop.mesh.json --time 0.23 --timing glquake --json
```

| Operation | Required options | Optional options |
| --- | --- | --- |
| `add-skin` | Skin source (below) | `--name <label>` |
| `replace-member` | `--skin N --member N` and skin source | — |
| `append-member` | `--skin N --duration <seconds>` and skin source | — |
| `remove-skin` | `--skin N` | — |
| `remove-member` | `--skin N --member N` | — |
| `skin-duration` | `--skin N --member N --duration <seconds>` | — |
| `group` | `--first-frame N --last-frame N --duration <seconds>` | — |
| `ungroup` | `--first-frame N --last-frame N` | — |
| `pose-duration` | `--frame N --duration <seconds>` | — |
| `header` | — | `--flags <uint32|0xhex> --sync 0|1 --eye x,y,z --mdl-size <number>` |
| `palette` | `--palette-file <768-byte RGB file>` | — |

Skin sources are mutually exclusive: `--image <file>` accepts indexed PNG, PCX,
LMP, miptexture, WAL or M8; `--package <archive|folder|draft.vibepackage>` requires
exactly one `--entry <unique-path>` or `--entry-index N`. Package indexes match
`package list --json` for that snapshot. Optional `--palette <family>` selects
package palette candidate paths (default `quake`), not a remapping operation.
It applies only to package sources. JSON `skinSource` records path/index, source
byte count/SHA-256, `paletteKind` (`embedded`, `package`, `model`), palette
path/index when applicable, generated status and palette SHA-256. Outputs cannot
replace package/draft inputs, even with `--overwrite`.

```sh
vibestudio --cli model mdl ./models/prop.mesh.json --operation add-skin --package ./textures.vibepackage --entry textures/prop.png --output ./out/prop.mesh.json --dry-run --json
vibestudio --cli model mdl ./models/prop.mesh.json --operation replace-member --skin 0 --member 1 --package ./textures.wad --entry-index 7 --output ./out/prop.mesh.json --json
```

Indices are zero-based; durations range from 0.000001 to 3600 seconds. Header
options change only the supplied values. Repeated, unknown or inapplicable
options are refused. Exit codes are 0 success, 1 read/write failure, 2 argument
errors, and 4 invalid native data. Build the derivative with
`model build <source.mesh.json> --output <name.mdl>`; its JSON reports stored
vertices, skin dimensions/count, native frames, precision errors and format notes.

## Release Status

Professional release acceptance remains open. The [modeller release gate](MODELLER_RELEASE.md)
records outstanding topology/UV, engine, platform, accessibility and performance
work, together with the scope of each accepted milestone. Native Quake III
animation configuration is available through [assemblies](MODEL_ASSEMBLY.md#quake-iii-native-animation).
The software preview uses
per-pixel depth for intersections and picking, retains texture coordinates when
clipping at the near plane, and composites transparent textures in depth order.
Its colour, depth and picking buffers are capped at 8,388,608 pixels;
larger viewports use a uniformly reduced render resolution. A cancellable worker
projects immutable mesh/material snapshots, prepares picking data, and paints
filled or wireframe images. Camera requests coalesce, while model and material
changes retire stale results. Selection and style changes reuse valid projection
data. Rendering state is visible and exposed in the accessible description.
Picks wait until the displayed camera and pose catch up. A conservative spatial
index retains exact continuous coverage, depth and texture-alpha checks; it
does not rebuild the scene for every pointer query. Wireframe remains an
intentional view through the whole mesh, with the same asynchronous completion
contract. Shared surface edges draw once, and selected edges retain a thicker
dashed cue above ordinary wires. Stroke widths scale with display density;
high-visibility themes use thicker strokes. Clipping keeps original selectable
edges distinct from the camera's cut boundary. Attachment markers follow the
displayed snapshot.

`model-viewport-latency-smoke` exercises an original grid with 65,536 vertices,
130,050 distinct triangles and 16 poses (1,048,576 frame vertices). It reports
API-call time, time to a completed image, heartbeat count and maximum event-loop
gap for camera, frame, selection, material, wireframe (including all faces
selected), picking and replacement
operations. `VIBESTUDIO_MODELLER_MAX_EVENT_GAP_MS` optionally enforces an explicit
machine/build budget; the default reports measurements with a suite watchdog.
`VIBESTUDIO_MODELLER_MAX_WIREFRAME_MS` optionally limits completed-image time
for wireframe mode changes, orbit and all-face selection on that machine/build.
`model-editor-latency-smoke` uses the same mesh in the real editor with the
studio dark theme and standard density to measure
preparation, component mode changes, Select All, vertex picks and pose refreshes.
It checks stable table rows, compact selection and displayed pose values;
`VIBESTUDIO_MODELLER_MAX_AUTHORING_GAP_MS` optionally enforces the local UI budget.
`VIBESTUDIO_MODELLER_TRACE_AUTHORING_EVENTS=1` adds the eight longest GUI event
dispatches taking at least 20 ms per stage, for diagnosis only. Default timing
runs omit that instrumentation.
Full save/recovery/export and worst-case translucent overlap remain separate
performance gates. See the recorded build and limits in
[Modeller Release](MODELLER_RELEASE.md).

## Quake III Native Animation

Native Quake III player timing can be authored in the linked [assembly workflow](MODEL_ASSEMBLY.md#quake-iii-native-animation). Its `animation.cfg` retains lower/upper frame offsets, loop tails, reverse playback and header metadata alongside the original models; composed bakes require their own frame mapping.

## Native Player Publication

A native lower/upper/head assembly can now use **Player Package…** to review its
models, skin assignments, animation, icon and dependency closure before publishing
a deterministic PK3. All source poses remain in separate MD3s and local assembly
transforms are baked into their native geometry and tags. Mesh edits, materials,
linked skins and staged project assets feed the existing shared services; sources
stay separate. See [native player packages](MODEL_ASSEMBLY.md#native-player-packages).
