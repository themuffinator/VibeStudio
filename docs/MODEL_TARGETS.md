# Model Delivery Targets

A delivery target is the engine or source port a native model export is meant
for, with the limits that engine's model loader and renderer enforce. The
modeller checks every export, stage and set of detail levels against the target
chosen for its format before it writes anything, and reports every limit the
model exceeds rather than the first. It can also prepare a model for a target:
apply the fixes the check suggests as one undoable change, after a review. The
user-facing summary is in the
[Models manual page](manual/models.md#delivery-targets); the writers are
described in [native model formats](MODEL_FORMATS.md), the editor's header
summary in [mesh tools](MODEL_TOOLS.md#delivery-targets-and-detail-levels), and
detail levels in [detail levels](MODEL_LOD.md).

> [!WARNING]
> **Status: Partial.** The limits were read from each engine's public source
> code, as listed below, and are checked against synthetic models in the tests.
> No exported model has yet been loaded in these engines to confirm them, and
> the table covers the engines and ports below only.

## Targets

Each format has one default target, which is what an export uses when no
target is chosen and none is saved with the project. The defaults keep the
original engines' limits, except that Quake III's working limit is one vertex
and one triangle lower than before (see below). A limit not listed is not set
by that target; the editable source's own limits still apply (32 surfaces,
1,024 frames, 16 tags per frame and so on).

| Format | Target | Default | Limits |
| --- | --- | --- | --- |
| MDL | `quake`: Quake (id Software) | Yes | 1,024 stored vertices, 2,048 triangles, 256 poses, 32 skins, skin height 480, 307,200 pixels per skin, width a multiple of 4 |
| MDL | `quakespasm`: QuakeSpasm | | 2,400 stored vertices, 4,096 triangles, 1,024 poses, 32 skins |
| MDL | `fte`: FTE QuakeWorld | | 65,535 drawn vertices (stored vertices plus a copy of each seam vertex) |
| MDL | `darkplaces`: DarkPlaces | | 65,535 each of stored vertices, triangles, poses and skins; skins up to 65,535 pixels wide and tall |
| MD2 | `quake2`: Quake II (id Software) | Yes | 2,048 positions, 4,096 triangles, 512 frames, 32 skin slots, skins up to 640 by 480 |
| MD2 | `q2pro`: Q2PRO | | 2,048 positions, 6,144 triangles, 1,024 frames, 6,144 drawn vertices (distinct position and texel pairs), 32 skin slots, skins up to 640 by 480 |
| MD3 | `quake3`: Quake III Arena (id Software) | Yes | 999 vertices and 1,999 triangles per surface, 32 surfaces, 1,024 frames, 16 tags per frame, 2 detail levels |
| MD3 | `ioquake3`: ioquake3 and Quake3e | | As Quake III Arena |
| MD3 | `rtcw`: Return to Castle Wolfenstein (single player) | | 3,999 vertices and 7,999 triangles per surface, 2 detail levels |
| MD3 | `rtcw-mp`: Return to Castle Wolfenstein (multiplayer) | | 999 vertices and 1,999 triangles per surface, no detail levels |
| MD3 | `et`: Wolfenstein: Enemy Territory | | 1,024 vertices and 2,049 triangles per surface, 3 detail levels |
| MD3 | `q2pro`: Q2PRO | | 6,144 vertices and 6,144 triangles per surface, no tags read, no detail levels |
| MD3 | `gzdoom`: GZDoom (MODELDEF) | | No vertex or triangle limit; 32 surfaces (MODELDEF's `SurfaceSkin` addresses 0 to 31), no detail levels |
| IQM | `iqm`: IQM format (any engine) | Yes | 256 joints (one-byte blend indexes), 4 influences per vertex |
| IQM | `ioquake3`: ioquake3 and Quake3e | | 128 joints, 999 vertices and 1,999 triangles per mesh |
| IQM | `fte`: FTE QuakeWorld | | 256 joints, 65,535 vertices per mesh |
| IQM | `gzdoom`: GZDoom (MODELDEF) | | 256 joints, 4 influences per vertex, no vertex limit |
| MD5 mesh | `doom3`: Doom 3 (id Software) | Yes | None |
| MD5 mesh | `q2pro`: Q2PRO | | 256 joints, 32 meshes, 6,144 vertices, 6,144 triangles and 8,192 weights per mesh |
| MD5 animation | `doom3`: Doom 3 (id Software) | Yes | None |
| MD5 animation | `q2pro`: Q2PRO | | 1,024 frames per clip, 256 joints |

OBJ and ASE have no targets. `vibestudio --cli model targets --json` prints the
same table with each target's description and sources.

Some targets carry advice the limits cannot express:

- Return to Castle Wolfenstein and Enemy Territory load `name.mdc` before
  `name.md3` when both exist (`R_RegisterModel`, unless `r_compressModels` is
  set), so a stale MDC beside a new MD3 hides it. The studio writes MD3 only;
  inspecting a detail set for these targets warns when an MDC is present.
- Q2PRO draws `md5/name.md5mesh` and `md5/name.md5anim` in place of an MD2 or
  MD3 of the same name when both files exist, with skins named by the MD2 or
  MD3 under `md5/`.
- GZDoom maps sprite frames to model frames, skins and surfaces in MODELDEF,
  which the studio does not write.

### Where the limits come from

Each limit was read from the engine's own loader; no engine code is copied.

- **Quake**: [id-Software/Quake](https://github.com/id-Software/Quake)
  (GPL-2.0-or-later, master, reviewed 2026-10-22). `WinQuake/gl_model.h`
  defines `MAXALIASVERTS` 1024, `MAXALIASTRIS` 2048, `MAXALIASFRAMES` 256 and
  `MAX_SKINS` 32 for GLQuake's static arrays; the software renderer's
  `r_local.h` allows 2,000 vertices, so GLQuake's limits are the ones both
  renderers accept. `Mod_LoadAliasModel` in `model.c` and `gl_model.c`
  refuses a skin taller than `MAX_LBM_HEIGHT` (480) and, in the software
  renderer, a width that is not a multiple of four; `GL_LoadTexture` in
  `gl_draw.c` uploads through a 640 by 480 buffer.
- **QuakeSpasm**: [sezero/quakespasm](https://github.com/sezero/quakespasm)
  (GPL-2.0-or-later, master, reviewed 2026-10-22). `Quake/gl_model.h` raises
  `MAXALIASVERTS` to 2400, `MAXALIASTRIS` to 4096 and `MAXALIASFRAMES` to
  1024; `Mod_LoadAliasModel` only warns about a tall skin.
- **FTE QuakeWorld**: [fte-team/fteqw](https://github.com/fte-team/fteqw)
  (GPL-2.0-or-later, master snapshot, reviewed 2026-10-22).
  `Mod_LoadQ1Model` in `engine/common/com_mesh.c` allocates as it loads and
  adds one vertex per seam vertex; `index_t` in `engine/client/render.h` is 16
  bits in the GL builds with legacy support, and `MAX_BONES` in
  `engine/gl/gl_model.h` is 256.
- **DarkPlaces**:
  [DarkPlacesEngine/DarkPlaces](https://github.com/DarkPlacesEngine/DarkPlaces)
  (GPL-2.0-or-later, master, reviewed 2026-10-22). `Mod_IDP0_Load` in
  `model_alias.c` refuses counts and skin sizes of 65,536 or more.
- **Quake II**: [id-Software/Quake-2](https://github.com/id-Software/Quake-2)
  (GPL-2.0-or-later, master, reviewed 2026-10-22). `qcommon/qfiles.h` defines
  `MAX_VERTS` 2048, `MAX_TRIANGLES` 4096, `MAX_FRAMES` 512 and `MAX_MD2SKINS`
  32; the PCX loader refuses images 640 pixels wide or more, and the alias
  loaders refuse skins taller than 480. [Yamagi Quake
  II](https://github.com/yquake2/yquake2) keeps the same limits
  (`src/common/header/files.h`, `src/client/refresh/files/models.c`, master,
  reviewed 2026-10-22).
- **Q2PRO**: the original repository (github.com/skullernet/q2pro) is no
  longer on GitHub; the limits were read from the preserved upstream
  [q2pro/q2pro](https://github.com/q2pro/q2pro) (GPL-2.0-or-later, `601a8df`,
  2025-12-11, reviewed 2026-10-22). `inc/format/md2.h` allows 2,048 positions,
  1,024 frames and skins up to 640 by 480; `MOD_ValidateMD2` in
  `src/refresh/models.c` allows `TESS_MAX_INDICES / 3` triangles and
  `MOD_LoadMD2` builds one drawn vertex per distinct position and texel pair, at
  most `TESS_MAX_VERTICES`, which `src/refresh/gl.h` sets to 6144 (raised from
  4096 in `0dc71b8`, 2024-07-21, so earlier builds hold 4,096 of each).
  `MOD_ValidateMD3Mesh` and `MOD_ValidateMD3` take 6,144 vertices and triangles
  per mesh, `MD3_MAX_MESHES` (32) meshes and `MD3_MAX_FRAMES` (1024) frames, and
  read no tags or detail levels. `MD5_ParseMesh` and `MD5_ParseAnim` read at
  most `MD5_MAX_JOINTS` (256) joints, `MD5_MAX_MESHES` (32) meshes,
  `TESS_MAX_VERTICES` vertices and triangles and `MD5_MAX_WEIGHTS` (8192)
  weights per mesh, and `MD5_MAX_FRAMES` (1024) frames; `MOD_LoadMD5` and
  `MD5_LoadSkins` find the files and skins under `md5/`.
- **Quake III Arena**:
  [id-Software/Quake-III-Arena](https://github.com/id-Software/Quake-III-Arena)
  (GPL-2.0-or-later, master, reviewed 2026-10-22). `code/qcommon/qfiles.h`
  defines `SHADER_MAX_VERTEXES` 1000, `SHADER_MAX_INDEXES` 6000 and
  `MD3_MAX_LODS` 3. `R_LoadMD3` in `code/renderer/tr_model.c` accepts a
  surface of exactly 1,000 vertices or 2,000 triangles, but
  `RB_SurfaceMesh` calls `RB_CheckOverflow` (`code/renderer/tr_surface.c`),
  which raises `ERR_DROP` when one surface has 1,000 vertices or 6,000
  indexes. The working limit is therefore 999 vertices and 1,999 triangles.
  `R_RegisterModel` loads `name_1.md3` and `name_2.md3` only.
- **ioquake3 and Quake3e**: [ioquake/ioq3](https://github.com/ioquake/ioq3)
  (GPL-2.0-or-later, main, reviewed 2026-10-22) and
  [ec-/Quake3e](https://github.com/ec-/Quake3e) (GPL-2.0-or-later, master,
  reviewed 2026-10-22). `R_LoadMD3` and `R_LoadIQM` refuse a surface or mesh
  with `numVerts >= SHADER_MAX_VERTEXES` or `numTriangles * 3 >=
  SHADER_MAX_INDEXES`; `IQM_MAX_JOINTS` is 128 (`code/renderercommon/iqm.h`
  in ioquake3, `code/renderer/iqm.h` in Quake3e).
- **Return to Castle Wolfenstein, single player**:
  [id-Software/RTCW-SP](https://github.com/id-Software/RTCW-SP)
  (GPL-3.0-or-later with id's additional terms, master, reviewed 2026-10-22).
  `src/qcommon/qfiles.h` defines `SHADER_MAX_VERTEXES` 4000,
  `SHADER_MAX_INDEXES` six times that and `MD3_MAX_LODS` 3. `R_LoadMD3` and
  `R_LoadMDC` (`src/renderer/tr_model.c`) refuse more than those, and
  `RB_CheckOverflow` (`src/renderer/tr_surface.c`) drops the game when one
  surface reaches either, so 3,999 vertices and 7,999 triangles are the working
  limits. `R_RegisterModel` tries `name.mdc` before `name.md3` unless
  `r_compressModels` is set.
- **Return to Castle Wolfenstein, multiplayer**:
  [id-Software/RTCW-MP](https://github.com/id-Software/RTCW-MP)
  (GPL-3.0-or-later with id's additional terms, master, reviewed 2026-10-22).
  `src/qcommon/qfiles.h` sets `SHADER_MAX_VERTEXES` back to 1000 (its comment:
  "was 4000, 1000 in q3ta") and `MD3_MAX_LODS` to 1, so no detail level loads.
  The same loader and overflow checks give Quake III's working limits.
- **Wolfenstein: Enemy Territory**:
  [id-Software/Enemy-Territory](https://github.com/id-Software/Enemy-Territory)
  (GPL-3.0-or-later with id's additional terms, master, reviewed 2026-10-22).
  `src/qcommon/qfiles.h` defines `SHADER_MAX_VERTEXES` 1025 ("1024+1"),
  `SHADER_MAX_INDEXES` six times that and `MD3_MAX_LODS` 4; `R_Init`
  (`src/renderer/tr_init.c`) sizes the batch from them. `R_LoadMD3` and
  `R_LoadMDC` refuse more, and `RB_CheckOverflow` drops the game at 1,025
  vertices or 6,150 indexes, so 1,024 vertices and 2,049 triangles are the
  working limits. `R_RegisterModel` loads `name_1.md3` to `name_3.md3`, each
  as an MDC first.
- **GZDoom**: [ZDoom/gzdoom](https://github.com/ZDoom/gzdoom)
  (GPL-3.0-or-later, master, `c26ce2e`, 2026-08-10, reviewed 2026-10-22).
  `FMD3Model::Load` (`src/common/models/models_md3.cpp`) allocates as it reads
  and sets no count limit; `MD3_MAX_SURFACES` (32, `src/common/models/model.h`)
  bounds MODELDEF's `SurfaceSkin` (`src/r_data/models.cpp`). `IQMModel::Load`,
  `LoadBlendIndexes` and `LoadBlendWeights`
  (`src/common/models/models_iqm.cpp`) read four one-byte blend indexes and
  weights per vertex.
- **IQM format**: [lsalzman/iqm](https://github.com/lsalzman/iqm) (MIT,
  master, reviewed 2026-10-22). Blend indexes are written as `IQM_UBYTE`, four
  per vertex, so the writer holds 256 joints and keeps the four strongest
  influences.
- **Doom 3**: [id-Software/DOOM-3](https://github.com/id-Software/DOOM-3)
  (GPL-3.0-or-later, GPL release, reviewed 2026-10-22).
  `idRenderModelMD5::LoadModel` (`neo/renderer/Model_md5.cpp`) and
  `idMD5Anim::LoadAnim` (`neo/game/anim/Anim.cpp`) allocate as they read and
  set no count limits.

## The Preflight

`assessModelTarget` (`src/core/model_targets.h`) checks a model against one
target and returns every issue with a stable limit identifier (`vertices`,
`render-vertices`, `triangles`, `frames`, `skins`, `skin-width`,
`skin-height`, `skin-pixels`, `skin-width-multiple`, `joints`, `weights`,
`tags-per-frame`, `surfaces`), the surface or frame concerned, the value, the
maximum, a message and a fix. It also reports the format's own conditions that
the writers refuse: one surface and no tags for MDL and MD2, an indexed MDL
skin, PCX skin paths for MD2, frame, tag, surface and material names that fit
their fields, UVs inside the tile for the texel formats, MD3 surface names that
Quake III would merge, and MD3 positions outside -512 to 511.984375 units or
triangles that collapse at 1/64-unit precision. For each limit it also records
the largest value measured, so a model that fits still shows its headroom; for
IQM that includes the most joint influences on one vertex (`influences`), which
the writer reduces to four with a note rather than refusing.
Each fix names the command and the sidebar page where it is, by the page's
neutral title: for example **Manage Surfaces…** > **Separate selected faces** on
the **Surface** page, **Mesh** > **Clean Up** > **Decimate**, or **Delete Tag**
under **Attachment tags** on the **Animation** page.

An issue about part of a surface names exactly which part, ascending, in
`faces` or `vertices` (also in the issue's JSON):

| Limit | Names |
| --- | --- |
| `vertices`, `triangles` (per surface) | The faces past the budget: the faces **Separate selected faces** would move so that every part fits (`modelTargetSurfaceParts`) |
| `uv-range` | The vertices whose UVs lie outside the tile |
| `coordinate-range` | The vertices outside MD3's range in some frame |
| `precision` | The triangles that collapse at 1/64 units in some frame |
| `weights` | The vertices with more influences than the target keeps |

Vertex counts are the writer's: an MDL's vertices after sharing exact
duplicates and compatible seams, an MD2's positions after sharing exact
duplicates across every frame, and an MD3 or IQM surface's vertices. A
counts-only assessment (`ModelTargetDepth::Counts`) skips the per-frame scans
and lists what it skipped in `unchecked` (`coordinate-range` and `precision`
for MD3); there an MDL's or MD2's source vertex count stands in as an upper
bound. The editor's header summary runs MD3's full assessment while the model
has at most 500,000 vertex positions across its frames, and counts otherwise;
an upper bound over its limit or an unchecked limit reads "check", which never
blocks. Every export runs the full assessment.

`exportEditableModel` runs the preflight when `ModelExportOptions::target` is
set or the format has a default target. When it finds an issue it writes
nothing, returns an error that lists each issue with its fix, and keeps the
assessment in `ModelExportReport::target`. The writers then read their count
and skin limits from the same target, so a writer and its preflight cannot
disagree.

Detail levels are part of the MD3 target: `ModelTargetLimits::detailLevels` is
the number of `name_N.md3` files the game loads, which caps `model lod`,
**Detail Levels…** and **Export Quake III Detail Levels**. See
[detail levels](MODEL_LOD.md) for how a set is built, checked and staged.

## Prepare For Target

`prepareModelForTarget` (`src/core/model_target_prepare.h`) applies the fixes a
target's preflight suggests to a copy of the model, each through the editor's
own edits (`applyModelEdit`), so skin weights, UV seams and every animation
pose follow as they do by hand, and reports what changed and what still
blocks:

| Fix | When | What it does |
| --- | --- | --- |
| `split` | MD3, IQM and MD5 surfaces over their vertex or triangle budget | Removes unused vertices (a listed change of its own, which may be all a surface needs), then separates the surface into connected parts that each fit (**Separate selected faces**), named `body2`, `body3` and so on so that no two merge after Quake III's renaming; every triangle and pose is kept, and each part wears its origin's material in every [skin variant](MODEL_SKIN_VARIANTS.md). A split that would exceed the surface limit is not made, and the surface is decimated instead. |
| `join` | MDL and MD2, which hold one surface | Joins every surface into the first (**Join surfaces**). Surfaces with other materials take the first surface's, which the review notes: their UVs must lie in the same skin. Skin-variant bindings of the surfaces joined away are removed, with a note naming them. |
| `decimate` | What a split cannot fit, and MDL or MD2 over their triangle, stored or drawn vertex limits | Reduces the surface or model with a triangle target (**Decimate**), measuring the target's own counts after each pass, a few passes at most. This removes detail. |
| `fit` | MD3 positions outside -512 to 511.984375 | Moves each axis the least that brings every pose into range, and scales the model down about its centre only when it is too long to fit; tags, frame origins and collision boxes move with it. A skeletal model is not moved, with a note. The review notes that the origin moved. |
| `influences` | IQM vertices with more than four influences, MD5 meshes over their weight limit | Keeps the strongest influences of each vertex, renormalised (as **Skin Weights and Binding…** > **Reduce and normalise weights**), so the export drops none. |

Limits no edit can meet safely, such as names, skins, frame counts, joints or
1/64-unit precision, stay in the `after` report with their fixes. When the
surfaces change and the source stores hand-made [detail levels](MODEL_LOD.md),
made for the surfaces it had, the review says to rebuild them. The editor
adopts the prepared model as one undo step through `ModelDocument::adopt`, which
refuses a preparation made from another revision of the document; `model
prepare` saves it as a new editable source.

## Saved Targets

A project keeps the target chosen for each format and the strict-export setting
in its manifest, under the top-level key `modelDelivery`, which
`ProjectManifest` keeps as an unknown key so older studios leave it untouched:

```json
"modelDelivery": {"targets": {"md3": "et", "mdl": "quakespasm"}, "strict": true}
```

When no target is chosen for an export (`--target`, or a choice in the editor),
it comes from, in order: the project's saved choice, what the game installation
in use suggests from its executable's name (QuakeSpasm, FTE and DarkPlaces for
MDL; Q2PRO for MD2, MD3 and MD5; ioquake3 and Quake3e for MD3 and IQM; `WolfSP`,
`WolfMP` and `ET` for the Wolfenstein targets; GZDoom for MD3 and IQM), then the
format's default (`src/core/model_delivery.h`). The strict setting comes from
`--strict` or `--no-strict`, else the project's saved setting, else off.

The CLI finds the project from `--project`, else from the nearest folder above
the source with a project manifest; the editor uses the open project. Both then
choose the installation by one rule, `installationModelDeliveryPreferences`,
which is how releases choose it (`releaseInstallationFor`): the installation the
project links, else one of the project's game (the selected one first), else the
selected installation, also with no project open. A saved target this studio no
longer has is ignored with a warning.

A manifest that exists but cannot be read (invalid JSON, say) is never replaced:
`model targets --save` or `--clear` exits 1 naming the file, and **Save Targets
with the Project** shows the same error, so the project's other settings stay as
they were. Model commands in such a project warn and use none of its saved
choices.

## CLI

```sh
vibestudio --cli model targets --format mdl --json
vibestudio --cli model targets ./models/prop.mesh.json --json
vibestudio --cli model targets ./models/prop.mesh.json --format md3 --target ioquake3 --json
vibestudio --cli model prepare ./models/prop.mesh.json --format md3 --output ./models/prop.prepared.mesh.json --json
vibestudio --cli model targets --project ./mymod --save --format md3 --target et --strict --json
vibestudio --cli model targets --project ./mymod --json
vibestudio --cli model build ./models/monster.mesh.json --output ./out/monster.mdl --target quakespasm --json
vibestudio --cli model lod ./models/prop.mesh.json --output ./out/prop.md3 --target ioquake3 --json
```

`model targets` without a source lists the targets (`--format` and `--target`
narrow the list). With a source it checks the model against each listed target
and prints one line per target, `ready` or its blockers, with every issue and
fix below it; JSON has a `reports` array of `format`, `target`, `label`,
`ready`, `issues` (with their `faces` and `vertices`) and `measures`. A survey
of several targets exits 0; with `--target`, a model that is not ready for it
exits 4. Some ids name targets of several formats (`fte` for MDL and IQM,
`ioquake3` for MD3 and IQM, `q2pro` for MD2, MD3 and MD5, `gzdoom` for MD3 and
IQM, `doom3` for both MD5 files): checking a model against such an id needs
`--format`, and exits 2 without it, while listing the targets shows each
format's.

With `--project DIR`, `model targets` shows the target each format's exports
use in that project instead of every target: JSON `delivery` lists each
format's `target`, `targetOrigin` (`explicit`, `project`, `installation` or
`default`), `strict` and `strictOrigin`, and with a source the `reports` check
those targets. `--save --format F --target ID` saves a format's choice, `--save
--strict` or `--save --no-strict` the strict setting, and `--clear [--format
F]` removes one choice or all of them; these take no source and print the saved
`modelDelivery` object.

`model prepare <source> --format F` prepares the model for the format's target
(`--target`, else the project's, else the default) and prints each change, its
notes, and what still blocks the target. `--skip split,join,decimate,fit,influences`
leaves fixes out. `--output name.mesh.json` writes the prepared source (an
existing file needs `--overwrite`); `--dry-run` only reviews. JSON carries
`fixes` (each with `kind`, `limit`, `summary`, `before` and `after`), `notes`,
the `before` and `after` reports, `changed`, `ready`, `targetOrigin` and
`written`. It exits 4 when limits remain, after writing the prepared source
unless `--dry-run`.

`model build --target ID` checks the named target of the output's format
(without it, the project's saved target, else the installation's suggestion,
else the format's default) and exits 4 when the model exceeds a limit, writing
nothing; its JSON then has `targetIssues` and the full `target` report. A target
of another format exits 2. On success the JSON carries the `target` report,
`targetOrigin`, `strict` and `strictOrigin` too; `--no-strict` overrides a
project that saves strict exports. For an MD3 it also lists the detail files
beside the output that the target still loads with the new model (JSON
`leftInPlace`; see
[detail files left in place](MODEL_LOD.md#detail-files-left-in-place)).

## GUI

The Mesh Editor's header button **Delivery targets** shows each format's chosen
target as "ready", "check" or "over", in words and with an icon: MD3, MD2 and
MDL, and for skeletal models IQM and MD5 (the mesh and animation files as one
"MD5" part, showing the worse of the two). Its menu and **Check Delivery
Targets…** list the MD5 mesh and animation targets separately. Its menu lists
each blocker under its format; choosing one selects exactly what it names: the
faces past a surface's budget (in face mode), the vertices outside the tile or
MD3's range (in vertex mode), or the surface (in surface mode), so the fix in
its tooltip can start there. Under a format with blockers, **Prepare for
FORMAT · Target…** opens the review. Then come a **Delivery Target** submenu per
format (MD3, MD2, MDL, and IQM and MD5 mesh and animation for skeletal models)
with the targets as an exclusive choice, its title
saying where the current choice comes from when it was not made in this session
(saved with the project, or suggested by the game installation), **Save Targets
with the Project**, which saves each choice and the strict setting with the
open project, and **Check Delivery Targets…**, which runs the full assessment
on the worker and lists every target, blocker, fix and measured value. Exports,
**Stage in Package** and detail levels use the chosen targets; a refused export
says how many limits block it in the status line and lists each with its fix
under **Export Report**. A choice made in the editor lasts for the session
unless saved; opening a project applies its saved strict setting.

**Prepare for Target** prepares the model on the worker and opens a review:
a check box per kind of fix the format can use (each named and described), and
the changes, their notes and what would still block the target. Clearing a box
prepares again without that kind. **Apply Changes** applies everything listed
as one undo step and selects the changed surfaces; **Cancel** leaves the model
unchanged.

## Not Yet Covered

- MDC is not written, so there are no MDC targets; Return to Castle
  Wolfenstein and Enemy Territory load MD3 directly when no MDC of the same
  name is present.
- GZDoom's MODELDEF is not written, and there is no GZDoom MD2 target: the MD2
  writer requires PCX skin paths, which GZDoom does not.
- Prepare for Target does not rename surfaces, resize skins, cut frames or
  joints, or fix 1/64-unit precision; those stay as blockers with their fixes.
- Targets are saved per project, not per game installation; an installation
  only suggests targets from its executable's name.
- No limit has been confirmed by loading an exported model in the engine.
