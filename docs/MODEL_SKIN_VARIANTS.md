# Skin Variants

Status: **Partial**. Skin variants are stored, edited with undo, previewed and
written as Quake III `.skin` files and Doom 3 skin declarations. Player packages
publish them. Choosing a variant in model browsers or on placed level models, and
staging variant skins with a model, are planned. The Doom 3 output is checked by
VibeStudio's own reader and the material decl parser, not yet in Doom 3.

A skin variant is a named skin an editable source owns beside its primary
materials, such as `red` and `blue` for team colours or `winter` for an alternate
look. A variant gives some surfaces another material, by surface name. Every
other surface keeps its primary material (skin slot 0), so `default` always means
the primary materials and is not a variant name.

Variant names use 1 to 24 ASCII letters, digits, underscores or hyphens and start
with a letter, the rule Quake III player skin names follow. A source can own up to
32 variants. Materials are package-relative paths of at most 255 characters, as
for [material slots](MODEL_MATERIAL_SLOTS.md). Models whose skins are embedded
(MDL) use their skin groups instead.

## In the Mesh Editor

**Skin Variants…** (Surface inspector, under **Manage Material Slots…**) edits a
working copy of the list:

- **Name** and **Add**, **Rename** and **Remove** manage the variants.
- **Surfaces** lists each surface's primary material and the selected variant's
  material. Select one or more rows, enter a **Material** and choose **Set
  Material**; **Use Primary** returns the rows to their primary material.
- **Show** displays the primary materials or a variant in the viewport. The model
  does not change.
- The review shows the selected variant's `.skin` text and Doom 3 declaration, or
  the reason either cannot be written.
- **Apply** keeps the list in the source as one undo step. Save the source to keep
  it. Closing without **Apply** discards the working copy.
- **Export .skin…** and **Export Doom 3 Skin…** write the selected variant's skin.
  **Doom 3 model** sets the model a Doom 3 skin names.

Renaming or deleting a surface keeps the variants that bind it. Such a binding is
listed as **Not in the model**; applying or writing that variant is refused until
**Use Primary** removes the binding.

A surface made from another's faces, by **Separate selected faces** or
**Duplicate surface** or by
[Prepare for Target](MODEL_TARGETS.md#prepare-for-target)'s splits, wears what its
origin wears in every variant, so a split part keeps its team colour; a binding an
earlier surface of the same name left behind is replaced. Prepare's review says
which variants now cover the new surfaces. When Prepare joins surfaces into the
one surface MDL and MD2 hold, the bindings of the surfaces joined away are removed
(their faces now wear the first surface's variant material), and the review names
them. Joining surfaces by hand keeps such bindings, listed as **Not in the model**.

## Quake III `.skin` files

A `.skin` names every surface of the model by its engine name: lower case, with a
final underscore and character dropped, so `body_1` is `body`. A variant's `.skin`
gives its bound surfaces the variant's material and every other surface its
primary material. Writing refuses surfaces without a material, names or materials
a `.skin` cannot hold (more than 63 characters, quotes, commas or `tag_`), and
surfaces that share an engine name but not a material. The written text is read
back with the same `.skin` reader as **Apply Skin** to confirm every surface gets
the intended material.

## Doom 3 skin declarations

A Doom 3 skin maps materials to materials, not surfaces: `idDeclSkin` replaces a
surface's material with the first mapping whose source is that material, and a
`model` line only associates the skin with a model in the editors. VibeStudio
writes one mapping per changed primary material, quoted, plus the model line when
a model path is given:

```text
// Doom 3 skin declaration written by VibeStudio: variant red.
skin skins/models/md5/monsters/imp/imp_red
{
	model	"models/md5/monsters/imp/imp.md5mesh"
	"models/monsters/imp/imp"	"models/monsters/imp/imp_red"
}
```

Because the mapping is by material, writing refuses a variant that the declaration
cannot express: a variant that changes nothing, a bound surface without a primary
material, two bound surfaces that share a material but not a variant material, and
a surface the variant keeps that shares a material it changes. The default
declaration name is `skins/<model path without extension>_<variant>`.

Doom 3 finds a material by its canonical name (`MakeNameCanonical` in
`neo/framework/DeclManager.cpp`): lower case, `\` as `/`, and the last extension
removed, so `Models/X/Body.tga` and `models/x/body` are one material. The checks
above compare materials that way, and each mapping is written once per canonical
material: a kept surface whose material differs from a changed one only in case
or extension is refused, and a variant material that is the same Doom 3 material
as the primary changes nothing.

## Player packages

A [player package](MODEL_ASSEMBLY.md#native-player-packages) skin uses a part's
stored variant of the same name when no `.skin` file is given for that part. Store
`red` and `blue` variants on the parts that change colour, then turn on **Team
skins (red and blue)**.

## CLI

```sh
vibestudio --cli model skin-variants ./models/prop.mesh.json --json
vibestudio --cli model skin-variants ./models/prop.mesh.json --operation add \
  --name red --surface-material body=models/prop/body_red.tga \
  --output ./models/prop.mesh.json --overwrite --json
vibestudio --cli model skin-variants ./models/prop.mesh.json --operation set \
  --name red --surface-material visor= --output ./models/prop.mesh.json --overwrite
vibestudio --cli model skin-export ./models/prop.mesh.json --variant red \
  --output ./out/prop_red.skin --json
vibestudio --cli model skin-export ./models/imp.mesh.json --variant red --format doom3 \
  --model-path models/md5/monsters/imp/imp.md5mesh --output ./out/imp_red.skin --json
```

`model skin-variants` lists the variants by default (with each binding, the
surfaces that keep their primary material and any binding to a missing surface).
`--operation add`, `rename` (`--to`), `remove` and `set` need `--name` and
`--output .mesh.json`; `--surface-material SURFACE=MATERIAL` is repeatable, and an
empty material returns the surface to its primary material. `--dry-run` validates
without writing and `--overwrite` replaces an existing output.

`model skin-export` reads an editable source or a native model. Without
`--variant` it writes the `.skin` of the primary materials, which covers any MD3.
`--format doom3` needs `--variant`, and takes `--model-path` and `--decl-name`.
JSON reports the bindings or mappings, the surfaces that keep their primary
material, the byte count and SHA-256. Usage errors exit 2 and refused content
exits 4; neither writes a file.

## Source format

Editable sources record variants from schema 14 as `skinVariants`, an array of
`{ "name", "bindings": [ { "surface", "material" } ] }`. Sources without variants
keep their earlier schema, and earlier sources load with none.

## References

Behaviour only; no code is copied (see [credits](CREDITS.md)):

- id Software, Quake III Arena GPL source: `code/renderer/tr_image.c`
  (`RE_RegisterSkin`) and `code/renderer/tr_model.c` (surface name normalisation).
- id Software, DOOM 3 GPL source: `neo/framework/DeclSkin.cpp` (`idDeclSkin::Parse`,
  `RemapShaderBySkin`) and `neo/framework/DeclManager.cpp` (`MakeNameCanonical`).
