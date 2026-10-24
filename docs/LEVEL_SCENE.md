# Level Scene Organization

Levels has a Scene tab for named layers, nested groups, object assignment,
selection, inherited visibility, editing locks and linked groups. GUI and CLI share
the map document, undo history and native-map persistence. The broader editor acceptance
matrix remains in `LEVEL_EDITOR.md`.

[Repeat object commands](LEVEL_REPEAT.md) uses the same group context, locks,
linked correspondence and protected properties. Replay stages the whole sequence
as one undo step; internal linked updates never enter its recorded history.

## Authoring

Create a layer at the root, or a group under the chosen Parent. Select a tree
row to rename, move or remove it. Assign selection moves the selected object
references into that node; choose Default to remove their explicit membership.
Select members selects visible objects in that node and its descendants.
Expand Selection to Layers and Move to Layer (`core/level_scene_layers.h`) work
from the views: the first adds the visible, unlocked objects of the selected
objects' layers, the second moves the selection into a layer as one scene undo
step, closed groups and brush entities whole. An object's layer is its own
membership, else its non-worldspawn owner's, walked up through groups; root
groups belong to Default. Scene visibility uses the same resolver.
Counts show direct members. The row tooltip includes the persistent UUID for
automation. Names are plain text, with case-insensitive uniqueness among siblings.

The check box controls local visibility, inherited from ancestors. A hidden
entity also hides its brushes and patches. Individual primitives may have their
own memberships. Hiding a Doom sector hides its surfaces, edges owned only by
hidden sectors, and vertices used only by hidden edges. Shared boundaries remain
visible for an adjacent visible sector. A directly hidden vertex hides its marker,
not the geometry connected to it. Plan picking and camera triangles follow the
same scene state; indexed Doom records are never removed from the preview data.
Hiding a node clears its hidden selection; Undo restores both scene and selection.
Temporary Hide/Show commands remain separate from persisted layer visibility.

Lock editing protects the node and its descendants. The tree spells out Locked;
an inherited lock shows Locked by parent in the native check box. Locked content
remains selectable for inspection, sampling, copying and prefab capture. Unlock
its ancestor before editing. Names and visibility remain editable. Membership,
parent changes, removal and reset cannot move protected objects out of a lock;
unlocking and reorganizing are separate undoable operations.

Locks protect entity properties and owned brushes/patches. A separately locked
primitive also protects its non-worldspawn owner's properties, without locking
its siblings. Doom sector locks protect sector fields, boundary lines, sides and
vertices; line locks protect their endpoints and sides. Shared records therefore
block an edit even when its directly selected object is outside the locked node.
Adding a boundary to a locked sector is also refused. Unrelated Doom record
compaction remaps identities and remains available. Locks do not spread from a
shared boundary into every neighboring sector's fields.

Authoring services use an isolated copy-on-write transaction when locks exist.
They compare protected native records and membership before publishing the edit.
A refusal retains selection, geometry, revision, dirty state, undo/redo and caller
output values. Batch material changes, CSG, topology, transforms and component
commits use this same boundary; they cannot partially change an unlocked subset.
Undo and Redo replay recorded history, including lock changes.

Create in chooses the destination for new primitives, point entities and prefab
placements for this editing session. Partial duplicates, split lines, clipping,
hollowing and carving inherit their source membership. Complete closed groups
duplicate as new siblings, including their nested hierarchy. A brush merge uses its first source's
membership, collapsing any differing memberships with that geometry. Removing a
node moves its members and child groups to its parent; name conflicts are refused
atomically. Removing a node never removes level geometry. Doom topology edits
carry memberships through record renumbering. All changes support Undo and Redo.

A locked creation destination refuses new objects. Partial duplication keeps its
source membership and cannot add members to a locked node or primitives to a
locked entity. Complete group copies retain locks on their new nodes and leave
the protected source unchanged. Copy/paste into an unlocked destination is supported. Model and
sound placement, texture application and prefab insertion reach the normal map
services; a rejected handoff leaves package staging unchanged too. Locks protect
map records, not the pixels/audio/model bytes of a shared asset: restaging an
asset with an unchanged map reference remains available.

## Edit a group

Closed groups are selection units in plan views, the camera, the Objects list
and selection commands. Picking any member selects its outermost closed group,
including nested and hidden members, so a whole-group transform keeps the
assembly together. A band toggles each group once. Layers remain organisational
containers and do not become selection units.

