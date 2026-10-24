# Repeat object commands

**Status: Partial.** VibeStudio remains pre-alpha. This implements a bounded
TrenchBroom-style repeat workflow; it does not establish complete editor parity
or native platform acceptance.

**Repeat Last Commands** replays the session's object moves, rotations, resizing,
shears, mirrors and ordinary or unique duplication on the current object selection. The
commands appear in **Edit > Repeat**, **Tools > Transform** and the command palette.
The TrenchBroom profile uses **Ctrl+R** and **Ctrl+Shift+R** for repeat and clear;
its compiler command uses **Ctrl+Alt+B**, reserving **Ctrl+B** for Select Siblings.
Other profiles have no new default keys,
and user overrides take precedence.

## Recording and replay

Consecutive repeatable edits form a sequence. A selection change leaves that
sequence available for replay, but the next new repeatable edit starts a new
sequence. Duplication's own selection change belongs to the sequence. Replaying
does not record itself. **Clear Repeatable Commands**, Undo and Redo forget the
sequence. Clear changes neither map contents nor undo history. Save and Save As
retain it; loading or reopening a document starts a new session.

Transforms retain their original world pivots, scale origins and shear/mirror
planes. Picking another object does not recalculate those values about its centre.
Replay uses the current **Texture Lock** and explicit Valve 220 conversion policy.
Component editing, surface edits, clipping, CSG, snapping, prefab insertion and
arrays with multiple copies are not recorded as repeatable commands. Existing
repeatable commands remain available after those operations. Repeat targets whole
objects even when component mode is active, and returns to object mode on success.

The shared service prepares a copy of the document and publishes the entire
sequence as **one undo step**. Failure, cancellation, locked targets or a stale
GUI result publish nothing. The placement guard includes transient repeat state,
so Clear cannot be undone accidentally by a worker finishing afterwards. Linked
instance propagation, protected properties and group editing context use the
normal map services. Internal linked operations do not enter the recorded sequence.

Binary Doom/Hexen moves retain their native thing-height rules. Sector movement
expands to boundary vertices, deduplicated against selected lines and vertices;
it is available through the ordinary move service and replay. Native moves
preflight missing targets, coordinate limits and collapsed lines before mutation.
Other transforms
retain the existing format, coordinate, entity-angle and topology restrictions.

## Automation and studio integration

**Copy Repeat Recipe** copies versioned JSON. Save it in the project, then run:

```sh
vibestudio --cli map repeat maps/room.map --recipe repeat.json \
  --object brush:6 --output maps/repeated.map --json
```

Repeat `--object kind:id` to target several objects. `--context <group UUID>` opens
a group before selecting. `--texture-lock on|off` defaults to on;
`--allow-valve220` permits otherwise-required map-wide conversion. `--map-name`
selects a WAD map. Normal `--dry-run`, `--overwrite` and save conflict checks apply.
The CLI map payload includes `map.repeatRecipe`, so the output of a move, rotation
or duplication can also supply recipe actions. Map files do not store
session repeat state, and `map inspect` after reopening reports an empty recipe.

Recipes are data, not scripts. Version 1 has only `version` and `actions` at the
top level. Each action contains `kind` (`move`, `transform`, `duplicate` or
`duplicate-unique`), a
12-number row-major affine `matrix`, `rotationAxis`, `rotationDegrees`, a
three-number `rotationPivot`, `transformYaw`, a two-number `yaw`, `mirror` and
`doomAllowed`. Moves and duplications accept translation only. Transforms require
a finite inverse, consistent winding and consistent rotation metadata. Unknown
fields, unsupported versions, malformed values and recipes larger than 4 MiB are
rejected before writing. Empty recipes cannot be replayed.

`duplicate-unique` requires `doomAllowed: false`. It replays partial-owner
cloning and name/reference remapping against the current destination namespace,
so each repetition allocates fresh names. Outgoing references participate too.
Multi-copy arrays remain outside the recorded sequence.

After applying, save before using the existing Build, dependency review and
package workflows. Repeat neither compiles nor launches a game. Authoring state,
asset references, validation and compiler inputs remain in the shared document.

## NetRadiant Custom's Repeat Transforms

**Repeat Transforms** (`map.repeatTransforms`, Ctrl+R in the NetRadiant Custom
profile) is a second policy beside the recorded sequence, after
`RadiantSelectionSystem::repeatTransforms`. `LevelRepeatState::transforms`
(`src/core/level_repeat_types.h`) holds one translation, one rotation (axis and
degrees), one scale and one skew. Every top-level capture updates the slot of
its kind (`detail::levelRepeatTransformsAfter`): a move its translation, a turn
its rotation, a diagonal matrix (resize, mirror) its scale factors and a single
off-diagonal term its skew. Component moves, turns, scales and shears record
through `transformLevelMapComponents` (`LevelRepeatCaptureScope::commitTransform`)
without adding a Repeat Last Commands entry. Snapping (a component grid),
duplicates and both replays record nothing. The slots survive selection changes
and undo, as in NetRadiant Custom; **Reset Repeat Transforms** clears them.

`repeatLevelMapTransforms()` copies the selection in place, then scales, skews
and turns the copies about the centre of their bounds (or an explicit pivot)
and moves them, through `applyLevelRepeatTransform`, `rotateLevelMapSelection`
and `moveLevelMapSelection`, folded into one undo step named after what it
repeated. Component mode composes the same parts into one
`LevelComponentTransformKind::Affine` request
(`levelRepeatTransformsComponentRequest`), so UV lock and independent face
picks behave as for **Transform Components**. NetRadiant Custom adds a skew
term to the rotation matrix; VibeStudio applies scale, skew, rotation and
translation in that order, which matches it whenever one of skew and rotation
is absent. Quake-family text maps only; Doom maps keep **Repeat Last
Commands**. `map repeat-transforms` names the transforms on the command line.
Tests: `level-duplicate-move-smoke` (recording, replay, arrays, locks,
components, CLI) and `level-duplicate-move-ui-smoke` (shell keys and actions).

## Verification and remaining acceptance

- `level-repeat-smoke`: world transforms, duplication, selection boundaries,
  direct and legacy moves, mixed brush/patch/entity rotations, linked instances,
  protected values, locks, Doom/Hexen boundaries and heights, fractional UDMF
  movement/duplication, array selection boundaries, rollback and undo.
- `level-repeat-ui-smoke`: actual shell actions, TB keys, recipe copying,
  cancellation, clearing during preparation, Save As/reopen, accessible controls
  and owned widget renders at 100%/200%, expanded text and RTL.
- `level-repeat-cli-smoke`: three brush dialects, captured recipes, save/reopen,
  dry runs, overwrite refusal and malformed/oversized input rejection.
- `src/tests/level_repeat_workflow.py --compiler ...` additionally compares six
  BSP/VIS/LIGHT builds and three validated PK3s against direct command sequences.

Large real-project sequences, physical input, assistive technology and native
macOS/Linux acceptance remain open. TrenchBroom's undo grouping can differ:
VibeStudio deliberately commits each replay atomically. Array, component and
other command families need separate applicability reviews. Reference provenance
is recorded in [Credits](CREDITS.md#repeatable-object-commands).
