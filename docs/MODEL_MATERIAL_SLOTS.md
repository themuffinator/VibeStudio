# Model Material Slots

The Surface inspector and `model slots` share ordered external material bindings.
Slot zero is the primary material. Further slots retain their exact order, and
repeated paths are allowed. Geometry, UVs, normals, seams, animation poses, tags,
collision and other surfaces are unchanged by a slot edit.

## Authoring and preview

Choose **Surface > Manage Material Slots** to review the active surface's list.
**Add** appends the entered path; **Replace** changes the selected slot;
**Remove**, **Move Up**, **Move Down** and **Clear All** edit the review buffer.
Apply commits the complete reviewed list as one undoable document operation.
Cancel discards the buffer. A path typed into the field must be added or replaced
before Apply is available. Removing slot zero makes the following slot primary;
clearing the list leaves the surface unassigned.

Each surface accepts up to 256 package-relative paths, with at most 255 characters
per path. Empty paths, absolute paths and traversal are refused. Native exports
enforce their narrower limits: MD2 requires compatible PCX paths in every slot;
MD3 preserves the stored shader records; OBJ exports the primary binding only.
Use the normal export validation to inspect target restrictions before staging.
The editor does not rewrite suffixes, generate texture files or copy materials.

**Preview slot** selects the material shown on that surface in Material mode and
the UV view. **Assign Material** replaces that selected slot, or creates the
primary binding when unassigned. **Show Material** opens the displayed path in
the texture workflow. Preview selection alone changes neither source, history,
export order nor package state. It is a session choice per surface name and slot
index. Reordering bindings keeps the index, so the visible material can change.
Out-of-range indices clamp after removal; removed or renamed surfaces reset
their preview choice. Opening or restoring a source resets all choices to zero.

Material resolution uses the same immutable package/staging snapshot, palette,
bounded shader lookup, progress, Cancel, Reload Images and Details as primary
materials. A new slot request retires stale work. Repeated paths reuse unchanged
image work. The snapshot passed to resolution retains no animation geometry.
Package source changes refresh both views through the existing worker.

Embedded MDL skins use the Animation inspector's native skin/member controls.
External slot authoring and override previews are disabled for those models;
their default embedded preview remains available. The legacy CLI `model edit
--operation material` still changes the primary external path only.

## Model browser appearances

The ordinary **Models > Skin** inspector offers the same alternate-material
preview without opening an editable document. Select **Preview surface**, then
**Material slot**; each surface keeps its own choice while this model is selected.
The image shows the selected surface. Changing appearance retains the current
geometry frame and camera. **Reset** restores primary bindings.

**Skin File…** opens the metadata-only package entry picker. It reads the
chosen exact occurrence on the preview worker and requires complete Quake III
surface-to-shader assignments. Details retain the path, directory index, SHA-256,
assignments and image diagnostics. Repeated paths require an exact occurrence;
an invalid mapping fails visibly without publishing partial bindings. Reset
leaves this mode before choosing individual slots. Changes to the model or
package while the picker is open invalidate that choice.

For Quake MDL, **MDL skin** and **Skin member** replace external material controls.
The selected native indexed pixels use the package palette; changing the skin
returns to member zero. This member stays fixed during ordinary geometry
playback. Use the Mesh Editor's native MDL playback for timed skin groups.

Loading and Cancel use the existing asynchronous model preview. Cancellation
retires results and re-enables the inspector when its worker finishes. Missing
images keep their textual diagnostics. Choosing another model, changing the
package snapshot or changing the palette resets these session choices. Opening
the Mesh Editor and exporting a browser frame use the original model bindings;
preview choices do not edit, stage or save the source.

## CLI

Commands accept one editable `.mesh.json` source positionally or with `--input`.
Listing can optionally restrict to one surface; edits require an explicit
zero-based `--surface` and an `--output` ending in `.mesh.json`.