**Open Group** enters the selected group; **Close Group** returns to its parent
and selects the group just closed. **Close All Groups** returns to the map.
In the TB profile these operations share selection history: Undo restores the
previous selection, open-group path and creation destination without modifying
map contents. Other profiles keep navigation outside history.
These commands are in **Select**, **Tools > Groups** and command search, with
named buttons in **Scene**. Double-click a group in a plan or camera to open it,
or empty/outside space to close it. Active drawing, clipping and component
gestures retain their own double-click handling. The Scene tree can open a
specific nested group directly. A breadcrumb and viewport label show the context.

Only the open group's contents can be picked or edited; nested groups remain
closed units until opened. New primitives, entities and prefab placements join
the open group. **Create in** is fixed until it closes, then restores the prior
session destination. Component handles and single-primitive geometry editors
require their containing group to be open. Context changes cancel pending
component/clip drafts and invalidate prepared plans. Opening and closing change
neither native bytes nor dirty state, and the open path is not saved. Removing
or undoing the creation of an open group falls back to a surviving ancestor.

The shared authoring transaction refuses changes to outside native records and
new objects created outside the context, retaining the entire original document
on refusal. Automatic linked-copy propagation may update siblings in the same
undo step; an explicit edit to an outside sibling remains blocked. Persistent
locks still apply. Scene administration (names, membership, parents, visibility
and locks) remains explicit and available while editing a group; Undo/Redo can
replay earlier map history. Builds, saves, dependency review and packages always
use the complete map, irrespective of the editing context.

Unassigned brush/patch geometry inherits its non-worldspawn entity's group.
Explicit primitive membership takes precedence; a transform of its owner is
refused if it would alter geometry outside an open context. Doom boundaries
inherit a sector's group only when all incident sectors agree, and vertices
inherit only when their incident lines agree. Shared outside endpoints remain
protected. Compound binary Doom/Hexen moves deduplicate endpoints across lines
and explicitly selected vertices. This does not introduce a new direct sector
move command. Explicit membership cannot bypass an outside sector or line's
shared boundary protection. UDMF global/extension blocks and outside custom
properties are protected too; mixed inside/outside property batches are atomic.

Complete closed groups retain their hierarchy through duplicate, arrays, native
map clipboard and prefab capture/placement. Duplicates stay beside the source
and stay in its link set; portable copies get fresh group and link set
identities in **Create in** or the open group. A portable lone linked group
becomes independent. A copied linked group's frame moves with its content. Empty descendants, ownership, visibility and locks survive,
with one atomic undo step and bounded metadata validation. Partial copies retain
source membership; layers and open ancestors are not cloned.

Grouped prefabs use version 2 and report group counts through the shared inspect
service. Flat version 1 files remain supported. Placing a prefab that holds
linked groups turns and moves their frames with its content, at any rotation.
Source prefab updates, standalone empty-group copy, automatic empty-group cleanup
and cross-editor metadata remain separate parity gates.

## Linked groups

Terminology follows TrenchBroom, whose feature this is: groups that share their
content are **linked groups**, and the groups sharing one link id form a **link
set**. **Create Linked Duplicate** makes a member (TrenchBroom's and Blender's
name for the act). None of the members is an original, so neither "copy" nor
"instance" is used for them; Hammer-style instances, which draw another map
file, are a different feature. The implementation is VibeStudio's own
(`core/level_linked_groups.*`).

**Create Linked Duplicate** (for the closed group the selection holds whole)
duplicates a group, with every group nested in it, beside itself along x by its
width rounded up to 16 units, as a sibling named after it ("Pillar 2"), links
the two and selects the new one. **Duplicate** on a linked group adds another
member to its set. An edit to the content of one member is made to every other,
each keeping its own place and frame, while moving, turning, mirroring, scaling
or shearing a whole closed member changes only that one.

A member's frame (`LevelSceneNode::linkFrame`) is the affine map, row by row
(3x4, translation last), that places the set's shared content where the member
has it, like TrenchBroom's group transformation. A new duplicate's frame is the
source's moved by the offset; copies and pastes move it with their content.

