# tools/asset_registry

Asset registry (`qmg.asset_registry` v1) and texture admission checks for plan section 9.2 (X2). Python 3.10+,
standard library only, always run with `python -B`. Each tool runs as its own process and reads local game data
read-only.

| File | Purpose |
|---|---|
| `registry.py` | Reads an extracted game tree (or a user-supplied Q1 WAD2) and writes registry JSON |
| `admission.py` | Library and CLI: `(game, name, role)` → accept or reject, each rejection with a reason code |
| `controls.json` | The recorded control list: the plan's negative controls plus verified stock controls |
| `test_asset_admission.py` | CTest `asset_admission` |
| `scan_data_bytes.py` | CTest `data_no_asset_bytes` |

## Decision D9 is pending

Tracking retail texture and shader metadata is decision D9(1), and it is not yet approved. Until it is:

- `registry.py` writes only under `out/assets/` (the default is `out/assets/<game>/registry.json`). It refuses any
  other `--out` path; `--stdout` writes no file.
- No registry is tracked. Only the sha256 values below are recorded here.
- The CTest builds its registries in memory, through `registry.py --stdout`, and writes none to disk.

## What a registry holds, and what it never holds

It holds names, relative source paths, sha256, byte sizes, image dimensions and header fields. Every entry also
carries `origin` and `licence_status`. It never holds pixels, lumps, geometry or any other asset bytes;
`data_no_asset_bytes` run over `out/assets` finds none.

- **q2** (`--root` = an extracted baseq2 tree):
  - `textures/**/*.wal`: width, height, internal name, animname, flags with SURF_* names, contents with
    CONTENTS_* names, and value. Unknown bits are kept as hex.
  - Replacement images under `textures/`: format and dimensions. Quake II 3.19 loads only `.wal`
    (`ref_gl/gl_model.c:470`), so a name that has only an image is rejected (`no_wal`).
  - `env/` sky sets, with pcx and tga completeness.
- **q3** (`--root` = an extracted baseq3 tree):
  - Every `scripts/*.shader` definition: name, file:line, surfaceparms, q3map_surfacelight, skyparms,
    q3map_lightimage, qer_editorimage, deformVertexes, fogparms, the raw directives, and a summary of each stage
    (maps, blendFunc, tcGen, tcMods, rgbGen, alphaFunc).
  - Duplicate definitions are all kept. The first one in file order is the one the 1.32 renderer uses.
  - Every image in the tree, with format, dimensions, and TGA pixel depth and alpha bits.
- **q1** (`--q1-wad PATH`): the miptex lumps of a user-supplied WAD2, with the sha256 of each lump and of the
  WAD. No WAD has been supplied yet, so no Q1 registry exists.

A `.pak` or `.pk3` in the root is refused, so a registry is never silently partial. Output is deterministic:
sorted walks, relative paths, sorted keys and LF line endings. Two processes produce byte-identical output. Bit
names cite `Quake-2-master/qcommon/qfiles.h:333-376` and `Quake-III-Arena-master/code/bspc/q2files.h:381-382`
(SURF_HINT, SURF_SKIP). Surfaceparms follow `q3map/shaders.c:69-112`.

## Recorded registries (local, untracked)

Recorded on 2026-10-15 by `registry.py` with sha256
`8bf4fa455f68e8f1848abecfaf3e04aace7b6fe882c3d8aac4db9b1ad5d75fbe`. A registry's hash depends on the tool hash and
on the source tree, so editing `registry.py` changes both values below.

| Registry | sha256 | Source tree | Source content sha256 (files hashed) |
|---|---|---|---|
| `out/assets/q2/registry.json` | `6ce7ba0e7395ad4774218d5bfb7168f04a10d28bddc8afce2092cd6c5f31efc4` | `E:/_SOURCE/_ASSETS/Q2` | `f19ddaa9eaafcce9d222e81c25fce223d53a97242c81c33b8f3aa6b76c290ac7` (2575) |
| `out/assets/q3/registry.json` | `18b11338beaeba4399e023afacd1978ac58e38a3b57e511606fad124902d004b` | `E:/_SOURCE/_ASSETS/Q3A` | `0446ef27a4177e5fd0a65fdf2169e38ba601d9c726a55cfb53b6ab9699b88a25` (2076) |
| `out/assets/q1/registry.json` | none; no Q1 WAD has been supplied | — | — |

Counts:
- **q2:** 2195 texture names. 267 have a WAL (all headers valid; 2 WAL internal names differ from their path).
  1928 have only a JPEG. There are 10 sky sets.
- **q3:** 30 shader files and 1439 definitions under 1434 names. 5 names are defined twice, each pair in the same
  file. There are 0 parse errors and 2046 images. 527 definitions have q3map_surfacelight and 49 have skyparms.

What the local trees contain (file dates, observed, not stored in the registry):
- **Q2:** the WAL, PCX and TGA files are dated 2023. The JPEGs are dated 2011 and come from a replacement set
  whose origin is not established.
- **Q3:** 129 files are dated after 2002. They are 127 `*-2.*` images dated 2026-08-18, `scripts/gfx.shader`
  (2023-10-11) and `textures/gothic_ceiling/woodceiling1a.jpg` (2025-02-15). Their origin is not established, and
  the registry lists them as found.
