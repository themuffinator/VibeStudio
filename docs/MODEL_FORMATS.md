# Native Model Formats And Skeletons

This record covers how VibeStudio reads and writes the model formats of the
idTech 1 to idTech 4 game families, and how skeletal models are held, previewed,
edited and exported. The user-facing summary is in the
[Models manual page](manual/models.md); per-format status is in the
[support matrix](SUPPORT_MATRIX.md).

> [!WARNING]
> **Status: Partial.** Every decoder below was written from public source code
> or specifications and is tested against synthetic fixtures built in the
> tests. None has yet been checked against a corpus of real game files, and no
> exported MD5, IQM or ASE model has been loaded in the original engines. Treat
> the output as unverified until it has been.

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
| Quake III MD3 | `.md3` | `IDP3`, version 15 | No | Surfaces, shaders, every frame and tag | Yes, with `name_1.md3` and `name_2.md3` detail levels |
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
hold at most 1,024 frames, so importing a longer skeletal model re-bakes it to
fit; the skeleton itself keeps every clip.

### Editing skeletal models

The frame that holds the bind pose is the one to edit: the baked `bindpose`
frame, or frame 0 when the first clip frame matches the bind pose (Ghoul 2,
MDR and animation-driven MDS bake no separate bind frame). When an edit moves
vertices there, only those vertices are re-bound to their joints; edits in
other frames are not re-bound. Topology
edits keep skinning in step: a new vertex takes the weights of the nearest
original vertex. Saving writes the skeleton into the `.mesh.json` source as
version 8 (see [editable meshes](MODEL_MESH.md#sources-import-and-export)).

Joints, weights and clips cannot be edited yet: there is no bone overlay, no
weight painting and no pose mode. The **Skeleton** sidebar page lists the joint
hierarchy and exports the model.

## Export

| Format | What is written |
| --- | --- |
| MD5 mesh | Joints from the skeleton's bind pose, meshes per surface, weights. A model with no skeleton binds every vertex to one `origin` joint. |
| MD5 animation | One clip: hierarchy, base frame, bounds and frames, at the clip's rate. A model that animates by frames has no clip and is refused. |
| IQM | Meshes, joints, weights and every clip as an animation. A model with no skeleton is written as a static mesh from its first frame, with a note. |
| ASE | One static frame, with a material per surface. |

Skeletal exports pose the edited bind frame: vertices moved there are re-bound
first, and untouched vertices keep their original offsets. Edits to other
frames do not reach MD5 or IQM, whose animation comes from the joints. The existing MDL,
MD2, MD3 and OBJ exports work on the baked frames, so a skeletal model can
also be exported as a vertex-animated MD3.

Exports are available from the Mesh Editor's export menu, the **Skeleton**
page, and `model build`:

```sh
vibestudio --cli model build ./monster.mesh.json --output ./out/monster.md5mesh --dry-run --json
vibestudio --cli model build ./monster.mesh.json --output ./out/walk.md5anim --frame 2 --json
vibestudio --cli model build ./monster.mesh.json --output ./out/monster.iqm --json
```

For `.md5anim`, `--frame` picks the clip by its zero-based index.

## Known Gaps

- No decoder has been run against real game files in bulk; malformed but
  loadable files may still be refused, and some valid variants may not be
  recognised.
- Writers exist for MD5, IQM and ASE only among the new formats. MDC, MDS,
  MDM/MDX, MDR, Ghoul 2, Half-Life MDL, Hexen II MDL, FM, LWO and KVX are read
  only.
- Levels of detail beyond the first are not read for MDS, MDM, MDR and
  Ghoul 2; MDS and MDM collapse maps are not applied.
- Game-side animation set-up is not read: Jedi Outcast and Jedi Academy
  `animation.cfg`, Return to Castle Wolfenstein and Enemy Territory animation
  scripts, and GZDoom MODELDEF and VOXELDEF lumps. Clips take their names from
  the files themselves.
- Torso blending (MDS, MDM) and Ghoul 2 bone overrides are run-time effects and
  are not previewed.
- Joint editing, weight painting and pose mode are planned.
