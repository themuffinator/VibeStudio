# Skin weights through mesh edits

**Status: Partial.** Mesh, UV, surface and frame edits on skeletal models keep
each vertex's joint influences from the vertex it came from, interpolate them
for vertices made between others, map them through a joint mirror map for
**Symmetrize**, blend them for merged vertices, and bind new primitives the way
you choose. Nothing is guessed from positions: an edit that cannot say where a
skinned vertex came from is refused. Frame edits keep the bind frame and
skeletal clip frames tied to the skeleton while any surface is skinned. See the
[tool-by-tool table](MODEL_SKINNING.md#weights-through-other-tools) and
[Limits](#limits). VibeStudio is pre-alpha and largely untested in real
projects; [production acceptance](MODELLER_RELEASE.md) remains open.

Skeletal models (MD5, IQM, MDS, MDM, MDR, Ghoul 2 and Half-Life) store joint
influences per vertex: each vertex has one or more joints, a weight for each,
and its position in each joint's bind space. When an edit adds, removes,
reorders, interpolates or mirrors vertices, or moves them between surfaces,
those influences have to follow what the tool did. Guessing them from where the
vertices end up gives seams and coincident vertices the wrong weights, steps
where weights should blend, the neighbouring part's joints on copies pushed
close to it, and the wrong side's joints after mirroring.

## How weights follow an edit

Before a tool runs on a skeletal model, every vertex records where it came
from: its own surface and index. The shared vertex operations carry that record
with the vertex data, in the same places that carry the persistent vertex
identities saved in editable sources:

| What the tool does | What the vertex records | Resulting influences |
| --- | --- | --- |
| Keeps, reorders or compacts vertices | Its original vertex | Copied unchanged, bit for bit. Coincident seam copies keep their own, different weights. |
| Copies a vertex (UV splits, hard-edge splits, detaching, repairs) | The copied vertex | Copied unchanged. |
| Copies a vertex and moves it (Extrude, Duplicate Faces with an offset, Extrude Edges, Inset, Solidify) | The copied vertex, marked as moved | The source's joints and weights, rebound at the new bind position, and posed from the skeleton in every retained skeletal pose, so the copy turns with its joints. |
| Makes a vertex between others (Subdivide, Split Edges, Loop Cut, Bisect, Bevel Vertices, Poke Faces, the cut points of Symmetrize) | The vertices and blend weights its position used | Interpolated with the same blend: each joint's weight is the blended weight, and its offset is averaged so the posed position equals the same blend of the posed sources in every frame. |
| Mirrors a vertex (Symmetrize) | The source vertex and the mirror plane | The source's weights through the joint mirror map, rebound at the mirrored bind position, and posed from the mapped joints. |
| Merges vertices (Merge Vertices, Weld by Distance with **Preserve Seams** off) | Every vertex it merged, equally | One equal blend of the merged vertices' weights for every merged vertex, rebound where the tool put it and posed from the skeleton. |
| Moves faces between surfaces (Separate, Move, Join, Duplicate, Delete Surface) | The vertex and the surface it came from | Copied unchanged, whatever the surfaces' order before or after. |
| Recomputes normals around a topology edit | — | Weights and offsets stay; the normal is rebound from the bind pose, and retained skeletal poses take the skeleton's normal. |
| Adds geometry with no source (Add Primitive) | Nothing | Bound by the edit's binding: rigidly to one joint, or to the nearest bones. Without a binding, adding to a skinned surface is refused. |

Edge bevel, knife, spin and rip interpolate and pose influences themselves;
they keep doing so, and mark the surface as written by the tool. Where one of
them leaves a normal in a retained skeletal pose that differs from the skin's on
a vertex it made or rewrote, the skin's normal is used, so weight painting and
binding still accept the surface. A vertex the tool left exactly as it was
(position, normal and influences, bit for bit) keeps its normal, including a
normal you edited in one frame. Skeleton, binding, weight painting and weight
transfer edits write influences directly.

Retained skeletal poses are the bind pose and frames baked from a skeletal clip.
Frames you edited independently keep their own geometry. Interpolated vertices
are posed from the skeleton only where the tool's position already agrees with
it, so a pose you shaped by hand is not snapped back.

Influences are only derived, never invented across a boundary:

- A skinned surface whose tool kept no record of where its vertices came from,
  and did not write the influences itself, is refused with the advice to
  unbind, edit and bind again, even when its vertex count is unchanged. Every
  tool in the editor keeps the record. A record that names a vertex the model
  did not have is refused too.
- A surface cannot mix vertices that follow the skeleton with vertices that do
  not. Joining an unskinned surface into a skinned one, or moving skinned faces
  into an unskinned surface, is refused with the surface to bind first.
- **Weld by Distance** with **Preserve Seams** treats different weights as a
  seam: vertices with different weights are not welded, and when that leaves
  nothing to weld the edit says so. Welding needs the vertices to coincide in
  every frame, which differently weighted vertices usually only do in the bind
  pose.
- A blend that would need more than 64 joints, or that meets negative weights,
  is refused. A blend with more joints than any vertex of the surface had is
  applied and counted (see [What the editor and CLI report](#what-the-editor-and-cli-report)).
- Rebinding a moved, mirrored, merged or new vertex needs a retained bind
  frame; without one the edit is refused.
- A refused or cancelled edit leaves the model, selection and history unchanged.

## Bind new geometry

**Add Primitive** on a skinned surface opens **Bind New Geometry**:

- **Bind rigidly to a joint** ties every new vertex to one joint with full
  weight. The selected joint is chosen first, otherwise the last joint used,
  otherwise the joint that weights the surface most. Use this for helmets,
  armour plates and weapons.
- **Bind to the nearest bones** gives each new vertex up to four of the closest
  bones, as **Skin Weights and Binding…** does. Check the result in **Weight
  Paint**.
- **Add as a new unskinned surface** adds the primitive as its own surface with
  no weights, named as you choose.
- **Add as a new surface** gives a bound primitive a surface of its own, for
  example for a separate shader, with the same binding.

The editor remembers the choice for the session, and **Adjust Last Operation**
(F9) replays it. **Repeat Last** (Shift+R) asks again with the last choice
selected and a fresh surface name, so a repeat never reuses a taken name. A
primitive added to an unskinned surface stays unskinned without asking. On the command line, `model tool --tool add` takes
`--bind-joint NAME` or `--bind nearest-bones`, and `--new-surface NAME`
(which can be combined with a binding):

```sh
vibestudio --cli model tool ./models/player.mesh.json --tool add --primitive cube --surface 0 \
  --at 0,0,64 --size 12,12,12 --bind-joint head --output ./models/helmet.mesh.json --json
vibestudio --cli model tool ./models/player.mesh.json --tool add --primitive cylinder \
  --new-surface "rifle" --output ./models/rifle.mesh.json
```

## Frame edits on skeletal models

While any surface is skinned, the bind frame and every frame baked from a
skeletal clip follow the skeleton. **Duplicate Frame**, **Delete Frame**,
**Insert In-between Frames** and **Copy Full Pose** would detach them, so on
those frames the buttons are disabled and their tooltip says why; the CLI and
document service refuse with the same text, which points to **Skeleton > Edit
Skeletal Animation** (resize, copy or interpolate clip poses) and its command
line, `model pose --operation resize|copy|interpolate|remove|bind`. In-between
frames are refused only when both endpoints follow the skeleton.

On frames you edited independently the operations work as usual, and every
other frame keeps its clip mapping: duplicating, inserting or deleting
independent frames shifts the map with the frames instead of resetting it.
Vertices keep their influences through every frame edit. Skin and skeletal
authoring still need every frame to come from the skeleton, so delete the
independent frames (or unbind) before painting weights or editing clips.

Once every surface is unbound (**Unbind surfaces**, or `model skinning
--operation unbind`), nothing follows the skeleton and the frames are ordinary
vertex animation, for example on the way from MD5 to MD3. The four operations
then work on every frame. Duplicated and in-between frames are independent and
the other frames keep their clips. A clip that loses a frame, or has one
overwritten by **Copy Full Pose**, no longer matches its poses in order, so its
remaining frames become independent too; the other clips are kept.

## Symmetrize skinned surfaces

Mirroring copies one half's weights to the other half. Joints on the mirror
plane (a spine, a head) keep their names, whether or not a map names them:
across Y, `arm.L` stays `arm.L`. Each off-centre joint that weights the surface
needs its mirror partner on the other side of the plane, so the new left half
follows the left arm rather than the right one:

1. Select the faces to symmetrize and choose **Mesh > Symmetrize** with an axis
   and direction.
2. On a skinned surface with off-centre joints, **Symmetrize Skin Weights**
   lists each of them. Choose **Detect Pairs** to fill the partners from left
   and right naming: `Left`/`Right` words, `L`/`R` tokens between separators
   (`Bip01 L Thigh`, `l_hand`, `hand.L`) and a leading `L` or `R` (Doom 3's
   `Lhand`/`Rhand`). Only partners that exist and sit on the other side of the
   plane are filled in. Review or type the names; each cell's accessible name
   reads the joint and its partner.
3. To bind the mirrored half to the same joints instead (props, deliberately
   asymmetric rigs), check **Keep joint names for unmapped joints**.
4. Choose **Symmetrize**. A pair applies in both directions.

Without a partner for every off-centre joint that the mirrored vertices use,
the edit is refused and names those joints. A mapped pair that is not on
opposite sides of the plane (`Lhand=Rhand` across Z) is refused and named, even
with joint names kept. The editor remembers the last map for the session.
**Adjust Last Operation** (F9) replays it with a new direction; a new axis, or
a merge distance that moves joints on or off the plane, asks for the map again
(cancelling keeps the last result), and an axis with no off-centre joints drops
it. **Repeat Last** (Shift+R) also asks again for its target surface.

## What the editor and CLI report

Every edit on a skeletal model reports, per resulting skinned surface, how many
vertices kept their influences exactly, were interpolated, were mirrored, were
rebound where the tool moved them, took the averaged weights of the vertices
they merged with, or were bound by the edit's binding. It also counts the
interpolated or merged vertices whose blend has more joints than any vertex of
the surfaces they came from (`widened`, up to `widest`, against the previous
`previousLimit`). MD5 keeps every influence, but a format with a lower limit
keeps only the strongest, so limit them with **Reduce and normalise weights**
in **Skin Weights and Binding…** (`model skinning --operation prune
--max-influences N`) before such an export.

- The Mesh Editor status line (and the tool banner for edit-mode tools) names
  interpolated, mirrored, rebound, merged, bound and widened weights after an
  edit. Surface operations also confirm how many vertices kept their weights.
- `model tool`, `model surfaces` and `model edit` print one line per skinned
  surface and add a `skinProvenance` object to `--json` output:

```json
"skinProvenance": {
  "surfaces": [{"index": 0, "name": "body", "authored": false, "kept": 12, "interpolated": 7,
                "mirrored": 0, "rebound": 0, "merged": 0, "bound": 0,
                "widened": 3, "widest": 3, "previousLimit": 2}],
  "kept": 12, "interpolated": 7, "mirrored": 0, "rebound": 0, "merged": 0, "bound": 0,
  "widened": 3, "widest": 3
}
```

`authored` marks surfaces whose influences the tool wrote itself (edge bevel,
knife, spin, rip). `model surfaces` also lists whether each surface is
`skinned`, in its JSON and, on skeletal models, in its text lines. `model tool --tool symmetrize` adds the `jointMap` it used and
`keepJointNames`; `model tool --tool add` adds a `binding` object with the
`method` (`joint`, `nearest-bones` or `none`), the `joint` and whether the
resulting surface is `skinned`.

```sh
vibestudio --cli model tool ./models/player.mesh.json --tool symmetrize --faces all --axis x \
  --direction positive --joint-map detect --output ./models/player-mirrored.mesh.json --json
vibestudio --cli model tool ./models/player.mesh.json --tool symmetrize --faces all --axis x \
  --joint-map "Lupperarm=Rupperarm,Lloarm=Rloarm,Lhand=Rhand" --output ./models/out.mesh.json
vibestudio --cli model edit ./models/player.mesh.json --operation duplicate-frame --frame 12 \
  --output ./models/out.mesh.json --json
```

`--joint-map detect` uses the same naming rules as **Detect Pairs** and keeps
only the pairs that mirror across the chosen axis and involve a joint of the
surface; `jointMap` lists them. A list of `left=right` pairs (comma-separated,
applying both ways) names them explicitly, and `--keep-joint-names` keeps
unmapped off-centre joints. Joint names that contain commas or `=` cannot be
given on the command line. A refused edit exits with code 4 and the same
message as the editor, which names the command-line route as well as the
editor's.

## Limits

- Position tools (**Transform**, **Shrink/Fatten**, **Smooth Vertices**,
  weighted transforms and **Edge Slide**) keep every weight by index and move
  the vertices in the frames you edit. On a skinned surface those frames then
  hold your own geometry, which weight painting, weight transfer and binding
  refuse to overwrite, as for any independent edit. They do not yet move the
  bind pose and re-pose the clips for you.
- Merged weights are an equal blend of the merged vertices; there is no
  weighting by distance or area.
- Blends with more joints than the surface had are counted, not pruned: limit
  them with **Reduce and normalise weights** before exporting to a format with
  a lower influence limit.
- Decimate keeps each surviving vertex's own weights; its cost does not yet
  avoid collapsing across joint boundaries.
- Joining or moving an unskinned surface into a skinned one is refused; binding
  it to a chosen joint as part of the join is not available. Bind it first with
  **Skin Weights and Binding…**.
- Shading tools that change normals in a single frame keep that frame's normals
  as authored.
- Origins are not saved: they exist only while one edit is applied.

## Tests

`model-skin-provenance-smoke` uses a skinned fixture with non-uniform weights,
coincident seam copies carrying different weights, an interleaved vertex order,
per-joint offsets and a clip that turns the arm joints. It checks every edit
kind's classification, interpolation for Subdivide, Split Edges, Loop Cut,
Bisect, Bevel Vertices and Poke Faces against analytic blends, exact weights
through every surface operation (including index shifts, MD5 export and
re-import, undo and redo), mapped, refused and kept-name Symmetrize on
symmetric and half meshes, a closed-component Extrude, refused, rigid,
nearest-bone and new-surface Add Primitive, cancellation, and CLI parity.
It also checks that joints on the mirror plane keep their names whatever the
map says and that pairs on the same side are refused, that edge bevel and spin
leave a hand-edited normal away from the cut alone, that surfaces without a
record, records naming no vertex and rebinding without a bind frame are
refused, and that blends with more joints than the surface had are counted
and can be pruned again. CLI checks add `--joint-map detect` across an axis the
pairs do not mirror across and the skinned marks in the `model surfaces`
listing.
Retained poses are checked with the same consistency test weight painting
uses, and a following weight paint succeeds. A skinned 65,536-vertex grid with
16 poses times an in-place edit and a compaction; set
`VIBESTUDIO_MODELLER_MAX_SKIN_RECONCILE_MS` to enforce a budget.

`model-skin-provenance-sweep-smoke` applies every topology, UV, surface and
frame edit kind to the fixture (and to variants for the repairs, weld, fill,
bridge and packing) and checks, for each: every vertex at an original position
keeps its weights unless it was merged or mirrored, the report accounts for
every skinned vertex, the untouched coincident pair keeps its distinct weights
bit for bit, retained poses follow the skin, the clip map is unchanged, weight
painting, weight transfer and binding still accept the result, undo and redo
are byte-exact, and the edit is deterministic. Two parallel strips on different
joints check that Extrude, Duplicate Faces, Extrude Edges, Inset and Solidify
copies pushed onto the other strip keep their own strip's joints. Frame edits
are refused on the bind and clip frames, keep the clip map on independent
frames, and skeletal and skin authoring work again once the independent frames
are deleted. On an unbound skeletal model, frame edits apply on every frame
and only the clip that lost or overwrote a frame stops claiming its frames.
Every edit kind is either swept or named with the reason it keeps vertex data
untouched. CLI checks cover the frame refusal (exit 4, naming `model pose`), a
frame edit on an independent frame, unbinding then duplicating a clip frame,
and a weld reporting merged weights.

`model-skin-provenance-ui-smoke` covers the joint map and binding dialogs at
standard and doubled text with expanded strings (including the mirror cells'
accessible names and a bound primitive on its own surface), the Mesh menu
flows, **Adjust Last Operation** across another axis, **Repeat Last** after a
bound and an unskinned primitive, cancellation, the status line, and the frame
buttons, disabled with their reason on the bind and clip frames and enabled on
an unbound model. `model-skeleton-smoke` checks Subdivide
midpoints against averaged weights and frame edits against the clip map.
