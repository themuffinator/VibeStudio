# Model Detail Levels

Quake III and the engines built on it load lower-detail versions of an MD3
beside it, `name_1.md3`, `name_2.md3` and so on, and draw one of them by
distance. An editable source can own its detail levels: how each is made is
stored with the model (the recipe), reviewed before it is used, checked the way
the engine reads the set, exported or staged with the model, and found by the
level dependency review. The user-facing steps are in the
[Models manual page](manual/models.md#detail-levels); the levels each engine
loads are part of its [delivery target](MODEL_TARGETS.md).

> [!WARNING]
> **Status: Partial.** Generated and hand-made levels, the recipe, the
> consistency check, inspection, staging and the switch distances are
> implemented and tested on synthetic models. No set has yet been loaded in an
> engine to confirm the switching, and only MD3 detail levels are covered.

## How Engines Load Them

`R_RegisterModel` (`code/renderer/tr_model.c` in the
[Quake III Arena source](https://github.com/id-Software/Quake-III-Arena),
GPL-2.0-or-later, reviewed 2026-10-22) tries `name_N.md3` for each level from
`MD3_MAX_LODS - 1` down to the base, skipping missing files but counting the
ones it loads, so a missing middle level leaves an empty slot it may later
draw. `R_AddMD3Surfaces` (`tr_mesh.c`) checks the frame against the base model
only and then reads the chosen level's vertices for that frame, so a level with
fewer frames is read past its end. Custom `.skin` files bind surfaces by name
after `R_LoadMD3` lowercases them and drops a final `_` and character, and
`R_LerpTag` reads tags from the base model only.

| Target | Levels loaded |
| --- | --- |
| Quake III Arena, ioquake3 and Quake3e | 2 (`name_1`, `name_2`) |
| Return to Castle Wolfenstein, single player | 2 |
| Return to Castle Wolfenstein, multiplayer | None |
| Wolfenstein: Enemy Territory | 3 (`name_1` to `name_3`) |
| Q2PRO, GZDoom | None |

## The Recipe

`ModelMesh::detailLevels` holds one entry per level, in order
(`src/core/model_mesh.h`, `src/core/model_lod.h`):

- **generated**: the level above it reduced by a quadric decimation, surface
  by surface, keeping a share of its triangles (`ratio`, 0.05 to 0.95) or
  reducing the whole model towards a triangle count (`targetTriangles`, shared
  between surfaces by their triangles). UV seams and every pose are kept; the
  first level also keeps borders unless told otherwise (`preserveBoundary`). A
  surface that cannot be reduced further keeps its detail, with a note.
- **hand-made**: another model file (an editable source, or an MD3, MD2, MDL or
  OBJ imported as one), absolute or relative to the base source's folder.

An editable source with a recipe is schema version 13 and stores it under
`detailLevels`:

```json
"detailLevels": [
  {"level": 1, "mode": "generated", "ratio": 0.5, "preserveBoundary": true},
  {"level": 2, "mode": "source", "source": "prop_low.mesh.json"}
]
```

A recipe has at most three levels (the most any target loads), numbered from 1
without gaps; the delivery target caps how many are built. Storing or changing a
recipe is one document change, with undo, recovery drafts and the source
fingerprint following it like any other edit.

The file stores a hand-made level's path relative to its own folder. An open
source holds the path absolute (`ModelDocument::load`), and every save writes it
relative to the folder it saves into (`ModelDocument::save`), so saving the
source somewhere else, by **Save As** or by any command's `--output`, keeps
each level pointing at its file. A path on another drive stays absolute.

## The Consistency Check

`validateModelLodSet` checks every level against the base model and the MD3
delivery target, and reports each finding as an error, warning or note with a
stable code:

| Code | Severity | Finding |
| --- | --- | --- |
| `frames` | Error | A level's frame count differs from the base's |
| `frame-names` | Warning | A level names frames differently, in order |
| `surface-names` | Error | A level has a surface the base lacks after normalisation, so a `.skin` cannot bind it |
| `surfaces` | Warning | A level drops a base surface, which then disappears at that distance |
| `surfaces` | Error | In an inspection, a level follows a missing one |
| `target` | Error | A level exceeds the target's limits, or cannot be read; a level the target never loads is a warning |
| `triangles` | Warning | A level has no fewer triangles than the one above |
| `tags`, `materials` | Note | A level's tags or shaders differ: the engine reads the base's tags, and a level's own shaders show unless a `.skin` names the surface |
| `stale` | Warning | A detail file stays beside a model written without it, and the game loads it with that model (see below) |
| `mdc` | Error | An MDC stays beside the model, and Return to Castle Wolfenstein or Enemy Territory loads it in place of the MD3 |

Preparing a set refuses one with errors, writing nothing. `modelLodSurfaceTable`
lists each surface's triangles on the base and on every level. Findings and
table columns name each level by its own number, also when a level before it is
missing.

## Inspecting An Existing Set

`inspectModelLodSet` reads `name.md3` and every level the target loads, through
a reader for loose files or package entries, imports each, and runs the
consistency check. Missing levels are listed, not refused; a level after a
missing one is an error; a present level the target never loads is a warning.
Return to Castle Wolfenstein and Enemy Territory try `.mdc` before `.md3` for
the base and for every level (`R_RegisterModel` changes the name's last
character to `c` first), so each `name.mdc` or `name_N.mdc` found is a warning,
and a level present only as an MDC loads, so the levels after it are no gap.
The review checks MD3 files; an MDC level is named for checking in the game.

## Detail Files Left In Place

Writing a base MD3 replaces only the files written, and the game still loads the
detail files beside it with the new model. `reviewModelLodSiblings` lists the
files a write of the base and its first levels leaves in place although the MD3
delivery target loads them: levels past those written, up to the target's
count, and on the MDC-first targets the `.mdc` of the base and of every level.
It checks each MD3 left in place against the new base as the consistency check
does, so a level that no longer has the base's frames or surfaces is an error.
Every way of writing an MD3 runs it and reports what stays:

- `model build` (JSON `leftInPlace`, and its text) and the Mesh Editor's
  **Export** review the folder of the output;
- `model lod`, **Export…** in Detail Levels and **Export Quake III Detail
  Levels…** review the levels past the set they write (JSON `leftInPlace`);
- **Stage in Package** and **Stage and Place** review the package's entries
  beside the staged path (matched without case), and say in the status line and
  the export report what stays and whether the game would read it wrongly.

`model build` and **Export** also note when the source stores detail levels they
do not write. The fix is to write the detail levels again with the model, or to
remove the files named.

## Switch Distances

`modelQuake3LodSwitchDistances` reproduces `R_ComputeLOD` and `ProjectRadius`
(`code/renderer/tr_mesh.c`) with `R_SetupProjection`'s matrix
(`code/renderer/tr_main.c`) and `CG_CalcFov`'s vertical field of view. With `n`
models loaded (the base and its levels), a model of radius `r` (`RadiusFromBounds`
of the base's frame bounds) shows level `k` from the view depth

```text
d(k) = r * r_lodscale / (tan(fov_y / 2) * (1 - k / n)),   tan(fov_y / 2) = tan(fov_x / 2) / aspect
```

shifted by `r_lodbias` levels and clamped as the engine clamps them. The
defaults are `cg_fov 90` at 4:3, `r_lodscale 5` and `r_lodbias 0`. The level
is truncated towards zero, as ioquake3's float-to-integer conversion does; an
engine build whose conversion rounds to nearest shows each level somewhat
sooner. Depth is measured along the view direction, not as a straight-line
distance.

## In The Editor

**Mesh > Clean Up > Detail Levels…** opens a review of the source's levels:

1. **Detail levels** sets how many the source owns, up to what the MD3
   delivery target loads. Each level is **Generated: keep a share of the
   triangles**, **Generated: reduce to a triangle count**, or **Hand-made
   model** with its file and **Browse…**.
2. **Review** builds every level on the worker and lists each surface's
   triangles per level, the consistency findings, any surface that could not
   be reduced further, and where each level shows. **Show** displays a
   reviewed level in the viewport in place of the model, changing nothing.
3. **Store in Source** keeps the reviewed recipe in the source as one undo step;
   with no levels it removes a stored recipe. **Export…** writes the base MD3
   and each reviewed level. Both need a review of the current settings without
   errors; changing a setting asks for a new review.

**Export Quake III Detail Levels…** writes the stored levels, or two halvings
when the source stores none. **Stage in Package** stages an MD3 together with
its stored levels as one package operation, so one undo removes them all;
**Stage and Place** places the base model. Staging takes as many stored levels
as the MD3 delivery target loads: on a target that loads fewer, or none (Return
to Castle Wolfenstein multiplayer, Q2PRO, GZDoom), the rest stay in the source
and the export report says so, and the model is still staged. Every file is
checked against the delivery target and the strict-export setting before any is
staged or written.

## Level Dependencies

The level dependency review (`inspectLevelDependencies`) adds each
`name_N.md3` the package holds beside a resolved MD3 as a `model-lod` row,
resolved, with its own materials reviewed, so a release of the map ships the
levels with the model. Levels the package lacks are not problems: the engine
skips them. A level whose frames or surfaces do not match its model gets a note
and a report warning, since the game would read it wrongly.

## CLI

```sh
vibestudio --cli model lod ./models/prop.mesh.json --output ./out/prop.md3 --levels 2 --ratio 0.5 --json
vibestudio --cli model lod ./models/prop.mesh.json --target-triangles 900,300 --save-recipe --json
vibestudio --cli model lod ./models/prop.mesh.json --level-source 2=./models/prop_low.mesh.json --output ./out/prop.md3 --json
vibestudio --cli model lod ./models/prop.mesh.json --output ./out/prop.md3 --json
vibestudio --cli model lod --inspect ./baseq3/models/mapobjects/lamp.md3 --json
vibestudio --cli model lod --inspect --package ./mymod.pk3 --entry models/mapobjects/lamp.md3 --target et --json
```

`model lod` builds the levels `--levels` (default 2, or fewer when the target
loads fewer) and `--ratio` describe; `--target-triangles N,N` gives each level
a triangle count instead, and `--level-source LEVEL=PATH[,LEVEL=PATH]` makes
levels hand-made (paths relative to the current folder, stored relative to the
source's). A source that stores a recipe builds it when none of these options
is given; `--use-recipe` insists, and exits 4 when there is none.
`--save-recipe` stores the recipe described in the editable source (with or
without `--output`). JSON adds `recipe`, `recipeFromSource`, `recipeSaved`,
`findings`, `targetOrigin` and `strictOrigin` to the set report. A set with
consistency errors exits 4, writing nothing.

`model lod --inspect BASE.md3` inspects an existing set beside a loose base;
with `--package P --entry E` it reads the set from a folder, PAK, PK3 or ZIP
(entries matched without case). JSON lists `files` (each `level`, `path`,
`present`, and `frames`, `surfaces` and `triangles` or an `error`), the
per-surface `surfaces` table, `switchDistances`, `levelsPresent`, `ready` and
the `findings`; it exits 4 when there is an error. In text mode a failed
inspection prints its findings before the refusal.

Writing a set also lists the detail files it leaves in place (JSON `leftInPlace`:
`files`, `findings` and `consistent`), as `model build` does for an MD3.

## Not Yet Covered

- Only MD3 levels are built; MDC, MDS and Ghoul 2 levels are not. Player
  packages build generated or stored levels
  ([Native Player Packages](MODEL_ASSEMBLY.md#native-player-packages)), but a
  package part cannot use hand-made levels.
- Existing sets are inspected from the CLI (`model lod --inspect`); the editor
  reviews the files beside an export or staging, but has no inspection view of
  its own. MDC levels are found, not decoded.
- The preview shows one level at a time in the viewport; switching by distance
  is computed, not shown.
- No set has been loaded in an engine to confirm `r_lodbias` switching.