Every map edit passes through the document's undo stack, and
`syncLevelLinkedGroups` runs after each one there, so studio and CLI edits behave
alike. Each linked node keeps, in memory only, the centre of its content's
bounds and three digests of its content relative to that centre: where things
are (points, planes, origins, keys, texture names and flags, which nested group
holds them, and nested groups' frames), that with texture scales, turns and
axes, and the texture offsets. After an edit each member is unchanged,
transformed whole or changed:

- A transform records the one affine map it applied to every record it moved
  (`LevelMapUndoCommand::hasAffine`, `affine`, `affineObjects`; per-record
  snapping records none). A closed member, not on the open editing path, whose
  every record that map moved was transformed whole: its frame becomes the map
  after its frame. This is exact for moves, rotations at any angle about any
  axis, mirrors, resizes and shears, with texture lock on or off.
- A closed member whose shape is unchanged but whose centre moved, by an edit
  that recorded no map, was moved whole; its frame moves with it.
- Any other change to a member's content is a change to the shared content,
  including every edit inside an open member, even one that moves all of it (as
  in TrenchBroom, where only transforming the closed group changes its frame).
- The one changed member's content then replaces the content of the other
  unlocked members: their content, nested groups included, is deleted and
  replaced with its content, duplicated and placed by their frame after the
  inverse of its frame (`transformLevelMapSelectionAffine`), with texture lock,
  retried without it where a classic projection cannot express the frame.
- Members changed the same way by one edit (a map-wide texture replacement, say)
  agree when their corresponding objects, by identity, match in the shared
  content within a hundredth of a unit, keys either entity protects aside; the
  rest of the set is then updated from one of them. Members changed in different ways are left as they are and
  marked out of sync (`linkOutOfSync`), and the edit says so in
  `LevelMapDocument::linkedGroupNotices`, which the studio shows and the CLI
  prints. TrenchBroom refuses such an edit instead.
- A locked member, or one holding a locked group, is passed by and marked out of
  sync. Any later update from another member clears the mark; saving lists the
  members still out of sync as warnings.
- A member emptied of everything leaves its link instead of emptying the others;
  a link left with one member is no link.

Groups nest in linked groups, as in TrenchBroom. A linked duplicate copies the
groups nested in the source and links each with its copy (a nested group not yet
linked gets a link first); each nested copy's frame is the outer map after the
nested frame, and the target's nested groups keep their names, visibility and
locks by link. Link sets are updated outermost first, so an edit inside a nested
group reaches the copies of its outer group with them. A nested set skips the
groups an outer update has just made, except that each such copy is updated
whenever the group it copies is (`Update::copiedFrom`), so two members of one
set inside one outer member both reach the outer member's counterparts. A group
cannot hold a member of its own link set at any depth.

The propagation is folded into the edit's own undo step as follow-up commands
(`LevelMapUndoCommand::followers`, `foldLevelMapFollowUpEdits`), so the step keeps
the edit's description and details and one Undo restores every member, frames
and out-of-sync marks included. Members are rebuilt, so their records get new
identities. Only Quake-family maps link groups.

**Separate Linked Groups** takes the chosen members out of their sets, keeping
their content: several chosen from one set get a new link of their own and stay
linked with each other, one alone is unlinked, and so is a member left on its
own. Nested groups keep their links. **Select Linked Groups** selects every
member of the sets the selection reaches. **Update Linked Groups** copies a
chosen member over the rest of its set. The 2D views draw the boxes of the sets
in use, the selected or open member solid and the others dashed, with names and
arrows from the selected member to the others; out-of-sync members are
dash-dotted in the warning colour. **Show > Linked Groups** draws every linked
group, and the map views' right-click menu has a **Groups** submenu with these
commands.

### Protected entity properties

`level_protected_properties.*` stores instance-local protected keys on
`LevelSceneState::protectedProperties`, keyed by native entity selector. Values
remain native entity properties, including the distinction between empty and
absent. Keys use the editor's case-insensitive matching and are stored trimmed,
case-folded, sorted and unique. Only non-worldspawn Quake-family entities qualify.
Protection survives unlinking, regrouping, deletion/undo, duplication, map
clipboard and prefab transfer, including entities without any scene nodes.

When replacing a linked group's content, correspondence identifies each target entity
before deletion. After the clone's spatial transform, keys protected by either
source or target regain the target's previous value or absence. The target keeps
its own protection flags; newly created entities inherit the source's flags.
Unprotecting restores the first existing unprotected counterpart's value, if any.
Clearing applies that rule to all protected keys of selected linked entities;
unlinked entities retain their flags. All-protected or absent counterparts supply
no restoration value. Protection changes alone do not propagate flags.

The shared service prepares metadata and values in an isolated document, retains
intermediate history until it can fold one undo step, and uses the ordinary lock,
open-group and linked-update boundaries. Legacy families must first establish
complete correspondence with an actual update; ambiguous matches are refused.
The inspector provides Local/Shared/Mixed check boxes, missing-key rows and
Add/Clear actions. Modal publication uses the shared stale-context guard. These
bounded workflows are Partial; nested links, arbitrary instance
transforms and wider conflict semantics remain separate parity gates.

### Extract linked contents

`level_linked_extraction.*` partitions a proper subset of an open linked group
across its complete link set. Persistent object UUIDs identify each selected
counterpart. Each instance is cloned independently and both halves are pruned;
partial brush-entity selections retain a local owner on both sides. The original
family keeps its identity, while the extracted family receives a fresh link UUID.
Logical object UUIDs are retained within each separate family. Geometry, UVs,
per-instance properties, protected absence, frame, parent and visibility survive.

A closed group nested in the open one is a selection unit and is taken whole.
Each instance's counterpart is the child group with the same link UUID whose
content has the same object UUIDs; it is reparented, not copied, into that
instance's extracted group, so its subtree keeps node UUIDs, links, frames,
names, visibility, native records and every nested scope's
correspondence. The extracted scope maps moved and copied objects to the
source scope's object UUIDs. When the open group is nested in linked groups,
those groups' scopes know each copy by its original's UUID, and a split
owner's new half by one fresh UUID per link set and original, so every member
of an enclosing set gains the same structure under the same identities.

The shared service validates correspondence, ownership, all affected locks
(moving nested subtrees included), node/metadata limits, a non-empty remainder
and that each enclosing linked group holds as many instances as its set's other
members before publication. Disjoint explicit geometry membership outside its
owner group is refused, as is part of a closed nested group. Intermediate
automatic propagation is suppressed, baselines are refreshed, and an isolated
candidate publishes one undo transaction. That transaction restores selection
and editing context on undo/redo; ordinary group navigation remains undo-free.
Success closes only the source group and selects its new counterpart in the
parent context. Redundant child selections are subsumed by a selected owner.
Baseline refresh covers the two halves and the enclosing linked groups whose
baselines matched their content beforehand, retaining unrelated families' and
pending enclosing edits when a caller defers propagation for a compound edit.

GUI preparation uses the cancellable placement worker and its stale-context
guard. CLI `extract-linked-groups` uses the same core service and normal save safeguards.
This is Partial parity: dense real-project acceptance remains outstanding.

Scene visibility does not change compilation, dependency inspection, native saves,
recovery, or package publication. Hidden placed models retain their asset paths and
remain compiler inputs. WAD map grouping recognizes `VS_SCENE` as a map sidecar so
map subsets, moves and deletion retain the correct ownership boundary.

## CLI

`editor scene <operation> <map>` reports structured `scene` JSON with `--json`.
WAD inputs require `--map-name`. Mutations require `--output`; `--dry-run` validates
without writing, and replacing an existing file requires `--overwrite`. The usual
source conflict checks and independent backups apply.

| Operation | Options |
| --- | --- |
| `list` | No mutation options |
| `select` | `--objects <selectors> [--context <group UUID>] [--selection <selectors>] [--mode replace\|add\|toggle] [--expand-layers]`; read-only group-aware selection, optionally expanded to whole layers |
| `translate` | `--objects <selectors> --offset x,y,z [--context <group UUID>]`; move selection units with texture lock |
| `create` | `--kind layer\|group --name <name> [--parent <UUID\|default>]` |
| `rename` | `--id <UUID> --name <name>` |
| `move` | `--id <UUID> --parent <UUID\|default>` |
| `assign` | `--id <UUID\|default> --objects brush:0,entity:3` |
| `move-to-layer` | `--id <layer UUID\|default> --objects <selectors>`; closed groups move whole and a brush takes its brush entity along |
| `visibility` | `--id <UUID> --visible true\|false` |
| `lock` | `--id <UUID> --locked true\|false` |
| `remove` | `--id <UUID>` |
| `reset` | Discard all scene organisation and protected-key flags, retaining geometry and entity values |
| `save` | No mutation options: write the map again, for example with another `--scene-format` |
| `create-linked-duplicate` | `--id <UUID> [--offset x,y,z]`: duplicate the group and link the two, by default beside it along x; reports `createdId` |
| `select-linked-groups` | `--id <UUID>`: read-only; reports the `selection` of every member of the group's link set and `linkedGroups` |
| `update-linked-groups` | `--id <UUID>`: make the other unlocked members of the group's link set match it; reports `updated` |
| `separate-linked-groups` | `--id <UUID,UUID...>`: take the groups out of their link sets; several from one set stay linked with each other; reports `separated` |
| `extract-linked-groups` | `--context <group UUID> --objects <selectors>`; move selected contents of the open linked group, and their counterparts, into a new link set |
| `protect-properties` | `--objects entity:N,... --keys a,b --protected true\|false [--context <group UUID>]` |
| `set-protected-property` | `--objects entity:N,... --key k --value v [--context <group UUID>]`; `--value=` preserves an explicitly empty value |
| `clear-protected-properties` | `--objects entity:N,... [--context <group UUID>]`; clear selected linked entities' flags and restore available shared values |

Every writing operation also takes `--scene-format vibestudio|trenchbroom`, the
scene storage to save with (see [TrenchBroom map keys](#trenchbroom-map-keys));
without it a map keeps the storage it opened with, and the report gives the one
written as `sceneStorage`. WADs refuse `trenchbroom`.
Creation and `create-linked-duplicate` return `createdId`. The earlier names
`link`, `update-links`, `unlink` and `extract-linked` remain accepted. Ordinary
`map` editing commands keep linked groups in step exactly as the studio does, and
any edit whose linked updates could not be made reports `linkedGroupNotices`. Doom
`entity:<id>` selectors canonicalize to
`thing:<id>`; `worldspawn` itself cannot be assigned. CLI operations reject unknown,
repeated, irrelevant or missing options before writing. `select` returns
`selection` and `openGroups`; `--context default` or omission means map context.
Outside hits are ignored by `select`, while any outside hit refuses the entire
`translate` request. Offsets are finite, bounded to ±32768 per axis and must
produce a nonzero move. The context is per invocation and never serialized.
Other low-level `map` commands retain their explicit primitive targets; use
`editor scene translate` for the group-aware move path.
Node JSON includes local `locked` and inherited `effectiveLocked`; text listings
spell out Locked or Editable. Ordinary `map` editing commands enforce the same
locks and do not write an output when the edit is refused.
The scene JSON report is version 6 and includes each node's `link`, `frame`
(12 numbers, a 3x4 affine map row by row), `outOfSync` and `linkObjects`
mapping from native selectors to persistent object UUIDs, plus the root
`protectedProperties` mapping from entity selectors to protected-key arrays.
The root's `storage` is `vibestudio` or `trenchbroom`, and a node read from
TrenchBroom's keys lists the ones VibeStudio keeps without editing as
`trenchBroom`.
Text listings add each linked group's frame in words and **Out of sync**.

## Storage

Version 2 stores bounded base64 JSON with `version`, `sha256` and `nodes`. Each node
contains `id`, `name`, `parent`, `kind`, `visible`, `locked` and `objects`. Version 3
added `link` (the link id or an empty string), `turn` (0 to 3 quarter turns) and
`mirror` to every node. Version 4 additionally stores `linkObjects`, an object
mapping canonical selectors to object UUIDs scoped to the group's `link`.
Corresponding entities, brushes and patches share a UUID across instances,
including primitives owned implicitly by member entities. New objects receive
fresh identities; deletion, replacement, undo and serializer renumbering retain
only the appropriate correspondence. A complete group duplicate retains these
identities. Portable families receive a fresh link scope; portable singletons
lose both the link and object correspondence.

Version 5 added a root `protectedProperties` object. Its nonempty arrays name
existing non-worldspawn entities and contain canonical protected keys, bounded
to 1,024 characters per key and 100,000 total keys. Selectors are remapped during
native serialization. Malformed, stale or future metadata remains opaque rather
than attaching flags to the wrong entity.

Version 6, written whenever a scene has links or protection, replaces `turn` and
`mirror` with `frame`, twelve finite numbers giving each node's affine frame row
by row (3x4, translation last), and adds `outOfSync`; the root always has
`protectedProperties`. Nodes keep `linkObjects`, empty for legacy links whose
correspondence is unknown until a real copy/update establishes it; the reader
never guesses legacy matches by order or geometry. A frame must not flatten its
content, and a group cannot hold a member of its own link set at any depth.
Versions 3 to 5 are read with each linked group's quarter turns and mirror as
the linear part of its frame and the centre of its content's bounds as its
translation, which places content exactly as those versions did. Older builds
must understand version 6 to edit such a scene. Scenes without links or
protection are still written as version 2. Anchors and digests are not saved:
they are worked out again when the map opens. Version 7 adds a
`trenchBroom` object to every node, the keys of the TrenchBroom layer or group
entity it was read from that VibeStudio does not edit (such as `_tb_id`,
`_tb_layer_color` and `_tb_layer_omit_from_export`), and a root `trenchBroom`
boolean: true when the map body carries TrenchBroom's entities and objects are
numbered as the map reads once they are taken out. It is written for TrenchBroom
storage and whenever a node keeps such keys. Version 1
is read with every node unlocked. UUIDs persist across
saves. Bounds are 1,000 nodes, 100,000 explicit members, 32 nested nodes, 128 UTF-16
code units per name, and a 4 MiB encoded payload. Unrepresentable metadata refuses
serialization rather than dropping membership.
Object identities have a separate 100,000-entry bound. They must be canonical,
unique within a group, refer to that group's content (nested groups included) and
agree on native kind across linked groups. Invalid or future metadata remains opaque. This native
correspondence supports protected properties and linked extraction. The extraction
core/GUI fixtures cover partial owners, local properties, exact geometry/UVs,
nested groups two deep, extraction inside linked groups with later edits and
identical simultaneous edits, independent subsequent edits, prefab transfer,
one-slot undo/context, cancellation, stale selection and atomic refusals. The optional
`src/tests/level_linked_extraction_workflow.py` compares compiled geometry, texture
coordinates, normals and entity values before/after extraction across classic,
Valve 220 and brush-primitive maps, then validates packaged source and BSP output.
Older VibeStudio builds that do not understand version 4 preserve it as opaque
metadata; use a version-4-aware build to edit that scene's organisation.

Explicit linked creation and update publish their prepared document only after
the geometry and metadata succeed. Refusals retain native records, selection,
revision, history and caller output. Their intermediate commands are retained
until folded, including when the configured undo limit is one.

Text maps append `\n// VibeStudioScene: <base64>\n` after the native body. Removing
that exact trailer preserves source line numbers. The fingerprint is SHA-256 of
the native bytes before the trailer. Serializer emission events translate sparse
editing IDs to the actual written parser order without parsing or solving the map
again. Existing files without scene metadata retain their ordinary output. With
TrenchBroom storage the body holds TrenchBroom's entities as well, described
below.

### TrenchBroom map keys

`core/level_scene_trenchbroom.*` reads and writes the keys TrenchBroom keeps its
layers, groups and linked groups in, implemented independently from its manual
("Linked Groups in the Map File") and the maps it writes:

- A layer or group is a `func_group` entity with `_tb_type` `_tb_layer` or
  `_tb_group`, a number `_tb_id` and a `_tb_name`, holding its brushes and
  patches. Layers add `_tb_layer_sort_index` and, when set, `_tb_layer_hidden`
  and `_tb_layer_locked` "1". A group names the layer or group holding it with
  `_tb_layer` or `_tb_group`; so do member entities. A linked group adds
  `_tb_linked_group_id`, its link set's UUID in braces, and
  `_tb_transformation`, sixteen numbers giving a 4x4 matrix row by row whose
  top three rows are its frame. An entity's protected keys are
  `_tb_protected_properties`, separated by semicolons (a backslash escapes a
  semicolon or backslash).
- Opening such a map parses it, takes the layer and group entities and their
  heading comments out of the text, moves their brushes and patches to the end
  of worldspawn in file order, takes the member keys off their lines, and
  parses the result as the document; issue lines are mapped back to the file.
  Layers come first in their sort order, then groups in file order. Node ids
  are name-based UUIDs of the `_tb_id` (or the node's place when it has none),
  so every open gives the same ones; link ids are made canonical (a link id that
  is not a UUID becomes a name-based one); sibling names are numbered, with
  TrenchBroom's name kept to write back. A missing or unusable transformation
  is taken as unmoved and the group marked out of sync. A group nested in its
  own link set is unlinked. Corresponding objects are matched as Update Linked
  Groups leaves them: an object's place in its group (nested group, entity,
  brush or patch, by order) names its identity in the set, which TrenchBroom's
  alike-in-structure linked groups give. Members that do not agree with the rest
  of their set in their frames (protected keys aside) are marked out of sync,
  the largest agreeing part, or the first, kept in sync. Missing or duplicated
  ids, dangling references and nesting cycles are reported as
  `trenchbroom-groups` issues and the object stays in Default or at the top.
- When the text cannot be taken apart (a layer or group entity, or one of its
  brushes, sharing a line with other text, a key's value on another line, or no
  worldspawn whose closing brace stands alone), the map opens as it is, with
  TrenchBroom's entities as ordinary entities, as earlier versions opened it.
- Precedence: a version 7 note with `trenchBroom` true that still matches the
  body wins, because it holds what TrenchBroom does not keep (group visibility
  and locks, out-of-sync marks, object identities). A matching note without it,
  written before VibeStudio read TrenchBroom's keys, binds to the map as it is,
  and the map opens as it is. Otherwise TrenchBroom's keys are read, and a note
  that no longer matches is set aside with an issue rather than preserved.
- A document opened from TrenchBroom's keys, or saved with
  `LevelDocumentSaveRequest::sceneStorage` set to TrenchBroom (Save As's
  **Level map with TrenchBroom groups (*.map)**, or `--scene-format
  trenchbroom`), saves with TrenchBroom storage: the native text is written and
  parsed again, worldspawn's brushes and patches in each layer or group move
  into a new `func_group` entity after worldspawn (layers in scene order, then
  groups, parents first), member entities get their keys after their last key,
  and the note follows with objects numbered as reopening the result gives
  them. `_tb_id` numbers are kept while free. Scenes with unrecognised metadata
  are written without TrenchBroom's entities, with a warning.

Not kept for TrenchBroom: group visibility and locks (only the note keeps them),
an explicit layer or group of a brush entity's own brush (it follows its
entity), `_tb_transformation` of a group that is not linked, and the "// brush
N" comments of moved brushes. Clipboard and prefab text with TrenchBroom
entities is not converted.

Binary Doom/Hexen maps put `VS_SCENE` after the contiguous native map records. Its
fingerprint hashes THINGS, LINEDEFS, SIDEDEFS, VERTEXES, SECTORS, BEHAVIOR and TEXTMAP
in that order, each framed by `name:length:`. Regenerated node lumps do not affect
membership. Unrelated archive records are preserved; duplicate scene records make
saving ambiguous and are refused. Parsed UDMF maps support layers, groups and
locks; VS_SCENE is placed before ENDMAP and binds to the exact TEXTMAP bytes.
Property edits retain object indices and refuse changes to protected objects,
including extension properties and dependent sidedefs. Global and unknown-block
edits are refused while any scene content is locked, because their effects are
namespace-specific.
Native UDMF transforms retain the same object identities and protect moved
vertices and mirrored linedef endpoints through this shared guard. Unlocked
things remain editable beside locked geometry. Lossless source adoption records
history against the active guarded document, including cancellation rollback.

Malformed, oversized, unknown-schema, empty or source-mismatched metadata is kept
as opaque data, with a visible diagnostic and no applied memberships. Reset scene
organization deliberately discards it and is undoable. Ordinary geometry edits
continue to preserve the opaque carrier. External editing that changes the bound
native body requires review/reset; there is no speculative reassignment by index.

## Required behavior

- Named layers and nested groups share the map document, GUI and CLI services.
  Objects belong to one explicit node or the implicit Default layer. Layers
  are roots; groups may nest in layers or groups. Moving a node must reject
  cycles and preserve object membership.
- Group selection and layer visibility must use the same object identities as
  the inspector, orthographic panes, camera, prefabs and model placement.
  Visibility is editor state: it never deletes objects from compiler/package
  output. Shared authoring services enforce locks before publishing edits,
  including indirect ownership and shared-geometry effects.
- Scene changes are atomic, undoable map edits. Structural edits must preserve
  or deliberately transfer membership, including clone/replacement operations,
owner changes and Doom record renumbering. Undo and redo restore membership.
- Save As, recovery, package snapshots and CLI output carry scene metadata with
  the map. Existing maps without metadata retain their established output.
- Quake-family metadata is an EOF comment. Binary Doom/Hexen metadata occupies
  a reserved, versioned map-local lump after ordinary map records. Unknown or
  stale metadata is retained without applying potentially incorrect memberships.
- Serialized membership uses the actual emitted object order, bound to a source
  fingerprint. A geometry-source mismatch must be visible and must never silently
  retarget membership. Nodes have persistent UUIDs independent of object indices.
  Native compiler output continues to contain the complete level.
- Metadata has explicit size, node, member and nesting bounds. Invalid names,
  duplicate identities, missing parents, cycles and conflicting memberships fail
  before changing a document. User strings remain plain text and localizable UI
  messages use the normal extraction pipeline.

## Verification gates

Core fixtures must cover nesting, visibility, locks, source binding, malformed metadata,
ordinal remapping across serialization, structural edits and exact undo/redo.
Actual GUI tests must exercise the scene controls and selection/visibility in all
views, with 100%/200% text, high contrast, expanded labels and RTL. CLI fixtures
must round-trip the same state. Compiler/package fixtures must prove that hidden
editor content remains in native output and that unrelated WAD data is retained.
No game launch, mouse/keyboard injection or OS capture is authorized by this plan.

## Implementation status

Production services, native persistence, GUI/CLI controls, structural-edit
reconciliation, shared preview filtering and linked groups are implemented. `level-scene-smoke`,
`level-scene-ui-smoke`, `level-scene-locks-smoke`, `level-scene-cli-smoke` and
`level-linked-groups-smoke` cover these paths, including
recovery, deterministic PK3 publication and WAD map-group expansion. UI tests use
semantic Qt controls and `QWidget::render` at 100% and 200% text with high contrast,
expanded labels and RTL. They inspect native accessible tree metadata and focus
policies; they do not replace OS screen-reader or physical keyboard acceptance.

`src/tests/level_scene_workflow.py` generates an original room and model, hides and locks
them through the actual CLI, compares baseline and organized map geometry after
a VibeMap3 BSP compile, runs VIS/LIGHT and publishes a validated package. Supply `--binary`,
`--compiler` and `--output-root` under `.agents/tmp`. No game is launched.

`level_scene_locks_benchmark` reports five unrelated-edit timings with no locks,
one protected object and dense protection, at 1,000/10,000 synthetic brushes and
1,000/5,000 detached Doom sectors. It measures the service transaction, not full
GUI interaction or native production maps; timings are evidence, not CI limits.
On Windows with clang-cl 20.1.7, Qt 6.10.1 and debug `/Od`, the local five-run
means were 38 ms for an unrelated move among 10,000 brushes with 9,999 locked,
and 119 ms among 5,000 sectors with 4,999 locked. With one protected object the
same cases averaged 5 ms and 23 ms. Typed identity sets and shared-array semantic
comparisons avoid serializing every unchanged record. Dense Doom protection
still needs production profiling before claiming interactive performance.

`level-group-context-smoke` and `level-group-context-ui-smoke` cover nested
navigation, unit selection, outside-edit refusal, creation/prefab placement,
owner/shared-boundary protection, linked propagation, pending-worker cancellation
and exact undo. The core test also reports an original 10,000-brush group's
index/selection/toggle time; this is not whole-editor performance acceptance.
Pass `--group-context` to the compiler workflow to compare nested group moves
against independently authored geometry before BSP/VIS/LIGHT and packaging.

`level-trenchbroom-groups-smoke` opens a hand-written TrenchBroom-style map,
propagates an edit through its frames, saves it with TrenchBroom storage and
checks that the note and the keys alone reopen to the same scene and bytes,
with out-of-sync, stale-note, untangleable-layout and CLI parity cases.

Remaining gates: wider hierarchy/transfer acceptance; round trips through maps
TrenchBroom itself saved (the fixtures are hand-written from its documented
format); large production-scene latency/memory measurements; native
accessibility and macOS/Linux execution.
The current UI rebuilds at most 1,000 node rows and stores explicit memberships in
undo snapshots. Large metadata may therefore require further memory work.

### Temporary group visibility

Session hiding stores canonical object selectors and `group:<UUID>` container
identities separately from saved scene visibility. `shownObjects` contains
explicit exceptions; absence from both sets inherits from group/owner parents.
The TB profile targets closed groups, while NRC/default target current members.
Hiding a parent resets shown descendants; independently hidden descendants stay
hidden. New authoring follows the document's session insertion policy inside the
same content transaction. Reparenting existing members retains inheritance.
Structural history remaps both sets and retains removed identities on either side
of undo/redo, including empty groups and reused Doom record indices. These states
never enter native map bytes or scene metadata; saved visibility remains a mask.

The same effective resolver feeds plan/camera rendering and selection queries.
Cache keys include both explicit sets. Hidden entity labels do not suppress an
explicitly shown child. Saved visibility interactions and native empty-container controls remain open
parity work; layer changes and duplication are covered below.

Ordinary viewport selection records a selection change without altering
temporary visibility. Closed-group membership can therefore include hidden
descendants without revealing them. The Objects list explicitly requests reveal
when activating rows; TB records that selection and reveal in one history step.

Layer-transfer history now records which existing identities change containing
layer, including group descendants and owner-inherited primitives. Shown resets
to Inherited; Hidden is retained. Undo restores only the affected shown states,
without overwriting later transient hiding. A redo captures the current states,
so a later Show All does not resurrect discarded overrides. Selection is tested
after the reset and restored with the scene transaction.

Duplicate preparation copies explicit attributes through native record origins
and the scene-copy UUID map. Arrays retain each copy's initial attributes;
Doom/Hexen/UDMF use the same integration. New root identities are ensured visible,
while hidden descendants retain their flags. Linked-copy creation carries the
root's state; linked propagation explicitly requests fresh inherited children,
as in TB's linked replacement path. Portable scene metadata remains unchanged.

`LevelMapDocument::insertionVisibility` chooses these preserved attributes and
revealed roots by default. NRC uses `FreshInherited`: existing identities still
remap, while added objects/groups neither copy explicit attributes nor receive a
shown exception. Drawing, paste, prefab insertion, duplicate, arrays and repeat
share that reconciliation. The shell supplies the current profile on document
load/adoption and uses the session revision to invalidate pending work when the
policy changes. A profile switch does not dirty contents, clear redo or rewrite
stored authoring history. This setting is absent from portable metadata.
