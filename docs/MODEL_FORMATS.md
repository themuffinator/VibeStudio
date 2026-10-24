# Native Model Formats And Skeletons

This record covers how VibeStudio reads and writes the model formats of the
idTech 1 to idTech 4 game families, and how skeletal models are held, previewed,
edited and exported. The user-facing summary is in the
[Models manual page](manual/models.md); per-format status is in the
[support matrix](SUPPORT_MATRIX.md).

> [!WARNING]
> **Status: Partial.** Every decoder below was written from public source code
> or specifications and is tested against synthetic fixtures built in the
> tests. None has yet been checked against a corpus of real game files. Exported
> MD5, IQM and ASE files are checked by an independent reader written from the
> engines' published source, but none has been loaded in the original engines.
> Treat the output as unverified until it has been.

## Coverage By Engine Family

| Family | Games | Read | Write |
| --- | --- | --- | --- |
| idTech 1 (Doom source ports) | GZDoom, Zandronum, Eternity, ZDoom | KVX voxels (VOXELDEF), MD2 and MD3 (MODELDEF) | MD2, MD3 |
| idTech 2 | Quake, Hexen II, Quake II, Heretic II, Half-Life (GoldSrc), Darkplaces, FTEQW | Quake MDL, Hexen II MDL, MD2, Heretic II FM, Half-Life MDL, MD3, IQM | MDL, MD2, MD3, IQM |
| idTech 3 | Quake III Arena, Team Arena, Return to Castle Wolfenstein, Wolfenstein: Enemy Territory, Elite Force, Jedi Outcast, Jedi Academy, Soldier of Fortune II, ioquake3 games | MD3, MDC, MDS, MDM with MDX, MDR, Ghoul 2 GLM with GLA, IQM; ASE and LWO for q3map2 `misc_model` | MD3, IQM, ASE |
| idTech 4 | Doom 3, Resurrection of Evil, Quake 4, Prey, Enemy Territory: Quake Wars, The Dark Mod | MD5 mesh and animation (with `.def` model declarations), LWO, ASE | MD5 mesh, MD5 animation, ASE |
| Interchange | Any modeller | Wavefront OBJ | OBJ (one frame) |

`vibestudio --cli model formats` prints the same catalogue, with each format's
engines, games, companion files and notes; `--json` adds the export format
identifiers.

Doom's own sprites and Quake's sprite files are handled by the
[texture editor](TEXTURE_EDITOR.md), not the modeller.

## Formats

| Format | Extension | Identified by | Skeletal | Read | Write |
| --- | --- | --- | --- | --- | --- |
| Quake MDL | `.mdl` | `IDPO`, version 6 | No | Every pose, frame groups and timing, indexed skins and skin groups, header fields | Yes |
| Hexen II MDL | `.mdl` | `RAPO`, version 50 | No | Poses, frame groups, skins, per-corner texture coordinates, model flags | No |
| Quake II MD2 | `.md2` | `IDP2`, version 8 | No | Every pose, ordered skin names, skin size | Yes |
| Heretic II FM | `.fm` | `header` chunk | No | Frames, skins, mesh nodes as surfaces | No |
| Quake III MD3 | `.md3` | `IDP3`, version 15 | No | Surfaces, shaders, every frame and tag | Yes, with the [detail levels](MODEL_LOD.md) its delivery target loads (`name_1.md3`, `name_2.md3`) |
| MDC | `.mdc` | `IDPC`, version 2 | No | Base and compressed frames, tags, shaders | No (export MD3, which both games load) |
| MDS | `.mds` | `MDSW`, version 4 | Yes | Bones, weights, frames, tags | No |
| MDM | `.mdm` | `MDMW`, version 3 | Yes | Surfaces and weights, skinned against the MDX | No |
| MDX | `.mdx` | `MDXW`, version 2 | Yes, animation only | Bones and frames | No |
| MDR | `.mdr` | `RDM5`, version 2 | Yes | Per-frame bone matrices, the first level of detail, tags | No |
| Ghoul 2 GLM | `.glm` | `2LGM`, version 6 | Yes | Surfaces, the surface hierarchy, bolts as tags, weights against the GLA | No |
| Ghoul 2 GLA | `.gla` | `2LGA`, version 6 | Yes, animation only | Skeleton and frames | No |
| Inter-Quake Model | `.iqm` | `INTERQUAKEMODEL`, version 2 | Yes | Meshes, joints, weights, poses, animations; static meshes too | Yes |
| MD5 mesh | `.md5mesh` | `MD5Version 10` | Yes | Joints, meshes, weights, shaders | Yes |
| MD5 animation | `.md5anim` | `MD5Version 10` with `numFrames` | Yes, animation only | Hierarchy, base frame, bounds, frames | Yes |
| LightWave object | `.lwo` | `FORM` with `LWO2`, `LWOB` or `LWLO` | No | The first layer: points, polygons, UV maps, surface names as materials | No |
| ASCII Scene Export | `.ase` | `*3DSMAX_ASCIIEXPORT` | No | Objects, materials, mapping; the first mesh of each object | Yes (one frame) |
| Half-Life MDL | `.mdl` | `IDST` or `IDSQ`, version 10 | Yes | Bones, sequences, body parts, embedded textures, attachments | No |
| KVX voxels | `.kvx` | Extension, then layout checks | No | The first mip level, meshed into coloured faces | No |
| Wavefront OBJ | `.obj` | Extension | No | Polygons, UVs, normals, smoothing groups, `usemtl` paths | Yes (one frame) |

