# Clipboard placement

**Status: Partial.** VibeStudio remains pre-alpha. These commands provide tested
object placement and face clipboard dispatch; they do not establish complete TB/NRC compatibility or native
platform acceptance.

**Edit** and **Tools > Clipboard** offer:

| Command | Placement |
| --- | --- |
| **Paste Map Content** | Cursor placement in the TrenchBroom profile; original coordinates in other profiles |
| **Paste at Original Position** | Saved coordinates in every profile; TB uses **Ctrl+Alt+V** |
| **Paste at Cursor** | Active plan's cursor plane, or the brush under the camera cursor |
| **Paste to Camera** | Assembly centre moved towards the camera by a grid-snapped translation; NRC uses **Shift+V** |
| **Paste with Offset…** | Existing numeric preview with an explicit offset |

**Ctrl+V** follows the profile. User shortcut overrides take precedence. Commands
are also in search. They use normal map selection, **Create in** / open-group
context, **Texture Lock**, locks and native group metadata. Model and sound paths,
brush owners, patch UVs and protected properties stay with their objects.

## Copy and cut objects

**Copy Map Selection** prepares Quake-family entities, brushes and patches on a
worker. Selecting children of a brush entity retains that entity's properties
and only the selected children, in their selection order. Selecting the whole
entity retains every child in source order; selecting both owner and children
never duplicates them. World geometry copies without project worldspawn settings.
Model/audio asset paths and protected properties remain with copied entities.
Only complete native groups transfer their hierarchy and linked relationships;
a partial owner wrapper cannot stand in for its entire group. NRC instead
traverses its source graph and exports worldspawn properties; these ordering and
world-property differences remain part of the Partial compatibility status.

**Cut Map Objects** prepares the same text and a whole-object deletion together.
It is unavailable during component or clipper tools, active viewport drags and
pending surface edits, and for worldspawn or TB face picks. Locked objects may
be copied but cannot be cut. After preparation, changed map, selection, group,
package, component/face picks or clipboard contents refuse publication. Cancel
keeps the map and clipboard unchanged. Copy accepts the text before Cut publishes
its single undo step; a synchronous tool/context change at that point leaves a
copy and preserves the original objects. Undo/redo restores the exact map edit.

Long operations show a cancellable, localised progress dialogue. Clipboard text
is bounded to 16,384 objects (including copied owner wrappers), 4,194,304 UTF-16
code units and 8 MiB of UTF-8, without NUL characters. A stale selection fails as
a whole. These operations require no cloud service or extra setup.

## Face clipboard

**Copy Map Selection** (Ctrl+C) exports picked face records in the TrenchBroom
profile. Without face picks it exports objects. Other profiles retain object
copy; **Copy Surface** exports picked component faces in Face mode, or the
inspected face in object mode, and also updates the session surface source.
Sampling a surface changes only that session
source. **Cut** is unavailable for TB face picks, so their owner brushes remain.

Normal paste, original-position, cursor and camera paste recognise plain `.map`
face records before attempting object placement. The last record supplies the
material, offsets, rotation, scales and explicitly written surface flags/value.
Picked faces receive the attributes; in object mode, without picks, all faces of
selected brushes and brush entities receive them. Component mode uses only its
picked brush-face components; empty picks or Vertex/Edge mode have no surface
targets. Target **content flags stay unchanged**. Omitted
source flags/value preserve the target values; explicit zero values clear them.
Face paste leaves selection and geometry in place and adds one undo step.

**Surfaces > Face attributes** exposes the same mode for the session source.
Valve 220 targets retain their own UV basis, rotated by the change in rotation
about that basis's normal. Classic and Valve values can cross without changing
the target dialect. Matrix-to-matrix paste copies the raw matrix; matrix/non-matrix
conversion and patch targets are refused. Use the existing **World Projection**
mode when conversion is intended. **Mapping only** retains material and flags.

The shared surface worker parses and validates the complete payload before
publication. Malformed records, zero scales, invalid Unicode, oversized input,
locks, cancellation, or changed map/face-selection context publish nothing.
Limits are 16,384 records and 4,194,304 UTF-16 characters. CLI files must also fit
8 MiB of valid UTF-8. Raw face paste leaves the sampled session source unchanged.
No cloud service or package image lookup is required for Face attributes.

## Cursor and camera placement

Plan views align the assembly's far side with the selection's far depth. With
no selection, they use the view's remembered brush-creation depth. The cursor
chooses the other two coordinates. Top and side views align minima; the front
view aligns the maximum Y side. The active pane is retained while menus or
search have focus. Leaving a pane makes its centre the fallback command point.
Only local pointer events update that anchor.

The camera ray tests visible existing brushes before insertion, so the pasted
objects cannot catch their own ray. Hidden scene objects and the current GUI
camera filters are excluded. Models and patches do not obscure brush targets.
A miss uses a plane 256 units along the ray, facing the camera. Placement follows
TB's corner rule: choose a corner away from the ray origin, snap towards the
camera, and leave an axial target's depth unsnapped. Sloped targets can therefore
intersect the pasted bounding box slightly. **Paste to Camera** instead follows
NRC's centre rule: snap the translation, rather than the camera coordinates.

The default profile's orthographic orbit view has no finite camera eye.
**Paste to Camera** uses its visible centre on the focus plane, including pan.
**Paste at Cursor** chooses the frontmost brush surface on either side of that
plane; a miss uses the grid-snapped focus plane. These rules also apply after
restoring an orthographic view bookmark in another profile.

