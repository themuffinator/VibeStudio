# Edit dense skeletal models with a working set

**Available:** a skeletal working set that chooses which clips are baked into
editable frames, with oversized skeletal imports re-baked to fit instead of
refused. VibeStudio remains pre-alpha and largely untested in real projects.
This does not establish the [production gate](MODELLER_RELEASE.md).

An editable source holds at most 1,024 frames and 1,048,576 frame vertices
(the vertices of every surface times the frames). A Doom 3 or Quake 4
character with about 2,000 vertices and dozens of md5anims needs far more than
that, so the Mesh Editor bakes only some of its clips into frames: the
**working set**. Every clip stays in the skeleton, exact and with its curves.
The skeletal tools still edit clips outside the working set, and the MD5 and
IQM exports write every clip.

The working set is the set of clips with baked frames, so it is saved with the
`.mesh.json` source and kept by recovery copies; it needs no setting of its
own.

A full working set and a skeleton of many clips can pass the 64 MiB limit as
readable JSON, so such a source saves its baked frames, joint matrices and
influences as packed arrays (source version 12). A 64-joint model with 40
clips of 45 frames and about three quarters of the frame budget baked saves in
under 34 MiB. A skeleton so large that even the packed source is over the limit
opens with a notice; delete clips until it fits before saving. See
[Editable Meshes](MODEL_MESH.md#sources-import-and-export).

## Open a large skeletal model

Open an MD5, IQM, MDR, MDS, MDM or Ghoul 2 model with **Open / Import…** (the
dialog also lists **All files**), or select one in **Models** and choose **Mesh
Editor**. Designs and baked assembly poses handed to the editor are treated the
same way. When the baked frames exceed the editable budget, VibeStudio re-bakes
them from the skeleton:

- It keeps the bind pose, then takes clips in file order while they fit three
  quarters of the frame-vertex budget, skipping any clip that does not fit what
  remains so a later, shorter clip can still join. The spare quarter leaves room
  for mesh tools that add vertices to every frame.
- A clip that cannot fit even on its own, such as a 600-frame animation on a
  2,100-vertex mesh, stays outside the working set. Edit it through the
  skeletal tools (below).
- The status line says the frames were re-baked, how much of the budget the
  working set uses and which clips were left out. **Skeleton** shows the same
  summary with a budget meter; both measure against the whole editable budget.

A model within the budget opens with the frames its format decoded.

## Choose the working set

Open **Skeleton** > **Working Set…**. The checklist shows every clip with its
frame count, its cost in frame vertices and its state in words: **In working
set**, **Joins working set**, **Leaves working set**, **Outside**, **Too long to
bake** or **No poses**. Space toggles the current clip, and Enter applies the
checked clips. A clip too long to bake cannot be checked; its row says why.

- The meter and the summary show the frames and frame vertices the checked
  clips use, and how many vertices mesh tools could still add to every frame.
  A choice over the budget says so and cannot be applied.
- **Fit Automatically** checks what an oversized import would bake.
  **Bind Pose Only** unchecks every clip.
- **Show Details** lists the full report: the working set, the budget, the room
  left and each group of clips left out.
- **Apply** re-bakes the checked clips as one undoable edit through the model
  worker. Clips that stay keep their frames exactly; clips that join are baked
  from the skeleton and take manual attachments, unskinned geometry, frame
  origin, collision and Quake MDL timing from the bind frame, as **Add skeletal
  clip** does.

A clip leaves the working set only when baking it again would restore its
frames. A frame with its own geometry or normal edits, an attachment or
collision box posed on its own, its own frame origin or Quake MDL group timing,
or a custom animation range over leaving frames refuses the change and names
what holds it. Undo those edits, or keep the clip in the working set. Frames
added with frame tools also refuse it, as they refuse other skeletal clip
edits.

Joining clips are baked in clip order, between the frames of clips that stay. A
custom animation range that spans two staying clips with a joining clip between
them would grow to cover it, so that change is refused and names both; change
or remove the range first, or keep together the clips it spans. A range inside
one clip keeps its frames.

Frame names of baked frames come from their clip and are generated again when
a clip rejoins the working set.

## Edit clips outside the working set

**Two-Bone Inverse Kinematics**, **Animation Curves**, **Edit Animation Keys**
and **Transfer Animation Range** list every clip with poses, marking clips
outside the working set. Their previews skin one sample at a time, so you can
review a clip that has no editable frames. **Edit Skeletal Animation** lists
every clip and says when one is outside the working set. Their edits apply to
the clip in the skeleton exactly as they do to a baked clip.

**Export MD5 Animation of Clip…**, **Export All Clips as MD5…** and **Export
IQM…** read the skeleton, so they write every clip, in the working set or not.
MDL, MD2, MD3 and OBJ exports write baked frames, so they write only the
working set; their export report names the clips left out of the file.

## Use the command line

```sh
vibestudio --cli model import ./models/monster.md5mesh --output ./out/monster.mesh.json --json
vibestudio --cli model import ./models/monster.md5mesh --clips idle,walk --output ./out/monster.mesh.json --json
vibestudio --cli model pose ./out/monster.mesh.json --json
vibestudio --cli model pose ./out/monster.mesh.json --operation working-set --clips idle,run --output ./out/run.mesh.json --json
```

`model import` re-bakes an oversized skeletal model the same way. `--clips`
chooses the working set instead, by clip name or zero-based index separated by
commas, or `-` for the bind pose alone. A name or index that matches no clip,
or names one twice, exits 2 as in `model pose`; a choice over the budget exits 4;
either way nothing is written. A model named relative to the working folder
still finds its md5anim, GLA or `.def` companions beside it. On a skeletal model
the JSON adds `rebaked` and `workingSet`, and the text says whether the frames
were fitted to the budget or baked from the clips `--clips` chose, then lists
the report.

`model pose --operation working-set` changes the working set of an editable
source through the same edit as the GUI. `--clips` takes the same list; without
it, clips are fitted automatically. Listing reports `workingSet` and, per clip,
`inWorkingSet`; the text gives the report after the clip lines and before the
joint poses. The report states counts after labels (for example "Frames used:
41 of 1024"), so it reads the same without a translation catalogue, and the
Mesh Editor shows the same lines with numbers in the region format.

`workingSet` holds `budget` (`frames`, `frameVertices`), `vertices`,
`bindFrame`, `frames`, `frameVertices`, `fits`, `vertexRoom`, `clipIndexes`,
and `clips` with each clip's `index`, `name`, `frames`, `frameVertices`,
`inWorkingSet` and `state`: `baked`, `outside`, `over-budget`, `too-large` or
`empty`.

## Limits

- A clip whose frames alone exceed the budget cannot join the working set, so
  the main viewport and the **Animation** transport cannot play it; tool
  previews show one sample at a time.
- Baked frames are still stored for every clip in the working set, so the
  budget scales with vertices times frames rather than joints times frames.
- Formats whose first clip frame is the bind pose (Ghoul 2, MDR and
  animation-driven MDS) have no separate bind frame: taking the first clip out
  of the working set leaves no frame to edit the bind pose in.