Detection reads the file's magic first, then a leading text token for the text
formats, and falls back to the extension. Three formats share `.mdl`; the magic
tells Quake (`IDPO`), Hexen II (`RAPO`) and Half-Life (`IDST`, `IDSQ`) apart. A
file that only has a header, or that declares surfaces it does not hold, is
refused with a named error; no decoder invents geometry.

## Decoder Contract

Every decoder lives in its own `src/core/model_format_*.cpp` file behind the
private interface in `src/core/model_formats_p.h`, and is reached only through
`decodeModelMesh`. Each one:

- bounds-checks every count, offset and size before reading, and treats data
  past the game's own limits as damage;
- outputs counter-clockwise front faces in the game's Z-up units, converting
  each format's convention on the way in and back again on the way out;
- reads companion files only through the shared budget (below);
- reports what it left out as warnings or detail lines, translated in the
  `VibeStudioModelMesh` context;
- names, in its header comment, the public source each layout and convention
  comes from. The [credits](CREDITS.md#native-model-formats-and-modeller-profiles-2026-10-08)
  list them with licences and revisions.

Conversions worth knowing about:

| Format | Conversion |
| --- | --- |
| MDL, MD2, MD3, MDC, FM, Hexen II, Half-Life, MD5, IQM, MDR, MDS, MDM, Ghoul 2 | Clockwise fronts in the file are reversed to counter-clockwise. |
| LWO | LightWave is Y-up: game (x, y, z) = LightWave (x, z, y), and V is inverted. |
| ASE | Positions are used as written; V is inverted. Sub-materials split into surfaces as q3map2 does, with a warning that Doom 3 draws the object with one material. |
| Ghoul 2 | The quarter turn about +Z that the game's root matrix applies is applied to the bind pose, frames and vertices, so the model faces +X as in the game. |
| MD5 | Joint quaternions rebuild w as -sqrt(1 - x² - y² - z²), which matches Doom 3's transposed rotation; the writers encode the same way. |
| KVX | Voxels are placed the way GZDoom's VOXELDEF places them, including its extra quarter turn, one voxel per map unit. The vertical pixel stretch GZDoom applies at draw time is not part of the model and is not applied. |
| Half-Life | Bone angles compose as yaw (Z) × pitch (Y) × roll (X); texture coordinates have no half-texel offset. |

## Companion Files

Some formats keep part of a model in other files. The decoder reads them from
the same package, or from the folders around a loose file:

| Model | Companions |
| --- | --- |
| MDM | The MDX it names, for bones and frames. |
| Ghoul 2 GLM | The GLA it names, for the skeleton and frames. |
| MD5 mesh | Every `.md5anim` beside it, and the animations named by any Doom 3 `.def` `model` declaration that uses the mesh, under their declared names. |
| Half-Life MDL | `<name>T.mdl` for textures and `<name>01.mdl` onwards for sequence groups. |

Inside a package, companions are looked up case-insensitively in the same
archive. For a loose file, a relative companion path is tried in the model's
folder and then in each folder above it, so `models/players/...` resolves from
the game's base folder. Reading stops at 128 companion files or 256 MiB in
total; a model whose companions are missing still opens, with a warning naming
what was not found. Animation-only files (MDX, GLA, `.md5anim`) open on their
own as a skeleton with frames and no geometry.

## Skeletons

A skeletal model holds its skeleton beside the ordinary frames
(`ModelMesh::skeleton`, `src/core/model_skeleton.h`):

- **Joints** have a name, a parent and a model-space bind matrix (3×4, row
  major, column vectors).
- **Skinning** is per surface: each vertex has up to the format's number of
  influences, each a joint, a weight, and an offset (and normal offset) in that
  joint's bind space. Weights must be finite; decoders that derive offsets
  from bind-pose vertices (IQM, Ghoul 2) also scale each vertex's weights to
  sum to one.
- **Clips** hold one model-space matrix per joint per frame, with a name and a
  rate where the format records them.
- **Skeletal tags** attach to a joint with an offset, and bake into ordinary
  tags.

`bakeModelSkeleton` poses every clip into ordinary frames, animations and tags,
so the preview, editor, MD3 export and every other frame-based tool work on
skeletal models unchanged. Baking stops at 8,192 frames or 4,194,304 vertex
slots, keeping whole clips and naming the ones it left out. Editable sources
hold at most 1,024 frames and 1,048,576 frame vertices, so importing a skeletal
model over either limit re-bakes it into a
[working set](MODEL_SKELETAL_WORKING_SET.md): the clips that fit three quarters
of the frame-vertex budget, in file order. The skeleton itself keeps every clip,
the import reports what it baked and what it left out, and `model import
--clips` chooses the clips instead.

### Editing skeletal models

The frame that holds the bind pose is the one to edit: the baked `bindpose`
frame, or frame 0 when the first clip frame matches the bind pose (Ghoul 2,
MDR and animation-driven MDS bake no separate bind frame). When an edit moves
vertices there, only those vertices are re-bound to their joints; edits in
other frames are not re-bound. Topology,
UV, surface and frame edits keep skinning in step from each vertex's recorded
origin, never from the nearest vertex: kept vertices keep their influences
exactly, interpolated vertices blend their sources' influences, moved copies
keep their source's joints and are rebound where they moved, merged vertices
take an equal blend, mirrored vertices go through a joint mirror map, and new
primitives follow the binding chosen for them. While a surface is skinned,
frame edits are refused on the bind frame and clip frames, so the clip mapping
survives (see
[skin weights through mesh edits](MODEL_SKIN_PROVENANCE.md) and the
[tool-by-tool table](MODEL_SKINNING.md#weights-through-other-tools)). Saving writes the skeleton into the `.mesh.json` source as
version 8, version 9 when editable animation curves are present, or version 10
when hold-next curves are needed for exact step reversal (see
[editable meshes](MODEL_MESH.md#sources-import-and-export)).

The **Skeleton** sidebar lists the hierarchy, exports the model and opens
**Edit Skeleton** for joint creation, renaming, reparenting, unused-leaf deletion
and numeric vertex weights. The [authoring contract](MODEL_RIG.md) protects
independent frame edits and explains imported-binding limitations.
**Edit Skeletal Animation** and `model pose` author bind transforms, dense joint
samples and clips, keeping the preview and native exports aligned.
**Working Set…** and `model pose --operation working-set` choose which clips
are baked into editable frames; see [the working set](MODEL_SKELETAL_WORKING_SET.md). Bone overlays,
named joint selection and shared viewport handles edit the same poses.
[Staged weight painting](MODEL_WEIGHT_PAINT.md), [mirrored/spatial transfer](MODEL_WEIGHT_TRANSFER.md),
[two-bone IK](MODEL_IK.md) and [animation curves](MODEL_ANIMATION_CURVES.md) are available.

**Skin Weights and Binding…** and `model skinning` add influence diagnostics,
component selection, rigid/distance binding, explicit rebinding, reduction and
unbinding. They preserve the retained bind shape and protect independent frame
edits. Native exports consume the same weights and baked poses; exporter limits
still apply. See [Skin weights and binding](MODEL_SKINNING.md).

## Export

| Format | What is written |
| --- | --- |
| MD5 mesh | Joints from the skeleton's bind pose, meshes per surface, weights and texture coordinates. A model with no skeleton binds every vertex to one `origin` joint. |
| MD5 animation | One clip: hierarchy, base frame, bounds and frames, at a whole frame rate. A model that animates by frames has no clip and is refused. |
| IQM | Meshes, bind-pose normals, joints, up to four weights per vertex, and every clip as an animation with its rate and loop flag. A model with no skeleton is written as a static mesh from its first frame. |
| ASE | One static frame, with vertex normals and a material per surface. |

Skeletal exports pose the edited bind frame: vertices moved there are re-bound
first, and untouched vertices keep their original offsets. The existing MDL,
MD2, MD3 and OBJ exports work on the baked frames, so a skeletal model can
also be exported as a vertex-animated MD3.

### Delivery targets

Before writing, every MDL, MD2, MD3, IQM and MD5 export checks the model against
a delivery target: the engine or source port it is for, such as the original
Quake or QuakeSpasm for MDL, Q2PRO for MD2, or ioquake3 for MD3 and IQM. Each
format defaults to its original engine (IQM to the format's own limits). The
check reports every limit the model exceeds, with the surface concerned and a
fix, and writes nothing until it fits; the writers then read their limits from
the same target. Quake III's working limit is 999 vertices and 1,999 triangles
per surface, one below the counts its loader accepts, because the renderer drops
the game when a surface reaches 1,000 or 2,000. The targets, their limits and
their sources are listed in [delivery targets](MODEL_TARGETS.md).

### What each export keeps, changes or leaves out

Every export reports what its format cannot hold. Notes about data that is left
out or changed (omissions) are counted separately: the Mesh Editor shows the
most important one in its status line and every note under **Export Report…**,
and `model build` lists them in `exportOmissions`. `--strict` refuses an export
with any omission and writes nothing; in the Mesh Editor, **Refuse exports that
leave data out** on the **Export** page does the same for every export, stage
and detail set, and keeps the refusal's notes under **Export Report…**.

- **Joint scale and mirroring.** MD5 joints hold a rotation and a translation
  only. The md5mesh moves each joint's bind scale, shear or mirroring into its
  weight offsets, so the bind-pose mesh is unchanged, and names those joints.
  An md5anim can then follow only poses that keep each joint's bind scale: a
  pose in which a joint that moves vertices is scaled, sheared or mirrored
  differently is refused, naming the joint, clip and frame. Remove the clip's
  scale keys, move the scale into the bind pose, or export IQM (which keeps
  joint scale) or MD3. Rigid rigs export byte for byte as before.
- **Frame rate.** An md5anim stores a whole number of frames per second from 1
  to 100,000. A clip with no rate is written at 24, with a note. A fractional
  rate is refused unless an MD5 frame rate is chosen (in the MD5 animation
  chooser, or `--md5-frame-rate N`); the note then says the playback speed
  changes. IQM stores each clip's rate as it is.
- **Influences.** IQM keeps the four strongest influences of each vertex and
  rescales them to sum to one. The report counts the vertices that lost
  influences and the largest share of a vertex's weight they carried, as an
  omission; **Skin Weights and Binding…** > **Reduce and normalise weights**
  with a limit of 4 shows the result before export. MD5 holds up to 64
  influences per vertex and refuses more.
- **Precision.** IQM stores blend weights in steps of 1/255 and each joint pose
  channel in 16-bit steps between its smallest and largest value. The report
  gives the largest weight and joint channel changes, and how far the stored
  weights and channels move skinned vertices over the clips
  (`maxPositionError`). That movement includes the format's own rounding, which
  grows with the distance between a vertex's influences, so it is a note. The
  same rebuild with exact weights and channels shows what IQM's layout loses
  before any rounding (`maxLayoutError`): one bind position and four influences
  per vertex, and no joint shear. Movement there beyond MD3's 1/64-unit
  precision is an omission; it usually comes from influences whose offsets
  disagree, as MD5 weights may, and rebinding those vertices fixes it. MD5
  writes numbers with ten decimals.
- **Normals.** MD5 stores no normals: Doom 3 rebuilds smooth normals from the
  faces, joining vertices that share a position. The md5mesh report gives the
  largest difference from the model's normals when it is over half a degree.
  IQM and ASE write the model's normals.
- **Surfaces without influences.** IQM gives them zero blend weights, so they
  keep their bind position in every pose, as in the editor. The md5mesh binds
  them to the first joint; an md5anim whose clip moves that joint reports that
  they follow it in the game.
- **Frame edits.** MD5 and IQM animate through joints. Clip frames edited apart
  from the skeleton (or a bind-pose edit the clips do not show yet), frames
  added or rearranged with frame edits, and frame animations renamed or retimed
  apart from their clip are reported. Export MD3 to keep them. Adding or
  removing a frame leaves no baked frame mapped to a clip: every md5anim, and
  every clip of an all-clips batch, then lists the frames no clip owns and says
  it cannot check its clip's frames, so `--strict` refuses it. An md5anim
  matches a frame animation to its clip by name as well as by its frames.
- **Attachments, collision and materials.** MD5, IQM and ASE have no attachment
  tags; Doom 3 attaches entities to joints by name. Collision boxes, MDL
  settings and embedded skins stay in the editable source. Only the first
  material slot of each surface is written, and a surface without a material is
  named after itself in MD5 and ASE.
- **ASE** writes one frame and no joints, and leaves out a surface the frame
  holds no drawable geometry for, with a note.
- **Clips, origins and the skeleton in MDL, MD2, MD3 and OBJ.** These formats
  have no clip table: readers rebuild clips from the frame names (`stand1`,
  `stand2` and so on). Clips that come back that way are kept, so an imported
  model exports strictly again; renamed or regrouped clips and clip rates are
  omissions. MDL, MD2 and OBJ store no frame origins and readers place each at
  the centre of the frame's bounds, so only an origin elsewhere is an omission;
  MD3 keeps origins. All four store vertex positions only, so a skeleton's
  joints, weights and skeletal clips are an omission. OBJ writes one frame:
  other frames are an omission, and a one-frame model loses nothing.

### Choosing MD5 animation clips

Doom 3, Quake 4 and Prey use one md5anim per action. **MD5 Animation of Clip…**
opens a chooser with the clip (starting at the one the current frame belongs
to) and the frame rate, and reviews the result before the destination is
chosen. **MD5 Animations of All Clips…** writes every clip into a folder as
`<clip>.md5anim`, and asks before replacing files. Both are in the **Model**
menu's **Export Other Format**; the **Skeleton** page offers them as **Export MD5
Animation of Clip…** and **Export All Clips as MD5…**.

```sh
vibestudio --cli model build ./monster.mesh.json --output ./out/monster.md5mesh --dry-run --json
vibestudio --cli model build ./monster.mesh.json --output ./out/walk.md5anim --clip walk --json
vibestudio --cli model build ./monster.mesh.json --all-clips --output-dir ./out/anims --json
vibestudio --cli model build ./monster.mesh.json --output ./out/monster.iqm --strict --json
```

`--clip` takes a zero-based index or a clip name. `--frame N` selects the clip
that frame belongs to, as the editor does; with several clips and neither
option, `model build` exits 2 and lists them. `--all-clips` refuses clip names
that would share a file, and checks every destination before writing any;
existing files need `--overwrite`. The JSON result names the clip written
(`clip`: index, name, frames, `frameRate`), and for IQM the influence and
quantisation figures. Each clip's first note describes the file
(`Skeletal clip 1 "wave": 4 frames at 30 frames per second`), so it reads the
same in dry runs and refusals. With `--strict`, a batch prepares every clip
before refusing; its JSON lists each clip's `exportOmissions` and whether it
was `refused`, every omission once at the top level, and `sharedNotes`, the
notes every clip shares, which the text output and **Export Report…** list
once.

**Stage in Package** with a `.md5anim` package path opens the same chooser,
starting at the clip the path names (`walk.md5anim`) or else the clip of the
current frame.

## Known Gaps

- No decoder has been run against real game files in bulk; malformed but
  loadable files may still be refused, and some valid variants may not be
  recognised.
- Writers exist for MD5, IQM and ASE only among the new formats. MDC, MDS,
  MDM/MDX, MDR, Ghoul 2, Half-Life MDL, Hexen II MDL, FM, LWO and KVX are read
  only.
- The MD5, IQM and ASE writers are checked through VibeStudio's own decoders,
  recorded output and an independent oracle
  (`src/tests/model_skeletal_writer_oracle.py`, see
  [engine acceptance](MODEL_ENGINE_ACCEPTANCE.md#independent-md5-iqm-and-ase-reader)),
  not yet in Doom 3, Quake 4 or ioquake3. Doom 3 `.def` declarations for
  exported animations are not written.
- Levels of detail beyond the first are not read for MDS, MDM, MDR and
  Ghoul 2; MDS and MDM collapse maps are not applied.
- Game-side animation set-up is not read: Jedi Outcast and Jedi Academy
  `animation.cfg`, Return to Castle Wolfenstein and Enemy Territory animation
  scripts, and GZDoom MODELDEF and VOXELDEF lumps. Clips take their names from
  the files themselves.
- Torso blending (MDS, MDM) and Ghoul 2 bone overrides are run-time effects and
  are not previewed.
- Joint hierarchy, numeric weights, dense skeletal clips and shared viewport
  poses are available, together with influence inspection, explicit binding and
  weight repair. Staged weight painting, mirrored/spatial transfer, two-bone IK
  and animation curves are available; broader animation and release gates remain open.