Point entities use loaded class-definition bounds; unknown classes use an
origin-relative 16-unit box. Brush entities use their brushes and patches, without
letting an optional origin key enlarge the placement bounds. Their origin keys
still move with the assembly. Point entities need a finite origin. Destination
coordinates are bounded to the editor's ±32,768 range.

**Move to Camera** (`moveLevelMapSelectionToCamera`, NetRadiant Custom's
`MoveToCamera`) applies the same centre rule to the live selection through
`levelPasteOffset`: the selection's paste bounds, point entities sized by the
loaded definitions, and a translation snapped to the grid while **Snap** is on.
It is one `moveLevelMapSelection` step through the placement worker
(`LevelPlacementOperation::MoveToCamera`), so objects keep their identities,
order and source spans, the clipboard is untouched and Repeat records the move.
A zero translation is refused. In a component mode the shell moves the picked
components by the snapped translation from their centre instead. `map move
--camera x,y,z` is the CLI route.

## Transactions and integration

Parsing, bounds, ray targeting, translation, UV validation and insertion run in
the shared asynchronous placement worker. Cancellation, malformed text, mixed
texture dialects, invalid targets, locked destinations or a changed document
publish nothing. A successful paste selects the new objects and adds one undo
step. Complete native hierarchies receive fresh identities and preserve internal
linked relationships. Clipboard placement does not record a repeatable command.

Save/reopen, compiler inputs, dependency review and package operations use the
same native document and asset paths. No setup change or AI connector is needed.
Progress and errors use the normal accessible, localised placement surface.

## CLI

`map paste <destination> --from <UTF-8 map text> --output <map>` keeps original
coordinates. Choose at most one alternative:

```text
--delta 128,0,0
--camera 128,0,64 --grid 16
--plane-point 128,0,0 --plane-normal 0,0,1 --ray-direction 0,0,-1 --grid 16
--ray-origin 128,0,256 --ray-direction 0,0,-1 --view-direction 0,0,-1 --grid 16
```

Ray targets additionally accept `--orthographic` for orbit-view semantics: the
ray origin lies on the focus plane, hits can precede it, and a miss stays on that
plane. JSON reports this flag as `orthographic`.

Targeted modes accept `--definitions <file-or-directory>` for point bounds.
`--grid 0` disables snapping and is the CLI default. `--context <UUID>` chooses a
layer or opens a group for insertion; `--context default` chooses Default.
CLI ray placement honours saved scene visibility; GUI-only view filters are not
stored in the map. `--texture-lock off` retains brush mapping parameters.
`--dry-run` validates without writing; `--overwrite` is required for an existing
output. JSON includes `placement` and the resolved `delta`. Input is bounded to
8 MiB and 4,194,304 UTF-16 characters. Duplicate, unknown or incomplete target
options are rejected.

For object copy and cut:

```sh
vibestudio --cli map copy source.map --object brush:0 --object patch:1 --output fragment.map --json
vibestudio --cli map cut source.map --object entity:1 --clipboard fragment.map --output remaining.map --json
```

Selectors are repeatable `entity:id`, `brush:id` or `patch:id`. Output directories
must already exist and cannot pass through symbolic links or junctions. The
clipboard path must differ from the source and remaining map paths, including
existing hard links and case aliases. Existing
outputs require `--overwrite`, including dry runs. `--dry-run` validates both
outputs without writing. In-place Cut (`--output source.map --overwrite`) uses
the normal map backup and external-change protection.

The two Cut output files are committed separately: the fragment is written
first, then the remaining map. If saving fails after the copy, JSON returns
`clipboardWritten: true`, `mapWritten: false` and a nonzero exit code; the error
names the saved fragment. The source retains its objects. Both fields are false
for preflight failures and dry runs. Object `map copy` reports `clipboardBytes`
and `selectedObjects`; it never modifies the source.

For face records, use the shared surface CLI:

```sh
vibestudio --cli map copy-surface source.map --target face:0:1 --target face:0:3 --format map --output faces.map
vibestudio --cli map paste-surface target.map --clipboard faces.map --target brush:0 --output pasted.map
```

Face numbers are one-based. JSON remains the default copy format and accepts one
source face. Paste auto-detects raw face records and defaults to `attributes`;
JSON defaults to `parameters`. `--mode attributes` also accepts JSON. An explicit
mode can select another surface transfer. `--dry-run` and `--overwrite` retain
the usual output protections. The object-placement `map paste` verb accepts
object text; `paste-surface` is its face-attribute equivalent.

## Evidence and remaining work

The core, offscreen UI and CLI tests are in `src/tests/level_paste_*`. The workflow
generates its own room and textured brush entity and can compare BSP/VIS/LIGHT
results with an independently calculated offset across classic, Valve 220 and
brush-primitive maps, then validate PK3 contents. Existing placement, scene,
linked-group and prefab suites cover the shared insertion machinery.

Native mouse/keyboard acceptance on Windows, macOS and Linux, assistive technology,
dense real maps and complete foreign clipboard/group/entity-ID compatibility
remain unverified. Complete foreign face-format conversion, optional colour
attributes and game-default surface semantics also remain open. The pane-centre
fallback is deliberate and does not reproduce TB's outside-camera-cursor branch.
See [reference credits](CREDITS.md#clipboard-placement).
