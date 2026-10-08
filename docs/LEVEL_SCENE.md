# Level Scene Organization

Levels has a Scene tab for named layers, nested groups, object assignment,
selection, inherited visibility, editing locks and linked copies of groups. GUI and CLI share
the map document, undo history and native-map persistence. The broader editor acceptance
matrix remains in `LEVEL_EDITOR.md`.

## Authoring

Create a layer at the root, or a group under the chosen Parent. Select a tree
row to rename, move or remove it. Assign selection moves the selected object
references into that node; choose Default to remove their explicit membership.
Select members selects visible objects in that node and its descendants.
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
placements for this editing session. Duplicates, split lines, clipping, hollowing
and carving inherit their source membership. A brush merge uses its first source's
membership, collapsing any differing memberships with that geometry. Removing a
node moves its members and child groups to its parent; name conflicts are refused
atomically. Removing a node never removes level geometry. Doom topology edits
carry memberships through record renumbering. All changes support Undo and Redo.

A locked creation destination refuses new objects. Duplicate keeps its source
membership and cannot add members to a locked node or primitives to a locked
entity. Copying and pasting into an unlocked destination is supported. Model and
sound placement, texture application and prefab insertion reach the normal map
services; a rejected handoff leaves package staging unchanged too. Locks protect
map records, not the pixels/audio/model bytes of a shared asset: restaging an
asset with an unchanged map reference remains available.

Prefab templates currently carry geometry and asset references, not their source
scene hierarchy. A placement joins Create in.

## Linked groups

Linked copy (**Create Linked Copy** for the selection's group) copies a group
beside itself, along x by its width rounded up to 16 units, as a sibling named
after it ("Pillar 2"), and links the two: the groups share a link id. An edit to
the content of one copy is then made to every copy, each keeping its own place
and frame, while moving, turning or mirroring a whole copy changes only that
copy. A copy's frame is a number of quarter turns about the vertical after an
optional mirror across x (`linkTurn`, `linkMirror`). This follows TrenchBroom's linked
groups; the implementation is VibeStudio's own (`core/level_linked_groups.*`).

Every map edit passes through the document's undo stack, and
`syncLevelLinkedGroups` runs after each one there, so studio and CLI edits behave
alike. Each linked node keeps, in memory only, the centre and size of its
content's bounds and three digests of its content relative to that centre:
where things are (points, planes, origins, keys, texture names and flags), that
with texture scales, turns and axes, and the texture offsets. The centre moves,
turns and mirrors with the content. After an edit:

- A copy whose shape is unchanged but whose centre moved was moved whole; only
  its centre follows. Texture lock may change its offsets as it moves.
- A copy whose content, turned back about its centre by one of the seven other
  quarter-turn and mirror frames, is where its things were was turned or
  mirrored whole: only its frame changes. Checking where things are, not
  texture turns, recognises it with texture lock on or off.
- The one copy whose content changed is copied over the other unlocked copies
  of its link: their content is deleted and replaced with its content, placed
  by the difference between their centres and its centre before the edit and
  turned from its frame into theirs, with texture lock. The copies join their
  groups.
- Copies changed in different ways by one edit are left as they are; **Update
  Linked Copies** makes the others match a chosen one.
- A copy emptied of everything leaves its link instead of emptying the others;
  a link left with one copy is no link.

The propagation is folded into the edit's own undo step as follow-up commands
(`LevelMapUndoCommand::followers`, `foldLevelMapFollowUpEdits`), so the step keeps
the edit's description and details and one Undo restores every copy. Copies are
rebuilt, so their records get new identities. Turns other than quarter turns
about the vertical, and turns about other axes, reach every copy.
A linked group holds brushes, patches and entities, not other layers or groups;
only Quake-family maps link groups. A locked copy is left as it is and catches
up with the next edit after it is unlocked. **Separate Linked Copy** unlinks a
copy, keeping its content; **Select Linked Copies** selects every copy.

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
| `create` | `--kind layer\|group --name <name> [--parent <UUID\|default>]` |
| `rename` | `--id <UUID> --name <name>` |
| `move` | `--id <UUID> --parent <UUID\|default>` |
| `assign` | `--id <UUID\|default> --objects brush:0,entity:3` |
| `visibility` | `--id <UUID> --visible true\|false` |
| `lock` | `--id <UUID> --locked true\|false` |
| `remove` | `--id <UUID>` |
| `reset` | Discard all scene organization, retaining geometry |
| `link` | `--id <UUID> [--offset x,y,z]`: a linked copy of the group, by default beside it along x |
| `update-links` | `--id <UUID>`: make the group's other unlocked copies match it |
| `unlink` | `--id <UUID>`: separate the group from its copies |

Creation and `link` return `createdId`; `update-links` reports `updated`. Ordinary
`map` editing commands keep linked copies in step exactly as the studio does. Doom
`entity:<id>` selectors canonicalize to
`thing:<id>`; `worldspawn` itself cannot be assigned. CLI operations reject unknown,
repeated, irrelevant or missing options before writing. Group selection in the GUI
is explicit through Select members; ordinary object editing does not automatically
expand to every member of its group.
Node JSON includes local `locked` and inherited `effectiveLocked`; text listings
spell out Locked or Editable. Ordinary `map` editing commands enforce the same
locks and do not write an output when the edit is refused.

## Storage

Version 2 stores bounded base64 JSON with `version`, `sha256` and `nodes`. Each node
contains `id`, `name`, `parent`, `kind`, `visible`, `locked` and `objects`. Version 3
adds `link` (the link id or an empty string), `turn` (0 to 3 quarter turns) and
`mirror` to every node, and is written only when a group is linked; a scene
without links is still written as version 2. Anchors and
digests are not saved: they are worked out again when the map opens. Version 1
is read with every node unlocked. UUIDs persist across
saves. Bounds are 1,000 nodes, 100,000 explicit members, 32 nested nodes, 128 UTF-16
code units per name, and a 4 MiB encoded payload. Unrepresentable metadata refuses
serialization rather than dropping membership.

Text maps append `\n// VibeStudioScene: <base64>\n` after the native body. Removing
that exact trailer preserves source line numbers. The fingerprint is SHA-256 of
the native bytes before the trailer. Serializer emission events translate sparse
editing IDs to the actual written parser order without parsing or solving the map
again. Existing files without scene metadata retain their ordinary output.

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
q3map2, runs VIS/LIGHT and publishes a validated package. Supply `--binary`,
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

Remaining gates: optional group-aware ordinary picking; linked copies turned by other than
quarter turns about the vertical; large production-scene latency/memory measurements;
native accessibility and macOS/Linux execution.
The current UI rebuilds at most 1,000 node rows and stores explicit memberships in
undo snapshots. Large metadata may therefore require further memory work.
