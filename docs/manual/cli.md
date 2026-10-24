# Command line

Model authoring `--dry-run` performs the shared source-save checks and complete
serialisation without creating output files, directories or save locks. Model
sources, auxiliary files and package/draft inputs are protected through path
aliases, including hard links to files inside source folders. Ordinary in-place `.mesh.json` editing remains supported
by commands that allow it. See [save and review output](models.md#save-and-review-output)
for overwrite rules and the limits of a dry run.

`model uv-density` inspects existing UV islands using `--texture-size N` or
`WIDTHxHEIGHT` and an optional `--reference-frame`. Add `--target`,
`--mode individual|group` and a separate `.mesh.json` `--output` to match
density. `--faces` expands to complete islands; dry-run and output protection
share the editor's document service. See [texture density](../MODEL_UV_DENSITY.md).

`model curve-transfer` also requires an output different from its input,
including path aliases, even with `--overwrite` or `--dry-run`.

`model tool --tool knife` cuts paths in a chosen orthographic or perspective
projection. Supply `--path 'u,v;u,v'` or a JSON `--path-file`, optionally with
`--through` and `--faces`. The reference pose determines cuts across all poses;
dry-run, source protection and output validation match the editor. See
[knife options](../MODEL_KNIFE.md#automate-a-knife-cut).

`model tool --tool bevel-edges` creates chamfers or rounded bands from explicit
edges. Use `--width`, `--segments 1..32`, `--roundness 0..1`, `--tile-size` and
`--reference-frame`; topology changes across every pose. See
[edge-bevel options](../MODEL_EDGE_BEVEL.md#automate-an-edge-bevel).

`model tool --tool rip` opens selected vertices or connected edge chains using
`--side-face N` and `--offset x,y,z`. `--opposite` swaps sides and `--fill`
adds gap faces. `--offset 0,0,0` without `--fill` saves a persistent split
without movement. Filled gaps require movement. The authoring source retains
connectivity through subsequent mesh and UV edits. Topology covers every retained pose; see
[rip options and limits](../MODEL_RIP.md#automate-a-rip).

`model tool --tool spin` makes curved face or open-border extrusions, or radial
face copies with `--duplicate`. Set `--centre`, `--axis` or `--axis-vector`,
`--angle`, `--steps`, `--tile-size` and optional `--no-close`. All poses share
the topology; see [spin options](../MODEL_SPIN.md#automate-a-spin).

`model tool --tool edge-slide` moves selected edge chains by a signed rail
fraction (`--slide`) or reference-pose distance (`--distance`). Current/all-frame
scope, validation and guarded output match the Mesh Editor. See
[edge-slide options](../MODEL_EDGE_SLIDE.md#automate-an-edge-slide).

`model tool --tool symmetrize` on a skinned surface needs a mirror partner for
each off-centre joint the mirrored half uses: `--joint-map detect` reads them
from left and right joint names, keeping only pairs on opposite sides of the
chosen axis, `--joint-map left=right,...` names them (each pair applies both
ways), and `--keep-joint-names` keeps unmapped joints. Joints on the mirror
plane keep their names. Without a partner, or with a pair on the same side of
the plane, the edit is refused and names the joints. On skeletal models,
`model tool`, `model surfaces` and `model edit` print how each skinned surface's
weights were kept, interpolated, mirrored, rebound, merged or bound, and how
many blended vertices now have more joints than the surface had; `--json` adds
a `skinProvenance` object, and `model surfaces` marks each surface skinned or
not skinned. See
[skin weights through mesh edits](../MODEL_SKIN_PROVENANCE.md#what-the-editor-and-cli-report).

`model tool --tool add` on a skinned surface needs a binding for the new
vertices: `--bind-joint NAME` ties them rigidly to one joint and
`--bind nearest-bones` binds each to its closest bones. `--new-surface NAME`
adds the primitive as its own surface instead, unskinned unless a binding is
also given (the editor's **Add as a new surface**). Without a binding on a skinned surface the edit is refused with
exit code 4. See [binding new geometry](../MODEL_SKIN_PROVENANCE.md#bind-new-geometry).

```sh
vibestudio --cli model tool ./models/player.mesh.json --tool add --primitive cube --at 0,0,64 --size 12,12,12 --bind-joint head --output ./models/helmet.mesh.json
```

On skeletal models with a skinned surface, `model edit --operation
duplicate-frame`, `delete-frame`, `insert-inbetweens` and `copy-frame-pose`
refuse the bind frame and frames baked from a skeletal clip (exit code 4, with
the same message as the editor, naming the `model pose` operation to use);
on independent frames they keep every other frame's clip mapping. After
`model skinning --operation unbind` of every surface they apply to any frame.
See
[frame edits on skeletal models](../MODEL_SKIN_PROVENANCE.md#frame-edits-on-skeletal-models).

`model select --select loop` or `--select ring` with `--edges a:b,c:d` (no
`--edge`) grows every listed edge to its loop or ring and joins the results,
as **Select Edge Loops of Selection** and **Select Edge Rings of Selection**
do in the Mesh Editor; without `--edges`, the edges between two `--vertices`
grow, as in the editor's vertex mode. Add `--extend` to keep the given
selection, as the editor does. JSON adds `seeds` and `refused` (each edge
without a loop or ring, with its `reason`); the command exits 4 when no edge
grows or no seed is given. `--edge a:b` still seeds a single loop. See
[selection operators](../MODEL_TOOLS.md#selection).

`model curve-transfer` copies or reverses a range between clips of the current
rig. Select joints/channels, destination start and a time scale or rate match;
review the JSON destination changes with `--dry-run` before writing. It shares
the GUI's curve, pose and output protections. See
[range-transfer options](../MODEL_ANIMATION_CURVES.md#automate-range-transfers).

`model curve-keys` selects stored keys across joints/channels and shares batch
retiming, duplication, removal and continuous-error reduction with **Edit
Animation Keys**. Use range or exact-sample selectors, inspect the JSON channel
reports and validate outputs with `--dry-run`. See
[key-group automation](../MODEL_ANIMATION_CURVES.md#automate-key-groups).

`model curves` inspects, captures, edits and bakes persistent skeletal animation
curves. Key actions share the GUI's channel, interpolation, tangent and sampled
pose validation, with guarded `.mesh.json` outputs and `--dry-run`. See
[animation curves](../MODEL_ANIMATION_CURVES.md#automate-curve-edits) for options
and examples. Native game exports receive sampled motion.

`model ik` solves two-bone inverse kinematics over an inclusive clip sample
range with an explicit end joint, target and pole. `--nearest-reachable` clamps
unreachable targets without stretching; JSON reports reach and residual distance
per sample. It shares the GUI pose service, source writer and dry-run validation.
See [inverse kinematics](../MODEL_IK.md) for required options and examples.

`model build` first checks the model against a delivery target, the game the
file is for: `--target ID` names one (such as `quakespasm` for MDL, `q2pro` for
MD2, `et` or `ioquake3` for MD3, and `gzdoom` for MD3 and IQM). Without it the
target saved with the project that holds the source (or `--project DIR`) is
used, else the one the game installation suggests (the installation the
project links, else one of its game, else the selected one, as releases choose
it), else the format's original engine; JSON says which in `targetOrigin`. A model over any limit
exits 4 without writing, and JSON lists every blocker in `targetIssues` with
its surface, the faces or vertices concerned, value, maximum and fix.
`model targets` lists the targets with their limits and sources, or, given a
source, checks it against them (`--format` and `--target` narrow the list; with
`--target`, a model that is not ready exits 4). An id that names targets of
several formats, such as `ioquake3` for MD3 and IQM, needs `--format` to check a
model, and exits 2 without it. `model targets --project DIR` shows the target
each format uses in that project; add `--save --format md3 --target et` (and
`--strict` or `--no-strict`) to save a choice, or `--clear` to remove them. A
manifest that exists but cannot be read is never replaced: `--save` and
`--clear` exit 1 and name it.
`model prepare SOURCE --format md3 --output NEW.mesh.json` applies the fixes the
check suggests (split, join, decimate, fit to MD3's range, reduce joint
influences; `--skip` leaves kinds out) and exits 4 when limits remain; use
`--dry-run` to review. See [delivery targets](../MODEL_TARGETS.md).

`model build` reports what each format leaves out or approximates in
`exportNotes`; the data a format cannot hold is also listed in
`exportOmissions`, and `--strict` refuses such an export (exit 4) without
writing, and `--no-strict` overrides a project that saves strict exports. For `.md5anim`, choose the clip with `--clip N|NAME`; `--frame N`
selects the clip that frame belongs to, and a model with several clips and
neither option exits 2. `--all-clips --output-dir DIR` writes one
`<clip>.md5anim` per clip after checking every destination, and
`--md5-frame-rate N` writes a whole frame rate in place of the clip's own, which
a fractional rate needs. With `--strict`, `--all-clips` prepares every clip
before refusing, and JSON lists each clip's `exportOmissions`, every omission
once at the top level and the `sharedNotes` every clip has. MDL, MD2 and MD3
keep clips a game rebuilds from the frame names, so an imported model exports
strictly again; renamed clips, clip rates and a skeleton are omissions. See
[what each export keeps](../MODEL_FORMATS.md#what-each-export-keeps-changes-or-leaves-out).
For an MD3, `model build` also names the detail files beside the output that
the target still loads with the new model (JSON `leftInPlace`, whose
`consistent` is false when the game would read one wrongly), and notes when the
source stores detail levels it does not write.

`model lod` writes Quake III detail levels, `name_1.md3` and `name_2.md3`, beside
the base MD3. `--target` sets the MD3 delivery target and so how many levels the
game loads (the project's saved target when omitted); more levels than it loads
exit 2. `--levels` and `--ratio`, `--target-triangles 900,300`, or
`--level-source 2=PATH` for a hand-made level describe the levels, and a source
that stores its levels builds them when none of these is given;
`--save-recipe` stores the described levels in the source. Every file is
prepared and checked before any is written, including that each level has the
base model's frames and surface names; existing files need `--overwrite`, and
JSON gives each file's `committed` state (`written`, `failed` or `pending`), its
`exportNotes` and `exportOmissions`, the `existing` files, any `findings`, and
in `leftInPlace` the detail files past the set that stay and still load.
`--strict` refuses a set whose files would leave data out (exit 4), writing
nothing. `model lod --inspect BASE.md3` (or `--package P --entry E`) checks an
existing set and exits 4 when the game would read it wrongly; in text mode it
prints the findings before the refusal. See
[detail levels](../MODEL_LOD.md).

`model skin-variants SOURCE.mesh.json` lists the named skin variants a source
owns; `--operation add`, `rename --to NEW`, `remove` and `set` need `--name` and
`--output`, and `--surface-material SURFACE=MATERIAL` (repeatable; empty returns
the surface to its primary material) gives surfaces their variant material.
`model skin-export MODEL --output FILE` writes the Quake III `.skin` of the
primary materials or of `--variant NAME`, from an editable source or any native
model; `--format doom3 --variant NAME` writes a Doom 3 skin declaration, with
`--model-path` and `--decl-name`, and exits 4 for a variant a material-to-material
skin cannot express. See [skin variants](../MODEL_SKIN_VARIANTS.md).

`model assembly --operation player-review` (and `player-export`) publishes more
skins with `--skin-variants a,b`, `--team-variants` (red and blue),
`--variant-skin NAME:ROLE=PATH` and `--variant-icon NAME=PATH`, detail levels with
`--lod-levels 1-2 --lod-ratio R` or `--lod-recipes`, and custom sounds with
repeated `--sound NAME=PATH`. A PATH starting with `package:` names a package
entry. Review refuses an upper part without `tag_weapon`, a team skin identical to
the model's own materials and any skin that misses a surface (exit 4). See
[native player packages](../MODEL_ASSEMBLY.md#native-player-packages).

`model assembly --operation players --package PATH` lists the Quake III players in
a package or folder, with their skins, detail levels, sounds and what stops one
from opening. `model assembly --new --from-player NAME --package PATH --output
FILE.assembly.json` opens one as a new assembly: three package parts on
`tag_torso` and `tag_head`, the `--skin-name` skin (by default `default`) linked
to each part, and native animation from the player's `animation.cfg`. The package
is never written. JSON `player.publication.arguments` holds the `player-review`
options that publish the player again. `animation-set --config-entry PATH` reads
an `animation.cfg` entry from `--package`; add `--config-entry-index N` when the
path is repeated. A missing player, model, `animation.cfg` or complete skin exits
4. See [opening a Quake III player](../MODEL_ASSEMBLY.md#opening-a-quake-iii-player).

VibeStudio includes a full command line that runs the same services as the studio. Use it to script
package checks, builds and conversions, to run VibeStudio in CI, or to inspect files without opening
the studio. This page shows how to run it, the options every command shares, and how to use its
output in scripts.

> [!NOTE]
> **Status: Available.** The command line covers every studio area, from projects and packages to
> maps, models, builds and diagnostics, and shares its services and tests with the studio. Like the
> rest of VibeStudio, it has had little real-world use so far.

## Connect map entities

```sh
vibestudio --cli map connect maps/start.map --profile netradiant-custom --object entity:2 --object brush:0 --output maps/linked.map --json
vibestudio --cli map connect maps/start.map --object entity:2 --object entity:7 --kill-target --game quake --output maps/kill-linked.map --dry-run --json
```

`map connect` (alias `map connect-entities`) connects sources to the last selected
entity owner. The default keeps multi-source, destination-first naming. The NRC
profile requires exactly two selected objects and prefers the source's existing
`target` value, then the destination's `targetname`, then a unique classname-based
name. `--kill-target` uses those two-object rules with `killtarget` under every
profile; `--game quake|nexuiz|xonotic` supplies a compatible context. Without
`--game`, built-in map evidence determines the game. Quake II/III and Doom
contexts reject KillConnect. World primitives resolve to worldspawn for NRC
connections. JSON reports `connectionMode`, `profile`, `game`, `property`,
`targetname`, resolved `sourceEntities`, `targetEntity`, and `changedEntities`
(the directly edited entity count, before linked-copy propagation).

Use repeated `--object` selectors, or the existing `--select` spelling, in pick
order; do not mix spellings. `--input` may replace the positional map path.
Unknown options, repeated singleton options, empty values and ambiguous paths
are rejected. `--dry-run` prepares without writing; `--overwrite` authorises an
existing output. Both operations share the GUI's validation, locks and undo
transaction, and preserve the input file.

## Move brushes between map entities

```sh
vibestudio --cli map move-to-entity maps/start.map --object brush:12 --object brush:7 --output maps/moved.map --json
vibestudio --cli map move-to-world maps/start.map --object brush:12 --selected-only --output maps/world.map --json
vibestudio --cli map explode-entity maps/start.map --object entity:4 --rename-targetnames --output maps/pieces.map --json
vibestudio --cli map detail maps/start.map --object brush:12 --structural --policy trenchbroom --output maps/structural.map --json
```

`map move-to-entity` (alias `map move-primitives`) moves the `--object` brushes
and patches, and those of any given brush entity, into an existing entity that
keeps its keys, as **Move Primitives to Entity** does. `--target last` (the
default) and `--target first` choose the entity of the last or first `--object`;
`--target entity:N` names one, worldspawn included. A point entity cannot take
brushes. `map move-to-world --selected-only` moves only the given primitives to
the world, and `map explode-entity` gives every brush and patch of the given
brush entities an entity of its own with identical keys; `--rename-targetnames`
gives each new piece its own `targetname`. `map detail --structural --policy
trenchbroom` also moves the brushes out of brush entities, as TrenchBroom's
**Make Structural** does; `--policy detail` is the default.

Entities left empty go, and primitives keep their ids, groups and visibility.
Locked or linked objects, worldspawn for `explode-entity`, and a move with
nothing to do are refused with exit code 1 and write nothing; a bad `--target`
or `--policy` is a usage error (2). JSON reports `target`, `targetClass`,
`moved`, `movedBrushes`, `movedPatches`, `removedEntities` and `keptHidden`, or
`explodedEntities`, `createdEntities` and `movedPrimitives`, or `policy`,
`movedToWorld` and `flagsCleared`. See
[Edit Quake-family maps](levels.md#edit-quake-family-maps) for the editing workflow.

## Colour map entities

```sh
vibestudio --cli map entity-colour maps/start.map --object entity:3 --object brush:12 --colour "1 0.5 0" --output maps/orange.map --json
vibestudio --cli map entity-colour maps/start.map --object entity:5 --object entity:3 --normalise --output maps/normalised.map --json
vibestudio --cli map entity-colour maps/start.map --where class=light --normalise --each --output maps/lights.map --json
```

`map entity-colour` (alias `map entity-color`) writes the `_color` key of the
`--object` entities and of the entities holding the `--object` brushes and
patches, as **Set Entity Colour** and **Normalise Entity Colour** do. `--colour`
takes three numbers, as 0-1 fractions or 0-255 (spaces or commas), or `#rrggbb`;
a key already written as 0-255 stays 0-255, and a new one is written as
fractions. `--normalise` (or `--normalize`) scales the last object's colour so
its brightest channel is 1, black becoming white, and sets it on every target;
with `--each`, every entity normalises its own colour and ones without a
colour are skipped. `--where` picks the objects by query instead of
`--object`, and `--key` names another colour key, such as `color`.

World brushes add nothing (worldspawn is changed only when named itself), and
hidden entities are left out. A selection with nothing to colour, a last
object without a colour to normalise, and locked content are refused with exit
code 1 and write nothing; giving both or neither of `--colour` and
`--normalise`, or an unreadable colour, is a usage error (2). JSON reports
`mode`, `key`, `value` (what `source`, the target picked last, now holds),
`entities`, `changed`, `skipped`, `worldPrimitives` and `hidden`. An unchanged value
still writes the `--output` copy, with `changed` empty.

## Create map entities by class

```sh
vibestudio --cli map create-entity maps/start.map --class light --origin 64,0,96 --output maps/lit.map --json
vibestudio --cli map create-entity maps/start.map --class info_player_start --definitions defs/game.fgd --ray-origin 32,32,500 --ray-direction 0,0,-1 --view-direction 0,0,-1 --grid 16 --output maps/start-spawn.map --json
vibestudio --cli map create-entity maps/start.map --class func_door --object brush:12 --key speed=100 --output maps/door.map --json
```

`map create-entity` does what a **Create** command on the Levels page does,
such as **Create light**. `--class` must be a point or brush class of the
entity definitions: those of `--definitions` (a file or folder), or else the
built-in classes of the map's game. A point class goes at `--origin x,y,z`
exactly, or is placed against the map as **Paste at Cursor** places objects:
`--camera x,y,z` centres it on a camera, `--plane-point` with `--plane-normal`
and `--ray-direction` stands its definition box on a plane, and `--ray-origin`
with `--ray-direction` and `--view-direction` stands it on the first visible
brush the ray meets (add `--orthographic` for an orthographic view); these
take an optional `--grid`. A brush class makes the `--object` brushes and
patches, with those of given brush entities, into one; an entity left without
brushes goes. Repeat `--key key=value` for more keys, and `--context` names the
layer or group the new entity goes to (`default`, a layer, or a group to open).

The change is one undo step in the editor. A class the definitions lack,
worldspawn, a locked or hidden brush, one outside the open group, or a locked
layer is refused with exit code 4 and writes nothing; options that do not fit
the class, such as `--object` for a point class, are a usage error (2). JSON
reports `className`, `kind`, `entity`, `entityId`, `commandId` (the Levels
command's id), `definitions`, and `origin` and `placement` for a point class
or `brushes` and `patches` for a brush class.

## Expand and move map layers

```sh
vibestudio --cli editor scene select maps/start.map --objects brush:12 --expand-layers --json
vibestudio --cli editor scene move-to-layer maps/start.map --id LAYER-ID --objects brush:12,entity:3 --output maps/layered.map --json
vibestudio --cli editor scene move-to-layer maps/start.map --id default --objects brush:12 --output maps/default.map --dry-run
```

`editor scene select --expand-layers` does what **Expand Selection to Layers**
does on the Levels page: after the `--objects` pick (with `--selection`,
`--mode` and `--context` as usual), it adds every visible, unlocked object in
the layers of the picked objects, nested groups included, and writes nothing.
JSON adds `layers` (layer UUIDs, `default` for the Default layer) and
`skippedGroups`, the closed groups left out because a member is hidden or
locked.

`editor scene move-to-layer` does what **Move to Layer** does: `--id` names a
layer UUID or `default`, and the `--objects` move into it as one undo step. A
member of a closed group moves its whole group, and a brush or patch of a brush
entity moves its entity. It needs `--output`, takes `--dry-run`, and replaces an
existing file only with `--overwrite`. A group's UUID, a locked layer or locked
objects are refused with exit code 4 and write nothing; worldspawn or an unknown
selector is a usage error (2). JSON reports `moved` with `layer`, `objects`,
`released` (brushes that now follow their entity), `groups`, `alreadyThere`,
`worldspawn` and `changed`; objects already in the layer leave the map as it
was. See [Work with layers](levels.md#work-with-layers) for the editing workflow.

## Keep groups for TrenchBroom

```sh
vibestudio --cli editor scene list maps/tb.map --json
vibestudio --cli editor scene save maps/start.map --scene-format trenchbroom --output maps/start-tb.map --json
vibestudio --cli editor scene save maps/start-tb.map --scene-format vibestudio --output maps/start.map --overwrite
```

A map that TrenchBroom saved opens with its layers, groups and linked groups in
the scene, read from its `_tb_` keys, and `editor scene list` reports the scene
with `storage` `trenchbroom`. `editor scene save` writes the map again;
`--scene-format trenchbroom` writes its layers and groups as TrenchBroom's
`func_group` entities too, so TrenchBroom opens it with the same groups and
linked groups, and `--scene-format vibestudio` keeps them in VibeStudio's scene
note alone. Every other writing `editor scene` operation takes the same option;
without it a map keeps the form it opened with. JSON reports the form written as
`sceneStorage`. WADs refuse `trenchbroom` (exit code 2). See
[Share layers and groups with TrenchBroom](levels.md#share-layers-and-groups-with-trenchbroom).

## Replay level edits

For a one-off unique copy or array, use the same operation as **Duplicate, Make
Unique** in the editor:

```sh
vibestudio --cli map duplicate maps/start.map --object brush:0 --object entity:2 --make-unique --delta 64,0,0 --copies 3 --output maps/copied.map --json
```

`--make-unique` clones partial brush/patch entity owners and remaps copied names
and target references together, including outgoing references. It accepts
Quake-family text maps. Each array copy gets fresh names. `map clone` is an alias;
JSON reports `cloneMode` as `make-unique` or `ordinary`.
`--dry-run` validates without writing; existing outputs require `--overwrite`.
See [duplication](levels.md#edit-quake-family-maps) for the editing workflow.

**Copy Repeat Recipe** in Levels exports a recorded sequence for automation:

```sh
vibestudio --cli map repeat maps/start.map --recipe repeat.json --object brush:0 --output maps/repeated.map --json
```

The shared core validates the entire sequence before saving. Repeated `--object`
selectors, group `--context`, `--texture-lock on|off`, `--allow-valve220`,
`--dry-run` and `--overwrite` are supported. JSON map results expose
`map.repeatRecipe`; reopened maps have no session recipe. See
[Repeat object commands](../LEVEL_REPEAT.md) for the schema and bounded coverage.

## Run the command line

`model weight-transfer` mirrors or transfers weights between surfaces using
nearest vertices or interpolated triangle samples, exact joint maps, locks and
bounded correspondence. It shares the GUI document service, guarded source
writes and dry-run validation. See [weight transfer](../MODEL_WEIGHT_TRANSFER.md)
for all options and examples.

`model weight-paint` stages replace/add/subtract/smooth vertex weights with
joint locks and normalised remainders. Supply explicit surface, joint and
vertices, or replay a versioned coverage recipe; `--output .mesh.json` is
required. See [weight painting](../MODEL_WEIGHT_PAINT.md) for options, examples
and atomic validation rules.

`model skinning` inspects influences, selects vertices by weight or issue, and
binds, rebinds, reduces or removes skinning. It shares the GUI review service,
guarded source outputs, dry-run and JSON diagnostics. See [skin weights and
binding](../MODEL_SKINNING.md#automate-the-workflow) for options and examples.

`model rig` inspects and authors skeleton joints and selected vertex weights.
It supports `list`, `add`, `rename`, `reparent`, `remove` and `weight`, with
guarded `.mesh.json` outputs, `--dry-run`, `--overwrite` and JSON reports.
See [skeleton authoring](../MODEL_RIG.md#automate-the-workflow) for options and
examples shared with **Edit Skeleton** in the Mesh Editor.

`model pose` manages skeletal clips, transforms bind joints and pose ranges,
copies joint poses and interpolates hierarchical motion. Both commands share
the modeller's document service, guarded outputs and dry-run validation. See
[skeletal animation authoring](../MODEL_RIG.md#author-skeletal-animation).
`model pose --operation working-set --clips a,b` chooses the clips baked into
editable frames (`-` keeps the bind pose alone; without `--clips` they are fitted
automatically), and its listing reports the working set. `model import` re-bakes
a skeletal model over the editable frame budget, reports `rebaked` and
`workingSet` in JSON, and takes the same `--clips` to choose instead. In both
commands a clip name or index that matches no clip, or names one twice, exits 2;
a choice over the budget exits 4. A model named relative to the working folder
still finds its companion files (md5anim, GLA, `.def`) beside it. See
[the skeletal working set](../MODEL_SKELETAL_WORKING_SET.md#use-the-command-line).

```sh
vibestudio --cli model import ./models/monster.md5mesh --clips idle,walk --output ./out/monster.mesh.json --json
vibestudio --cli model pose ./out/monster.mesh.json --operation working-set --clips idle,run --output ./out/run.mesh.json
```
`model import` and `model edit` report the source they wrote (or would write
with `--dry-run`) in JSON as `"source": {"bytes", "encoding", "limitBytes"}`.
The encoding is `readable`, or `packed` when the readable source would pass the
64 MiB limit and the bulk arrays were packed (source version 12); text output
then adds a line saying so. The skeletal writers (`model pose`, `model rig`,
`model skinning`, `model ik`, `model curves`, `model curve-keys`,
`model curve-transfer`, `model weight-paint`, `model weight-transfer` and
`model skin`) report the same object as `"outputSource"`, with the same text
line for a packed source. An edit that would take a source over the limit
exits 4 with its size and writes nothing. See
[Editable Meshes](../MODEL_MESH.md#sources-import-and-export).
Joint transforms also accept an exclusive `--matrix` with twelve row-major
3×4 affine values, shared with the viewport handles. See
[viewport posing](../MODEL_RIG.md#pose-joints-in-the-viewport) for its contract.

The command line is the studio program itself, started with `--cli`:

```text
vibestudio --cli [--json] <family> <command> [arguments]
```

| How you got VibeStudio | Program to run |
| --- | --- |
| Windows installer or portable zip | `vibestudio.exe` in the `bin` folder where you installed or extracted VibeStudio |
| macOS disk image | `/Applications/VibeStudio.app/Contents/MacOS/vibestudio` |
| Linux AppImage | `./VibeStudio-<version>-linux-x86_64.AppImage` |
| Nightly package on macOS or Linux | `bin/vibestudio` in the folder you extracted |
| Built from source | `builddir/src/vibestudio`, or `builddir\src\vibestudio.exe` on Windows; see [Build from source](building-from-source.md) |

The examples in this manual write `vibestudio`. Replace it with the program's path, or add its folder
to your PATH. For example:

```sh
/Applications/VibeStudio.app/Contents/MacOS/vibestudio --cli --version
./VibeStudio-<version>-linux-x86_64.AppImage --cli package info ./id1/pak0.pak
```

`vibestudio --cli` on its own, or with `--help`, prints the full help: the shared options, then every
command with an example. Each command there lists the options it supports in brackets, such as
`[--json --dry-run]`. Quote paths that contain spaces, as in PowerShell:

```powershell
vibestudio --cli package validate "C:\Games\Quake\id1\pak0.pak" --json
```

## Options every command shares

Options can go anywhere after `--cli`.

| Option | What it does |
| --- | --- |
| `--help` | Prints the full help and exits. |
| `--version` | Prints the version and exits. |
| `--json` | Prints one JSON object instead of text. Every command supports it. |
| `--quiet` | Hides the text output of a successful command; errors still go to standard error. Ignored with `--json`. |
| `--verbose` | Adds timing and extra diagnostics to text output. Ignored with `--json`. |
| `--settings-file <path>` | Reads and writes settings in this INI file instead of your own settings. |
| `--dry-run` | Shows what a command that writes files would do, without writing. |
| `--overwrite` | Lets a command replace an existing output file. |
| `--watch` | Streams progress lines from `build run`, `compiler run` and `compiler rerun` in text output. Nothing is streamed with `--json`. |
| `--task-state` | Adds task-state objects to JSON output where supported. |
| `--exit-codes` | Prints the exit-code table below. |
| `--renderer <automatic\|opengl\|vulkan>` | Chooses the 3D renderer for this run (`material render`, `render backends`, `render test`) without changing the setting. |

The command line uses the same settings as the studio, so commands such as `install add`,
`compiler set-path` or `editor select` change what the studio sees. In scripts and CI, pass
`--settings-file` with a file of your own to keep them apart. `vibestudio --cli --settings-report`
prints where the settings in use are stored.

## Command families

Commands are grouped into families. `vibestudio --cli cli commands` prints the complete, current
list, and `--json` adds whether each command supports `--json`, `--dry-run` and `--watch`.

| Family | Covers |
| --- | --- |
| `cli` | The command list and the exit-code table |
| `ui` | Status chips, default keyboard shortcuts and command palette entries |
| `project` | Project manifests, health checks and file lists |
| `install` | Game installation profiles, Steam and GOG detection, and the index of each game's own assets |
| `workspace` | `.vibeworkspace` files |
| `package` | Inspecting, extracting, validating, comparing, staging and saving packages, drafts and recovery |
| `release` | Packaging and releasing a project, map, model or textures: plans, release notes, the changelog, publishing and release history |
| `asset` | Format capabilities, image conversion, audio editing, and project text search and replace |
| `map` | Inspecting, editing, rendering and generating maps, their textures and dependencies |
| `entity` | Entity definition catalogues and map entity validation |
| `editor` | Editor profiles, gestures, keys, view layout and plan panes, saved views and map layers |
| `model` | Inspecting, importing, editing, building and exporting models |
| `texture` | Decoding, creating, editing, exporting, staging and generating textures |
| `shader` | Quake III shader scripts |
| `material` | Textures, shaders and materials of idTech 1 to 4: list, check, render, edit |
| `render` | The 3D renderers: what each one found, a drawing test, and which one to use |
| `sprite` | Doom and Quake sprite plans |
| `code` | Code file lists, local language servers and format-preserving text saves |
| `compiler` | Compiler discovery, paths, single compiler runs and command manifests |
| `build` | Build pipelines and prepared builds |
| `bsp` | Compiled map inspection |
| `launch` | Game launch plans and launching |
| `localization` | Language targets and translation catalogue status |
| `accessibility` | Accessibility settings, speech voices and a speech test |
| `diagnostics` | Support bundles and crash reports |
| `ai` | Optional AI: status, connectors, connection tests, questions (`ai ask`), pictures (`ai image`) and rule-based proposals |
| `extension` | Extension manifests |
| `about`, `credits` | Version and credits, and credits validation |

## Common recipes

List every command:

```sh
vibestudio --cli cli commands
```

Inspect, filter and validate a package:

```sh
vibestudio --cli package info ./id1/pak0.pak
vibestudio --cli package list ./id1/pak0.pak --where "ext=wav"
vibestudio --cli package validate ./release.pk3 --json
```

Render a map to an SVG picture, optionally with a compiler leak trail. The projection is `top`,
`front` or `side`; without `--output` the SVG goes to standard output. `--grid` sets the grid spacing in map
units, fractions such as `0.5` included; a grid too dense for the picture is left out with a warning.

```sh
vibestudio --cli map render ./maps/start.map --output ./start.svg
vibestudio --cli map render ./maps/start.map --projection front --leak ./maps/start.pts --output ./start-leak.svg --overwrite
```

Index a game's own files once, then review and publish a map release with only your own files:

```sh
vibestudio --cli install register build quake3-games-quake3
vibestudio --cli release plan ./mymod --map maps/arena1.map
vibestudio --cli release publish ./mymod --map maps/arena1.map --release-version 1.0.0
```

Plan a build without running anything, then run it with live progress:

```sh
vibestudio --cli build list
vibestudio --cli build plan quake-full --input ./maps/start.map
vibestudio --cli build run quake-full --input ./maps/start.map --watch
```

Ask a model through your Claude Code login, or build a level with Quake-MapGen:

```sh
vibestudio --cli ai test-connection --provider claude-code --json
vibestudio --cli ai ask --provider claude-code --model sonnet --prompt "why does qbsp report a leak?" --context-file build/qbsp.log --yes
vibestudio --cli map generate --generator quake-mapgen --prompt "Quake 2 deathmatch, 8 rooms" --output maps/qmg.map --report maps/qmg.json
```

`--provider` takes a connector ID from the table in [Use a cloud provider](ai.md#use-a-cloud-provider),
such as `openai`, `claude`, `gemini`, `elevenlabs`, `local-offline` or `custom-http`; text commands
also take `claude-code`, `codex`, `custom-command` and `local-gguf`. For `claude-code`, `codex` and
`custom-command`, `--endpoint` is the program or command line; for `local-gguf`, `--model` is the
`.gguf` file. Map generation adds `--generator quake-mapgen` for Quake-MapGen, and `--ranker ggml`
ranks its layouts through ggml in builds made with `-Dggml=enabled`; the default, `scalar`, gives the
same scores. AI is off until you allow it; see [AI assistant](ai.md).

Write a diagnostics bundle to `./diagnostics/vibestudio-diagnostics.json` for a bug report:

```sh
vibestudio --cli diagnostics bundle --output ./diagnostics
```

List the target languages, then report on one of them:

```sh
vibestudio --cli localization targets
vibestudio --cli localization report --locale ar --json
```

Print the status chips with their non-colour signals, the default shortcuts with any conflicts, and
the command palette entries:

```sh
vibestudio --cli ui semantics
```

More examples are on the pages for each area, such as [Packages](packages.md),
[Package and release](releases.md) and [Build and launch](build-and-launch.md).

## Use JSON output in scripts

With `--json`, a command prints one JSON object on standard output, even when it fails. Every object
has the same core fields, plus fields of its own:

| Field | Meaning |
| --- | --- |
| `command` | The command that ran, such as `package info`. |
| `ok` | `true` only when the exit code is 0. |
| `status` | `success` or `error`. |
| `exitCode` | The exit code as an object with `code`, `id`, `label` and `description`. |
| `message` | What went wrong, when the command failed. |

For example, a package that does not exist:

```json
{
    "command": "package info",
    "exitCode": {
        "code": 1,
        "description": "The command was understood but the operation failed.",
        "id": "failure",
        "label": "Operation failure"
    },
    "message": "Unable to open package: Package path does not exist.",
    "ok": false,
    "status": "error"
}
```

Without `--json`, results go to standard output and errors to standard error.

## Exit codes

| Code | ID | Meaning |
| --- | --- | --- |
| 0 | `success` | The command completed successfully. |
| 1 | `failure` | The command was understood but the operation failed. |
| 2 | `usage-error` | Arguments were missing, malformed or incompatible. |
| 3 | `not-found` | A requested project, package, entry, installation or tool was not found. |
| 4 | `validation-failed` | Validation completed and found blocking problems. |
| 5 | `unavailable` | The workflow is recognised but no capable implementation or tool is available yet. |

A check that finds a problem exits with 4, not 1, so a script can tell "the check failed" from "the
command failed". For example, `package validate`, `package compare` and `entity validate` exit with 4
when they find problems or differences:

```sh
vibestudio --cli package validate ./release.pk3 --json > validation.json
if [ $? -eq 4 ]; then echo "release.pk3 has problems"; fi
```

```powershell
vibestudio --cli package validate .\release.pk3 --json | Out-File validation.json
if ($LASTEXITCODE -eq 4) { Write-Host "release.pk3 has problems" }
```

<details>
<summary>Single-option reports</summary>

These options print a report and exit:

| Option | Prints |
| --- | --- |
| `--platform-report` | Platform and Qt runtime details |
| `--settings-report` | Where settings are stored, and recent projects |
| `--preferences-report` | Accessibility and language preferences |
| `--setup-report` | First-run setup status |
| `--compiler-registry` | Compiler discovery, the same as `compiler list` |
| `--localization-report` | Localization targets and catalogue status |

</details>

## Learn more

- [CLI strategy](../CLI_STRATEGY.md): the full reference for every family, option and output field.
- [Troubleshooting and FAQ](troubleshooting.md): logs, diagnostics bundles and crash reports.


`map copy-surface --format map` exports ordered `.map` face records from repeated
`--target face:brushId:faceNumber` options. `paste-surface --clipboard faces.map`
auto-detects them and defaults to `--mode attributes`: last face supplies values,
target contents remain, and absent source surface flags/value preserve the target.
The default JSON copy and parameter paste remain available. See
[face clipboard examples](../LEVEL_PASTE.md#cli) for output protections and limits.

`map copy <map> --object brush:0 --output fragment.map` exports selected objects
as `.map` text. `map cut <map> --object entity:1 --clipboard fragment.map --output
remaining.map` exports them and saves the remaining map. Repeat `--object` for
brushes, patches or entities. Create output directories first; existing files
require `--overwrite`, including dry runs. Cut JSON reports `clipboardWritten`
and `mapWritten` separately, so a saved fragment is recoverable if the map save
fails. See [object clipboard examples](../LEVEL_PASTE.md#cli).


## Select level objects by hierarchy or source line

`map select-siblings <map> --object brush:12` returns selectable siblings;
`map select-lines <map> --line "42,57"` finds objects in the loaded source text.
Repeat **--object** or **--line** for multiple inputs. Both accept **--context**
for an open group and repeated **--exclude** selectors for external filters.
They are read-only queries: JSON includes `selection`, `sourceSha256`,
`queryOnly: true` and `mapWritten: false`. Pass returned selectors to normal map
authoring commands. See [Select by hierarchy or source line](levels.md#select-by-hierarchy-or-source-line)
for ownership, group, source-snapshot and undo behaviour.

## Query level objects and faces by type

`map select-type <map> --object brush:12` matches every selected owner's exact
class. With no non-world owner, supply **--material** to query matching brushes
and patches. **--mode faces --object entity:2 --material stone** returns matching
faces while retaining object selectors. **vertices** and **edges** modes are
no-ops. Repeated **--exclude** and **--context** supply view/group boundaries.
The read-only JSON response includes `matchedClasses`, `materialFallback`,
`componentSelectionChanged`, component `sets`, reusable face `selectors` and
one-based `surfaceTargets` for **map copy-surface**, **map paste-surface** and
material commands. Pass each surface selector as **--target**; component
selectors remain zero-based and use the component-transform syntax.
See [Select all of a type](levels.md#select-all-of-a-type) for exact matching,
material fallback, ownership, visibility and history rules.

## Query textured level surfaces

`map select-textured <map> --material stone` adds matching brush faces as
independent surface picks and matching patches as whole object picks. Repeated
**--object** and **--face face:brushId:faceNumber** supply existing picks to
preserve; face numbers are one-based. The query does not add parent brushes,
change component handles or replace other picks. Matching ignores case and
preserves whitespace. A no-match result leaves the supplied picks unchanged.

New matches respect hidden objects, locks, repeated **--exclude** filters and
the **--context** open-group boundary. Closed groups must be opened first.
Existing hidden picks remain in the response; check their editability before
using them for authoring. The combined face and matching patch result is limited
to 16,384 targets; overflow or invalid brush geometry fails the complete query.

JSON includes `faces`, `matchedPatches`, `selection`, `originalSelection`,
`addedFaces`, `addedPatches` and `surfaceTargets`. Pass each returned surface
target to **map paint-material --target**, **map copy-surface --target** or a
compatible **map paste-surface --target** workflow. `surfaceTargets` includes
the independent face picks and matching patches, without expanding unrelated
object picks. The response also includes `sourceSha256` and
`brushGeometrySha256`: re-query after source or geometry changes rather than
reusing stale face indices. Queries do not write a map or package draft.

**--within-selection** limits new matches to the **--object** objects (their
brushes and patches, and those of an entity), as the **Find** button of
**Find and Replace Textures…** does for **Selected objects**; JSON then reports
`withinSelection: true`.

**Status: Partial.** The shared query, the CLI and the **Select Textured**
viewport action share one service. See
[Select textured surfaces](levels.md#select-textured-surfaces).

## Find and replace level textures

`map replace-texture <map> --from base/wall --to base/metal --output out.map`
replaces every use of a texture, compared without case, on brush faces,
patches, or Doom wall textures and flats, as one edit. Repeated
**--object kind:id** limits it to those objects; repeated
**--face face:brushId:faceNumber** (one-based, as **map select-textured**
prints it) limits it to those faces instead, with **--context GROUP** to open a
group first. The two cannot be combined.

```sh
vibestudio --cli map select-textured room.map --material base/wall --json
vibestudio --cli map replace-texture room.map --from base/wall --to base/metal --face face:6:5 --face face:9:5 --output room-metal.map
```

Hidden and locked objects, and anything outside **--context**, keep their
textures; they are counted in `skipped` instead of failing the command. JSON
adds `replaced`, `skipped`, `scope` (`map`, `selection` or `picks`) and
`targets`, the surfaces in scope that used the texture. A missing face, a
`--face` that is not a brush face, or nothing in scope using the texture fails
without writing. **Find and Replace Textures…** in Levels uses the same
service; see [Find, replace and reveal textures](levels.md#find-replace-and-reveal-textures).

## Move, rotate and scale textured faces

Pass the `faces` from **map select-textured** to **map transform-components**
with repeated **--face face:brushId:faceNumber** to transform those faces
without selecting their brushes:

```sh
vibestudio --cli map select-textured room.map --material stone --json
vibestudio --cli map transform-components room.map --face face:6:5 --face face:9:5 --operation move --delta 0,0,16 --uv-lock on --output raised.map
```

**--face** can be combined with **--set** in the same edit and accepts the same
`move`, `rotate`, `scale` and `shear` options; `snap` refuses it.
**--context GROUP** opens a group before the picks are checked. A pick on a
hidden, locked or closed-group brush, a missing face, or picks on more than 256
brushes fail with exit code 2 and write nothing. JSON adds `faces` (the picks
after the edit, which may have new face numbers), `brushGeometrySha256`,
`retiredFaces` and `parentBrushesSelected: false`. Pass the new `faces` to the
next command to keep working on the same surfaces. See
[Transform independently picked faces](levels.md#transform-independently-picked-faces).

## Step textures by Radiant increments

`map align-textures` takes NetRadiant Custom's Surface Inspector steps.
**--scale-step U,V** adds each increment to the texture scale of every selected
face, keeping the texture origin in place; a scale that would reach zero flips
its sign instead, and `0` leaves that axis alone. **--degrees N** turns about
each face centre, or about the texture origin with **--anchor origin**:

```sh
vibestudio --cli map align-textures room.map --object brush:12 --scale-step -0.5,0 --output narrower.map --json
vibestudio --cli map align-textures room.map --face 12:3 --degrees -45 --anchor origin --output turned.map
```

Brush-primitive faces need **--package** or **--texture-size** for a scale step,
as patches do for a turn. Patches multiply their coordinates by a positive step
and divide them by a negative one. **--anchor** goes only with **--degrees**.
JSON reports `operation` (`scale-step` or `degrees`) and, for turns, `anchor`
(`centre` or `origin`). `editor controls <profile>` reports the profile's
`surfaceRotationStep` and `surfaceRotationPivot` (`face-centre` or
`texture-origin`). See
[Step textures by Radiant increments](levels.md#step-textures-by-radiant-increments).

## Round level geometry to the grid

`map round-to-grid` rounds each brush vertex, patch control point and
point-entity origin of the **--object** objects onto **--grid**, as Radiant's
Snap To Grid and the Levels page's **Round Selection to Grid** do. Brush
entities round their brushes and patches and keep their origin key. Repeat
**--set** to round only picked components instead:

```sh
vibestudio --cli map round-to-grid room.map --object brush:12 --object entity:3 --grid 8 --output rounded.map --json
vibestudio --cli map round-to-grid room.map --set brush:12/vertex/0,3 --grid 16 --uv-lock fit --output rounded.map
```

Hidden objects and brushes the grid would collapse are left as they were and
listed under `skipped`; when every object is refused the command fails and
writes nothing. **--set** components are all or nothing, and
**--allow-collapse** lets their corners merge. **--uv-lock off|on|fit**,
**--allow-valve220** and **--context GROUP** behave as they do for
**map transform-components**: `on` keeps every corner's texture coordinates or
refuses, `fit` keeps three corners of a face that cannot hold them all. JSON
reports `brushes`, `patches`, `entities`, `collapsedVertices`, `fittedFaces`,
`skipped` and the save fields.

**Status: Partial.** The shared service, the CLI and the Levels command share
one code path. See [Edit Quake-family maps](levels.md#edit-quake-family-maps).

## Nudge level objects and components

`map nudge` is the command line form of the Levels page's **Nudge** commands. It
moves the **--object** objects, or the repeated **--set** components, by
**--steps** grid steps of **--grid** units (1 and 16 by default) towards
**--direction**: `left`, `right`, `forward`, `back`, `depth-up`, `depth-down`,
`z-up` or `z-down`. The direction resolves as it does in a view: **--view top**,
**front** or **side** names a plan view (top by default), and
**--camera-yaw** with **--camera-pitch** names a camera looking that way in
degrees, yaw 0 along +X and counter-clockwise, pitch up positive:

```sh
vibestudio --cli map nudge room.map --object brush:12 --direction depth-up --view front --grid 8 --output nudged.map --json
vibestudio --cli map nudge room.map --set brush:0/face/2 --direction forward --camera-yaw 60 --uv-lock on --output nudged.map
```

In the front view `depth-up` is towards the viewer, -Y; in the camera it is up
world Z, and `forward` follows the horizontal world axis nearest the view.
Objects take **--texture-lock on|off** (on by default); components take
**--uv-lock off|on|fit** and **--allow-collapse**, as **map transform-components**
does, with **--allow-valve220** and **--context GROUP**. Doom maps nudge along X
and Y only. A locked object or a usage error fails and writes nothing. JSON
reports `direction`, `frame`, `axis`, `delta`, `grid`, `steps`, `mode`, the
counts and the save fields.

**Status: Partial.** The axes, the CLI and the Levels commands share one core
service. See [Nudge the selection](levels.md#nudge-the-selection).

Add **--duplicate** to copy the **--object** selection and move the copies
instead, as the Levels **Duplicate and Move** commands do. JSON adds
`duplicate` and the `copies`:

```sh
vibestudio --cli map nudge room.map --object brush:12 --direction right --duplicate --grid 32 --output copied.map --json
```

See [Copy and move in one step](levels.md#copy-and-move-in-one-step).

## Move level objects to the camera

`map move` with **--camera x,y,z** is the command line form of **Move to
Camera**. It centres the repeated **--object** selection on that point by the
**Paste to Camera** translation, snapped to **--grid** (0, no snapping, by
default), and keeps the objects' identities and place in the file. Point
entities are sized from **--definitions** when you give one, otherwise as a
16-unit marker. **--texture-lock on|off**, **--allow-valve220** and
**--context GROUP** work as for other moves; Quake-family maps only. A selection
already at the camera fails and writes nothing. JSON reports `camera`, `delta`,
`grid` and the save fields:

```sh
vibestudio --cli map move room.map --object brush:3 --object entity:2 --camera 128,0,64 --grid 16 --output at-camera.map --json
```

## Repeat transforms on copies or components

`map repeat-transforms` is the command line form of **Repeat Transforms**. Name
the transforms to repeat: **--scale x,y,z** (a negative factor mirrors),
**--shear AXIS,ALONG,FACTOR**, **--rotate AXIS,DEGREES** and **--translate
x,y,z**; they apply in that order. With **--object**, the selection is copied
where it stands and the copies are scaled, sheared and turned about their
centre, or **--pivot x,y,z**, then moved. With repeated **--set** components
they are transformed in place instead, with **--uv-lock off|on|fit** and
**--allow-collapse**:

```sh
vibestudio --cli map repeat-transforms room.map --object brush:4 --rotate z,15 --translate 64,0,0 --output array.map --json
vibestudio --cli map repeat-transforms room.map --set brush:0/face/2 --translate 0,0,8 --uv-lock on --output raised.map
```

Objects take **--texture-lock on|off** (on by default). Quake-family maps only.
One undo step; a locked object or a usage error fails and writes nothing. JSON
reports `transforms`, `mode`, `pivot`, the `copies` or component counts, and
the save fields.

**Status: Partial.** The CLI and the Levels commands share one core service.
See [Repeat the last transforms](levels.md#repeat-the-last-transforms).

## Turn and flip level objects as a view shows them

`map view-transform` is the command line form of the Levels page's
view-relative commands: **Roll Clockwise** and **Counter-clockwise**, **Rotate
90° Right** and **Left** (yaw), **Pitch Clockwise** and **Counter-clockwise**,
**Flip Horizontal** and **Flip Vertical**. Name one with **--transform**:
`roll-clockwise`, `roll-counterclockwise`, `yaw-clockwise`,
`yaw-counterclockwise`, `pitch-clockwise`, `pitch-counterclockwise`,
`flip-horizontal` or `flip-vertical`. The axis resolves as it does in a view:
**--view top**, **front** or **side** names a plan view (top by default), and
**--camera-yaw** with **--camera-pitch** names a camera, as for `map nudge`:

```sh
vibestudio --cli map view-transform room.map --object brush:12 --transform roll-clockwise --view front --pivot-grid 16 --output rolled.map --json
vibestudio --cli map view-transform room.map --object entity:3 --transform flip-horizontal --camera-yaw 60 --camera-pitch -20 --output flipped.map
```

From the front view, `roll-clockwise` turns +90° about Z; from a camera looking
nearest +Y, `flip-horizontal` mirrors along X. The objects turn or flip about
the exact centre of what moves, about **--pivot x,y,z**, or about that centre
rounded with **--pivot-grid N**: to N for turns and to N/2 for flips, as the
TrenchBroom profile does while **Snap** is on. **--texture-lock on|off** (on by
default), **--allow-valve220** and **--context GROUP** work as for `map rotate`.
Doom maps flip along X or Y and turn about Z only, so roll and pitch fail there.
A locked object or a usage error fails and writes nothing. JSON reports
`transform`, `commandId`, `frame`, `axis`, `degrees`, `mirror`, `pivotPolicy`,
`pivot` and the save fields. To change picked components, use
`map transform-components`.

**Status: Partial.** The axes, the CLI and the Levels commands share one core
service. See [Turn and flip from the view](levels.md#turn-and-flip-from-the-view).

## Flip and turn level objects on world axes

`map flip --axis x|y|z` and `map rotate --axis x|y|z --turns N` are the command
line form of the Levels page's **Flip X/Y/Z** and **Rotate X/Y/Z**. Rotate X and
Rotate Z are `--turns -1`, Rotate Y is `--turns 1`. **--pivot-grid** turns about
the selection's centre rounded to that grid, as quick turns do in the
NetRadiant Custom and TrenchBroom profiles, so a brush whose sides differ by an
odd number of grid steps stays on the grid:

```sh
vibestudio --cli map rotate room.map --object brush:12 --axis x --turns -1 --pivot-grid 16 --output turned.map --json
vibestudio --cli map flip room.map --object brush:12 --axis z --output flipped.map
```

**--pivot-grid** cannot be combined with **--pivot**. JSON adds the resolved
`pivot` and `pivotGrid` to the save fields. `editor controls <profile>` reports
the profile's `quickTurnPivot` (`grid` or `centre`) and `flipPivot` (`half-grid`
or `centre`), and `editor gestures <profile> --set editing.quickTurnSnapsPivot=on`
or `--set editing.flipSnapsPivot=on` changes them.

**Status: Partial.** The CLI and the Levels commands share one rotation and
mirror service. See [Edit Quake-family maps](levels.md#edit-quake-family-maps).

## Query connected level entities

`map select-connected <map> --object patch:2` adds one hop of incoming and
outgoing `target`/`targetname` connections to the supplied object selectors.
Repeat **--object** for more seeds; **--context** limits results to an open group
and repeated **--exclude** supplies view filters. JSON includes `selection`,
`originalSelection`, `added`, `connectionKeys`, `transitive: false`,
`sourceSha256`, `queryOnly: true` and `mapWritten: false`. No map is written.
See [Select connected entities](levels.md#select-connected-entities) for exact
matching, owner expansion, visibility, group and history behaviour.

## Query temporary level visibility

`map visibility <map> --mode inspect|hide|show-all|isolate` runs the shared session
visibility service without writing a map. Hide and Isolate require repeated
**--object** selectors. Repeated **--hidden** and **--shown** selectors supply
explicit session states; either accepts an object selector or **group:UUID**.
A target cannot be both hidden and shown. **--policy inherited** applies TB-style
container hiding and isolation; **--policy flat** targets existing
objects. **--context GROUP** supplies an open group. Show All accepts no
**--object** and resets both hidden and shown overrides.
Use **--map MAP02** to choose a map in a Doom/Hexen WAD; **--engine** supplies
the same optional engine hint as other map queries.

**--profile ID** selects a registered editor profile, defaulting to
**vibestudio-default**. TB defaults to inherited hiding; NRC and VibeStudio default
to flat hiding. Explicit **--policy** overrides that hiding choice. Insertion
behaviour still follows the profile: NRC copies start with fresh inherited
states, while TB/default copies retain explicit descendants and reveal roots.
JSON also reports `profile` and `insertionVisibility`.

JSON returns `hidden` and `shown` (explicit session selectors), `policy`,
`effectiveHidden` (object selectors including saved scene visibility and dependent
geometry), `selection`, `sourceSha256`, `changed`, `queryOnly: true` and
`mapWritten: false`. Feed `effectiveHidden` selectors to selection queries as
**--exclude**, or pass the remaining selection
to authoring commands. No CLI undo session is persisted. **--output**,
**--overwrite**, **--dry-run**, unknown modes and invalid selectors are rejected.
To inspect authoring effects without writing a map, use **--mode move --node
GROUP --parent NODE**, **--mode assign --object SELECTOR --parent NODE**, or
**--mode duplicate --object SELECTOR --offset x,y,z**. Assign and Duplicate
accept repeated object selectors. Omit **--parent** or use **default** for the
Default layer; Duplicate defaults to a zero offset. The same scene and insertion
services apply layer transitions, copied attributes, new-root visibility and
validation. Results also include the resulting `scene`; the source hash and
`mapWritten: false` still refer to the unchanged source. These modes preview an
operation in memory and do not persist an undo session.

See [Temporary visibility](levels.md#temporary-visibility) for profile behaviour
and remaining native parity limits.

## Query view filters

`map filters <map>` lists the view filters the map's format offers, as the
**Filters** section of the Levels **View** tab does: each kind's `id`, `title`,
`description`, `count`, whether it is `shown`, and the `command` that toggles it
in the studio (`map.filterWorld`). **World Brushes** counts worldspawn,
func_group and func_detail as the world, as Radiant does. Doom maps list their
thing categories (`things:0` and on, and `things:other`).

**--off world,clip** names the kinds to treat as switched off (repeat it or use
commas); a kind the map does not offer is refused. **--saved** starts from the
list the View tab saved in the settings store (choose another store with
**--settings-file**), leaving out kinds saved for other formats. **--invert**
then applies **Invert Filters**. **--objects** adds each kind's object
selectors. `filteredObjects` and `filteredCount` report what the views would
hide; `off` is the final list. Use **--map MAP02** for a map in a Doom or Hexen
WAD and **--engine** for the usual engine hint.

```text
vibestudio --cli map filters start.map --json
vibestudio --cli map filters start.map --off world --invert --objects
```

The query writes nothing: the map, `mapWritten: false`, and the saved filters
stay as they were. See [Look around the map](levels.md#look-around-the-map) for
what each kind names.

## Query a level camera focus

`map camera-focus <map>` reports where **View** > **Camera** would put the
Levels 3D camera, through the same service. **--object brush:12** (repeat it for
more) names what to focus on, as **Focus Camera on Selection** does: the camera
keeps **--camera-yaw** and **--camera-pitch** (degrees, 0 by default; yaw 0
looks along +X, pitch up is positive) and backs off along its view until the
objects' box fits a **--fov** vertical field of view (90 by default) at
**--aspect** width to height (`4:3` by default; `1.6`, `16:9` and `1920x1080`
also work), never nearer than 64 units to the box's centre. **--offset 8**
applies eight free-focus wheel notches, halving the distance; negative values
move farther. **--level** first levels the camera as **Level Camera** does: no
pitch and the heading rounded to the nearest 22.5 degrees. With **--level** and
no objects, the camera stays at **--camera-position x,y,z** (0,0,0 by default).
**--context** opens a group first; hidden objects are refused. Use **--map
MAP02** for a map in a Doom or Hexen WAD and **--engine** for the usual engine
hint.

```text
vibestudio --cli map camera-focus start.map --object brush:12 --camera-yaw 45 --camera-pitch -30 --fov 100 --aspect 16:9 --json
vibestudio --cli map camera-focus start.map --level --camera-yaw 50 --camera-pitch 20 --camera-position 0,0,64
```

JSON reports `camera` (`position`, `yaw`, `pitch`, `fieldOfView`, `aspect`),
`levelled`, and with objects a `focus` object: the `objects`, their box
(`mins`, `maxs`, `centre`), `fitDistance`, `distance` after the offset, `offset`
and `minimumDistance`. The query writes nothing (`mapWritten: false`). See
[Look around the map](levels.md#look-around-the-map) for the commands in the
studio.

## Select level objects by volume

`map select-region` supports **--policy bounds|trenchbroom|radiant**. Queries are
read-only; **--output** writes the profile's prepared result, including source
volume deletion. **--dry-run** validates that output without writing it.
JSON distinguishes new matches from the resulting selection and reports whether
a map was written. See [Select by brush volume](levels.md#select-by-brush-volume)
for consuming semantics, modes, definitions and examples.
