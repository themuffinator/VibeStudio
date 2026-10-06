# Placed Model Appearances

Quake III `misc_model` instances can use different compiler skins and material
remaps while sharing one MD3 asset. Levels previews each appearance separately;
dependency review, asset subset export and prepared builds use the same resolver.
The source model, its animation frames and its original material slots remain
unchanged.

## Author and inspect

Select the model entity in **Levels**, then use the entity inspector or **Edit
Key** to set `_skin` and `_remap0`, `_remap1`, and so on. Each key edit uses normal
map undo/redo. Save the map before compiling. The camera retains its position
while the background worker refreshes the affected geometry and materials.

**Camera > Details** lists the instance selectors, requested frame, derived skin
file, effective material on each surface, omitted surfaces and exact input
SHA-256 hashes. An unresolved appearance is excluded from the camera and shown
in its status; it also blocks dependency subset export. Other valid instances
remain visible. **Reload** rereads the current package snapshot. Staged additions
and replacements participate in previews and dependency checks before a package
is published.

The equivalent CLI workflow is:

```sh
vibestudio --cli map edit ./maps/room.map --engine idTech3 --entity 3 --set _skin=blue --set '_remap0=trim;models/prop/metal' --output ./maps/room-blue.map
vibestudio --cli map materials ./maps/room-blue.map --package ./assets.vibepackage --engine idTech3 --geometry --json
vibestudio --cli map dependencies ./maps/room-blue.map --package ./assets.vibepackage --engine idTech3 --json
```

`map materials` accepts asset folders, archives and portable `.vibepackage`
drafts. Its JSON `materials.modelAppearances` array carries the appearance
receipts. Dependency JSON includes the same receipts and `model-skin` rows for
explicit and implicit skin inputs. Failed appearance validation returns exit
code 4. Generic `map edit` can preserve an unresolved key for later correction;
run material/dependency review before packaging or building.

## Compiler skin rules

The contract follows the pinned NetRadiant Custom q3map2 importer, reviewed in
[Credits](CREDITS.md#placed-model-compiler-appearances). It is separate from the
[native player skin workflow](MODEL_MESH.md#quake-iii-skin-assignments).

| Entity value for `models/prop.md3` | Compiler file |
| --- | --- |
| `_skin=blue` | `models/prop_blue.skin` |
| `_skin=14` | `models/prop.md3_14.skin` |

The first nonempty `_skin` takes precedence over `skin`. Names are suffixes,
not full paths. The studio accepts ASCII letters, digits, underscores, dots and
hyphens. Skin files accept these plain text forms:

```text
models/prop/body,models/prop/blue
replace models/prop/trim models/prop/metal
```

The source column matches the compiler's **material name**, case-insensitively.
It does not match the MD3 surface name. The first matching row wins. When at least
one valid mapping exists, every unmapped surface is omitted, including when no
surface matches. Empty/comment-only explicit skins retain the original bindings.
Whole-line `//` comments and trailing `//` comments are allowed. The studio
diagnoses malformed directives, oversized tokens, mixed newline conventions,
NUL bytes and a UTF-8 BOM instead of reproducing silent compiler discards or
truncation. Explicit missing skins fail review; q3map2 itself may fall back.

MD3 shader slot zero supplies the original material. The compiler removes its
extension, normalizes slashes, and resolves a bare filename beside the model.
Absolute/traversing source hints are reduced using the compiler's `models/` or
`textures/` root rules; effective material paths must be safe package paths.

Before applying the explicit compiler skin, the importer also looks for an
optional default skin. It removes the model filename's last underscore suffix
(or its extension if there is no underscore) and appends `_default.skin`.
For example, both `prop.md3` and `prop_lod.md3` look for `prop_default.skin`.
This file uses whitespace/comma-separated **surface/material** pairs, exact
case-sensitive surface names, and standalone `tag_...` tokens. It does not use
the native player's quoted/comment grammar. Present malformed, duplicate or
unreadable default skins fail review; absent default skins are allowed.

`_remap*` values use `FROM;TO` after skin selection. A case-insensitive suffix
match wins over `*`; the longest matching suffix wins, then the first equal
match in map property order. With no suffix match, the last wildcard supplies
the fallback. Remaps apply once. Destination materials have a 63-byte ASCII
limit. Each appearance allows at most 256 remaps; each skin allows 4,096 mappings
and 256 KiB. Preview budgets count distinct appearances, share source-model
decoding, and include skin bytes. Dependency review caps appearance expansion
at 128 variants.

## Animated poses and compiler limits

The pinned Assimp MD3 importer validates `_frame`/`frame` but reads vertex data
from frame zero. This was independently reproduced with a real NetRadiant
Custom BSP. VibeStudio refuses nonzero frame requests rather than displaying a
pose that this compiler will not produce. A nonempty `_frame` wins over `frame`.

Bake the desired pose to a separate static MD3 through **Models > Assemblies**,
then place that derivative at frame zero. The CLI path is:

```sh
vibestudio --cli model assembly --new --part prop --model ./assets/models/prop.md3 --first-frame 1 --last-frame 1 --output ./prop-pose.assembly.json
vibestudio --cli model assembly ./prop-pose.assembly.json --operation bake --time 0 --output ./assets/models/prop-pose.md3
```

The compiler acceptance fixture verifies this bake and its resulting BSP
coordinates. It leaves the original animated model intact.

Compiler appearance overrides currently cover MD3 `misc_model` in Quake III
maps. Other formats keep their default preview and reject unverified compiler
overrides. Runtime entities and standalone model dependency review retain their
existing behavior, including all retained native material slots. These previews
do not simulate game shaders, shader deprecation/secondary references, SOF2
shader renaming, parent brush-entity transforms or every third-party compiler
variant. A patched importer that supports nonzero frames needs a separate
verified capability contract.

## Verification

`level-model-appearance-smoke` covers parameters, exact input identities, byte
budgets, immutable staged snapshots, effective dependency closure, invalid
inputs, cancellation, map history/save/reopen and CLI authoring/review.
`level-model-appearance-ui-smoke` covers cache invalidation, retired requests,
the real entity inspector, undo/redo, camera preservation, visible failures,
high contrast, RTL and translation expansion. It uses owned Qt APIs and
`QWidget::render`, without OS input or screen capture.

The optional real compiler proof uses synthetic assets and inspects BSP data:

```sh
python src/tests/level_model_appearance_compiler_workflow.py --binary builddir/src/vibestudio --compiler /path/to/q3map2 --output-root .agents/tmp/model-appearance-proof
```

The accepted local executable and source revision are recorded in the scoped
milestone evidence. This is a Windows compiler/UI check, not cross-platform
release approval.