- **Q3:** 461 of the 1434 active shaders reference at least one image that is not in the local tree (759
  references in all). Admission rejects them for visible roles (`missing_stage_image`).

## Admission

`admit(index, game, name, role)` returns `{verdict, reasons:[{code, detail}], facts, notes}`. It decides from
registry metadata only. Pixel content, such as the luminance test in plan 9.6, is not judged here.

| Role | Accepted when |
|---|---|
| `wall`, `floor`, `ceiling`, `trim` | An opaque, static, non-emissive, structural solid |
| `hidden` | As above, but animation is allowed, and Q3 `nodraw` (caulk) is accepted |
| `light_emitter` | Q2 SURF_LIGHT with value > 0; Q3 q3map_surfacelight > 0. Unsupported in Q1 |
| `sky` | Q2 SURF_SKY; Q3 surfaceparm sky plus skyparms; Q1 a `sky` prefix |
| `water`, `slime`, `lava` | The matching contents. Q1 `*lava`, `*slime`, other `*` names water |
| `fog` (q3) | Fog volumes |
| `window` (q2) | Windows |
| `clip`, `trigger`, `hint`, `skip` (q1, q2), `origin` | Tool surfaces |

Closed reason codes (`admission.REASON_CODES`):
- Input and lookup: `name_missing`, `unknown_role`, `unsupported_role`, `no_q1_wad`, `registry_unavailable`,
  `absent`, `no_wal`, `header_invalid`.
- Surface properties: `emitter`, `sky`, `liquid`, `warp`, `translucent`, `flowing`, `tool_surface`,
  `non_structural`, `nonsolid`, `slick`.
- Q3 stages and shaders: `animated`, `deform`, `transparent_stage`, `layer_image`, `missing_stage_image`, `fog`.
- Role fit: `role_requirement`.

How Q3 names are resolved: a scripted shader comes first (`tr_shader.c:2465`). Otherwise the name is an implicit
shader made from `<name>.tga`, which falls back to `.jpg` (`tr_shader.c:2485-2491`, `tr_image.c:1827-1837`).
An implicit image is rejected as `layer_image` when shipped shaders use it, and every one of those uses is a
scrolled, waved or additive layer. A use that modulates over the lightmap (blendFunc filter) counts as a static
base.

`python -B tools/asset_registry/admission.py --registry q2=out/assets/q2/registry.json --registry q3=out/assets/q3/registry.json profiles`
prints the verdict for every generator-current material in `data/profiles/*/generator-current.json`. These are
the 2026-10-15 results:
- **Q1:** every material is rejected (`no_q1_wad`).
- **Q2:** `e1u1/ceil1_4` as a ceiling is rejected (`emitter`), and `e1u1/water1`, `slime1` and `lava1` are rejected
  (`absent`). `flat1_1`, `floor3_1` and `e3u1/trigger` are accepted.
- **Q3:** `base_wall/comp3text` on every wall role is rejected (`layer_image`), and `qmg/water`, `qmg/slime` and
  `qmg/lava` are rejected (`absent`). `clang_floor`, `pewter_shiney`, `common/caulk` (hidden) and `common/trigger`
  are accepted.

## CTests

- `asset_admission`, label `assets`:
  - It first runs about 187 contract checks on synthetic trees built from project-authored header bytes, in a
    temporary directory. These cover WAL, JPEG, TGA and WAD2 decoding, the shader parser (comments, quotes,
    braces, duplicates, unclosed bodies), the `.pak`/`.pk3` refusal, the `out/assets` write guard and every
    reason path.
  - It then runs every control in `controls.json` against registries built from `QMG_ASSET_REGISTRY_Q2_DIR` and
    `QMG_ASSET_REGISTRY_Q3_DIR`. When those are empty, it uses `QMG_BENCHMARK_Q2_ASSETS` and
    `QMG_BENCHMARK_Q3_BASEPATH/QMG_BENCHMARK_Q3_GAME`.
  - When a tree is absent, the contract checks still run and the test then reports `QMG-NOT-CONFIGURED`
    (skipped, exit 77).
  - `QMG_ASSET_REGISTRY_Q1_WAD` is optional. When it is set, WALL1_1, FLOOR0_1 and CEIL1_1 verdicts against that
    WAD are printed. The no-WAD controls always run with no Q1 registry loaded.
- `data_no_asset_bytes`, label `assets`: `scan_data_bytes.py --self-test` over `data/`. It looks for image, WAL,
  miptex, BSP, WAD, PAK, ZIP/PK3, AAS, model and sprite signatures, embedded PNG, JPEG, ZIP and TGA markers,
  `data:image/` URIs, NUL bytes, and base64 or hex runs of 1024 or more characters. A `qmg.asset_registry` file
  must also give every entry `source`, `origin` and `licence_status`. ASCII signatures count only at the start of
  a file, so prose such as "MISSIONPACK" passes. The self-test checks that 24 synthetic samples are detected and
  that a clean file is not.

## Not done here

- **Generated header and report v5.** Compiling the registry into a generated header and putting its sha256 in
  report v5 waits for D9 and the Phase-3 report bump.
- **Q1.** No Q1 WAD has been supplied (D9(2)).
- **Q3 hint.** The stock `textures/common/hint` has no `surfaceparm hint`, and q3map marks hint splits only from
  SURF_HINT (`q3map/facebsp.c:331`). The Q3 `hint` role therefore has no stock candidate.