```text
vibestudio --cli model slots prop.mesh.json --json
vibestudio --cli model slots prop.mesh.json --surface 0 --operation set --slot 1 --material models/prop/blue.pcx --output changed.mesh.json
vibestudio --cli model slots prop.mesh.json --surface 0 --operation insert --slot 2 --material models/prop/red.pcx --output changed.mesh.json
vibestudio --cli model slots prop.mesh.json --surface 0 --operation move --slot 1 --to 0 --output changed.mesh.json --dry-run --json
vibestudio --cli model slots prop.mesh.json --surface 0 --operation remove --slot 1 --output changed.mesh.json
vibestudio --cli model slots prop.mesh.json --surface 0 --operation replace --material models/prop/red.pcx --material models/prop/blue.pcx --output changed.mesh.json
vibestudio --cli model slots prop.mesh.json --surface 0 --operation clear --output changed.mesh.json
vibestudio --cli model materials prop.mesh.json --package assets --surface 0 --material-slot 1 --json
vibestudio --cli model materials player.md3 --package assets.vibepackage --entry models/players/example/default.skin --json
vibestudio --cli model materials player.md3 --package assets --entry-index 12 --json
vibestudio --cli model materials player.mdl --package assets --skin 0 --member 1 --json
```

Insert accepts indices from zero through the current count; count appends. Move's
destination is the final index in the reordered list. Set and Insert require one
material; Replace accepts repeated `--material` options in order, including
duplicate values. Clear explicitly removes every binding. Unknown, repeated or
inapplicable options are refused; only Replace accepts repeated material values.

`--dry-run` validates without writing. An existing output requires `--overwrite`,
including dry-run review. Normal source fingerprints, atomic writes and protected
targets apply. JSON reports the resulting indexed surfaces and ordered
`materials`, `previousMaterials` for edits, `operation`, `source`, `outputPath`,
`written`, `dryRun` and `externalSlotsEditable`. Exit codes are 0 for success,
2 for usage, 4 for model/edit validation and 1 for read/write failures.

`model materials` accepts a native/editable source and a package archive, folder
or portable `.vibepackage` draft. Use one of three separate appearance modes:

- Pair `--surface N` with `--material-slot N` for one external alternate. Other
  surfaces use their primaries.
- Use `--entry path.skin`, `--entry-index N`, or both for a package skin. A path
  paired with an index guards that exact row; a repeated path alone is refused.
- Use `--skin N` and/or `--member N` for embedded MDL pixels. An omitted index is
  zero. Native MDL imports use the selected package palette (`--palette` defaults
  to Quake); editable sources retain their stored palette.

All indices are zero-based nonnegative integers. JSON includes `appearance`
with `previewOnly: true`, effective surface bindings, the MDL selection (including
`indexedSha256` and `paletteSha256`) or verified skin receipt, plus the existing
`previewSlots` and material report.
Input modes cannot be mixed. Unknown, repeated, write-oriented and inapplicable
options are refused. Nothing is written. Exit codes are 0 for success, 1 for
source reads, 2 for usage, 3 for opening the package, and 4 for model/appearance
validation or unresolved images. The bounded resolver remains shared with the GUI.

## Integration boundaries

Saves, checksummed recovery, undo/redo, native export and package/level handoff
use the ordinary document services. Manual binding edits invalidate the last
`.skin` import receipt. Importing a `.skin` file still assigns primary bindings
and retains alternate slots; select preview zero to inspect that result.
External `.skin` files, shaders, texture contents and project references are not
rewritten automatically. Review dependencies after changing paths or order.

This is ordered binding authoring and static image preview. It does not provide
a project-wide named variant system, per-face material assignment within one
surface, runtime skin switching, animated shader simulation, or automatic
texture rebaking. Separate faces into surfaces when they require independent
bindings. [The release gate](MODELLER_RELEASE.md) remains open for full production
interaction, clean packages and cross-platform acceptance.

Level preview and compiler `_skin`/`skin` remapping use the separate shared
contract below. Native Quake III surface bindings and q3map2 source-material
remapping have different matching rules. Author primary model bindings in the
Mesh Editor and review effective level dependencies before compiling or packaging.

## Placed compiler appearances

MD3 `misc_model` placement uses shader slot zero, optional compiler skins and
entity remaps. Each instance has its own cached appearance and dependency closure.
Native player surface bindings and browser preview slots retain their own
contracts. See [Placed Model Appearances](LEVEL_MODEL_APPEARANCE.md), including
the verified static-pose bake required by the pinned q3map2 importer.
