# VibeStudio Credits

## Audio reverb reference

The stereo reverberator in `src/core/audio_reverb.cpp` derives its parallel-comb /
serial-diffuser topology and 44.1 kHz delay tuning from Jezar at Dreampoint's
[Freeverb Components](https://github.com/sinshu/freeverb/tree/cfcea55553fb59ac57ebf2a237f72cad4296f2b0/Components),
June 2000, mirrored at revision `cfcea55553fb59ac57ebf2a237f72cad4296f2b0`.
The [upstream readme](https://github.com/sinshu/freeverb/blob/cfcea55553fb59ac57ebf2a237f72cad4296f2b0/readme.txt)
and component headers explicitly dedicate the implementation to the public
domain and permit unrestricted reuse. Reviewed 2026-10-06 for compatibility
with VibeStudio's GPL-3.0 licence. The upstream work carries no warranty.
The source retains the author's name, date and public-domain notice.

VibeStudio adds original sample-rate/room scaling, per-comb low-frequency decay,
frequency-based damping, stereo-preserving excitation, pre-delay, true allpass
sections, double precision, memory admission and constant-time history reset.
No VST SDK, binary or third-party runtime is incorporated. Chorus, flanger,
tremolo, phaser, preset handling and their integration are original VibeStudio code.

## Audio session EQ

The low/high-pass, notch, peaking and shelving coefficients in
`src/core/audio_effects.cpp` implement the mathematical formulae in section 2 of
Robert Bristow-Johnson / W3C's [Audio EQ Cookbook, 2021-06-08 Note](https://www.w3.org/TR/2021/NOTE-audio-eq-cookbook-20210608/).
The C++ processing, parameter model, dynamics, delay, saturation, routing and
tests are original VibeStudio work. The formula reference is published under
the [W3C Software and Document License, 2015](https://www.w3.org/copyright/software-license-2015/).
Reviewed 2026-10-05 against this repository's GPL-3.0 license; W3C explicitly
[documents GPL compatibility](https://www.w3.org/news/2015/w3c-adopts-new-software-and-document-license/).
No external effect binary or runtime library is linked.

This software includes material derived from the Audio EQ Cookbook linked
above. Copyright © 2021 W3C® (MIT, ERCIM, Keio, Beihang). The changes are an
original C++ coefficient implementation and integration into VibeStudio's
bounded stereo effects engine. The following notice is retained in this
credits document, which is included in portable license bundles.

### W3C Software and Document License (2015) notice

By obtaining and/or copying this work, you (the licensee) agree that you have
read, understood, and will comply with the following terms and conditions.

Permission to copy, modify, and distribute this work, with or without
modification, for any purpose and without fee or royalty is hereby granted,
provided that you include the following on ALL copies of the work or portions
thereof, including modifications:

- The full text of this NOTICE in a location viewable to users of the
  redistributed or derivative work.
- Any pre-existing intellectual property disclaimers, notices, or terms and
  conditions. If none exist, the W3C Software and Document Short Notice should
  be included.
- Notice of any changes or modifications, through a copyright statement on the
  new code or document such as "This software or document includes material
  copied from or derived from [title and URI of the W3C document]. Copyright
  © [YEAR] W3C® (MIT, ERCIM, Keio, Beihang)."

THIS WORK IS PROVIDED "AS IS," AND COPYRIGHT HOLDERS MAKE NO REPRESENTATIONS
OR WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO, WARRANTIES
OF MERCHANTABILITY OR FITNESS FOR ANY PARTICULAR PURPOSE OR THAT THE USE OF
THE SOFTWARE OR DOCUMENT WILL NOT INFRINGE ANY THIRD PARTY PATENTS,
COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS.

COPYRIGHT HOLDERS WILL NOT BE LIABLE FOR ANY DIRECT, INDIRECT, SPECIAL OR
CONSEQUENTIAL DAMAGES ARISING OUT OF ANY USE OF THE SOFTWARE OR DOCUMENT.

The name and trademarks of copyright holders may NOT be used in advertising
or publicity pertaining to the work without specific, written prior
permission. Title to copyright in this work will at all times remain with
copyright holders.

## Recording APIs

`core/audio_take`, `app/audio_capture`, `app/audio_input_device` and the take
review/CLI are original VibeStudio C++ implementations. Public interface
references reviewed 2026-10-05 are Qt's
[QAudioSource](https://doc.qt.io/qt-6.10/qaudiosource.html) byte-stream API
(Qt 6.4.2/6.10.1) and [application permissions](https://doc.qt.io/qt-6.10/permissions.html)
(Qt 6.5+), under the existing GPL-3.0/LGPL-3.0-compatible Qt dependency;
[Microsoft `_commit`](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/commit?view=msvc-170),
[POSIX `fsync`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fsync.html),
and Apple's
[bundle info lookup](https://developer.apple.com/documentation/corefoundation/cfbundlegetvalueforinfodictionarykey%28_%3A_%3A%29)
and [microphone declaration](https://developer.apple.com/documentation/bundleresources/information-property-list/nsmicrophoneusagedescription).
OS APIs are supplied by system runtimes/frameworks, with guarded portable
alternatives; they add no copied implementation or redistributed library.
No reference code or documentation prose is incorporated. Journal layout,
queue logic, corruption fixtures and sample oracles are original work.

## Quake III Animation Configuration

Native player package discovery and assembly contracts additionally reference
`CG_RegisterClientModelname`, `CG_RegisterClientSkin`, `CG_FindClientModelFile`,
`CG_FindClientHeadFile` and `CG_Player` in id Software's
[`cg_players.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/cgame/cg_players.c),
plus native skin tokenization and image behavior in
[`tr_image.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_image.c),
native image/shader lookup in
[`tr_shader.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_shader.c),
and TGA layout in
[`qfiles.h`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/qcommon/qfiles.h).
Local `master` snapshots were reviewed on 2026-10-06 under GPL-2.0-or-later,
compatible with VibeStudio's GPL-3.0 licence. Production bundling and fixtures
are original implementations with no game assets. The optional
`model_player_bundle_engine_oracle.py` extracts unmodified discovery, skin, TGA,
animation and token-parser functions into a separately compiled test harness,
preserving upstream notices and source hashes. Model registration and shader
lookup are explicit harness shims, not renderer/gameplay certification.

Player package team skins, custom sounds, detail levels and the `tag_weapon`
check (reviewed 2026-10-22, same licence and snapshot) follow the team-skin and
icon lookup in `CG_FindClientModelFile`, `CG_FindClientHeadFile` and
`CG_RegisterClientModelname`, the per-model sound registration and fallback in
`CG_LoadClientInfo` with the `cg_customSoundNames` list, and the `tag_flag` test
in [`cg_players.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/cgame/cg_players.c);
the held weapon's `tag_weapon` in
[`cg_weapons.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/cgame/cg_weapons.c)
(`CG_AddPlayerWeapon`); the `name_1.md3` and `name_2.md3` detail levels in
[`tr_model.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_model.c);
and the mono Microsoft PCM requirement in
[`snd_mem.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/client/snd_mem.c)
(`GetWavinfo`, `S_LoadSound`). Behaviour only; the implementation is original.
The oracle's team cases extract the same unmodified functions as above.

Opening an existing player as an assembly (`core/model_player_import`, reviewed
2026-10-22, same licence and snapshot) reads the base layout that
`CG_RegisterClientModelname` and `CG_RegisterClientSkin` load
(`models/players/<model>/lower.md3`, `upper.md3`, `head.md3`, `animation.cfg` and
`<role>_<skin>.skin`), the `icon_<skin>.tga` lookup in `CG_FindClientHeadFile`,
the custom sounds of `CG_LoadClientInfo` and the 19,998-byte limit of
`CG_ParseAnimationFile`, all in
[`cg_players.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/cgame/cg_players.c).
Behaviour only; the implementation is original.

The native animation roster, lower-body frame adjustment, legacy gesture
fallback and reverse/loop timing reference id Software's
[`bg_public.h`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/game/bg_public.h)
and [`CG_ParseAnimationFile` / `CG_RunLerpFrame`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/cgame/cg_players.c).
The local `master` source was reviewed on 2026-10-06; its GPL-2.0-or-later licence
is compatible with this GPL-3.0 project. Production parsing, validation,
sampling, assembly integration and synthetic fixtures are original VibeStudio
implementations. No game assets are included. The
[supported authoring contract](MODEL_ASSEMBLY.md#quake-iii-native-animation)
deliberately rejects unsupported directives and unsafe ranges.
The optional `model_q3_animation_engine_oracle.py` harness extracts unmodified
parser and playback-selection functions, plus the token parser from
[`q_shared.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/game/q_shared.c),
from a supplied source checkout into the project test output directory. It
preserves the GPL-2.0-or-later header and records source digests. These functions
are compiled only into the acceptance harness; the production library uses the
original VibeStudio implementation described above.

## Model Skin Bindings

The independently implemented `.skin` parser and surface matching reference id
Software's [Quake III skin reader](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_image.c)
(`CommaParse` / `RE_RegisterSkin`),
[MD3 surface-name normalization](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_model.c)
and [custom-skin lookup](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_mesh.c).
The local `master` reference was reviewed on 2026-10-05. These sources are
GPL-2.0-or-later, compatible with VibeStudio's GPL-3.0-or-later licence.
No upstream implementation or game assets were copied. The bounded authoring
parser, strict coverage/duplicate validation, transactional edits, package
selection, CLI and fixtures are original VibeStudio code. See
[the supported contract](MODEL_MESH.md#quake-iii-skin-assignments); import does
not emulate missing-surface fallback or source-port extensions.
The assembly linked-skin workflow reuses this reader and matching contract
through `core/model_assembly_skin.cpp` (integrated 2026-10-06). Source references,
verified snapshots, history, preview and bake handoffs are original VibeStudio
integration; no additional upstream code, assets or dependencies were imported.

The `.skin` writer (`writeModelQ3Skin`, 2026-10-22) emits records this reader
accepts, following the same `tr_image.c` and `tr_model.c` references. Named
[skin variants](MODEL_SKIN_VARIANTS.md) also write Doom 3 skin declarations
following `idDeclSkin::Parse` and `RemapShaderBySkin` in id Software's
[`neo/framework/DeclSkin.cpp`](https://github.com/id-Software/DOOM-3/blob/master/neo/framework/DeclSkin.cpp)
(GPL-3.0-or-later with id's additional terms, `master` reviewed 2026-10-22):
materials map to materials, the first match wins, `*` matches every material and
`model` lines only associate a model. Materials are compared by the canonical
names `idDeclManagerLocal::MakeNameCanonical` in
[`neo/framework/DeclManager.cpp`](https://github.com/id-Software/DOOM-3/blob/master/neo/framework/DeclManager.cpp)
gives them (lower case, `/` for `\`, the last extension removed). Behaviour only;
no code was copied.

## Model Collision

`core/model_collision` uses public format/interface facts from
[ericw-tools `qbsp/brush.cc`](https://github.com/ericwa/ericw-tools/blob/f80b1e216a415581aea7475cb52b16b8c4859084/qbsp/brush.cc)
(`clip` skips draw hull 0; revision `f80b1e216a415581aea7475cb52b16b8c4859084`),
id Software's [Quake II `game/q_shared.h`](https://github.com/id-Software/Quake-2/blob/master/game/q_shared.h)
(`CONTENTS_PLAYERCLIP` and player/monster masks), and Quake III's
[`q3map/map.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/q3map/map.c)
and [`q3map/shaders.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/q3map/shaders.c)
(shader-derived contents, `playerclip` and `nodraw`). id Software `master` snapshots
reviewed 2026-10-05; GPL-2.0-or-later references compatible with VibeStudio's
GPL-3.0. Original VibeStudio code implements the box document, controls, parsing,
serialization and fixtures and reuses existing VibeStudio hull/placement services.
No upstream implementation, commercial shader, texture, model or palette is copied.

The independent compiler acceptance reader in
`src/tests/model_collision_compiler_workflow.py` uses BSP record-layout facts from
ericw-tools [`bspfile_q1.hh`](https://github.com/ericwa/ericw-tools/blob/f80b1e216a415581aea7475cb52b16b8c4859084/include/common/bspfile_q1.hh)
and [`bspfile_q2.hh`](https://github.com/ericwa/ericw-tools/blob/f80b1e216a415581aea7475cb52b16b8c4859084/include/common/bspfile_q2.hh)
at the same revision, and NetRadiant Custom
[`q3map2.h`](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/q3map2.h)
and [`bspfile_ibsp.cpp`](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/bspfile_ibsp.cpp)
at `68ecbed64b7be78741878c730279b5471d978c7c`. These GPL-2.0-or-later references
were reviewed for GPL-3.0 compatibility on 2026-10-05. BSP readers, membership
oracles, fixtures and workflow code are original VibeStudio implementations.

## Placed Model Compiler Appearances

`core/level_model_appearance` implements the filename, frame, material-remap
precedence and omitted-surface behavior of NetRadiant Custom
[`model.cpp`](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/model.cpp),
entity-key precedence in
[`bspfile_abstract.cpp`](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/bspfile_abstract.cpp),
and extension removal in
[`shaders.cpp`](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/shaders.cpp).
These files are GPL-2.0-or-later. The optional default-skin lookup, exact surface
matching and shader-slot-zero behavior follow the bundled Assimp
[`MD3Loader.cpp`](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/libs/assimp/code/AssetLib/MD3/MD3Loader.cpp)
and `MD3Loader.h`, under
[BSD-3-Clause](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/libs/assimp/LICENSE).
All references are pinned to `68ecbed64b7be78741878c730279b5471d978c7c` and were
reviewed for GPL-3.0 compatibility on 2026-10-06. VibeStudio's bounded parser,
immutable package reads, cache keys, receipts, dependency integration and
synthetic fixtures are original implementations. No upstream implementation or
commercial model/skin content was copied or linked.

## Prepared Quake-family Builds

`core/level_build_workspace`, `level_build_artifacts` and `level_quake_assets`
use public interface/format facts from these local source references, reviewed
2026-10-05:

- ericw-tools [`common/settings.cc`](https://github.com/ericwa/ericw-tools/blob/f80b1e216a415581aea7475cb52b16b8c4859084/common/settings.cc),
  [`common/bspfile.cc`](https://github.com/ericwa/ericw-tools/blob/f80b1e216a415581aea7475cb52b16b8c4859084/common/bspfile.cc),
  [`qbsp/qbsp.cc`](https://github.com/ericwa/ericw-tools/blob/f80b1e216a415581aea7475cb52b16b8c4859084/qbsp/qbsp.cc) and
  [`qbsp/map.cc`](https://github.com/ericwa/ericw-tools/blob/f80b1e216a415581aea7475cb52b16b8c4859084/qbsp/map.cc):
  isolated search paths, logging, Quake II BSP selection and Quake WAD lookup.
  Runtime/diagnostic output names follow [`light/write.cc`](https://github.com/ericwa/ericw-tools/blob/f80b1e216a415581aea7475cb52b16b8c4859084/light/write.cc),
  [`qbsp/writebsp.cc`](https://github.com/ericwa/ericw-tools/blob/f80b1e216a415581aea7475cb52b16b8c4859084/qbsp/writebsp.cc)
  and [`vis/vis.cc`](https://github.com/ericwa/ericw-tools/blob/f80b1e216a415581aea7475cb52b16b8c4859084/vis/vis.cc).
  VIS/LIGHT's preserved input dialects in `vis/vis.cc` and
  [`light/light.cc`](https://github.com/ericwa/ericw-tools/blob/f80b1e216a415581aea7475cb52b16b8c4859084/light/light.cc)
  also inform `core/compiler_artifact_validation`'s Quake II acceptance.
  Pin `f80b1e216a415581aea7475cb52b16b8c4859084`, GPL-2.0-or-later.
- id Software Quake [`WinQuake/wad.h`](https://github.com/id-Software/Quake/blob/master/WinQuake/wad.h)
  and [`bspfile.h`](https://github.com/id-Software/Quake/blob/master/WinQuake/bspfile.h):
  WAD2 directory records and four-level miptexture layout, local `master` snapshot,
  GPL-2.0-or-later, copyright Id Software (1996–1997).

The licenses were verified as compatible with VibeStudio's GPL-3.0. Bounded
parsing, deterministic WAD assembly, dependency integration and orchestration
are original VibeStudio code. No upstream implementation, commercial textures,
models, palettes or sounds were imported. Existing package reader/writer and
WAL decoding retain their PakFu/id Software credits elsewhere in this document.

## Classic Prepared Deployment

The numbered PAK planner in `src/core/level_build_pak_deployment.cpp` and launch
profiles in `core/level_build_deployment.cpp` / `core/build_pipeline.cpp` use
interface facts from id Software's public source releases:

- [Quake `WinQuake/common.c`](https://github.com/id-Software/Quake/blob/master/WinQuake/common.c),
  `COM_AddGameDirectory` and `COM_InitFilesystem`: consecutive PAK search,
  `-basedir` and `-game`; and
  [`gl_vidnt.c`](https://github.com/id-Software/Quake/blob/master/WinQuake/gl_vidnt.c),
  `VID_Init`: `-window`.
- [Quake II `qcommon/files.c`](https://github.com/id-Software/Quake-2/blob/master/qcommon/files.c),
  `FS_AddGameDirectory` and `FS_InitFilesystem`: slots 0–9 and the `basedir` cvar;
  [`common.c`](https://github.com/id-Software/Quake-2/blob/master/qcommon/common.c)
  applies early `+set` commands before filesystem initialization; and
  [`win32/vid_dll.c`](https://github.com/id-Software/Quake-2/blob/master/win32/vid_dll.c)
  defines `vid_fullscreen`.

Local `master` source snapshots reviewed 2026-10-05. Their GPL-2.0-or-later
headers, copyright id Software 1996–1997 (Quake) and 1997–2001 (Quake II), were
verified as compatible with VibeStudio's GPL-3.0. No upstream implementation or
game content was copied. The slot receipts, bounded review, package-service
integration and recorder fixtures are original VibeStudio code.

## Doom Camera Materials

The wall texture anchors in `src/core/doom_preview_geometry.cpp` and binary
texture layouts/order in `src/core/doom_preview_materials.cpp` use
[Chocolate Doom 3.1.0 `r_segs.c`](https://github.com/chocolate-doom/chocolate-doom/blob/chocolate-doom-3.1.0/src/doom/r_segs.c)
and [`r_data.c`](https://github.com/chocolate-doom/chocolate-doom/blob/chocolate-doom-3.1.0/src/doom/r_data.c).
Their GPL-2.0-or-later notices were verified and reviewed for GPL-3.0
compatibility on 2026-10-04. Copyright Id Software (1993–1996) and Simon Howard
(2005–2014); notices remain beside the adaptations. Sector decomposition,
bounded Qt parsing/composition, namespace resolution and fixtures are original
VibeStudio code. No game assets or palettes were imported.

## WAD Semantic Groups

Shared WAD subset/edit map/GL-group recognition and new-document assembly in
`src/core/package_wad_groups.cpp` use
[ZDBSP `wad.cpp`](https://github.com/rheit/zdbsp/blob/bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659/wad.cpp)
as a format reference, including binary map members and delimited UDMF maps.
Revision `bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659`; GPL-2.0-or-later was
verified in that file and reviewed for GPL-3.0 compatibility on 2026-10-04.
The selection, group-expansion and plan-ordering code is original VibeStudio code; no upstream
source or prose was copied. Namespace recognition reuses the existing credited
package reader.

## Project
- Creator and lead: [themuffinator](https://github.com/themuffinator) (DarkMatter Productions)

## Package Draft Reader Protection

Saved-draft reader protection uses an original GPL-3.0 VibeStudio adapter to the
public [Windows directory sharing](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew),
[Linux flock](https://man7.org/linux/man-pages/man2/flock.2.html) and
[Apple flock](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/flock.2.html)
contracts, reviewed 2026-10-04. No external source code or documentation prose was
copied. These operating-system APIs add no bundled library.

The adapter lives in `src/core/package_draft_access.cpp`. Windows uses directory
sharing and native file identity; Unix uses shared/exclusive advisory locks on
an existing directory. The implementations and fixtures are VibeStudio code.

## Working Import Lock Recovery

`src/core/package_import_locks.cpp` is an original GPL-3.0 VibeStudio adapter
to [Windows file sharing](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew),
[handle-based deletion](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle),
[Linux flock](https://man7.org/linux/man-pages/man2/flock.2.html) and the Apple
flock contract credited above. Owner exclusion was cross-checked against
[Qt 6.10.1 Windows locks](https://github.com/qt/qtbase/blob/v6.10.1/src/corelib/io/qlockfile_win.cpp)
and [Unix locks](https://github.com/qt/qtbase/blob/v6.10.1/src/corelib/io/qlockfile_unix.cpp),
whose GPL-3.0/LGPL-3.0 alternatives are compatible with this GPL-3.0 repository.
Reviewed 2026-10-04. No source or documentation prose was copied; no bundled
dependency was added. Native macOS/Linux execution remains a release gate.

## Core Technology
- Prepared deployment launch flags follow id Software's Quake III [`FS_Startup` in files.c](https://github.com/id-Software/Quake-III-Arena/blob/master/code/qcommon/files.c), [`Com_StartupVariable` in common.c](https://github.com/id-Software/Quake-III-Arena/blob/master/code/qcommon/common.c), and [`r_fullscreen` in tr_init.c](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_init.c). Local `master` source reviewed 2026-10-05; GPL-2.0-or-later is compatible with this GPL-3.0 repository. Only interface behavior informed the existing profile adapter; no implementation or game content was copied. Package review/deployment and the recorder fixtures are original VibeStudio code.
- Prepared build filesystem flags follow q3map2's [path initialization](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/path_init.cpp) and shader-list behavior in its [shader loader](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/shaders.cpp), NetRadiant Custom revision `68ecbed64b7be78741878c730279b5471d978c7c`. GPL-2.0-or-later was reviewed for this GPL-3.0 repository on 2026-10-05. The independent-copy inventory and Qt orchestration are original VibeStudio code; no upstream code or proprietary assets were copied.
- `core/level_build_artifacts` follows that pinned revision's generated `scripts/q3map2_<map>.shader` naming in [shaders.cpp](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/shaders.cpp), external map-directory layout in [lightmaps_ydnar.cpp](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/lightmaps_ydnar.cpp), and `EXTERNAL_LIGHTMAP` naming in [q3map2.h](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/q3map2.h). The 17-lump IBSP v46 validator correction uses id Software's [Quake III qfiles.h](https://github.com/id-Software/Quake-III-Arena/blob/master/code/qcommon/qfiles.h), reviewed locally on 2026-10-05; the Quake Live advertisement extension follows the pinned [bspfile_ibsp.cpp](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/bspfile_ibsp.cpp). All are GPL-2.0-or-later references, compatible with this GPL-3.0 repository. Only format/interface facts are used; receipt verification, history retention, Qt review and package orchestration are original implementations. Compiler proofs use original generated assets only.
- Level texture projection conventions use [q3map2's classic mapping](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/map.cpp) and [brush-primitive basis](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/common/qmath.h), from NetRadiant Custom revision `68ecbed64b7be78741878c730279b5471d978c7c`. The GPL-2.0-or-later Quake III tools licence was reviewed for GPL-3.0 compatibility on 2026-10-04. Original id Software and contributor notices are retained in `src/core/level_texture_mapping.cpp`; the inverse mapping and rotation solver is VibeStudio code.
- [Qt 6](https://www.qt.io/product/qt6)
  - Application framework and primary UI toolkit.
  - Theme replacement was checked against Qt 6.10.1's
    [application stylesheet lifecycle](https://github.com/qt/qtbase/blob/v6.10.1/src/widgets/kernel/qapplication.cpp)
    and [recursive stylesheet refresh](https://github.com/qt/qtbase/blob/v6.10.1/src/widgets/styles/qstylesheetstyle.cpp),
    reviewed 2026-10-06. GPL-3.0/LGPL-3.0 alternatives are compatible with this
    GPL-3.0 repository. VibeStudio's detach/install sequence uses public Qt APIs;
    its implementation and nesting regression are original. No Qt code or
    documentation prose was copied.
    Shell page construction also defers application styling until the widget
    trees are attached, restoring it before item-view polish and layout. The
    2026-10-21 timing comparison and implementation use those public APIs;
    no additional Qt code was incorporated.
  - Progress styling was checked against Qt 6.10.1's
    [stylesheet progress delegation](https://github.com/qt/qtbase/blob/v6.10.1/src/widgets/styles/qstylesheetstyle.cpp)
    and [Fusion progress painting](https://github.com/qt/qtbase/blob/v6.10.1/src/widgets/styles/qfusionstyle.cpp),
    reviewed 2026-10-06 under the same GPL-3.0/LGPL-3.0 alternatives. Public palette
    roles preserve contrasting text on the filled and empty regions. The token
    adjustment and pixel regression are original; no Qt implementation was copied.
  - Right-to-left panel placement reads the window state layout that Qt 6.10.1's
    [QMainWindow::saveState()](https://github.com/qt/qtbase/blob/v6.10.1/src/widgets/widgets/qmainwindow.cpp),
    [QMainWindowLayoutState::saveState()](https://github.com/qt/qtbase/blob/v6.10.1/src/widgets/widgets/qmainwindowlayout.cpp),
    and [QDockAreaLayout/QDockAreaLayoutInfo::saveState()](https://github.com/qt/qtbase/blob/v6.10.1/src/widgets/widgets/qdockarealayout.cpp)
    write, with the section markers from their private headers, reviewed
    2026-10-07 under the same GPL-3.0/LGPL-3.0 alternatives. Only the byte layout
    is used: `src/app/studio_docks.cpp` is an original reader and writer that
    swaps the left and right dock areas, and its tests are original. No Qt code
    or documentation prose was copied.
  - The public [QRegularExpression non-path glob contract](https://doc.qt.io/qt-6/qregularexpression.html#WildcardConversionOption-enum)
    informs the original Qt 6.0–6.5 compatibility adapter in
    `core/project_text_search.cpp`. Reviewed 2026-10-04 against Qt 6.4.2 and
    6.10.1; Qt's GPL-3.0/LGPL-3.0 alternatives remain compatible with this
    GPL-3.0 repository. No Qt source or documentation prose was copied.
  - Multitrack streaming uses the public [QAudioSink](https://doc.qt.io/qt-6.10/qaudiosink.html),
    [QAudioDevice](https://doc.qt.io/qt-6.10/qaudiodevice.html) and
    [QAudioFormat](https://doc.qt.io/qt-6.10/qaudioformat.html) contracts, reviewed
    against Qt 6.4.2/6.10.1 headers on 2026-10-05. Qt's GPL-3.0/LGPL-3.0 alternatives
    are compatible with this GPL-3.0 repository. The transport, block mixer and
    buffered adapter are original; no Qt source or documentation prose was copied.
  - Editor and browser audio transport use the public [QMediaPlayer](https://doc.qt.io/qt-6.10/qmediaplayer.html),
    [QAudioOutput](https://doc.qt.io/qt-6.10/qaudiooutput.html) and
    [QMediaDevices](https://doc.qt.io/qt-6.10/qmediadevices.html) contracts.
    Reviewed against Qt 6.10.1 headers on 2026-10-04; their existing GPL-3.0/
    LGPL-3.0 alternatives are compatible with this GPL-3.0 repository. The
    adapter and lifecycle controller are original VibeStudio code. No Qt source
    or documentation prose was copied; dependency and packaging remain unchanged.
    The [Qt 6.4.2 FFmpeg player](https://github.com/qt/qtmultimedia/blob/v6.4.2/src/plugins/multimedia/ffmpeg/qffmpegmediaplayer.cpp)
    was also reviewed on 2026-10-04 to explain an observed end-of-stream event
    despite Infinite looping. Its GPL-3.0/LGPL-3.0 alternatives are compatible;
    VibeStudio's deferred, cancellable repeat fallback is original code.
  - Qt 6.10.1 [PNG handler](https://github.com/qt/qtbase/blob/v6.10.1/src/gui/image/qpnghandler.cpp)
    and [QImage grayscale semantics](https://github.com/qt/qtbase/blob/v6.10.1/src/gui/image/qimage.cpp)
    informed regression tests and original code restoring identity grayscale
    index planes in `.vtexture` projects. GPL-3.0/LGPL-3.0 options are compatible
    with this GPL-3.0 repository; no Qt source was copied. Reviewed 2026-10-04.
- [Qt Accessibility](https://doc.qt.io/qt-6/accessible.html), [Qt TextToSpeech](https://doc.qt.io/qt-6/qttexttospeech-index.html), and [Qt internationalization](https://doc.qt.io/qt-6/internationalization.html)
  - Planned accessibility, OS-backed speech, and localization support.
- [Meson](https://mesonbuild.com/) and [Ninja](https://ninja-build.org/)
  - Build configuration and build execution.

## Runtime Distribution

The original runtime deployment and notice tools follow
[Qt's deployment contract](https://doc.qt.io/qt-6/windows-deployment.html) and
[SPDX inventory format](https://doc.qt.io/qt-6/sbom.html), reviewed against the
installed Qt 6.10.1 MSVC SDK on 2026-10-05. Its binary/source SPDX documents are
CC0-1.0 data. Original licence documents are fetched from the `v6.10.1` tags of
[qtbase](https://github.com/qt/qtbase/tree/v6.10.1/LICENSES),
[qtmultimedia](https://github.com/qt/qtmultimedia/tree/v6.10.1/LICENSES),
[qtimageformats](https://github.com/qt/qtimageformats/tree/v6.10.1/LICENSES),
[qtsvg](https://github.com/qt/qtsvg/tree/v6.10.1/LICENSES) and
[qttranslations](https://github.com/qt/qttranslations/tree/v6.10.1/LICENSES).
Qt's [FFmpeg-specific notices](https://github.com/qt/qtmultimedia/tree/v6.10.1/src/3rdparty/ffmpeg)
describe its FFmpeg 7.1.2 build. Git blob/SHA-256 records retain each origin and
every differing original text. Qt's LGPL-3.0 and FFmpeg's LGPL-2.1-or-later /
permissive profile were reviewed for this GPL-3.0 application; the runtime
reports retain the full upstream expressions and copyrights. Corresponding
source distribution and publication acceptance remain separately tracked.

The local source-distribution audit preserves the unchanged
[Qt 6.10.1 module source archives](https://download.qt.io/official_releases/qt/6.10/6.10.1/submodules/)
with their LGPL-3.0/GPL-3.0 alternatives and original third-party notices.
It also retains [Qt's build and provisioning sources](https://github.com/qt/qt5/tree/56657e036f579ae0b858ae8c9659a0f7b7a53407)
(`v6.10.1`, revision `56657e036f579ae0b858ae8c9659a0f7b7a53407`), including
the FFmpeg/zlib Windows recipes and helpers under `coin/provisioning/common`.
Those recipes offer LGPL-3.0/GPL-3.0 alternatives; their original headers and
licence texts remain in the archive. Their pinned inputs are
[FFmpeg n7.1.2](https://github.com/FFmpeg/FFmpeg/tree/f893221c8d89cb798b829bebe71d55e1a3f242fd)
(LGPL-2.1-or-later/permissive deployed profile) and
[zlib 1.3.1](https://github.com/madler/zlib/releases/tag/v1.3.1) (zlib licence).
Reviewed 2026-10-05. These are source-distribution references; no downloaded
recipe is executed or newly linked into VibeStudio. The audit records Qt's
zlib build adjustments, archive hashes and SDK source-SPDX comparisons.

The source companion also retains [QtTools 6.10.1](https://github.com/qt/qttools/tree/9e0030f889168f7a0ec1bb47a7d7138a497b3c96)
and [QtShaderTools 6.10.1](https://github.com/qt/qtshadertools/tree/86c4b079a05c2dbe5fdb6f46ad9df8ef297487a9)
as unchanged official source archives. Their `lrelease` and `qsb` tools use
GPL-3.0 with Qt-GPL-exception-1.0; their Qt library components offer
LGPL-3.0/GPL-3.0 alternatives, alongside original third-party notices.
Reviewed against the SDK SPDX records and official archive metadata on
2026-10-05. These are build-tool sources, not new linked runtime dependencies.
The original source-companion utilities use Python's standard library; no
upstream packaging implementation was copied. Build guidance references
[Qt's Windows source build documentation](https://doc.qt.io/qt-6.10/windows-building.html)
and [FFmpeg's MSVC documentation](https://ffmpeg.org/platform.html#Microsoft-Visual-C_002b_002b-or-Intel-C_002b_002b-Compiler-for-Windows)
without copying their prose or installer scripts.

The original build-evidence and paired-release orchestration use Python's
standard library and the existing Meson/Ninja tools. SDK discovery follows
the documented `QT_ROOT_DIR` output of
[install-qt-action v4](https://github.com/jurplel/install-qt-action/tree/v4)
(MIT, reviewed 2026-10-05). The action was already used by the workflows;
no upstream action implementation or documentation prose was copied.

The PE checksum comparison is original code following Microsoft's
[PE/COFF specification](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
([CC-BY-4.0 documentation](https://github.com/MicrosoftDocs/win32/blob/docs/LICENSE)),
reviewed 2026-10-05. No specification prose or implementation was copied.
Signing-envelope removal is an in-memory content comparison, not signature/
trust verification; shipped DLLs are unchanged. Microsoft's
[Windows ICU contract](https://learn.microsoft.com/en-us/windows/win32/intl/international-components-for-unicode--icu-)
identifies the OS prerequisite. Qt's
[FindWrapAtomic](https://github.com/qt/qtbase/blob/v6.10.1/cmake/FindWrapAtomic.cmake)
(BSD-3-Clause) was inspected to classify the Windows compiler link probe;
no CMake source was copied. See [packaging](PACKAGING.md#windows-qt-and-audio-runtime).

## Branding, Documentation And Release Tooling (2026-10-07)

**Typeface.** The wordmark, banners, social preview and installer captions use
[Manrope](https://github.com/sharanda/manrope) 4.504 by Mikhail Sharanda,
copyright The Manrope Project Authors, under the
[SIL Open Font License 1.1](../assets/branding/fonts/OFL.txt). The ExtraBold and
SemiBold files are vendored unchanged in `assets/branding/fonts/` (SHA-256
`effbf6efd56d3bc969fcfa43097932e1a858b6cd0ff6564425e0cc48554ad463` and
`9cb6bdf00c2c6b64d4bc77087aadf88207a577f32dae410745e4d55582355d3c`) with the
licence beside them. `scripts/generate_branding.py` converts their outlines to
SVG paths for the artwork and subsets them to Latin WOFF2 files for the HTML
documentation; both are documents or derivatives the OFL permits, and the
licence travels with the fonts. Reviewed 2026-10-07.

**Artwork.** The VibeStudio mark, wordmark, lockups, icons at every size,
social preview, installer art and documentation theme are original, generated
by `scripts/generate_branding.py` from one geometry description. Vibe Orange is
the studio's own dark-theme accent (`src/app/studio_theme.cpp`). See
[Branding](BRANDING.md).

**Release flow.** The changelog queue, curated-notes precedence, release-notes
layout (highlights, build details, collapsed commit list) and metadata helper
that prints GitHub outputs follow the pattern of
[FnQL](https://github.com/themuffinator/FnQL/tree/e24b4dfd319f55e094e8e446d6e2b4e864d6f848)'s
`scripts/changelog.py`, `scripts/manual_release.py`, `scripts/version.py` and
`.github/workflows/release.yml` (GPL-2.0, same author, revision `e24b4df`,
2026-09-24, reviewed 2026-10-07). Pattern only: VibeStudio's
`scripts/release_meta.py`, `version.py`, `changelog.py` and `release.py` are
original implementations, and no FnQL code was copied. The changelog follows
[Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/) (Olivier Lacan,
MIT) and versions follow [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html)
(Tom Preston-Werner, CC BY 3.0), whose precedence rules
`release_meta.Version.sort_key` implements independently. Manual callouts use
GitHub's documented [alert syntax](https://docs.github.com/en/get-started/writing-on-github/getting-started-with-writing-and-formatting-on-github/basic-writing-and-formatting-syntax#alerts).

**Build and packaging tools** (run by the release workflow; not linked into
VibeStudio):

- [Inno Setup 6](https://jrsoftware.org/isinfo.php) (Jordan Russell and Martijn
  Laan, Inno Setup License) builds the Windows installer from
  `packaging/windows/vibestudio.iss`; the installer carries Inno Setup's setup
  runtime, which its licence allows redistributing.
- [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) and
  [linuxdeploy-plugin-qt](https://github.com/linuxdeploy/linuxdeploy-plugin-qt)
  (MIT) assemble the AppImage, which embeds the
  [AppImage type 2 runtime](https://github.com/AppImage/type2-runtime) (MIT).
- [create-dmg](https://github.com/create-dmg/create-dmg) (MIT) lays out the
  macOS disk image; Qt's `macdeployqt` and `windeployqt` copy the Qt runtime
  (see Runtime Distribution above).
- [Python-Markdown](https://github.com/Python-Markdown/markdown) (BSD-3-Clause)
  and [Pygments](https://github.com/pygments/pygments) (BSD-2-Clause) render the
  HTML documentation; Pygments' `default` and `github-dark` styles colour its
  code blocks.
- [fontTools](https://github.com/fonttools/fonttools) (MIT),
  [Pillow](https://github.com/python-pillow/Pillow) (MIT-CMU),
  [NumPy](https://github.com/numpy/numpy) (BSD-3-Clause) and
  [Brotli](https://github.com/google/brotli) (MIT) generate the artwork, icons
  and web fonts.
- README badges are served by [Shields.io](https://shields.io/) (CC0-1.0).

## Audio duplex device backend

The optional private device backend incorporates selected common, WASAPI,
CoreAudio and ALSA sources from [PortAudio](https://github.com/PortAudio/portaudio/tree/873e3c83fbe2f57ebcf59083e627a3f8fa051ffe),
revision `873e3c83fbe2f57ebcf59083e627a3f8fa051ffe`, reviewed 2026-10-06.
Its MIT-style permissive terms are GPLv3-compatible. Upstream copyright,
permission/disclaimer and non-binding requests remain in each source and the
distributed license. Source hashes and the exact selection are recorded in
`external/audio/portaudio/UPSTREAM.json`. No ASIO SDK is imported.

VibeStudio's checked build-time patches repair packet timing/flags, silent
input, explicit channel mapping and output drain in WASAPI, and atomic xrun
consumption in CoreAudio. Packet interpretation follows Microsoft's
[IAudioCaptureClient::GetBuffer](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer)
and [buffer flags](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/ne-audioclient-_audclnt_bufferflags)
contracts, reviewed 2026-10-06; no Microsoft sample code is copied. See the
[integration and patch record](../external/audio/portaudio/VIBESTUDIO.md).

## Audio Loudness Metering

Jan Kokemüller's [libebur128 1.2.6 source and public API](https://github.com/jiixyj/libebur128/tree/67b33abe1558160ed76ada1322329b0e9e058b02/ebur128),
revision `67b33abe1558160ed76ada1322329b0e9e058b02`, supplies integrated loudness.
The existing r8brain-free-src dependency supplies true-peak reconstruction.
The unmodified sources retain the library's MIT licence,
Chris Moeller's MIT R128Scan notice for derived filter code, and the University
of California BSD-3-Clause queue notice. All were reviewed for GPLv3 compatibility
before incorporation on 2026-10-04. Notices and pinned hashes are retained under
`external/audio/libebur128` and included in distribution licence bundles.
The C++ adapter, channel review controls and synthetic fixtures are VibeStudio
code. See the [integration record](../external/audio/libebur128/VIBESTUDIO.md).
Calibration and speaker-weight facts were checked against the
[ITU-R BS.1770-5 recommendation (November 2023)](https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-5-202311-I!!PDF-E.pdf)
on 2026-10-04. No ITU text or audio sequences are redistributed. The optional
`audio_metering_oracle.py` compares original generated PCM with a separately
installed [FFmpeg EBU R128 filter](https://ffmpeg.org/ffmpeg-filters.html#ebur128);
FFmpeg is a test tool and is not linked or bundled by this integration.

## Compressed Audio Decoding

David Reid's [dr_libs](https://github.com/mackron/dr_libs/tree/dfe8377631000664666519fdb83da193fd8037f4),
revision `dfe8377631000664666519fdb83da193fd8037f4`, supplies unchanged `dr_mp3.h`
0.7.4 and `dr_flac.h` 0.13.4 under the selected MIT-0 licence. The MP3 backend
credits its public-domain [minimp3](https://github.com/lieff/minimp3) ancestry.
Xiph.Org's [libvorbis](https://github.com/xiph/vorbis/tree/c2aa86b05e981c96bf381fc6aa11cdd03eccc2fb),
revision `c2aa86b05e981c96bf381fc6aa11cdd03eccc2fb` (1.3.7 plus upstream fixes),
and [libogg 1.3.6](https://github.com/xiph/ogg/tree/be05b13e98b048f0b5a0f5fa8ce514d56db5f822),
revision `be05b13e98b048f0b5a0f5fa8ce514d56db5f822`, supply reference Vorbis/container
decoding under BSD-3-Clause. These licences were reviewed on 2026-10-04 as
compatible with GPLv3. Original notices and sources remain intact;
`UPSTREAM.json` records hashes. Meson adapters and allocation wrappers are
VibeStudio code. The static source subset omits the separate encoder API.

The unbuilt evaluation copy of Sean Barrett and contributors'
[stb_vorbis.c 1.22](https://github.com/nothings/stb/blob/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_vorbis.c)
is pinned at `2c980bb59875b0d32144a71867fbdebb2f77cd20` under the selected MIT
alternative, reviewed as GPLv3-compatible on 2026-10-04. Its original licence,
source notices and hash manifest remain in `external/audio/stb`. This evaluation
copy is excluded from Meson, application binaries and portable bundles; the
active Vorbis implementation is Xiph.

Synthetic test assets are generated by VibeStudio's fixture script. Independent
reference samples use [FFmpeg](https://ffmpeg.org/) and
[libsndfile](https://libsndfile.github.io/libsndfile/) through
[python-soundfile](https://python-soundfile.readthedocs.io/); versions are recorded
in the fixture manifest. Their code/binaries are not incorporated or distributed.

Container checks and speaker mapping use the public [Ogg RFC 3533](https://www.rfc-editor.org/rfc/rfc3533.html),
[FLAC RFC 9639](https://www.rfc-editor.org/rfc/rfc9639.html),
[Vorbis I specification](https://www.xiph.org/vorbis/doc/Vorbis_I_spec.html), and
[ID3v2.4 structure](https://id3.org/id3v2.4.0-structure), reviewed 2026-10-04.
No prose or code from these specifications was copied.

## Mesh UV Atlas Generation

The modeller uses Jonathan Young's [xatlas source](https://github.com/jpcy/xatlas/tree/f700c7790aaa030e794b52ba7791a05c085faf0c/source/xatlas),
revision `f700c7790aaa030e794b52ba7791a05c085faf0c`, reviewed 2026-10-04.
Only `xatlas.cpp`, `xatlas.h` and the root licence are imported. MIT terms cover
xatlas, thekla_atlas (Thekla, Inc.; NVIDIA / Ignacio Castaño) and Fast-BVH
(Brandon Pelfrey); the embedded OpenNL implementation credits Bruno Lévy under
BSD-3-Clause terms. All are GPLv3 compatible. Original notices are retained and
the OpenNL notice is reproduced in binary licence bundles. No example assets
are included. [Integration and maintenance notes](../external/modelling/xatlas/VIBESTUDIO.md)
describe the build adaptations that preserve indexed seams and chart shape,
the independent width/height packing adaptation reviewed 2026-10-06, and the
bounded VibeStudio worker wrapper. Original vendored source and header bytes
remain unchanged; generated copies retain all upstream notices.

## Blender-Style Mesh Editing (2026-10-07)

The Mesh Editor's edit-mode workflow follows [Blender](https://www.blender.org/)
(GPL-2.0-or-later application; manual CC-BY-SA-4.0). These behaviours were
reviewed on 2026-10-07 in the [mesh editing manual](https://docs.blender.org/manual/en/latest/modeling/meshes/index.html)
and the [default keymap](https://github.com/blender/blender/blob/main/scripts/presets/keyconfig/keymap_data/blender_default.py)
(`main`):

- operator names and results: inset, loop cut, merge at center/cursor/collapse,
  dissolve, poke, beautify, make face, rotate edge, shrink/fatten, smooth,
  bisect, symmetrize and decimate;
- select operators: linked, more/less, loop, ring, shortest path, similar,
  non-manifold, boundary loop, sharp, random, checker deselect and mirror;
- the edit-mode keymap: G/R/S, E, I, Ctrl+R, select modes 1/2/3, numpad views,
  Shift+right-click for the 3D cursor, Shift+S snap, F3 search, F9 Adjust Last
  Operation and Shift+R repeat;
- modal transforms: X/Y/Z constraints, a second press for local axes, Shift for
  planes, typed values, Ctrl to snap and Shift for precision;
- proportional editing's falloff curves (Smooth, Sphere, Root, Inverse Square,
  Sharp, Linear, Constant);
- the navigation gizmo;
- Triangles to Quads pairing defaults (40 degree face and shape limits).

Texel-density matching also references the workflow described in Blender's
[Average Island Scale manual](https://docs.blender.org/manual/en/3.3/modeling/meshes/uv/editing.html#average-island-scale)
(3.3 manual, CC-BY-SA-4.0, reviewed 2026-10-21). The density service, original
area-ratio calculations, Qt review dialog and CLI are independently written;
no Blender implementation, manual prose or assets are incorporated. The
existing GPL-compatible application reference and documentation licence were
reviewed before using this behavioural reference.

Behaviour only; no Blender code, icons or assets are used. The implementation
in `src/core/model_geometric_topology.*`, `src/core/model_mesh_tools*.*`,
`src/core/model_mesh_decimate.cpp`, `src/core/model_selection_tools.*`,
`src/cli/model_tools.*`, `src/app/model_editor_tools.*` and
`src/app/model_editor_modal.cpp` is VibeStudio's own.

Knife interaction was reviewed on 2026-10-21 against the
[Blender 5.4 development knife manual](https://docs.blender.org/manual/en/dev/modeling/meshes/editing/mesh/knife_topology_tool.html)
(manual CC-BY-SA-4.0; application GPL-2.0-or-later): K, path placement,
confirmation/cancellation, disconnected chains, path undo, midpoint snapping,
cut-through and angle constraints. `src/core/model_knife*` and
`src/app/model_editor_knife.cpp` are original implementations; no upstream
source, prose, icons or assets were incorporated. VibeStudio's S selection
scope and semantic keyboard crosshair are documented in [Knife](MODEL_KNIFE.md).

Spin's centre/axis/angle/steps and radial duplication workflow references the
[Blender 4.0 Spin manual](https://docs.blender.org/manual/en/4.0/modeling/meshes/tools/spin.html)
(CC-BY-SA-4.0; application GPL-2.0-or-later, reviewed for GPL-3.0 compatibility
on 2026-10-21). `src/core/model_spin.cpp` and the Qt/CLI integration are original
implementations. No upstream source, prose, icons or assets were incorporated.
See [Spin](MODEL_SPIN.md) for VibeStudio's closure, pose and skinning semantics.

Rip's vertex/edge separation, side choice and optional gap fill reference the
[Blender 4.5 Rip Vertices manual](https://docs.blender.org/manual/en/4.5/modeling/meshes/editing/vertex/rip_vertices.html)
and [Rip Vertices and Fill](https://docs.blender.org/manual/en/4.5/modeling/meshes/editing/vertex/rip_vertices_fill.html)
(CC-BY-SA-4.0; application GPL-2.0-or-later, reviewed for GPL-3.0 compatibility
on 2026-10-21). `src/core/model_rip.cpp`, `model_rip_apply.cpp` and the Qt/CLI
integration are original implementations. No upstream source, prose, icons or
assets were incorporated. [Rip](MODEL_RIP.md) documents VibeStudio's explicit
side-face, nonzero movement, all-pose behaviour and whole-operation cancellation.

Decimation implements the quadric error metric published by Michael Garland and
Paul S. Heckbert, ["Surface Simplification Using Quadric Error Metrics"](https://doi.org/10.1145/258734.258849)
(SIGGRAPH 1997), as half-edge collapses with quadrics summed over sampled
animation poses. Polygon filling uses ear clipping after G. H. Meisters,
"Polygons Have Ears" (American Mathematical Monthly 82, 1975). Both are
published algorithms; no third-party code is used.

## Level Geometry, Patch And Navigation Tools (2026-10-15)

The Levels page's vertex editing, patch matrix commands, Thicken, hollow modes,
regions and navigation follow the behaviour of GtkRadiant 1.5, NetRadiant Custom,
TrenchBroom and Hammer. NetRadiant Custom's editor sources were read at revision
`8216133984031afaa9a857b56ea66dd9c3d54b26` (the revision VibeMap3 continues
from) on 2026-10-15; they are GPL-2.0-or-later (copyright William Joseph and
the GtkRadiant contributors), compatible with this GPL-3.0 repository:

- [`radiant/patch.cpp`](https://github.com/Garux/netradiant-custom/blob/8216133984031afaa9a857b56ea66dd9c3d54b26/radiant/patch.cpp): the exact cylinders of
  `Patch::ConstructPrefab` (`EPatchPrefab::ExactCylinder`), each span's middle
  control where the tangents at its ends meet; `Patch::InsertPoints` and
  `Patch::RemovePoints`; `Patch::TransposeMatrix`; `Patch::Redisperse`;
  `Patch::Smooth`; `Patch::NaturalTexture` and `Patch::CapTexture`; and
  `Patch::createThickenedOpposite` and `Patch::createThickenedWall`.
- [`radiant/patchmanip.cpp`](https://github.com/Garux/netradiant-custom/blob/8216133984031afaa9a857b56ea66dd9c3d54b26/radiant/patchmanip.cpp): `Patch_thicken`
  and the names and order of the Patch menu's matrix commands.
- [`radiant/csg.cpp`](https://github.com/Garux/netradiant-custom/blob/8216133984031afaa9a857b56ea66dd9c3d54b26/radiant/csg.cpp): `CSG_Hollow`'s wrapped walls
  (`eWrap`), which meet without overlapping, and `CSG_MakeRoom`, which builds
  the walls outside the brush.
- Make Room's caulk and open-face options (2026-10-24) follow
  [`CSG_MakeRoom`, `HollowSettings` and `CaulkFace`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/csg.cpp)
  (every wall face but the room's caulked, faces picked in component mode left
  open), with the game caulk names after
  [`GetCommonShader`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/filterbar.cpp)
  and the Quake caulk filter in
  [`brushmanip.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brushmanip.cpp),
  revision `449778b3840f8f2062daab15c87cb012f2ac945c`, GPL-2.0-or-later, reviewed
  before implementation. `core/level_hollow.*`, the options in
  `hollowLevelMapSelection`, `cli/level_hollow_options.*`, **Make Room
  Options…** and `editor make-room` are original VibeStudio implementations;
  no upstream code was copied. As upstream, wall faces sharing an open face's
  plane keep its material; unlike upstream, VibeStudio fills the outer corners
  and makes both options switchable.
- The CSG Tool (2026-10-25) follows
  [`CSG_Tool`, `CSGdlg_getSettings`, `CSG_Hollow`, `HollowSettings`, `FaceMakeBrush`, `brush_extrudeDiag`, `FaceOffset` and `CSGdlg_BrushShrink`/`CSGdlg_BrushExpand`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/csg.cpp)
  at revision `449778b3840f8f2062daab15c87cb012f2ac945c`, GPL-2.0-or-later,
  reviewed before implementation: the thickness with its Grid button, the
  -faces, +faces, -proj and -cam face policies (faces at either end of the
  axis), Caulk and Remove inner brush, Shrink and Expand, and the diagonal,
  wrap, extrude and pull hollows. The Tools tab's **CSG Tool** section
  (`app/level_csg_tool_actions.cpp`), the Diagonal and Extrude modes and
  keep-inner option of `hollowLevelMapSelection`, `levelCsgExcludedFaces`,
  `offsetLevelMapSelectionFaces` and `map shrink-brushes`/`expand-brushes` are
  original VibeStudio implementations; no upstream code was copied. Diagonal
  walls are built as the points of the grown shell furthest outside their
  face rather than by sewing windings; with Keep inner brush, caulk covers the
  faces between the walls and the brush and leaves the outside's materials,
  and the pull hollow keeps Make Room's filled corners.
- Make Prism…, Make Cone…, Make Sphere… and Make 3- to 9-Sided Prism
  (2026-10-26) follow
  [`Brush_ConstructPrism`, `Brush_ConstructCone`, `Brush_ConstructSphere`, `max_extent_2d`, `Scene_BrushConstructPrefab`, `BrushPrefab` and `BrushMakeSided`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brushmanip.cpp)
  at revision `449778b3840f8f2062daab15c87cb012f2ac945c`, GPL-2.0-or-later,
  reviewed before implementation: the selected brush rebuilt in its own bounds
  along the current 2D view's axis, side planes tangent to a circle on the
  larger cross-section extent with a face towards the first axis, a cone's
  apex over the centre of the high end, a sphere of sides × sides planes on the
  largest extent with its poles on Z, and the Ctrl+3 to Ctrl+9 keys. The
  circle fit in `core/level_primitive.*`, `rebuildLevelMapBrushesAsShape`
  (`core/level_shapes.*`), `rebuildLevelMapBrushHulls` (`core/level_map.cpp`),
  `app/level_prism_actions.cpp` and `map add-shape --in-place --fit` are
  original VibeStudio implementations built on its own hull builder: they
  compute the solid's corners rather than writing NetRadiant Custom's plane
  points, and no upstream code was copied. Unlike upstream, every selected
  brush is rebuilt rather than only the last one, its content flags are kept,
  side counts stop at the component editor's 128 faces (126 for a prism, 127
  for a cone, 11 × 11 for a sphere) and the keys act only in the map views
  while a brush is selected, so the studio's Ctrl+digit mode keys keep working.

The patch shapes, square cylinder, bevel and end cap follow the Patch menu of
[GtkRadiant](https://github.com/TTimo/GtkRadiant) 1.5; Region Set XY, Region
Set Brush, Find Brush and Expand Selection to Entities follow the Radiant
editors' commands of those names, and the entity and brush numbers of Find
Brush are the ones q3map2 prints. Vertex, edge and face handles and Snap
Vertices follow [TrenchBroom](https://github.com/TrenchBroom/TrenchBroom)'s
vertex tool and Snap Vertices command (GPL-3.0, behaviour as documented in its
[manual](https://trenchbroom.github.io/manual/latest/)); Go to Coordinates
follows Valve Hammer Editor's command of that name.

These are behaviours and results only: no code was copied. The implementation
in `src/core/level_patch.cpp`, `src/core/level_patch_tools.*`,
`src/core/level_brush.*` (`moveLevelMapComponents`,
`snapLevelMapBrushVertices`), `src/core/level_map.cpp`
(`hollowLevelMapSelection`), `src/core/level_selection_tools.*`,
`src/core/level_prefab.cpp` (`replaceLevelMapSelectionWithPrefab`),
`src/app/map_viewport_components.cpp` and `src/app/level_geometry_actions.cpp`
is VibeStudio's own, and the source comments name the upstream functions.

### Level deletion policy

Level deletion routing was reviewed on 2026-10-21 against NRC
[`deleteSelection`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/select.cpp),
[`CSG_DeleteComponents`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/csg.cpp)
and [`vertexModeBuildHull`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brush.cpp),
revision `449778b3840f8f2062daab15c87cb012f2ac945c` (GPL-2.0-or-later), and TB
[`MapWindow::deleteSelection`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapWindow.cpp),
v2026.2 / `4c756be1a54a28d921d18c77716a5f170bdc31d0` (GPL-3.0-or-later).
These licences are compatible with VibeStudio's GPL-3.0. No code was copied.
The original Qt routing and worker integration preserve NRC's empty-component
object fallback and ignored patch points, and TB's clip-point priority.
VibeStudio refuses a whole invalid multi-brush edit atomically; NRC's per-brush
hull builder can retain an invalid brush while changing another one.

### Brushes from points and vertex listings

**Create Brush from Points…**, **Copy Vertex Coordinates**,
`map add-brush --points` and multi-brush `map brush-components` were reviewed on
2026-10-26 against TB
[`MapWindow::debugCreateBrush` and `MapWindow::debugPrintVertices`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapWindow.cpp)
and their Debug menu entries in
[`ActionManager.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp),
v2026.2 / `4c756be1a54a28d921d18c77716a5f170bdc31d0` (Kristian Duske and
contributors, GPL-3.0-or-later, compatible with VibeStudio's GPL-3.0). Facts
about behaviour only: a list of at least four `(x y z)` points becomes the
convex brush enclosing them in the current material, and the selected faces'
corners, or else the selected brushes', are written out at full precision.
`core/level_brush_points.*`, `app/level_brush_points_*` and the CLI routes are
original VibeStudio code over its own envelope builder and hull insertion; no
code was copied. Unlike upstream, both are ordinary commands rather than
debug-build ones, the dialog previews the brush and names flat or malformed
input, several point forms and comments are accepted, edges, vertices and patch
control points can be listed too, and coordinates use the shortest exact
decimal rather than 17 significant digits.

### Assemble Brush tool

The **Assemble Brush Tool**, **Create Assembled Brush** and
`map add-brush --points --extrude` were reviewed on 2026-10-26 against TB
[`AssembleBrushTool`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/AssembleBrushTool.cpp),
[`AssembleBrushToolController3D`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/AssembleBrushToolController3D.cpp),
`MapViewToolBox` (its exclusive tool groups) and the "Menu/Edit/Tools/Brush
Tool" (B) and "Controls/Map view/Create brush" (Return) entries in
[`ActionManager.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp),
v2026.2 / `4c756be1a54a28d921d18c77716a5f170bdc31d0` (Kristian Duske and
contributors, GPL-3.0-or-later, compatible with VibeStudio's GPL-3.0). Facts
about behaviour only: a click on a face adds the grid point in its plane, a drag
from a face draws the grid rectangle on that plane, a double-click adds the
face's corners, Shift-drag duplicates a flat outline along its normal by grid
steps, Escape clears the points, and Return builds their convex hull in the
current material and clears them. `core/level_brush_assemble.*`,
`app/level_brush_assemble_*`, `app/model_viewport_assemble.cpp`,
`app/map_viewport_assemble.cpp` and the CLI option are original VibeStudio code
over its own envelope builder; no code was copied. Unlike upstream, the 2D views
collect, move and extrude points too, an empty camera view uses the Draw Brush
construction plane, Backspace removes the last point, and the hull is checked on
a worker before Enter publishes it.

## Materials, Shaders And Textures (2026-10-08)

The materials module (`src/core/material_*`, `src/cli/materials.*`,
`src/app/material_*`) reimplements how each engine parses, looks up, animates
and draws surfaces. The rules were read in these GPL source releases and
documents, at their default branches, between 2026-10-07 and 2026-10-08. No
code, shader, texture, palette or other game data was copied; the tests build
their own fixtures.

| Upstream | Files read | What the module follows | Licence |
| --- | --- | --- | --- |
| [Quake III Arena](https://github.com/id-Software/Quake-III-Arena) (id Software) | `code/renderer/tr_shader.c`, `tr_shade.c`, `tr_shade_calc.c`, `tr_init.c`, `tr_noise.c`, `tr_sky.c`, `tr_image.c` | Shader parsing and the keywords that drop a shader, the default shader and default `rgbGen`, sort, depth-write and alpha-function rules, the 1024-entry wave tables, `EvalWaveForm`, seeded noise, every tcMod, tcGen, colour generator and deform, sky boxes and cloud layers, fog factors, overbright and image lookup (`.tga` then `.jpg`) | GPL-2.0-or-later |
| [ioquake3](https://github.com/ioquake/ioq3) | `code/renderergl1/tr_shader.c`, `tr_image.c` | Scripts read in reverse order (the alphabetically last wins), the first definition in a file wins, and the extra image extensions | GPL-2.0-or-later |
| [Wolfenstein: Enemy Territory](https://github.com/id-Software/Enemy-Territory) (id Software) | `src/renderer/tr_shader.c` (`ParseShader`, `SetImplicitShaderStages`, `R_FindShader`) | `implicitMap`, `implicitMask` and `implicitBlend` stages, cull and image naming, and the derivative general keywords read with a warning (`fogvars`, `skyfogvars`, `waterfogvars`, `sunshader`, `lightgridmulamb`, `lightgridmuldir`, `nofog`, `allowcompress`, `nocompress`, `distancecull`) | GPL-3.0-or-later with id's additional terms |
| [Doom 3](https://github.com/id-Software/DOOM-3) (id Software) | `neo/renderer/Material.cpp`, `Image_program.cpp`, `Image_init.cpp`, `tr_render.cpp`, `draw_arb2.cpp`, `neo/idlib/Lexer.cpp`, `neo/framework/DeclManager.cpp` | The material grammar and its right-associative expressions, tables, defaulted materials, coverage and sort, implicit `_flat` and `_white` stages, texture matrices, image programs, built-in images, interaction pairing, the specular table and the ARB2 interaction's inputs, and the first decl winning | GPL-3.0-or-later with id's additional terms |
| [Doom 3 BFG Edition](https://github.com/id-Software/DOOM-3-BFG) (id Software) | `base/renderprogs/interaction.pixel` | The BFG interaction's specular term | GPL-3.0-or-later with id's additional terms |
| [Doom](https://github.com/id-Software/DOOM) `linuxdoom-1.10` (id Software) | `p_spec.c`, `p_switch.c`, `r_data.c`, `r_main.c`, `r_plane.c`, `r_sky.c` | The animation table (22 ranges at 8 tics), the switch list (40 pairs), wall composition, colormap light tables for walls and flats, fake contrast, sky mapping and row wrapping | GPL-2.0-or-later (1999 relicence) |
| [SMMU](https://github.com/fragglet/smmu) (Boom lineage) | `utils/defswani.dat` | Boom's SWANTBLS input columns (`speed last first`, `episode texture1 texture2`) | GPL-2.0-or-later |
| [Doom Wiki](https://doomwiki.org/wiki/ANIMATED) ([SWITCHES](https://doomwiki.org/wiki/SWITCHES)) | `ANIMATED`, `SWITCHES` | Boom's 23-byte animation and 20-byte switch records | Facts from CC BY-SA text |
| [ZDoom wiki: ANIMDEFS](https://zdoom.org/wiki/ANIMDEFS) and [GZDoom](https://github.com/ZDoom/gzdoom) | `ANIMDEFS` grammar; the warp and warp2 texture effects | Hexen/ZDoom animation entries and warp speeds | Facts from GFDL text; GZDoom GPL-3.0-or-later |
| [Quake](https://github.com/id-Software/Quake) (id Software) | `WinQuake/gl_model.c`, `gl_rsurf.c`, `gl_warp.c`, `r_light.c`, `r_surf.c`, `d_sky.c` | `+0` to `+9` and `+a` to `+j` frames at 0.2 s, liquid warps (GL and software), two-layer skies, light styles at 10 Hz and the GLQuake lightmap scale | GPL-2.0-or-later |
| [Quake re-release QuakeC](https://github.com/id-Software/quake-rerelease-qc) (id Software) | `quakec/world.qc` | Light style patterns 0 to 11 | GPL-2.0 |
| [Quake II](https://github.com/id-Software/Quake-2) (id Software) | `qcommon/qfiles.h`, `game/q_shared.h`, `ref_gl/gl_warp.c`, `gl_rsurf.c`, `gl_image.c` | The WAL header and its next-frame chain at 2 Hz, surface and content flags, warping and flowing, `trans33`/`trans66`, `intensity` and env sky boxes | GPL-2.0-or-later |
| [Quake II re-release game DLL](https://github.com/id-Software/quake2-rerelease-dll) (id Software) | `rerelease/game.h` | The added surface flag bits (alpha test and the N64 scrolling bits) | GPL-2.0 |
| [ericw-tools](https://github.com/ericwa/ericw-tools) | `.wal_json` handling and documentation | The sidecar's fields (`width`, `height`, `flags`, `contents`, `value`, `animation`, `color`) | GPL-2.0-or-later |
| [QuakeSpasm](https://github.com/sezero/quakespasm), [DarkPlaces](https://github.com/DarkPlacesEngine/darkplaces), [FTEQW](https://github.com/fte-team/fteqw) | Lightmap overbright, fullbright and companion-image loading | The modern-port lightmap scale and the `_norm`, `_gloss`, `_glow`, `_luma`, `_pants`, `_shirt` and `_reflect` companions | GPL-2.0-or-later |

The node graph, its layout, the text edits that keep a script's layout, the
renderer (GLSL shaders on OpenGL or Vulkan since 2026-10-08; see
[3D Rendering: OpenGL And Vulkan](#3d-rendering-opengl-and-vulkan-2026-10-08)),
the library scan and every user interface are VibeStudio's own.

## 3D Rendering: OpenGL And Vulkan (2026-10-08)

Every 3D view (the Levels camera, model previews, the modeller, the Doom
preview) and the material previews draw on the GPU through VibeStudio's own
frame layer in `src/core/render_*`, with an OpenGL and a Vulkan backend; the
CPU renderers they replace are removed. The layer, its shaders in
`src/core/shaders/`, the material shaders' port of the engine rules credited
above, the wireframe coverage and the ID-buffer picking are VibeStudio's own.

| Upstream | Used for | Licence | Revision and review |
| --- | --- | --- | --- |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers/tree/409c16be502e39fe70dd6fe2d9ad4842ef2c9a53) (The Khronos Group) | `include/vulkan/vulkan_core.h`, `vk_platform.h` and the `include/vk_video` headers it includes, vendored unchanged in `external/graphics/vulkan-headers` with `LICENSE.md` and `LICENSES/` ([integration notes](../external/graphics/vulkan-headers/VIBESTUDIO.md)) | Apache-2.0 (the repository also offers MIT for other files) | v1.4.313, `409c16be502e39fe70dd6fe2d9ad4842ef2c9a53`, reviewed 2026-10-08 |
| [glslang](https://github.com/KhronosGroup/glslang) (The Khronos Group) | `glslangValidator` from the Vulkan SDK compiles the shaders to SPIR-V and checks their OpenGL 3.3 core and OpenGL ES 3.0 forms in `scripts/build_render_shaders.py`; only its output, compiled from VibeStudio's sources, is committed | BSD-3-Clause and others (tool only; not linked or distributed) | 15.x, reviewed 2026-10-08 |
| [Mesa](https://docs.mesa3d.org/drivers/llvmpipe.html) lavapipe, Windows build from [mesa-dist-win](https://github.com/pal1000/mesa-dist-win) (pal1000) | CI only: Mesa's Vulkan driver that runs on the processor draws the 3D tests on hosted runners. Windows CI uses `vulkan_lvp.dll` and `lvp_icd.x86_64.json` from the `mesa3d-26.2.4-release-msvc.7z` package, fetched and hash-checked by `scripts/install_software_vulkan.py`; Linux CI installs Ubuntu's `mesa-vulkan-drivers` | MIT (Mesa and mesa-dist-win); the driver contains LLVM (Apache-2.0 with LLVM exceptions); test tool only, not linked or distributed | Mesa 26.2.4, SHA-256 `351fc8c8b695878ffb3eaa044b3ead08672a48b1a045e3c3e3975811df0f6695`, reviewed 2026-10-09 |
| [Vulkan Runtime](https://vulkan.lunarg.com/sdk/home#windows) (LunarG; the Khronos Vulkan loader) | CI only: `vulkan-1.dll` and `vulkaninfo.exe` from the runtime components ZIP give Windows CI a Vulkan loader for lavapipe, fetched and hash-checked by `scripts/install_software_vulkan.py` | Apache-2.0 and MIT; test tool only, not linked or distributed | 1.4.363.0, SHA-256 `a25a927aa8b9f0371048f1861cf88ac3b9bc9b1fb332c42d897c8ab32695769a`, reviewed 2026-10-09 |
| [Vulkan 1.0 specification](https://registry.khronos.org/vulkan/specs/1.0/html/), [OpenGL 3.3 core](https://registry.khronos.org/OpenGL/specs/gl/glspec33.core.pdf), [OpenGL ES 3.0](https://registry.khronos.org/OpenGL/specs/es/3.0/es_spec_3.0.pdf) and [GLSL](https://registry.khronos.org/OpenGL/index_gl.php) specifications | API, synchronisation, rasterisation and texel-fetch rules both backends follow | Specification text; facts only | Read 2026-10-08 |
| [Qt 6](https://doc.qt.io/qt-6/qopenglcontext.html) | `QOpenGLContext`, `QOffscreenSurface` and `QOpenGLExtraFunctions` for the OpenGL backend; `QLibrary` opens the Vulkan loader | GPL-3.0 / LGPL-3.0 | 6.10.1 headers, reviewed 2026-10-08 |
| Cass Everitt, ["Interactive Order-Independent Transparency"](https://developer.nvidia.com/content/interactive-order-independent-transparency) (NVIDIA, 2001) | Depth peeling: translucent skins drawn as up to four layers, nearest first, and composited | Published technique | Reviewed 2026-10-08 |
| Nathan Reed, ["Depth Precision Visualized"](https://developer.nvidia.com/content/depth-precision-visualized) (NVIDIA, 2015) | Reversed depth with an infinite far plane, so distant level geometry keeps its precision | Published technique | Reviewed 2026-10-08 |

No code was copied from these sources. The renderer opens the Vulkan loader
(`vulkan-1`, `libvulkan.so.1`, the macOS loader or MoltenVK) and the system's
OpenGL driver at run time; VibeStudio does not distribute either.

## Audio Sample Rate Conversion

The in-tree WAV writer uses factual layouts from Microsoft's
[WAVEFORMATEXTENSIBLE documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ksmedia/ns-ksmedia-waveformatextensible)
(updated 2023-03-13) and the Microsoft WAVE specifications indexed by
[Peter Kabal, McGill MMSP](https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/WAVE.html)
(updated 2022-09-27), reviewed 2026-10-04. This covers integer/float subtype IDs,
extension fields, speaker masks, RIFF padding, and `fact` frame counts. These are
format facts; no external prose or implementation code was copied. The writer,
bounded file service, TPDF implementation, and fixtures are original code.

Sample rate converter designed by Aleksey Vaneev of Voxengo.

[`CDSPResampler24` and supporting headers](https://github.com/avaneev/r8brain-free-src/blob/cb2abb9977efe2471979b380ed95daa56ab4fdb9/CDSPResampler.h)
from **r8brain-free-src 7.5**, revision
`cb2abb9977efe2471979b380ed95daa56ab4fdb9`, are included unchanged in
`external/audio/r8brain-free-src`. The MIT licence was reviewed on 2026-10-04
before incorporation and is compatible with VibeStudio's GPL-3.0 licence.
The included [`fft/fft4g.h`](https://github.com/avaneev/r8brain-free-src/blob/cb2abb9977efe2471979b380ed95daa56ab4fdb9/fft/fft4g.h)
wraps Takuya Ooura's FFT, copyright 1996–2001, under the
[author's permissive use/copy/modify/distribute terms](https://www.kurims.kyoto-u.ac.jp/~ooura/fft.html),
also reviewed for GPL-3.0 compatibility. Original notices, both licence texts,
and a file-hash manifest are retained; portable releases and Meson installs
include the attribution. Optional PFFFT and Intel IPP backends are not included.
VibeStudio's bounded worker/CLI wrapper and numerical tests are original code.

## PakFu Lineage
VibeStudio intentionally uses [PakFu](https://github.com/themuffinator/PakFu)
as a reference for repository layout, Meson/Qt conventions, README style,
GitHub Actions structure, archive/package handling, game installation
detection, parser hardening, and credits practice.

When PakFu code is moved into VibeStudio, this file must name the source module
and revision when practical.

Current adapted modules:

| VibeStudio area | PakFu reference | Revision | Notes |
|---|---|---|---|
| Package/archive interface, read-only readers, and virtual path safety in `src/core/package_archive.*` | `src/archive/archive_entry.h`, `src/archive/archive.h`, `src/archive/archive_session.h`, `src/archive/dir_archive.*`, `src/archive/path_safety.h`, `src/pak/pak_archive.*`, `src/wad/wad_archive.*`, and `src/zip/zip_archive.*` | `c82dfb0ef0b5d7442e243ace8cd83bc45f82f257` | VibeStudio reimplements the concepts as a smaller core layer with explicit path-safety issue reporting, normalized virtual paths, safe output joining, folder/PAK/WAD/ZIP/PK3 entry listing, and mount-layer metadata. |
| Package staging, manifest, and save-as writer concepts in `src/core/package_staging.*` | PakFu archive write-back direction, including deterministic archive handling and explicit safe output paths | `c82dfb0ef0b5d7442e243ace8cd83bc45f82f257` | Conceptual adaptation only: VibeStudio owns the staged operation model, conflict reporting, package manifests, deterministic PAK/ZIP/PK3 writers, and tested PWAD writer implementation in this repository. |
| Asset preview/edit workflow concepts in `src/core/asset_tools.*` and `src/core/package_preview.*` | PakFu package browsing, format-aware preview, and durable CLI workflow direction | `c82dfb0ef0b5d7442e243ace8cd83bc45f82f257` | Conceptual adaptation only: VibeStudio owns the Qt image conversion path, WAV helper, local text highlighter boundary, and native MDL/MD2/MD3 metadata readers in this repository. No third-party game assets are imported. |
| Game installation profile and detection workflow concepts in `src/core/game_installation.*` | PakFu's profile-driven game installation and detection workflow direction | `c82dfb0ef0b5d7442e243ace8cd83bc45f82f257` | Conceptual adaptation only: VibeStudio owns the Qt-native profile structs, defaults, validation, and Steam/GOG candidate heuristics in this repository. |

## Level Dependencies And Selective Packages (2026-10-03)

`src/core/level_dependencies.*`, `src/core/package_selection.*`, and the package
subset UI take conceptual inspiration from [PakFu's CLI asset graph and entry/prefix selectors](https://github.com/themuffinator/PakFu/blob/13111e4c07513548a29fd7eb74fc9d004c44aa59/src/cli/cli.cpp)
and [archive search index](https://github.com/themuffinator/PakFu/blob/13111e4c07513548a29fd7eb74fc9d004c44aa59/src/archive/archive_search_index.cpp).
Reference checkout: `13111e4c07513548a29fd7eb74fc9d004c44aa59`, including local
uncommitted workflow changes inspected on 2026-10-03. Both repositories carry
GPL-3.0 licences, reviewed before implementation. No PakFu source was copied.

The shader parser's skybox and light-image reference extraction follows factual
syntax in the [Quake III Shader Manual, revision 12, sections 3.1 and 4.6](https://icculus.org/gtkradiant/documentation/Q3AShader_Manual/index.htm),
by Paul Jaquays and Brian Hook and credited contributors (consulted 2026-10-03).
The manual is a specification reference; no manual prose or implementation code
was imported. Fixture data is generated by VibeStudio's tests.

The connected model-design/package/level workflow also reviewed PakFu's model
surface/material representation in
[`src/formats/model.h`](https://github.com/themuffinator/PakFu/blob/13111e4c07513548a29fd7eb74fc9d004c44aa59/src/formats/model.h)
and the archive search index linked above (GPL-3.0, same checkout and review
date). VibeStudio's parametric design editor, generated-byte staging snapshots,
material dependency expansion, and atomic stage-and-place service are original
implementations; no PakFu code was copied.

The MD3 writer in `src/core/model_export.cpp`, shared by primitive designs and
editable animated meshes, uses factual layout and
format limits from id Software's
[`code/qcommon/qfiles.h`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/qcommon/qfiles.h)
and normal reconstruction conventions in the released Quake III source
(GPL-2.0-or-later, `master` reviewed 2026-10-03; compatible with this GPL-3.0
repository). No upstream implementation was copied. Primitive geometry and
sample designs are generated mathematically and contain no game assets.

The animated writer also checks the original renderer's stricter surface limits
against [`R_LoadMD3` in `code/renderer/tr_model.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_model.c).
Both that source and `qfiles.h` were reviewed on 2026-10-04 at `master`, under
GPL-2.0-or-later (compatible with GPL-3.0). Mesh document storage, topology/UV
operations, history, and test fixtures are original implementations.

Attachment basis interpretation in `src/core/model_tags.cpp` and the viewport's
local-axis overlay follows the convention in
[`CG_PositionEntityOnTag` in `code/cgame/cg_ents.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/cgame/cg_ents.c)
from id Software's Quake III Arena (`master` reviewed 2026-10-04,
GPL-2.0-or-later, compatible with GPL-3.0). Each stored row is a local basis
vector in model coordinates. The tag operations, controls and fixtures are
original implementations; no engine function or game asset was copied.

The original MD2 writer in `src/core/model_export.cpp` uses Quake II's
[`qcommon/qfiles.h`](https://github.com/id-Software/Quake-2/blob/master/qcommon/qfiles.h)
for layout and limits, [`ref_gl/gl_mesh.c`](https://github.com/id-Software/Quake-2/blob/master/ref_gl/gl_mesh.c)
for GL command consumption, and [`ref_soft/r_model.c`](https://github.com/id-Software/Quake-2/blob/master/ref_soft/r_model.c),
[`r_image.c`](https://github.com/id-Software/Quake-2/blob/master/ref_soft/r_image.c),
and [`r_polyse.c`](https://github.com/id-Software/Quake-2/blob/master/ref_soft/r_polyse.c)
for software renderer bounds and non-wrapping skin access. The texel-centre
convention was checked against Quake II Tools'
[`qdata/models.c`](https://github.com/id-Software/Quake-2-Tools/blob/master/qdata/models.c).
The original MD2 import consistency audit in `src/core/model_mesh.cpp`
checks strip/fan winding and texel coordinates against indexed triangles using
those same format/renderer references. Normal matching reuses the existing transcribed
[`ref_gl/anorms.h`](https://github.com/id-Software/Quake-2/blob/master/ref_gl/anorms.h)
table in `model_mesh.cpp`; decoder UV sampling now follows the same convention.
All sources are GPL-2.0-or-later, compatible with this GPL-3.0 repository,
`master` reviewed 2026-10-04. No upstream compiler or renderer implementation
was copied. All fixtures are generated geometry without commercial game data.

MDL group, indexed-skin and header preservation/export in `src/core/model_mesh.cpp`
and `src/core/model_mdl*` follows id Software's Quake
[`WinQuake/modelgen.h`](https://github.com/id-Software/Quake/blob/master/WinQuake/modelgen.h)
and [`WinQuake/model.c`](https://github.com/id-Software/Quake/blob/master/WinQuake/model.c),
with original renderer limits and group playback behavior from
[`gl_model.c`](https://github.com/id-Software/Quake/blob/master/WinQuake/gl_model.c),
[`gl_model.h`](https://github.com/id-Software/Quake/blob/master/WinQuake/gl_model.h),
and [`gl_draw.c`](https://github.com/id-Software/Quake/blob/master/WinQuake/gl_draw.c),
`master` reviewed 2026-10-04. These sources are GPL-2.0-or-later, compatible
with this repository's GPL-3.0 licence. The metadata, JSON storage, timing-edit
logic, compatible-seam packing, writer and synthetic fixtures are original
implementations; no commercial
palette, skin or model is included. The native preview/CLI sampler in
`src/core/model_mdl_playback.cpp` follows `R_AliasSetupSkin`/`R_AliasSetupFrame` in
[`r_alias.c`](https://github.com/id-Software/Quake/blob/master/WinQuake/r_alias.c),
`R_SetupAliasFrame`/`R_DrawAliasModel` in
[`gl_rmain.c`](https://github.com/id-Software/Quake/blob/master/WinQuake/gl_rmain.c),
and `Mod_LoadAllSkins` in `gl_model.c`, under the same licence and review date.
The sampler is independently implemented with bounded modular arithmetic and
explicit deterministic phase; no engine functions are copied.

## Model Native Engine Acceptance

The winding conversion in `src/core/model_mesh.cpp`, `model_export.cpp` and
`model_mdl_export.cpp` follows the clockwise front-face convention in id
Software's [Quake `WinQuake/gl_rmain.c`](https://github.com/id-Software/Quake/blob/master/WinQuake/gl_rmain.c),
[Quake II `ref_gl/gl_rmain.c`](https://github.com/id-Software/Quake-2/blob/master/ref_gl/gl_rmain.c)
and [Quake III `code/renderer/tr_backend.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_backend.c).
Native byte-layout oracles in `src/tests/model_winding_smoke_test.cpp` and
`model_engine_workflow.py` use the model layout references credited above. MD3
tag basis semantics were checked against Quake III's
[`tr_model.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_model.c)
and [`cg_ents.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/cgame/cg_ents.c).
All are GPL-2.0-or-later, compatible with this repository's GPL-3.0 licence;
`master` source references reviewed 2026-10-05. Implementations and generated
fixtures are original; no renderer functions or commercial game data were copied.

The optional dedicated-server fixture in `src/tests/model_engine_fixture.qc`
uses public API names from Quake's
[`progdefs.q1`](https://github.com/id-Software/Quake/blob/master/WinQuake/progdefs.q1)
and [FTE's `engine/server/pr_cmds.c`](https://github.com/fte-team/fteqw/blob/master/engine/server/pr_cmds.c).
Runtime/command conventions were checked in FTE's
[`sv_sys_unix.c`](https://github.com/fte-team/fteqw/blob/master/engine/server/sv_sys_unix.c),
[`common/fs.c`](https://github.com/fte-team/fteqw/blob/master/engine/common/fs.c),
[`common/net_wins.c`](https://github.com/fte-team/fteqw/blob/master/engine/common/net_wins.c)
and [QuakeC compiler](https://github.com/fte-team/fteqw/tree/master/engine/qclib).
Player trace-mask selection follows
[`server/world.c`](https://github.com/fte-team/fteqw/blob/master/engine/server/world.c).
The MD3 frame-name/tag differences documented in
[Model Engine Acceptance](MODEL_ENGINE_ACCEPTANCE.md) were traced through
[`common/com_mesh.c`](https://github.com/fte-team/fteqw/blob/master/engine/common/com_mesh.c)
and [`client/pr_skelobj.c`](https://github.com/fte-team/fteqw/blob/master/engine/client/pr_skelobj.c).

The original offscreen fixtures `src/tests/model_render_fixture.qc` and
`model_assembly_render_fixture.qc` use MenuQC
API/ABI declarations in
[`client/pr_menu.c`](https://github.com/fte-team/fteqw/blob/master/engine/client/pr_menu.c),
[`common/pr_common.h`](https://github.com/fte-team/fteqw/blob/master/engine/common/pr_common.h)
and [`client/pr_csqc.c`](https://github.com/fte-team/fteqw/blob/master/engine/client/pr_csqc.c).
Their shared Python runner `model_render_common.py` follows
[`gl/gl_videgl.c`](https://github.com/fte-team/fteqw/blob/master/engine/gl/gl_videgl.c)
for the EGL pbuffer and
[`client/cl_screen.c`](https://github.com/fte-team/fteqw/blob/master/engine/client/cl_screen.c)
for registered render-target screenshots. Replacement-model behavior was
checked in [`gl/gl_model.c`](https://github.com/fte-team/fteqw/blob/master/engine/gl/gl_model.c)
and [`client/renderer.c`](https://github.com/fte-team/fteqw/blob/master/engine/client/renderer.c).
The MDL skin-padding guidance and deliberate flood-fill control follow
`Mod_FloodFillSkin` in Quake's
[`WinQuake/gl_model.c`](https://github.com/id-Software/Quake/blob/master/WinQuake/gl_model.c)
(GPL-2.0-or-later) and FTE's `common/com_mesh.c`. No flood-fill implementation is
copied or added to the studio. Pixel projection/oracles and all generated assets
are original. Pillow's existing external-test attribution below also covers this
runner; external [Mesa](https://docs.mesa3d.org/license.html) supplies EGL/llvmpipe
(primarily MIT; no Mesa code is imported or linked into VibeStudio).

The assembly workflow's direct MD2/MD3 layout checks use the same id Software
model-format references credited above. Its closed-form attachment geometry,
normal calculations, pixel-centre silhouette checks and generated panels are
original; they do not call the studio sampler or import engine implementations.
The shared runner and assembly extension were reviewed on 2026-10-06; the
original engine references were reviewed on 2026-10-05. FTE's repository
[`LICENSE`](https://github.com/fte-team/fteqw/blob/master/LICENSE) contains GPL-2.0;
individual engine files retain their GPL-2.0-or-later notices where present.
API-only fixture declarations do not import engine implementations, and the
engine/compiler remain separate processes from this GPL-3.0 application.
The reference checkout has no revision metadata;
the audit retains its per-file SHA-256 inventory and built-tool hashes. FTE and
FTEQCC remain separate external GPL tools, with their licence notices preserved
in the temporary reference copy. No engine/compiler implementation is copied
into VibeStudio or linked into its binaries.

## Model Delivery Targets And The Skeletal Writer Oracle (2026-10-22)

The delivery-target table, preflight, Prepare for Target, saved targets and
detail levels (`src/core/model_targets.*`, `src/core/model_target_prepare.*`,
`src/core/model_delivery.*`, `src/core/model_lod.*`, the target checks in
`src/core/model_export.cpp`, `model_mdl_export.cpp` and `model_format_iqm.cpp`,
`src/cli/model_tools.cpp`, `src/app/model_editor_tools.cpp`,
`src/app/model_editor_export.cpp`, `src/app/model_editor_targets.cpp`,
`src/app/model_lod_dialog.*`) and the independent writer oracle
(`src/tests/model_skeletal_writer_oracle.py`,
`model_skeletal_writer_oracle_test.cpp`) are original VibeStudio code. The
sources below supplied facts only: each engine's model count limits, as the
constants and checks of its loader and renderer state them, and the
conventions the oracle reimplements to read MD5, IQM and ASE files the way the
engines do. No engine code is copied; every fixture is built by the tests. See
[Model Delivery Targets](MODEL_TARGETS.md) and
[engine acceptance](MODEL_ENGINE_ACCEPTANCE.md#independent-md5-iqm-and-ase-reader).

| Reference | What it informed | Revision | Licence / use |
|---|---|---|---|
| [Quake](https://github.com/id-Software/Quake) (`WinQuake/gl_model.h`, `gl_model.c`, `model.c`, `r_local.h`, `d_iface.h`, `gl_draw.c`) | The original MDL limits: `MAXALIASVERTS`, `MAXALIASTRIS`, `MAXALIASFRAMES`, `MAX_SKINS`, `MAX_LBM_HEIGHT`, the skin width multiple and the 640 by 480 texture buffer | master, reviewed 2026-10-22 | GPL-2.0-or-later; limits only |
| [QuakeSpasm](https://github.com/sezero/quakespasm) (`Quake/gl_model.h`, `Quake/gl_model.c`) | QuakeSpasm's MDL limits | master, reviewed 2026-10-22 | GPL-2.0-or-later; limits only |
| [FTE QuakeWorld](https://github.com/fte-team/fteqw) (`engine/common/com_mesh.c`, `engine/client/render.h`, `engine/gl/gl_model.h`) | FTE's MDL seam vertices, 16-bit index builds and `MAX_BONES` | master snapshot, reviewed 2026-10-22 | GPL-2.0-or-later; limits only |
| [DarkPlaces](https://github.com/DarkPlacesEngine/DarkPlaces) (`model_alias.c`) | DarkPlaces' MDL bounds checks | master, reviewed 2026-10-22 | GPL-2.0-or-later; limits only |
| [Quake II](https://github.com/id-Software/Quake-2) (`qcommon/qfiles.h`, `ref_gl/gl_model.c`, `ref_soft/r_model.c`, `ref_gl/gl_image.c`) and [Yamagi Quake II](https://github.com/yquake2/yquake2) (`src/common/header/files.h`, `src/client/refresh/files/models.c`) | The MD2 limits | master, reviewed 2026-10-22 | GPL-2.0-or-later; limits only |
| [Q2PRO](https://github.com/q2pro/q2pro), the preserved upstream of the former github.com/skullernet/q2pro (`inc/format/md2.h`, `inc/format/md3.h`, `src/refresh/models.c`, `src/refresh/gl.h`) | Q2PRO's MD2, MD3 and MD5 replacement limits: `MD2_MAX_*`, `MD3_MAX_*`, `MD5_MAX_*`, `TESS_MAX_VERTICES` (raised to 6144 in `0dc71b8`, 2024-07-21) and how `MOD_LoadMD5` finds `md5/` files and skins | `601a8df` (2025-12-11), reviewed 2026-10-22 | GPL-2.0-or-later; limits only |
| [Quake III Arena](https://github.com/id-Software/Quake-III-Arena) (`code/qcommon/qfiles.h`, `code/renderer/tr_model.c`, `code/renderer/tr_surface.c`) | `SHADER_MAX_VERTEXES`, `SHADER_MAX_INDEXES` and `MD3_MAX_LODS`; the working limit set by `RB_CheckOverflow`; the detail-level names | master, reviewed 2026-10-22 | GPL-2.0-or-later; limits only |
| [ioquake3](https://github.com/ioquake/ioq3) (`code/renderergl1/tr_model.c`, `tr_model_iqm.c`, `code/renderercommon/iqm.h`) and [Quake3e](https://github.com/ec-/Quake3e) (`code/renderer/tr_model.c`, `tr_model_iqm.c`, `iqm.h`) | MD3 and IQM load limits, `IQM_MAX_JOINTS` | main and master, reviewed 2026-10-22 | GPL-2.0-or-later; limits only |
| [Quake III Arena](https://github.com/id-Software/Quake-III-Arena) (`code/renderer/tr_mesh.c` `R_ComputeLOD`, `ProjectRadius` and `R_AddMD3Surfaces`, `tr_main.c` `R_SetupProjection`, `tr_model.c` `R_RegisterModel` and `R_LerpTag`, `code/cgame/cg_view.c` `CG_CalcFov`) | The detail-level switch distances, the frame check against the base model, the empty slot a missing level leaves, and that tags come from the base model | master, reviewed 2026-10-22 | GPL-2.0-or-later; behaviour only |
| [Return to Castle Wolfenstein single player](https://github.com/id-Software/RTCW-SP) and [multiplayer](https://github.com/id-Software/RTCW-MP) (`src/qcommon/qfiles.h`, `src/renderer/tr_model.c`, `src/renderer/tr_surface.c`) | `SHADER_MAX_VERTEXES`, `SHADER_MAX_INDEXES` and `MD3_MAX_LODS` of each game, the `RB_CheckOverflow` working limits, and MDC loading before MD3 for the base model and every detail level | master, reviewed 2026-10-22 | GPL-3.0-or-later with id's additional terms; limits only |
| [Wolfenstein: Enemy Territory](https://github.com/id-Software/Enemy-Territory) (`src/qcommon/qfiles.h`, `src/renderer/tr_init.c`, `src/renderer/tr_model.c`, `src/renderer/tr_surface.c`) | Enemy Territory's 1,025-vertex batches, three detail levels and MDC loading before MD3 for the base model and every detail level | master, reviewed 2026-10-22 | GPL-3.0-or-later with id's additional terms; limits only |
| [GZDoom](https://github.com/ZDoom/gzdoom) (`src/common/models/models_md3.cpp`, `models_iqm.cpp`, `model.h`, `src/r_data/models.cpp`) | That GZDoom sets no MD3 count limits, MODELDEF's 32-surface `SurfaceSkin` range, and IQM's one-byte blend indexes and weights | master `c26ce2e` (2026-08-10), reviewed 2026-10-22 | GPL-3.0-or-later; limits only |
| [Inter-Quake Model](https://github.com/lsalzman/iqm) by Lee Salzman (`iqm.txt`, `iqm.h`, the reference demo) | The oracle's IQM reader: header, arrays, joints, poses, channels, `Matrix3x3(rotation, scale)` and frame concatenation | master, reviewed 2026-10-22 | MIT; specification only |
| [Doom 3 GPL source](https://github.com/id-Software/DOOM-3) (`neo/renderer/Model_md5.cpp`, `Model_ase.cpp`, `tr_trisurf.cpp`, `neo/idlib/math/Quat.h`, `Quat.cpp`, `JointTransform.h`, `neo/game/anim/Anim.cpp`, `neo/framework/FileSystem.cpp`, `DeclManager.cpp`) | The oracle's MD5 and ASE readers: `CalcW`, `ToMat3`, weight offsets, joint concatenation, face planes and material naming; Doom 3's absence of MD5 count limits | GPL release, reviewed 2026-10-22 | GPL-3.0 with id's additional terms; conventions only |
| q3map2 in [q3mapx](https://github.com/themuffinator/q3mapx) (`tools/quake3/q3map2/model.cpp`, `AssModelMesh`) | The oracle's q3map2 ASE shader-name rule | `8975244`, reviewed 2026-10-22 | GPL-2.0-or-later; behaviour only |

## Imported Compiler Toolchains

| Tool | Role | Upstream | Imported revision | License notes |
|---|---|---|---|---|
| VibeMap2 | VibeStudio's Quake/idTech2 compilers: `vibemap2-bsp`, `vibemap2-vis`, `vibemap2-light`, the `vibemap2-bspinfo`, `vibemap2-bsputil` and `vibemap2-maputil` utilities, and the `vibemap2-hub` build and preview GUI. A fork developed as part of the VibeStudio project, derived from ericw-tools. | [themuffinator/VibeyMapTools](https://github.com/themuffinator/VibeyMapTools), branch `main` (to be renamed VibeMap2) | `4495049a9e4c1f6deadae3a76b8256614840af35` | GPL-3.0; keeps ericw-tools' history and notices. See `external/compilers/vibemap2/COPYING`; release bundles carry it as `licenses/external/compilers/VibeMap2/COPYING`. |
| VibeMap3 | VibeStudio's Quake III/idTech3 compiler (`vibemap3`) for BSP compile, VIS, light, conversion and packaging, plus the `vibemap3-workbench` GUI. A fork developed as part of the VibeStudio project: a performance-focused continuation of q3map2 from NetRadiant Custom revision `8216133`. | [themuffinator/q3mapx](https://github.com/themuffinator/q3mapx), branch `main` (to be renamed VibeMap3) | `897524439cb58d2b736bc96b16c231d22dc5ddce` | GPL-3.0-or-later; imported q3map2 files keep their GPL-2.0-or-later notices, and the inherited NetRadiant Custom tree keeps its per-file GPL/LGPL/BSD licensing. See `external/compilers/vibemap3/COPYING`, `LICENSE`, `GPL`, and `LGPL`; release bundles carry `licenses/external/compilers/VibeMap3/COPYING`. |
| ZDBSP | Doom-family node builder | [rheit/zdbsp](https://github.com/rheit/zdbsp) | `bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659` | GPL-2.0-or-later. See `external/compilers/zdbsp/COPYING`. |
| ZokumBSP | Doom-family node/blockmap/reject builder | [zokum-no/zokumbsp](https://github.com/zokum-no/zokumbsp) | `22af6defeb84ce836e0b184d6be5e80f127d9451` | GPL-2.0 text in `external/compilers/zokumbsp/src/COPYING`; based on ZenNode lineage credited upstream. |

These compiler projects are imported as submodules so their history and license
files remain intact. VibeStudio invokes them as external tools until a specific
source-level integration has a documented compatibility review. VibeMap2 and
VibeMap3 are VibeStudio's own forks; their URLs, branch, pins and reasons are
recorded in [Compiler Integration](COMPILER_INTEGRATION.md#vibestudio-compilers).

### Compiler Upstreams

The compiler known-issue reconciliation and Quake map preflight decisions use
VibeMap2's [upstream audit](https://github.com/themuffinator/VibeyMapTools/blob/4495049a9e4c1f6deadae3a76b8256614840af35/docs/upstream-audit.rst)
and [3.1.0 changelog](https://github.com/themuffinator/VibeyMapTools/blob/4495049a9e4c1f6deadae3a76b8256614840af35/CHANGELOG.md),
reviewed 2026-10-09 at that pinned revision (GPL-3.0). These are documented
behaviour/evidence references; no compiler implementation was copied.

VibeMap2 and VibeMap3 build on these projects, which stay credited as their
upstreams. Until VibeStudio moved to its own compilers, both were imported here
as submodules at the revisions below, and VibeStudio's code comments and format
credits still cite those revisions.

| Upstream | Relationship | Link | Reviewed revision | License notes |
|---|---|---|---|---|
| ericw-tools | Upstream of VibeMap2. Its `qbsp`, `vis`, `light`, `bspinfo`, `bsputil` and `lightpreview` were VibeStudio's Quake/idTech2 compilers before VibeMap2, and its issue tracker supplies the known-issue catalogue's issue numbers. | [ericwa/ericw-tools](https://github.com/ericwa/ericw-tools) | `f80b1e216a415581aea7475cb52b16b8c4859084` | GPL-2.0-or-later; upstream notes GPL-3.0+ compatibility for Embree-enabled builds (its `COPYING` and `gpl_v3.txt`). |
| q3map2 from NetRadiant Custom | Upstream of VibeMap3, which starts from NetRadiant Custom revision `8216133`. q3map2 was VibeStudio's Quake III compiler before VibeMap3. | [Garux/netradiant-custom](https://github.com/Garux/netradiant-custom) | `68ecbed64b7be78741878c730279b5471d978c7c` | Mixed GPL/LGPL/BSD by file; upstream marks Quake III tools, including q3map2, as GPL (its `LICENSE`, `GPL`, and `LGPL`). |

## Format And Engine Lineage
VibeStudio targets public idTech-era formats and workflows from the Doom,
Quake, Quake II, and Quake III ecosystems. Format behavior should be credited
to public specifications, open-source engine/tool references, or source-port
documentation whenever implementation details are derived from them.

Every format and algorithm implemented in `src/core` is credited in the next
section with a link, the module that relies on it, and a revision or date where
the source carries one. The tables mirror the citation comments kept at the top
of each header and implementation, so a reader can move from a credit to the
code that depends on it without guessing.

## Specification And Format References

### Asset And Licensing Boundary
- No commercial game asset, palette, texture, sound, model, or data file is
  embedded in this repository. The only game-shaped sample file,
  `samples/projects/doom-minimal/maps/doom_stub.wad`, is plain text that says so
  in its own first bytes; it exists only so command-planning smoke tests have a
  path to point at.
- Real palettes are resolved at runtime from packages the user already owns.
  `resolveIdTechPalette()` and `resolveIdTechPaletteFromDirectory()` in
  `src/core/idtech_image.cpp` look for `gfx/palette.lmp`, `PLAYPAL`, and
  `pics/colormap.pcx` inside the opened package, and WAD3 miptex entries carry
  their own palette tail.
- The built-in palettes are generated stand-ins, not shipped palettes.
  `generatedIdTechPalette()` builds each one procedurally from a hue/saturation
  recipe in `kPaletteRecipes`; every display name ends in "(generated)", each
  describes itself as "Procedurally generated ramp palette (not a game
  palette)", and a decode that falls back to one raises an explicit warning.
  They intentionally do not reproduce any commercial palette.
- Specifications are consulted for layout and algorithm only. Where VibeStudio
  follows a released tool's convention - for example the `PlaneFromPoints`
  winding convention and the qbsp epsilon constants in
  `src/core/map_geometry.cpp` - the code reimplements it and says so in the
  comment beside the constant.

### Compression And Archive Formats

The incremental `DeflateStreamEncoder` and two-pass ZIP writer are original
VibeStudio GPL-3.0 implementations, reviewed against RFC 1951 version 1.3
(May 1996), sections 3.2.4–3.2.7, on 2026-10-04. They reuse the already credited
format tables and package-merge implementation below. No upstream compressor
code or specification prose was copied, and no dependency/license changed.

| Specification | Reference | VibeStudio modules | Revision / date |
|---|---|---|---|
| DEFLATE Compressed Data Format Specification | [IETF RFC 1951](https://www.rfc-editor.org/rfc/rfc1951) | `src/core/deflate.h`, `src/core/deflate.cpp` - inflate, fixed-Huffman deflate, length/distance/code-length tables transcribed from sections 3.2.4-3.2.7 | Version 1.3, May 1996 |
| DEFLATE dynamic Huffman blocks, RFC 1951 sections 3.2.2 and 3.2.7 | [IETF RFC 1951](https://www.rfc-editor.org/rfc/rfc1951) | `src/core/deflate.cpp` - `buildDynamicTrees()` writes the `HLIT`/`HDIST`/`HCLEN` header, `runLengthEncodeLengths()` emits the 16/17/18 repeat codes in the code-length transmission order, `canonicaliseCodes()` assigns canonical codes, and `deflateRaw()` measures a stored, a fixed and a dynamic block per chunk and keeps the smallest | Version 1.3, May 1996 |
| Package-merge length-limited Huffman construction | Larmore and Hirschberg, "A Fast Algorithm for Optimal Length-Limited Huffman Codes", [Journal of the ACM 37(3)](https://doi.org/10.1145/79147.79150) | `src/core/deflate.cpp` - `buildLimitedLengths()`, which produces the 15-bit literal/length and distance code lengths and the 7-bit code-length alphabet that RFC 1951 section 3.2.7 requires | 1990 |
| ZLIB Compressed Data Format Specification | [IETF RFC 1950](https://www.rfc-editor.org/rfc/rfc1950) | `src/core/deflate.cpp` - `inflateZlib()` header/Adler-32 handling, `adler32Bytes()` | Version 3.3, May 1996 |
| CRC-32 as used by GZIP and the ZIP appnote (ITU-T V.42 polynomial) | [IETF RFC 1952](https://www.rfc-editor.org/rfc/rfc1952) | `src/core/deflate.cpp` - `crc32Bytes()`, consumed by the ZIP reader and writer | Version 4.3, May 1996 |
| PKWARE .ZIP File Format Specification (APPNOTE.TXT), including ZIP64 and Unicode paths | [APPNOTE.TXT](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT) | Original implementations in `src/core/package_zip.cpp`, `package_archive.cpp` and `package_staging.cpp`: central/local headers, descriptors, ZIP64 end records, extra fields `0x0001` and `0x7075`, and deterministic writing | Version 6.3.10 FINAL, revised 2022-11-01; sections 4.3.6–4.3.16, 4.5.3, 4.6.9 and Appendix D |
| Unicode CP437 mapping | [cp437_DOSLatinUS table](https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/PC/CP437.TXT) | `src/core/package_zip.cpp`: fixed high-byte mapping for legacy ZIP filenames; low bytes preserve ASCII/control values | Table 2.00 (1996-04-24), Unicode 2.0; [Unicode License V3](licenses/UNICODE-LICENSE.txt), retrieved 2026-10-06 |
| idTech2 PACK (`.pak`) directory layout | [Quake Wiki `.pak`](https://quakewiki.org/wiki/.pak) and the released id Software Quake sources | `src/core/package_archive.cpp` - `PackageArchive::loadPak()` | Living wiki page; no pinned revision |
| Doom IWAD/PWAD and Quake/Half-Life WAD2/WAD3 directories | [Unofficial Doom Specs v1.666](https://www.gamers.org/dhs/helpdocs/dmsp1666.html), [Quake Wiki WAD](https://quakewiki.org/wiki/WAD), [Valve Developer Community WAD3](https://developer.valvesoftware.com/wiki/WAD) | `src/core/package_archive.cpp` - `PackageArchive::loadWad()`; `src/core/package_staging.cpp` - the PWAD and WAD2/WAD3 writers | Unofficial Doom Specs v1.666 (1994); wiki pages have no pinned revision |

The ZIP reader changes use format facts permitted for reader/writer implementations by APPNOTE section 1.4; no PKWARE implementation or specification prose is copied. Encryption, central-directory encryption and signature authentication are not implemented. The Unicode License V3 permits modification and redistribution of the CP437 mapping with its notice retained, making the mapping compatible with this repository’s GPL-3.0 distribution. The complete Unicode notice is preserved in `docs/licenses/UNICODE-LICENSE.txt` and installed under `share/vibestudio/licenses/unicode`. All 128 high-byte values were checked against the published table.

### idTech Image And Palette Formats
All rows below are implemented in `src/core/idtech_image.h` and
`src/core/idtech_image.cpp`.

The modeller's package-to-indexed-skin handoff reuses these decoders and palette
parsers in `src/core/model_mdl_edit.cpp` and `src/core/model_skin_source.cpp`.
Its snapshot selection, validation and UI/CLI adapters are VibeStudio code; no
additional upstream implementation, palette or game asset was imported.

| Specification | Reference | What it covers | Revision / date |
|---|---|---|---|
| Unofficial Doom Specs | [dmsp1666](https://www.gamers.org/dhs/helpdocs/dmsp1666.html) | Doom picture posts, flats, `PLAYPAL`, `COLORMAP`, and the WAD directory | v1.666, 1994 |
| Doom Wiki picture format | [Picture format](https://doomwiki.org/wiki/Picture_format) | Patch header, per-column offsets, post `{topdelta, length, pad, pixels, pad}` runs | Living wiki page |
| Doom Wiki flat format | [Flat](https://doomwiki.org/wiki/Flat) | Raw 64x64 indexed flat lumps | Living wiki page |
| Doom Wiki PLAYPAL | [PLAYPAL](https://doomwiki.org/wiki/PLAYPAL) | 14 banks of 256 RGB triplets; VibeStudio reads the first bank | Living wiki page |
| Quake Specifications (Olivier Montanuy / Quake Standards Group) | [Quake Documentation](https://www.gamers.org/dEngine/quake/spec/quake-spec34/) | Quake `.lmp` pictures, WAD2 miptex, `gfx/palette.lmp`, and the `.spr` sprite header | "Quake Documentation Version 3.4" |
| Released id Software Quake sources | [id-Software/Quake](https://github.com/id-Software/Quake) | Cross-check for miptex and `spritegn.h` sprite layout; layouts reimplemented, no code copied | GPL source release |
| Quake II BSP / WAL format notes | [flipcode archive](https://www.flipcode.com/archives/Quake_2_BSP_File_Format.shtml) plus the released id Software Quake II headers | Quake II `.wal` 100-byte header, mip chain, and `pics/colormap.pcx` palette | Community reference; no pinned revision |
| Valve Developer Community WAD3 | [WAD](https://developer.valvesoftware.com/wiki/WAD) | Half-Life WAD3 miptex entries and the per-texture 256-colour palette tail | Living wiki page |
| ZSoft PCX File Format Technical Reference Manual | ZSoft's own manual is no longer vendor-hosted; the public [Encyclopedia of Graphics File Formats PCX summary](https://www.fileformat.info/format/pcx/egff.htm) documents the same structure | PCX header, RLE planes, and the 768-byte VGA palette tail | ZSoft revision 5 era |
| Truevision TGA File Format Specification | Truevision's document is likewise unhosted; see the public [Encyclopedia of Graphics File Formats TGA summary](https://www.fileformat.info/format/tga/egff.htm) | Targa header, colour map, RLE and uncompressed image types, origin handling | Version 2.0 |
| GPL Quake II engine sources, `m8tex_t` and `m32tex_t` | [yquake2 `src/common/header/files.h`](https://github.com/yquake2/yquake2/blob/master/src/common/header/files.h), matching id Software's release at [id-Software/Quake-2](https://github.com/id-Software/Quake-2) | Quake II `.m8` (version 2, 1040-byte header, embedded 768-byte palette, sixteen mip entries) and `.m32` (version 4, 968-byte header, RGBA pixels, sixteen mip entries), plus the surface flags, contents, value, animation name and texture scale fields both carry | GPL source release; no pinned revision |
| GPL Quake II engine sources, `dsprite_t` and `dsprframe_t` | The same `files.h` layouts listed above | Quake II `.sp2` sprite container: the `IDS2` ident, version 2, and the per-frame `width`, `height`, `origin_x`, `origin_y` and `name[64]` records naming external images. The container holds no pixels of its own | GPL source release; no pinned revision |
| Half-Life SDK sprite generator | [`utils/sprgen/sprgen.c`](https://github.com/ValveSoftware/halflife/blob/master/utils/sprgen/sprgen.c) | Half-Life `IDSP` version 2 sprites: the extra `texFormat` field (normal, additive, index alpha, alpha test), the `short` palette count and embedded RGB palette that follow the 40-byte header, and the frame/group stream shared with the Quake version 1 dialect | Valve SDK source release; no pinned revision |

### Texture Export Formats

Optional independent raster verification uses [Pillow 11.3.0](https://github.com/python-pillow/Pillow/tree/11.3.0)
under MIT-CMU, verified from installed distribution metadata on 2026-10-04.
It is an external test tool, not bundled code or an application dependency.

The explicit indexed PNG writer independently implements the [PNG third edition,
24 June 2025](https://www.w3.org/TR/2025/REC-png-3-20250624/) chunk, palette,
transparency and scanline layout. The reference carries the [W3C Software and
Document License 2023](https://www.w3.org/copyright/software-license-2023/), reviewed
2026-10-04; no sample code or specification prose is incorporated. Compression
uses the existing Qt dependency and CRC uses VibeStudio's existing core service.

Texture export layouts in `src/core/texture_export.*` are independently implemented
from the following primary sources, reviewed 2026-10-04. No engine source code or
game content was copied. The GPL-2.0-or-later and BSD-3-Clause source licenses are
compatible with VibeStudio's GPL-3.0 license.

- [Quake `bspfile.h`](https://github.com/id-Software/Quake/blob/master/WinQuake/bspfile.h),
  [`wad.h`](https://github.com/id-Software/Quake/blob/master/WinQuake/wad.h),
  [`model.c`](https://github.com/id-Software/Quake/blob/master/WinQuake/model.c), and
  [`r_sky.c`](https://github.com/id-Software/Quake/blob/master/WinQuake/r_sky.c):
  miptexture/WAD2 layout, 16-pixel alignment, compiler texture lump budget and sky dimensions;
  GPL-2.0-or-later, master reviewed on the date above.
- [Quake II `qfiles.h`](https://github.com/id-Software/Quake-2/blob/master/qcommon/qfiles.h)
  and [`gl_image.c`](https://github.com/id-Software/Quake-2/blob/master/ref_gl/gl_image.c):
  WAL/PCX layouts, index 255, classic image size and PCX row-stride constraints;
  GPL-2.0-or-later, master reviewed on the date above.
- [Quake III `tr_image.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_image.c):
  compatible bottom-origin 32-bit Targa layout and alpha;
  GPL-2.0-or-later, master reviewed on the date above.
- [Chocolate Doom `v_patch.h`](https://github.com/chocolate-doom/chocolate-doom/blob/chocolate-doom-3.1.0/src/v_patch.h):
  patch header and column post fields, GPL-2.0-or-later, tag `chocolate-doom-3.1.0`.
- [GZDoom `patchtexture.cpp`](https://github.com/ZDoom/gzdoom/blob/master/src/common/textures/formats/patchtexture.cpp):
  relative top-delta interpretation and patch recognition limits, BSD-3-Clause,
  master reviewed on the date above. VibeStudio's encoder and bounded decoder
  apply these format rules with independent implementations.

### idTech Map Formats
| Specification | Reference | VibeStudio modules | Revision / date |
|---|---|---|---|
| Doom Wiki map lump pages | [WAD](https://doomwiki.org/wiki/WAD), [Linedef](https://doomwiki.org/wiki/Linedef), [Sidedef](https://doomwiki.org/wiki/Sidedef), [Sector](https://doomwiki.org/wiki/Sector), [Thing](https://doomwiki.org/wiki/Thing) | `src/core/level_map.cpp` (lump recognition, record strides, two-sided flag `0x0004`, geometry validation), `src/core/map_geometry.cpp` (sector outline tracing), `src/core/map_render.cpp` and `src/app/map_viewport.cpp` (thing angles, light levels) | Living wiki pages |
| Doom Wiki linedef types and Boom's generalized linedefs | [Linedef type](https://doomwiki.org/wiki/Linedef_type), [Generalized linedef](https://doomwiki.org/wiki/Generalized_linedef) (Boom's `boomref.txt`) | `src/core/level_map.cpp` `doomSpecialActsBehindLine()`: vanilla's manual doors (specials 1, 26-28, 31-34, 117 and 118) and Boom's generalized types with a D1 or DR trigger in their low three bits act on the sector behind the line, so their tags draw no links | Living wiki pages; Boom 2.02 reference |
| Doom Wiki Hexen map format | [Hexen map format](https://doomwiki.org/wiki/Hexen_map_format) | `src/core/level_map.cpp` - 16-byte linedef and 20-byte thing strides selected by the `BEHAVIOR` lump | Living wiki page |
| Doom Wiki UDMF | [UDMF](https://doomwiki.org/wiki/UDMF) | `src/core/level_map.h`, `src/core/level_map.cpp` - `TEXTMAP`/`ENDMAP` detection and the ZDoom-era lump set that must survive a round trip | Living wiki page |
| Quake `.map` text format | [Quake Wiki Quake Map Format](https://quakewiki.org/wiki/Quake_Map_Format) | `src/core/level_map.cpp` - the shared brush/face tokenizer | Living wiki page |
| Quake II `.map` surface arguments | [Quake Wiki Quake 2 Map Format](https://quakewiki.org/wiki/Quake_2_Map_Format) | `src/core/level_map.cpp` - `readOptionalFlags()` for the trailing `contents surface value` triple | Living wiki page |
| Valve 220 `.map` texture axes | [Valve Developer Community MAP (file format)](https://developer.valvesoftware.com/wiki/MAP_%28file_format%29) | `src/core/level_map.cpp` - Valve 220 face parsing | Living wiki page |
| Q3Radiant / GtkRadiant brush primitives | [Q3Radiant manual](https://icculus.org/gtkradiant/documentation/q3radiant_manual/) | `src/core/level_map.cpp` - `brushDef`, `brushDef3`, `patchDef2`, `patchDef3` | Manual as published by the GtkRadiant project |
| q3map2 shader manual | [q3map2 shader manual](https://q3map2.robotrenegade.com/docs/shader_manual/) | `src/app/syntax_highlight.cpp` - shader keyword highlighting | Living document |
| ericw-tools sources (VibeMap2's upstream) | [ericwa/ericw-tools](https://github.com/ericwa/ericw-tools), reviewed at the revision VibeStudio imported as a submodule before moving to VibeMap2 | `src/core/map_geometry.cpp` - the `PlaneFromPoints` winding convention and the `ON_EPSILON`/`DIST_EPSILON` tolerance conventions, reimplemented rather than copied | Pinned revision `f80b1e216a415581aea7475cb52b16b8c4859084` |
| q3map2 sources from NetRadiant Custom (VibeMap3's upstream) | [Garux/netradiant-custom](https://github.com/Garux/netradiant-custom), reviewed at the revision VibeStudio imported as a submodule before moving to VibeMap3 | `src/core/map_geometry.cpp` - the +/-65536 `MAX_WORLD_COORD` base-winding extent and quadratic Bezier patch tessellation of `(2n+1)x(2m+1)` control grids | Pinned revision `68ecbed64b7be78741878c730279b5471d978c7c` |

Patch authoring (`src/core/level_patch.*`, reviewed 2026-10-04) uses the
31-point axis limit and width-major map layout documented by NetRadiant Custom's
[q3map2 header](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/q3map2.h)
and [patch parser](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/patch.cpp)
at revision `68ecbed64b7be78741878c730279b5471d978c7c`,
GPL-2.0-or-later, compatible with VibeStudio's GPL-3.0 license. Preset generation,
control editing and Bézier splitting are original implementations; no upstream
code or game assets were copied.

Material-token conversion in patch authoring and package resolution
(`src/core/map_assets.*`, `src/core/level_dependencies.cpp`) follows that
parser and [`ParseRawBrush`](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/map.cpp):
q3map2 supplies the `textures/` prefix for map tokens. This is a format-behavior
reference under the same license and revision, reviewed 2026-10-04.

### Compiled Artifact Formats
All rows below are implemented in `src/core/bsp_inspect.h` and
`src/core/bsp_inspect.cpp`.

| Specification | Reference | What it covers | Revision / date |
|---|---|---|---|
| Quake Specifications, chapter 4 "BSP files" | [qkspec_4](https://www.gamers.org/dEngine/quake/spec/quake-spec34/qkspec_4.htm) | BSP29 lump order and record layouts | "Quake Documentation Version 3.4" |
| ericw-tools documentation | [ericw-tools docs](https://ericwa.github.io/ericw-tools/) | The `BSP2` and `2PSB` widened node/leaf/clipnode/edge/marksurface records, and the `.prt` / `.pts` / `.lin` files the compilers write | Matches revision `f80b1e216a415581aea7475cb52b16b8c4859084`, VibeStudio's ericw-tools submodule pin before VibeMap2 |
| Released id Software Quake II sources | [`qcommon/qfiles.h`](https://github.com/id-Software/Quake-2/blob/master/qcommon/qfiles.h) | IBSP v38 header and lump records | GPL source release |
| q2tools-220 / qbism extended Quake II BSP | [qbism/q2tools-220](https://github.com/qbism/q2tools-220) | The `QBSP` widened `dqnode_t`, `dqleaf_t`, `dqface_t`, `dqbrushside_t`, `dqedge_t` records | Upstream project; not imported as a submodule |
| Released id Software Quake III Arena sources | [`code/qcommon/qfiles.h`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/qcommon/qfiles.h) | IBSP v46 header and lump records | GPL source release |
| q3map2 sources from NetRadiant Custom (VibeMap3's upstream) | [Garux/netradiant-custom](https://github.com/Garux/netradiant-custom), at VibeStudio's q3map2 submodule pin before VibeMap3 | Raven `RBSP` v1 draw-surface and draw-vertex records | Pinned revision `68ecbed64b7be78741878c730279b5471d978c7c` |

`src/core/asset_tools.cpp` reads the `IDPO` (Quake MDL), `IDP2` (MD2) and
`IDP3` (MD3) headers for metadata only; those layouts come from the same
Quake Specifications and released id Software sources listed above.

### idTech Model Formats
The rows below cover the original Quake MDL, Quake II MD2 and Quake III MD3
decoders in `src/core/model_mesh.cpp`. The formats added on 8 October 2026,
MDC, MDR and IQM among them, are credited in
[Native Model Formats And Modeller Profiles](#native-model-formats-and-modeller-profiles-2026-10-08).
No commercial model, skin, or animation data is embedded in this repository.

| Specification | Reference | What it covers | Revision / date |
|---|---|---|---|
| Quake Specifications, chapter 5 "MDL files" | [qkspec_5](https://www.gamers.org/dEngine/quake/spec/quake-spec34/qkspec_5.htm) | Quake MDL (`IDPO` version 6) header, the scale/translate vertex compression, the `onseam` skin-vertex rule, simple and group frames, and the Z-up model axis convention (X forward, Y left, Z up) that `src/app/model_viewport.cpp` draws with | "Quake Documentation Version 3.4" |
| Released id Software Quake sources, `modelgen.h` | [`WinQuake/modelgen.h`](https://github.com/id-Software/Quake/blob/master/WinQuake/modelgen.h) | `mdl_t`, `stvert_t`, `dtriangle_t`, `daliasframe_t`, `daliasgroup_t` and `trivertx_t` field order, and the `ALIAS_ONSEAM` `0x0020` flag; layouts reimplemented, no code copied | GPL source release |
| Released id Software Quake II sources, `qcommon/qfiles.h` | [`qcommon/qfiles.h`](https://github.com/id-Software/Quake-2/blob/master/qcommon/qfiles.h) | Quake II MD2 (`IDP2` version 8): `dmdl_t`, `dstvert_t`, `dtriangle_t`, `daliasframe_t` and `dtrivertx_t`, the separate position and texture-coordinate indexing that `decodeQuake2Md2()` recombines, the 64-byte external skin names, and the GL command block counted by preview and audited against indexed triangles/UVs during editable import | GPL source release |
| id Software `anorms.h` vertex normal table | [`ref_gl/anorms.h`](https://github.com/id-Software/Quake-2/blob/master/ref_gl/anorms.h) | The 162 vertex normals that MDL and MD2 index with one byte per vertex, transcribed as `kAliasNormals` in `src/core/model_mesh.cpp`. It is a fixed mathematical constant of the two formats rather than game content | GPL source release |
| Released id Software Quake III Arena sources, `md3.h` | [`code/qcommon/qfiles.h`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/qcommon/qfiles.h) and `code/renderer/tr_types.h` | Quake III MD3 (`IDP3` version 15): `md3Header_t`, `md3Frame_t`, `md3Tag_t`, `md3Surface_t`, `md3Shader_t`, `md3Triangle_t`, `md3St_t` and `md3XyzNormal_t`, the `MD3_XYZ_SCALE` 1/64 unit step, the per-surface `IDP3` chain walked by each surface's own `ofsEnd`, and the packed latitude/longitude normal pair reconstructed by `md3Normal()` | GPL source release |
| Return to Castle Wolfenstein and Elite Force / ioquake3 `qfiles.h` | The `mdcHeader_t` and `mdrHeader_t` layouts published in those source releases | MDC and MDR, now decoded in full by `src/core/model_format_mdc.cpp` and `model_format_mdr.cpp`; see [Native Model Formats And Modeller Profiles](#native-model-formats-and-modeller-profiles-2026-10-08) for the pinned revisions | Public source releases |
| Inter-Quake Model specification | [sauerbraten.org/iqm](http://sauerbraten.org/iqm/) | IQM, now read and written in full by `src/core/model_format_iqm.cpp`; see [Native Model Formats And Modeller Profiles](#native-model-formats-and-modeller-profiles-2026-10-08) | Inter-Quake Model public specification (MIT) |

### Wavefront OBJ Polygon Interchange

The original polygon importer in `src/core/model_obj.*` follows format facts from
[Wavefront Advanced Visualizer 3.0, Appendix B1](https://www.martinreddy.net/gfx/3d/OBJ.spec),
reviewed 2026-10-05: independent corner indices, relative references, groups,
normal precedence, smoothing and text continuation. The mirrored reference has
no explicit licence grant. No source code, sample geometry or specification prose
was incorporated; the parser, bounded polygon triangulation, worker and generated
fixtures are original code under this repository's GPL-3.0 licence. No external
library or commercial asset is included. MTL shading libraries and free-form
geometry remain unsupported and are rejected explicitly.

### Entity Definition Formats
All rows below are implemented in `src/core/entity_definitions.h` and
`src/core/entity_definitions.cpp`. No game's entity definitions are shipped
here; the studio parses whatever `.def`, `.fgd`, `.ent`, or `.qc` file the user
points it at, through `loadEntityDefinitions()` and the `entity definitions`
and `entity validate` CLI commands.

| Specification | Reference | What it covers | Revision / date |
|---|---|---|---|
| Radiant `/*QUAKED` definition blocks | [GtkRadiant](https://github.com/TTimo/GtkRadiant), `radiant/eclass_def.cpp`, and the q3map2 entity documentation shipped with [NetRadiant Custom](https://github.com/Garux/netradiant-custom) | `parseRadiantBlock()` - the `/*QUAKED <classname> (r g b) (mins) (maxs) FLAG1 .. FLAG8` header, the 0..1 colour triple, the literal `?` written in place of the size pair to mean "brush entity", up to eight header flags mapped to spawnflag bits 0..7, and the documented key and flag lines in the comment body. QuakeC `.qc` sources carry the same block and parse through the same path | Living repositories; no pinned revision |
| Valve Forge Game Data (`.fgd`) | [Valve Developer Community FGD](https://developer.valvesoftware.com/wiki/FGD) | `parseFgdDefinitions()` - the `@PointClass`, `@SolidClass`, `@BaseClass` and related class statements, the `base()`, `size()`, `color()`, `model()`, `studio()`, `iconsprite()` and `sprite()` helpers, `key(type) : "display" : "default" : "description"` bodies, `choices` and `flags` row blocks, and `@include` resolution relative to the including file | Living wiki page |
| Quake III Arena BSP entity lump (`.ent`) | The released q3map2 sources and the [Quake III Arena shader and entity manual](https://www.qeradiant.com/manual/Q3AShader_Manual/) | `parseEntDefinitions()` - the plain `{ "key" "value" }` blocks q3map2 writes back out. This is a list of placed entities rather than a definition file, so the reader recovers classnames and the union of keys each class was seen with, and warns that key types, defaults, descriptions and spawnflag names are unavailable | Living document |
| Quake Specifications, "Entities" appendix | [Quake Documentation](https://www.gamers.org/dEngine/quake/spec/quake-spec34/) | The skill and deathmatch spawnflag bits 8..11 the game code owns (`NOT_EASY` `0x100`, `NOT_MEDIUM` `0x200`, `NOT_HARD` `0x400`, `NOT_DEATHMATCH` `0x800`). `validateLevelMapEntities()` skips those bits for `LevelMapFormat::QuakeMap` documents instead of reporting them as undeclared spawnflags | "Quake Documentation Version 3.4" |

### Audio Delivery Compatibility

The original implementation in `src/core/audio_delivery.cpp` follows format facts
and reader behaviour in these references, reviewed 2026-10-04. Their
GPL-2.0-or-later licences are compatible with VibeStudio's GPL-3.0. No engine
source or game assets are copied; preset defaults and encoding code are our own.

| Reference | Behaviour used | Revision / date |
|---|---|---|
| [Chocolate Doom `src/i_sdlsound.c`](https://github.com/chocolate-doom/chocolate-doom/blob/chocolate-doom-3.1.0/src/i_sdlsound.c) | DMX format 3 header, 16-byte edge padding, minimum padded length | `chocolate-doom-3.1.0` |
| [Quake `WinQuake/snd_mem.c`](https://github.com/id-Software/Quake/blob/master/WinQuake/snd_mem.c) | Original SFX reader requires mono legacy PCM WAV | Master reviewed 2026-10-04 |
| [Quake II `client/snd_mem.c`](https://github.com/id-Software/Quake-2/blob/master/client/snd_mem.c) | Mono legacy PCM sound-effect layout | Master reviewed 2026-10-04 |
| [Quake III `code/client/snd_mem.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/client/snd_mem.c) | Mono PCM reader; warnings for 8-bit precision and rates other than 22050 Hz | Master reviewed 2026-10-04 |

### Audio Level Placement

`src/core/audio_level.cpp` and the speaker branch of `level_dependencies.cpp`
implement original Quake II/III sound entity contracts from id Software's
[Quake II `game/g_target.c`](https://github.com/id-Software/Quake-2/blob/master/game/g_target.c),
[Quake II `client/snd_mem.c`](https://github.com/id-Software/Quake-2/blob/master/client/snd_mem.c),
and [Quake III `code/game/g_target.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/game/g_target.c),
master reviewed 2026-10-04. These specify `target_speaker`, `noise`, looping
flags, trigger target names, the 64-byte path limit and Quake II's `sound/`
lookup. Their GPL-2.0-or-later notices were reviewed for GPL-3.0 compatibility.
VibeStudio's validation, transaction, dialog and CLI are original implementations;
no engine source, entity definition files or game assets were imported.

### Audio Container Formats
`src/core/audio_markers.cpp` independently implements the factual cue, `adtl`,
`CSET`, and `smpl` layouts from Microsoft's
[Multimedia Programming Interface and Data Specifications 1.0](https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/Docs/riffmci.pdf)
(1991) and [Multimedia Data Standards Update](https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/Docs/RIFFNEW.pdf)
(1994-04-15), reviewed 2026-10-04. These are public format references, not imported
code or assets; no specification prose is copied. The Quake/II references above
also establish the first-cue loop start and `LIST/adtl/ltxt` `mark` loop length;
their GPL-2.0-or-later licences are compatible with this repository.

All rows below are implemented in `src/core/asset_tools.cpp`.
The bounded Ogg metadata page walk, Vorbis identification checks and independent
header fixtures in `src/tests/ogg_preview_smoke_test.cpp` were reviewed against
[RFC 3533 sections 6–7](https://www.rfc-editor.org/rfc/rfc3533),
[Vorbis I section 4.2.2](https://xiph.org/vorbis/doc/Vorbis_I_spec.html), and
[RFC 7845 section 4](https://www.rfc-editor.org/rfc/rfc7845) on 2026-10-06.
RFC 3533 carries the Internet Society's 2003 implementation-use notice; RFC 7845
uses the IETF Trust Legal Provisions (April 2016). Xiph's Vorbis specification
permits independent implementations under other licences (1994–2015 copyright).
Only factual layouts and timing constraints inform this original GPL-3.0 code;
no specification prose, external code or sound assets were incorporated.
Preview metadata is separate from the fully decoded/imported audio checks above.

| Specification | Reference | What it covers | Revision / date |
|---|---|---|---|
| Microsoft "Multimedia Programming Interface and Data Specifications 1.0" | [IETF RFC 2361](https://www.rfc-editor.org/rfc/rfc2361) for the wave format tag registry | RIFF/WAVE chunk layout, PCM / IEEE float / A-law / mu-law tags, `WAVE_FORMAT_EXTENSIBLE` | RFC 2361, June 1998 |
| Ogg encapsulation format | [IETF RFC 3533](https://www.rfc-editor.org/rfc/rfc3533) | Ogg page header and segment table parsing | May 2003 |
| Vorbis I specification | [Xiph Vorbis I spec](https://xiph.org/vorbis/doc/Vorbis_I_spec.html) | Section 4.2.2 identification header | Xiph.Org |
| Ogg Opus | [IETF RFC 7845](https://www.rfc-editor.org/rfc/rfc7845) | `OpusHead` and the fixed 48 kHz decode rate (section 5.1) | April 2016 |
| FLAC format | [Xiph FLAC format](https://xiph.org/flac/format.html) | Metadata block headers and `STREAMINFO` | Xiph.Org |
| MPEG-1 / MPEG-2 audio frame headers (ISO/IEC 11172-3, ISO/IEC 13818-3) | [MPEG audio frame header reference](http://www.mp3-tech.org/programmer/frame_header.html) | Bitrate and sample-rate tables, frame sizing | Public reference document |
| Doom Wiki sound and PC speaker formats | [Sound](https://doomwiki.org/wiki/Sound), [PC speaker sound effects](https://doomwiki.org/wiki/PC_speaker_sound_effects) | `parseDmxSound()`: the DMX format 3 header (rate, sample count including the pad bytes) and format 0 tone lumps held for 1/140 s | Living wiki pages |
| Chocolate Doom `i_sdlsound.c` (GPL-2.0) | [chocolate-doom/src/i_sdlsound.c](https://github.com/chocolate-doom/chocolate-doom/blob/master/src/i_sdlsound.c) | Behaviour only, no code copied: the DMX library skips 16 bytes at each end of the samples and does not play sounds of 48 samples or fewer, which `parseDmxSound()` and the analysis warning follow | Behaviour as of the `master` branch, September 2026 |

### Output And Presentation Formats
| Specification | Reference | VibeStudio modules | Revision / date |
|---|---|---|---|
| SVG 1.1 (Second Edition) | [W3C SVG 1.1](https://www.w3.org/TR/SVG11/) | `src/core/map_render.cpp` - the deterministic headless map renderer emits only the static subset: no scripting, no external references, no embedded raster data | W3C Recommendation, 16 August 2011 |
| Unicode CLDR layout guidance | [CLDR layout](https://cldr.unicode.org/translation/getting-started/layout) | `src/core/localization.cpp` - `rightToLeftLanguageCodes()`, derived from CLDR's per-language `characterOrder` | Living document |
| Engine and source-port command lines | [Quake](https://quakewiki.org/wiki/Command_line_parameters), [Quake II](https://www.quake2.com/q2guide/q2cmdline.html), [ioquake3](https://ioquake3.org/help/command-line-options/), [ZDoom](https://zdoom.org/wiki/Command_line_parameters) | `src/core/build_pipeline.cpp` - the game launch profiles and their argument templates | Living documents |

### Language Server Protocol

The original implementation in `src/core/language_server.*`, `src/core/language_diagnostics.cpp`,
`src/core/language_completion.*`, `src/core/completion_snippet.*`, `src/core/language_hover.*`, `src/core/language_signature.*`, `src/core/language_formatting.*`, `src/core/language_rename.*`, `src/core/language_workspace_edit.*`,
`src/core/language_code_actions.*`, `src/core/language_references.*`, the Code integration
and protocol fixtures follow Microsoft's
[Language Server Protocol 3.17 specification](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/),
including stdio framing, initialization, UTF-16 positions, synchronization,
push diagnostics, definitions, hover content, reference lists, completion lists/defaults and edit ranges,
cancellation and shutdown. Completion interface facts, including item defaults,
opaque data and lazy detail/documentation/additional-edit resolution, come from the
[3.17 completion specification](https://github.com/microsoft/language-server-protocol/blob/gh-pages/_specifications/lsp/3.17/language/completion.md).
Its snippet-syntax section also supplies tab-stop, placeholder, choice, escaping
and document-variable facts for the original snippet parser and field editor,
reviewed 2026-10-04 under the same CC-BY-4.0 license. No upstream parser or editor
code was incorporated.
Document diagnostic full/unchanged reports, cancellation and refresh follow the
[pull diagnostics specification](https://github.com/microsoft/language-server-protocol/blob/gh-pages/_specifications/lsp/3.17/language/pullDiagnostics.md).
Successful-save notifications follow the
[didSave specification](https://github.com/microsoft/language-server-protocol/blob/gh-pages/_specifications/lsp/3.17/textDocument/didSave.md).
Both were reviewed 2026-10-04 under CC-BY-4.0; no upstream code or prose was copied.
Optional interoperability verification used
[Ruff 0.16.4](https://github.com/astral-sh/ruff/tree/0.16.4), © Charles Marsh and
contributors, as a separate test executable under its MIT license and retained
third-party notices. The PyPI Windows wheel and binary hashes are recorded in
isolated test output. No Ruff implementation is bundled or linked into VibeStudio.
Code-action literal, diagnostic context and lazy resolution facts follow the
[code action and resolve specification](https://github.com/microsoft/language-server-protocol/blob/gh-pages/_specifications/lsp/3.17/language/codeAction.md),
reviewed 2026-10-04 under CC-BY-4.0. No upstream code or prose was copied.
Rename preparation, text-edit maps and versioned document edits follow the
[rename](https://github.com/microsoft/language-server-protocol/blob/gh-pages/_specifications/lsp/3.17/language/rename.md)
and [WorkspaceEdit](https://github.com/microsoft/language-server-protocol/blob/gh-pages/_specifications/lsp/3.17/types/workspaceEdit.md)
sections, reviewed 2026-10-04 under the same CC-BY-4.0 specification license.
Only protocol facts informed the original implementation; no code or prose was copied.
Reference capability, request context and location-list facts come from the
[3.17 references specification](https://github.com/microsoft/language-server-protocol/blob/gh-pages/_specifications/lsp/3.17/language/references.md).
Hover capability, content shapes and optional ranges follow the
[3.17 hover specification](https://github.com/microsoft/language-server-protocol/blob/gh-pages/_specifications/lsp/3.17/language/hover.md).
Call-signature overloads, UTF-16 parameter-label ranges, active-index defaults and
trigger/retrigger context follow the
[3.17 signature-help specification](https://github.com/microsoft/language-server-protocol/blob/gh-pages/_specifications/lsp/3.17/language/signatureHelp.md),
reviewed 2026-10-04 under the same CC-BY-4.0 specification license. Only protocol
facts informed the original parser and client; no upstream code or prose was copied.
Formatting capability, options and edit-array facts follow the
[document formatting](https://github.com/microsoft/language-server-protocol/blob/gh-pages/_specifications/lsp/3.17/language/formatting.md),
[range formatting](https://github.com/microsoft/language-server-protocol/blob/gh-pages/_specifications/lsp/3.17/language/rangeFormatting.md)
and [TextEdit](https://github.com/microsoft/language-server-protocol/blob/gh-pages/_specifications/lsp/3.17/types/textEdit.md)
sections of the same 3.17 specification.
Reference: `gh-pages`
3.17 specification, reviewed 2026-10-04; specification license:
[Creative Commons Attribution 4.0 International](https://github.com/microsoft/language-server-protocol/blob/gh-pages/License.txt).
Only interface facts were used; no upstream implementation or specification prose
was copied. The protocol implementation remains original GPL-3.0 repository code.
External language servers are user-installed executables, not linked or bundled.
Completion-resolution and parameter-hint interoperability were checked with Microsoft's
[Pyright 1.1.414](https://github.com/microsoft/pyright/tree/1.1.414), installed only
in the isolated test output directory. Its
[MIT license](https://github.com/microsoft/pyright/blob/1.1.414/LICENSE.txt) was
reviewed 2026-10-04. No Pyright code was incorporated or distributed with the IDE.

## Editor Workflow Inspirations

The GtkRadiant version and QeRadiant profile expansion was audited on 2026-10-07:

- [GtkRadiant's 1.4.0-era ZeroRadiant source](https://github.com/TTimo/GtkRadiant/tree/5fc27697b313ddb925e57605c9983f5727a3c19f),
  revision `5fc27697b313ddb925e57605c9983f5727a3c19f` (2008-08-24),
  identifies 1.4.0 in `include/version.default`. The `radiant/mainframe.cpp`,
  `camwindow.cpp`, `preferences.cpp`, `drag.cpp` and `qe3.cpp` files informed
  command keys, discrete camera defaults, selection/material gestures and grid.
- [GtkRadiant 1.5](https://github.com/TTimo/GtkRadiant/tree/017673373699174b574c92a262496826a6b409e9),
  revision `017673373699174b574c92a262496826a6b409e9`, informed its separate
  preset through `radiant/mainframe.cpp`, `camwindow.cpp`, `xywindow.cpp`,
  `selection.cpp`, `surfacedialog.cpp`, `brushmanip.cpp` and `grid.cpp`.
  The reviewed source headers in both trees permit GPL-2.0-or-later,
  compatible with this GPL-3.0 repository. No upstream implementation is copied.
- [Eutectic's QeRadiant/Q3Radiant shortcut and mouse reference](https://icculus.org/gtkradiant/documentation/q3radiant_manual/appndx/sskey_dl.htm),
  hosted by the GtkRadiant project, informed the QeRadiant-specific fit keys,
  shared camera gestures and removal of Q3-only shortcuts. The manual credits
  Eutectic and is reproduced upstream by permission; its prose is not imported.
  Only behavioural facts inform the original VibeStudio preset. Classic camera
  services retain the compatible Q3Radiant attribution below.

Version presets use VibeStudio's shared geometry, surface, camera, undo and
CLI services. [Adaptations and unsupported behaviours](EDITOR_PROFILES.md#gtkradiant-14-and-15)
remain visible in the profile reference. No upstream assets or game data are imported.

Classic Q3Radiant behavior was audited on 2026-10-06 against
[id Software's source](https://github.com/id-Software/Quake-III-Arena/tree/dbe4ddb10315479fc00086f08e25d968b4b43c49/q3radiant),
revision `dbe4ddb10315479fc00086f08e25d968b4b43c49`:
[`MainFrm.cpp`](https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/q3radiant/MainFrm.cpp)
supplies command keys and fixed movement steps;
its texture shift, rotation and fit key table also informs the seven Q3Radiant
quick surface bindings. The actions use VibeStudio's existing surface mapping
service, with explicit target/step controls and documented mapping differences;
[`CamWnd.cpp`](https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/q3radiant/CamWnd.cpp)
supplies position steering, dead-zone/speed behavior and ground-plane movement;
`XYWnd.cpp`, `DRAG.CPP`, `PrefsDlg.cpp` and `QE3.CPP` supply plan gestures,
selection and layout/grid defaults. Copyright (C) 1999–2005 id Software, Inc.
The source headers permit GPL-2.0-or-later, reviewed as compatible with this
GPL-3.0 repository. VibeStudio independently expresses these behaviors in
`level_editor_familiar_controls`, `level_camera_keys` and `model_viewport_drive`,
with portable Qt lifecycle handling. No native Windows input code, artwork or
game data is imported. [Profile differences](EDITOR_PROFILES.md#classic-q3radiant)
remain explicit.

Standalone NetRadiant and Sledge profile behavior was audited on 2026-10-05:

- [Xonotic NetRadiant](https://github.com/xonotic/netradiant/tree/b4b295d7a37797cc2752e48aa8ce42492e7016f0),
  revision `b4b295d7a37797cc2752e48aa8ce42492e7016f0` (2026-04-29):
  [`radiant/camwindow.cpp`](https://github.com/xonotic/netradiant/blob/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant/camwindow.cpp)
  and `camwindow.h` supply camera/FOV defaults; `xywindow.cpp`, `selection.cpp`,
  `grid.cpp` and `mainframe.cpp` supply plan navigation, selection, grid and
  command bindings. Their GPL-2.0-or-later notices were checked as compatible
  with this GPLv3 repository. This is a separate profile from NetRadiant Custom.
- [Sledge 2.0.7.2](https://github.com/LogicAndTrick/sledge/tree/8762a6de07a9fa486d51aff0913cdc0306fd775c),
  revision `8762a6de07a9fa486d51aff0913cdc0306fd775c`:
  [`CameraNavigationViewportSettings.cs` and the perspective/orthographic listeners](https://github.com/LogicAndTrick/sledge/tree/8762a6de07a9fa486d51aff0913cdc0306fd775c/Sledge.BspEditor.Rendering/Viewport),
  `Sledge.BspEditor/Controls/Layout/LayoutSettings.cs`,
  `Sledge.BspEditor/Grid/SquareGridFactory.cs`, and `DefaultHotkey` attributes in
  `Sledge.BspEditor/Commands`, `Sledge.BspEditor.Editing/Commands` and
  `Sledge.BspEditor.Tools`. Copyright Daniel Walder (2018),
  [BSD-3-Clause](https://github.com/LogicAndTrick/sledge/blob/8762a6de07a9fa486d51aff0913cdc0306fd775c/LICENSE),
  reviewed as GPLv3-compatible. The released tag matches the official website's
  download reference; the incomplete master rewrite was not used as a default-control reference.

Only behavioral facts were used. No upstream implementation, artwork, runtime
or game data is incorporated. The shared C++ profile, navigation, validation,
preferences and tests are original VibeStudio code. Remaining differences are
listed in [Editor Profiles](EDITOR_PROFILES.md#standalone-netradiant-and-sledge).

Temporary level-view maximization and equal sizing follow behavioral facts in
the [original Hammer 3.4 hotkey reference](https://documentation.help/Valve-Hammer-Editor-3.4/Hotkey_Reference.htm)
(Shift+Z and Ctrl+A), the [J.A.C.K. 1.1 author manual](https://valvedev.info/tools/jack/jack_manual.pdf)
(November 2016, page 80, Shift+Z), and
[NetRadiant Custom mainframe.cpp](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/radiant/mainframe.cpp)
(F12, pinned `68ecbed64b7be78741878c730279b5471d978c7c`). Reviewed 2026-10-05.
Hammer and J.A.C.K. are proprietary references; their code/prose was not copied.
The Radiant file's GPL-2.0-or-later notice is compatible with this GPLv3 repository;
only command facts informed the original Qt visibility/snapshot implementation
in `src/app/level_view_actions.cpp`. VibeStudio targets the focused pane and
keeps the studio's inspector and asset context visible.

The DoomEdit and BSP profile additions were reviewed on 7 October 2026.
[Doom 3 `MainFrm.cpp`](https://github.com/id-Software/DOOM-3/blob/a9c49da5afb18201d31e3f0a429a037e56ce2b9a/neo/tools/radiant/MainFrm.cpp),
[`CamWnd.cpp`](https://github.com/id-Software/DOOM-3/blob/a9c49da5afb18201d31e3f0a429a037e56ce2b9a/neo/tools/radiant/CamWnd.cpp)
and [`XYWnd.cpp`](https://github.com/id-Software/DOOM-3/blob/a9c49da5afb18201d31e3f0a429a037e56ce2b9a/neo/tools/radiant/XYWnd.cpp)
at `a9c49da5afb18201d31e3f0a429a037e56ce2b9a` supply command and gesture facts.
Their [GPL-3.0-or-later licence with additional terms](https://github.com/id-Software/DOOM-3/blob/a9c49da5afb18201d31e3f0a429a037e56ce2b9a/COPYING.txt)
was read before implementation; no upstream code is incorporated or linked.
The independently written C++ profile remains under VibeStudio's GPLv3 licence.

[BSP Quake Editor 0.97q7](https://www.bspquakeeditor.com/downloads.php),
specifically `Settings/bspmouse.cfg`, `Settings/bspmou3d.cfg` and
`Settings/keyboard.cfg`, and the author's [release notes](https://www.bspquakeeditor.com/)
and [mouse documentation](https://www.bspquakeeditor.com/doc/mousexy.htm),
inform the BSP familiarity profile. These are proprietary reference material;
no compatible code-reuse licence was established, so only binding facts were
used. No configuration text, implementation, documentation prose, binaries or
game assets were copied into the repository. Differences are listed in
[Editor Profiles](EDITOR_PROFILES.md#doomedit-and-bsp-quake-editor).

The October 5, 2026 familiarity expansion is an independent implementation of
documented interaction behavior. No upstream code, documentation prose, assets
or proprietary SDKs were incorporated, and no new library is linked. VibeStudio
remains GPLv3. The reference software/documentation retains its own license;
proprietary references supply behavioral facts only.

| Reference | Source/revision reviewed | License / use |
|---|---|---|
| QuArK | [Map mouse/key defaults](https://github.com/QuakeEngines/QuArK_quake_editor-clone/blob/412bf28a14d4e369479bf38408bd93e6a2612f87/runtime/trunk/addons/Defaults.qrk), `412bf28a`; [Infobase](https://quark.sourceforge.io/infobase/intro.mapeditor.html) | QuArK GPL lineage; behavior only, no code imported |
| Hammer / Worldcraft / Hammer++ family | [Archived original Hammer 3.4 hotkey reference](https://documentation.help/Valve-Hammer-Editor-3.4/Hotkey_Reference.htm), reviewed 2026-10-05; [Valve community reference](https://developer.valvesoftware.com/wiki/Hammer_Hotkey_Reference) (direct access blocked during review) | Proprietary editor/documentation; classic interaction facts only, no Source 2 parity claim |
| J.A.C.K. | [Official site](https://jack.hlfx.ru/en/main.html), [author's reference manual, version 1.1, November 2016](https://valvedev.info/tools/jack/jack_manual.pdf), accessed 2026-10-05 | Proprietary editor/manual; behavior only |
| DarkRadiant | [User Guide](https://www.darkradiant.net/userguide/), accessed 2026-10-05; [license](https://github.com/codereader/DarkRadiant/blob/master/LICENSE), updated 2024-03-08 | GPL-2.0-or-later by default, with upstream exceptions; behavior only |
| Doom Builder / Ultimate Doom Builder | [Default settings](https://github.com/UltimateDoomBuilder/UltimateDoomBuilder/blob/6d9f6038db30adfee0edd74221b74b2de4837f6f/Assets/Common/UDBuilder.default.cfg), [enhanced visual actions](https://github.com/UltimateDoomBuilder/UltimateDoomBuilder/blob/6d9f6038db30adfee0edd74221b74b2de4837f6f/Source/Plugins/BuilderModes/Resources/Actions.cfg), `6d9f6038` | [GPLv3](https://github.com/UltimateDoomBuilder/UltimateDoomBuilder/blob/6d9f6038db30adfee0edd74221b74b2de4837f6f/LICENSE.txt); key facts only |
| SLADE | [Default bindings](https://github.com/sirjuddington/SLADE/blob/351fd983fa986423c1825c87128691c27e0817aa/src/General/KeyBind.cpp), `351fd983` | [GPLv2](https://github.com/sirjuddington/SLADE/blob/351fd983fa986423c1825c87128691c27e0817aa/LICENSE); key facts only |
| Eureka | [Bindings](https://github.com/ioan-chera/eureka-editor/blob/f951281878f1f8f2619e0073592b6995aeea0dfc/bindings.cfg), `f9512818` | GPL-2.0-or-later per upstream README; key facts only |
| Unreal Editor | [Viewport controls](https://dev.epicgames.com/documentation/en-us/unreal-engine/viewport-controls-in-unreal-engine), accessed 2026-10-05 | Epic proprietary software/documentation; behavior only |
| Unity | [Scene navigation](https://docs.unity3d.com/Manual/SceneViewNavigation.html), accessed 2026-10-05 | Unity proprietary software/documentation; behavior only |
| Godot | [3D navigation](https://docs.godotengine.org/en/stable/tutorials/3d/introduction_to_3d.html), stable documentation accessed 2026-10-05 | MIT engine; documentation CC-BY-3.0; behavior only |
| Blender | [Navigation manual](https://docs.blender.org/manual/en/latest/editors/3dview/navigate/index.html) (reference link), [default keymap](https://github.com/blender/blender/blob/main/scripts/presets/keyconfig/keymap_data/blender_default.py), reviewed 2026-10-05 | GPL Blender, manual CC-BY-SA-4.0; keymap behavior only |

The actual adaptations and remaining differences are documented in
[Editor Profiles](EDITOR_PROFILES.md) and in the Controls reference. Shared
four-view layout, numeric tools, package/build handoff and explicit material
tools are VibeStudio behavior, not claims of exact upstream implementation.

VibeStudio's adaptable level-editor profiles and the look of its shell are
intended to help users feel at home without copying third-party assets or
proprietary content. Profile and interface inspiration and compatibility
research should credit:
- [GtkRadiant](https://github.com/TTimo/GtkRadiant) (GPL-2.0), especially the GtkRadiant 1.6.0-era layout and control expectations. The GtkRadiant 1.6.0 editor profile's controls are its default bindings, read from the `1.6-release` branch at `270af88f3c2471f6773bded0b5760a3115b52965` (August 2024): key bindings from the `g_Commands[]` table in `radiant/mainframe.cpp`, the 2D view's mouse from `radiant/xywindow.cpp` (right drag pans, Shift+right zooms, the middle button aims and with Ctrl moves the camera, a left drag with nothing selected draws a brush), selection from `radiant/drag.cpp` (Shift toggles, Shift+Alt drills, Alt+drag selects an area, Ctrl+Shift picks a face in the camera), the camera from `radiant/camwindow.cpp` (a right click toggles free look, the wheel moves along the view), defaults from `radiant/preferences.cpp` (free look on, wheel zoom about the centre), and the 8-unit grid from `radiant/qe3.cpp`. Facts about behaviour only; no GtkRadiant code is used.
- [NetRadiant Custom](https://github.com/Garux/netradiant-custom) (GPL-2.0), for modern Radiant-family workflow refinements and q3map2-oriented editing expectations. The NetRadiant Custom editor profile's controls are its default bindings, read from its sources at `68ecbed` (January 2026; the revision VibeStudio's q3map2 submodule pinned before the move to VibeMap3): key bindings from the `GlobalCommands_insert`, `GlobalToggles_insert`, and `GlobalShortcuts_insert` calls in `radiant/*.cpp`, the 2D view's mouse from `radiant/xywindow.cpp` (right drag pans, Alt+right zooms, the middle button aims and moves the camera, a left drag with nothing selected draws a brush), selection from `radiant/selection.cpp` (Shift toggles, Ctrl picks faces, a plain click tunnels), and the camera from `radiant/camwindow.cpp` (a right click toggles free look, strafe mode 3, Alt+right orbits, a 100 degree field of view) and `radiant/grid.cpp` (16-unit grid). Facts about behaviour only; no NetRadiant Custom editor code is used, and the implementation in `src/core/level_editor_controls.*`, `src/app/map_viewport.*`, and `src/app/model_viewport.*` is VibeStudio's own.
- [TrenchBroom](https://trenchbroom.github.io/) (GPL-3.0), for modern single-window brush editing and project workflow expectations. The TrenchBroom editor profile's controls are its default bindings, read from [its sources](https://github.com/TrenchBroom/TrenchBroom) at `master` `90de03c` (September 2026): the camera from `lib/TbAppLib/src/CameraTool3D.cpp` (right drag looks, Alt+right orbits, middle pans, the wheel moves, Shift+wheel zooms), fly keys from `lib/TbPreferencesLib/include/prefs/Preferences.h` (W S A D, Q up, X down) and `lib/TbUiLib/src/FlyModeHelper.cpp` (Shift faster, Alt slower), selection from `lib/TbAppLib/src/SelectionTool.cpp` (Ctrl toggles, Shift picks faces), menu keys from `lib/TbUiLib/src/ActionManager.cpp`, and the 16-unit grid from `lib/TbMdlLib/src/Map.cpp`. Facts about behaviour only; no TrenchBroom code is used.
- [QuArK](https://quark.sourceforge.io/), for integrated object/package/map editing lineage.
- [Ultimate Doom Builder](https://github.com/UltimateDoomBuilder/UltimateDoomBuilder) (GPL-3.0), and the Doom Builder line it continues, for how Doom map editing behaves: drawing, deleting, merging, and joining sectors, and Make Door (`Source/Plugins/BuilderModes/ClassicModes/SectorsMode.cs`, `MakeDoor`), whose rules VibeStudio's `makeLevelMapDoors` follows: ceiling to floor, lines facing out with the door action, door and track textures, lower-unpegged tracks. Behaviour only; no Doom Builder code is used, and the implementation in `src/core/level_map.cpp` is VibeStudio's own. Reference as of the `master` branch, September 2026.
- [idStudio](https://idstudio.idsoftware.com/), id Software's editor for DOOM Eternal (public beta, August 2024), for the visual language of the studio shell: neutral charcoal panels with an orange accent, black-backed viewports with corner readouts, dense panel groups with bottom-edge tabs, a grouped Key / Value entity property grid, and an asset browser with a folder tree, breadcrumb path, and thumbnail tiles. Inspiration only: idStudio is proprietary, and no idStudio code, icons, assets, or content are used. The corresponding implementation is VibeStudio's own, in `src/app/studio_theme.*`, `src/app/studio_icons.*`, `src/app/studio_layout.*`, and `src/app/application_shell.cpp`.
- [Visual Studio Code](https://github.com/microsoft/vscode) (MIT), for the arrangement of the studio bar adopted in October 2026: the menus, the history buttons, and a command search centred on the window share one row, as VS Code's title bar does with its Command Center (the `window.commandCenter` setting), and command shortcuts read as key caps. Pattern only, reviewed 2026-10-06: no VS Code code, icons, or assets are used, and the implementation (`CommandSearchButton`, `keepCentredInToolBar()`, and `paintKeyCaps()` in `src/app/studio_layout.*`, and `buildToolBar()` in `src/app/application_shell.cpp`) is VibeStudio's own.

## Level Editor Sidebars, Shapes And Tools (2026-10-08)

The Levels page's tabbed sidebars, Shapes tab, region selections, brush-entity
tools, CSG intersect, detail tools, drop to floor, display filters, built-in
entity catalogues and linked groups were added on 8 October 2026. They are
original VibeStudio code (`src/app/studio_sidebar.*`, `src/app/tile_grid.*`,
`src/app/level_sidebar_actions.cpp`, `src/app/level_sidebar_panels.cpp`,
`src/app/level_asset_browsers.cpp`, `src/app/level_editing_actions.cpp`,
`src/app/level_shapes_panel.cpp`, `src/app/level_scene_panel.cpp`,
`src/core/level_sidebar.*`, `src/core/level_shapes.*`,
`src/core/level_view_filters.*`, `src/core/entity_builtin_catalogue.*`,
`src/core/level_linked_groups.*` and the tools in `src/core/level_map.cpp`). No upstream code, artwork or game data is
copied; the references below supplied patterns and behavioural facts.

| Reference | What it informed | Revision | Licence / use |
|---|---|---|---|
| [VibeRadiant](https://github.com/themuffinator/VibeRadiant) [`radiant/assetbrowser.cpp`](https://github.com/themuffinator/VibeRadiant/blob/f2fb5340333099dc8767c8d08f7e4757b8d23a02/radiant/assetbrowser.cpp) | One tabbed browser for entities, materials, surfaces, sounds and models, and its Globals tab's worldspawn editor and checklist, which the Map tab's **Worldspawn** and **Checklist** follow | `f2fb5340333099dc8767c8d08f7e4757b8d23a02` (2026-08-26), read 2026-10-08 | GPL-2.0 (GtkRadiant licence); pattern only |
| [Blender's sidebar and panels](https://docs.blender.org/manual/en/latest/interface/window_system/regions.html#sidebar) | Tabs down a side region, collapsible panels, folding to the tab column, and the Item panel's numeric transform | Current manual | GPL software, CC-BY-SA-4.0 manual; pattern only |
| [NetRadiant Custom](https://github.com/Garux/netradiant-custom) [`radiant/select.cpp`](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/radiant/select.cpp) and [`radiant/brushmanip.cpp`](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/radiant/brushmanip.cpp) | Select Inside and Select Touching; Make Detail and Make Structural; the Prism, Cone and Sphere commands that replace the selected brush with a shape filling its bounds | `68ecbed64b7be78741878c730279b5471d978c7c`, the revision of VibeStudio's former q3map2 submodule, read 2026-10-08 | GPL-2.0-or-later; behaviour only |
| [id Software's Q3Radiant `SELECT.CPP`](https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/q3radiant/SELECT.CPP) | Select Complete Tall and Select Partial Tall, looking along the view's depth | `dbe4ddb10315479fc00086f08e25d968b4b43c49`, read 2026-10-08 | GPL-2.0-or-later; behaviour only |
| [TrenchBroom](https://trenchbroom.github.io/) | CSG Intersect; the shear tool; linked groups, groups that take on each other's edits while each keeps its place and frame (see [Linked group frames, nesting and commands](#linked-group-frames-nesting-and-commands)); one inspector of Map, Entity and Face tabs; bundled definitions standing in when a game has none; the `_tb_` map keys of its layers, groups and linked groups (see [TrenchBroom map keys](#trenchbroom-map-keys)) | The release credited under Editor Workflow Inspirations (`90de03c`); linked groups at `4c756be1`; map keys from the 2026.2 manual, read 2026-10-23 | GPL-3.0; behaviour and the documented map format only, no code |
| Hammer and [J.A.C.K.](https://valvedev.info/tools/jack/jack_manual.pdf) | Tie to Entity and Move to World; the object bar's block, wedge, cylinder, spike, sphere, arch and torus fitted to a drawn box; the Properties, Face Edit and Primitives names | J.A.C.K. manual 1.1 (November 2016), credited above | Proprietary software and documentation; behaviour only |
| [Sledge](https://github.com/LogicAndTrick/sledge/tree/8762a6de07a9fa486d51aff0913cdc0306fd775c) | Arch and pipe shapes with segment, wall and sweep settings | `8762a6de07a9fa486d51aff0913cdc0306fd775c` | BSD-3-Clause; behaviour only |
| [Ultimate Doom Builder](https://github.com/UltimateDoomBuilder/UltimateDoomBuilder) | Rectangle and ellipse sector drawing, offered as the Shapes tab's sector shapes; visual mode's texture auto-align along joined walls; Make Sectors mode | `6d9f6038db30adfee0edd74221b74b2de4837f6f` | GPL-3.0; behaviour only |
| [linuxdoom-1.10 `r_segs.c`](https://github.com/id-Software/DOOM/blob/a77dfb96cb91780ca334d0d4cfd86957558007e0/linuxdoom-1.10/r_segs.c) | How upper, middle and lower textures are pegged to floors and ceilings, which wall auto-align's Y offsets follow | `a77dfb96cb91780ca334d0d4cfd86957558007e0` | GPL-2.0 source release; facts only |
| [Unreal Editor](https://dev.epicgames.com/documentation/en-us/unreal-engine/viewport-controls-in-unreal-engine) | Snapping the selection to the floor beneath it, as Drop to Floor does | Current documentation | Proprietary documentation; behaviour only |

The built-in entity catalogues record facts (class names, keys and their
defaults, spawnflag bits, editor sizes and colours) from id Software's GPL
releases, each checked against the code where comments and code disagree; every
description is VibeStudio's own wording:

- Quake: the [QuakeC v1.01 release in Quake-Tools](https://github.com/id-Software/Quake-Tools/tree/c0d1b91c74eb654365ac7755bc837e497caaca73/qcc/v101qc),
  checked against the [1.06 progs](https://github.com/maddes-b/QuakeC-releases) at
  `2811c02`, with compiler keys from the same Quake-Tools revision's
  `qutils/QBSP/WRITEBSP.C` and `qutils/LIGHT/`.
- Quake II: the [game DLL](https://github.com/id-Software/Quake-2/tree/372afde46e7defc9dd2d719a1732b8ace1fa096e/game)
  and the [qrad3 light compiler](https://github.com/id-Software/Quake-2-Tools/blob/707e849167cb520a5592aa2181308ab947f2a2fd/bsp/qrad3/lightmap.c).
- Quake III Arena: the [game module](https://github.com/id-Software/Quake-III-Arena/tree/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/game),
  q3map's `light.c` and `misc_model.c`, and the bot library's `be_ai_goal.c` at
  the same revision; compiler keys id's q3map does not read follow q3map2 from
  NetRadiant Custom at `68ecbed`.
- Quake compiler classes (`func_group`, `func_detail` and its `_illusionary`,
  `_wall` and `_fence` variants) follow the
  [ericw-tools qbsp documentation](https://github.com/ericwa/ericw-tools/blob/f80b1e216a415581aea7475cb52b16b8c4859084/docs/qbsp.rst)
  at `f80b1e2`.

All are GPL-2.0-or-later, compatible with this GPL-3.0 repository; the source
comment at the top of `src/core/entity_builtin_catalogue.cpp` names them too.

## Native Model Formats And Modeller Profiles (2026-10-08)

The model decoders and writers added on 8 October 2026
(`src/core/model_format_*.cpp`, `src/core/model_formats_p.h`,
`src/core/model_skeleton.*`, `src/core/model_md5.h`, `src/core/model_iqm.h`,
`src/core/model_ase.h`) and the modeller's controls profiles and layout
(`src/core/model_editor_controls.*`, `src/core/model_sidebar.*`,
`src/app/model_editor_layout.cpp`, `src/app/model_editor_profiles.cpp`,
`src/app/model_controls_dialog.*`, `src/cli/model_controls.*`) are original
VibeStudio code. The sources below supplied byte layouts, conventions and
behaviour; each decoder's header comment names the files and functions it was
checked against. No upstream code is copied, apart from format constants
(magics, versions, limits and scale factors) that a reader must match. No game
models, skins, skeletons or animations are included; every test fixture is
built by the tests.

| Reference | What it informed | Revision | Licence / use |
|---|---|---|---|
| [Doom 3 GPL source](https://github.com/id-Software/DOOM-3/tree/a9c49da5afb18201d31e3f0a429a037e56ce2b9a) (`neo/renderer/Model_md5.cpp`, `Model_lwo.cpp`, `Model_ase.cpp`, `Model.cpp`, `neo/game/anim/Anim.cpp`, `Anim_Blend.cpp`, `neo/idlib/math/`) | MD5 mesh and animation layouts, joint quaternions and matrices, `.def` model declarations; LightWave and ASE loading, axes, winding and material naming | `a9c49da5afb18201d31e3f0a429a037e56ce2b9a` (2012-02-01), read 2026-10-08 | GPL-3.0 with id's additional terms; layout and behaviour only |
| [Return to Castle Wolfenstein GPL source](https://github.com/id-Software/RTCW-SP/tree/70951bc71b730efe6bcb07db7ae76ef0c4ae7c14) (`src/qcommon/qfiles.h`, `src/renderer/tr_model.c`, `tr_animation.c`, `tr_surface.c`) | MDC compressed frames and tags; MDS bones, weights and tags | `70951bc71b730efe6bcb07db7ae76ef0c4ae7c14` (2012-01-31), read 2026-10-08 | GPL-3.0 with id's additional terms; layout and behaviour only. The MDC 256-direction normal table is rebuilt from its rule, not copied |
| [Wolfenstein: Enemy Territory GPL source](https://github.com/id-Software/Enemy-Territory/tree/40342a9e3690cb5b627a433d4d5cbf30e3c57698) (`src/qcommon/qfiles.h`, `src/renderer/tr_animation_mdm.c`) | MDM meshes, MDX bones and frames, MDM tags | `40342a9e3690cb5b627a433d4d5cbf30e3c57698` (2012-01-31), read 2026-10-08 | GPL-3.0 with id's additional terms; layout and behaviour only |
| [ioquake3](https://github.com/ioquake/ioq3/tree/83a776283bdb958f82db25554b5ed0966aaf6e49) (`code/qcommon/qfiles.h`, `code/renderergl1/tr_model.c`, `tr_animation.c`, `tr_model_iqm.c`) | MDR layout, compressed bones and tags; how IQM is drawn | `83a776283bdb958f82db25554b5ed0966aaf6e49` (2026-09-17), read 2026-10-08 | GPL-2.0-or-later; layout and behaviour only |
| [Inter-Quake Model](https://github.com/lsalzman/iqm/tree/1077b9c195a7f76f9b26266562f6e36bb4d5dac9) by Lee Salzman (`iqm.txt`, `iqm.h`) | IQM version 2 layout, joints, poses and animations, for reading and writing | `1077b9c195a7f76f9b26266562f6e36bb4d5dac9` (2026-08-15), read 2026-10-08 | MIT; specification only |
| [OpenJK](https://github.com/JACoders/OpenJK/tree/260c59c2907187af555a676fe0cc798893bf7757) (`codemp/rd-common/mdx_format.h`, `codemp/rd-vanilla/tr_ghoul2.cpp`, `codemp/qcommon/matcomp.cpp`) | Ghoul 2 GLM and GLA layouts, compressed bones, the root matrix, weights and bolts | `260c59c2907187af555a676fe0cc798893bf7757` (2026-09-29), read 2026-10-08 | GPL-2.0 only, which is not compatible with this GPL-3.0 repository: layouts and behaviour were reimplemented and nothing is copied. Jedi Academy's bone remap table for Jedi Outcast meshes is deliberately left out for this reason |
| [Xash3D FWGS](https://github.com/FWGS/xash3d-fwgs/tree/9137964147d8) (`public/xash3d_mathlib.c`, `ref/gl/gl_studio.c`, `engine/common/mod_studio.c`) | Half-Life studio model animation values, Euler order, triangle commands, texture coordinates and companion file names | `9137964147d8`, read 2026-10-08 | GPL-3.0; behaviour only |
| [Half-Life SDK](https://github.com/ValveSoftware/halflife/tree/b1b5cf5892918535619b2937bb927e46cb097ba1) (`engine/studio.h`) | Studio model structure layout | `b1b5cf5892918535619b2937bb927e46cb097ba1` (2024-10-02), read 2026-10-08 | Half-Life SDK licence, not GPL-compatible: used only as a description of the byte layout; no SDK code is copied |
| [uHexen2](https://github.com/sezero/uhexen2/tree/475c048b1c8b) (`common/genmodel.h`, `engine/h2shared/gl_model.c`, `gl_mesh.c`, `gl_draw.c`) | Hexen II mission-pack MDL (`RAPO`) layout, texture coordinates and flags | `475c048b1c8b`, read 2026-10-08 | GPL-2.0-or-later; layout and behaviour only |
| [GtkRadiant `qdata_heretic2`](https://github.com/TTimo/GtkRadiant/tree/270af88f3c24/tools/quake2/qdata_heretic2) (`qcommon/fmodel.h`, `qcommon/flex.h`, `fmodels.c`) | Heretic II FM chunks, frames, mesh nodes and GL commands | 1.6-release at `270af88f3c24`, read 2026-10-08 | GPL-2.0-or-later; layout only |
| [Heretic2R](https://github.com/m-x-d/Heretic2R/tree/4d677156a458) (`src/ref_gl1/src/gl1_FlexModel.c`) | How FM models are loaded | `4d677156a458`, read 2026-10-08 | GPL-3.0; behaviour only |
| [GZDoom](https://github.com/ZDoom/gzdoom/tree/c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7) (`src/common/models/voxels.cpp`, `models_voxel.cpp`) and Ken Silverman's `slab6.txt` | KVX layout, slab meshing, VOXELDEF placement and palette colours | `c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7` (2026-08-10), read 2026-10-08 | `voxels.cpp` BSD-3-Clause, GZDoom GPL-3.0; layout and behaviour only |
| picomodel and q3map2 in [NetRadiant Custom](https://github.com/Garux/netradiant-custom/tree/68ecbed64b7be78741878c730279b5471d978c7c) (`libs/picomodel/pm_ase.c`, `pm_lwo.c`, `tools/quake3/q3map2/model.cpp`) | How q3map2 reads ASE and LWO for `misc_model`: sub-materials, winding, texture paths | `68ecbed64b7be78741878c730279b5471d978c7c` (VibeStudio's former q3map2 submodule), read 2026-10-08 | picomodel BSD-style, q3map2 GPL-2.0-or-later; behaviour only |
| [Blender keymap](https://docs.blender.org/manual/en/latest/interface/keymap/blender_default.html) and [3D viewport navigation](https://docs.blender.org/manual/en/latest/editors/3dview/navigate/index.html) | The Blender and VibeStudio profiles' navigation, selection, modal transforms and keys; the Blender sidebar names | Blender 4 manual, read 2026-10-08 | Manual CC-BY-SA-4.0; behaviour only |
| [Autodesk 3ds Max keyboard shortcuts](https://help.autodesk.com/view/3DSMAX/2024/ENU/?guid=GUID-A73E1B09-7BFE-4A22-8153-1D3D2237B8E9) | The 3ds Max profile's views, navigation, selection, tools, sub-object levels and keys; command panel names | 3ds Max 2024 help, read 2026-10-08 | Proprietary documentation; behaviour only |
| [MilkShape 3D](https://chumbalum.swissquake.ch/) by chUmbaLum sOft | The MilkShape 3D profile's four views, navigation, tools, keys and tab names | MilkShape 3D 1.8 documentation, read 2026-10-08 | Proprietary software and documentation; behaviour only |

## Project Releases And The Game Asset Index (2026-10-08)

The game asset index, release planning, the script reference scanner, release
notes and publishing (`src/core/game_asset_register.*`,
`src/core/project_content.*`, `src/core/release_plan.*`,
`src/core/release_script_refs.*`, `src/core/release_notes.*`,
`src/core/release_publish.*`, `src/cli/release.*`,
`src/app/release_dialog.*`, `src/app/release_actions.cpp`) are original
VibeStudio code. The references below supplied file formats, conventions and
engine behaviour only; no upstream code, text or game data is copied, and the
tests generate every package and script they read. Packages are read and written with
VibeStudio's existing PAK, WAD and ZIP/PK3 code, credited under
[Compression and archive formats](#compression-and-archive-formats).

| Reference | What it informed | Revision | Licence / use |
|---|---|---|---|
| [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/) by Olivier Lacan | The project changelog VibeStudio reads and writes: the Unreleased section, `## [version] - date` headings and the six change categories | 1.1.0, read 2026-10-08 | MIT; format only |
| [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html) by Tom Preston-Werner | Patch, minor and major bumps, and pre-release versions bumping to their release | 2.0.0, read 2026-10-08 | CC BY 3.0; rules only |
| The [/idgames archive](https://www.doomworld.com/idgames/)'s text file template | The field layout of the generated readme (Title, Filename, Author, Description, the construction and copyright fields) | Read 2026-10-08 | Community convention; field names only |
| [Quake GPL source](https://github.com/id-Software/Quake) (`WinQuake/common.c`, `COM_AddGameDirectory`) | The numbered `pak*.pak` search order, which decides which stock file shadows another, and why a release should not take a numbered slot | Read 2026-10-08 | GPL-2.0-or-later; behaviour only |
| [Quake II GPL source](https://github.com/id-Software/Quake-2/tree/372afde46e7defc9dd2d719a1732b8ace1fa096e) (`qcommon/files.c`, `FS_AddGameDirectory`) | The same order for Quake II's game folders | `372afde46e7defc9dd2d719a1732b8ace1fa096e`, read 2026-10-08 | GPL-2.0-or-later; behaviour only |
| [Quake III Arena GPL source](https://github.com/id-Software/Quake-III-Arena/tree/dbe4ddb10315479fc00086f08e25d968b4b43c49) (`code/qcommon/files.c`, `code/q3_ui/ui_gameinfo.c`, `code/botlib`) | PK3 load order by name; `.arena` scripts and `levelshots/`; `.aas` bot files beside the BSP | `dbe4ddb10315479fc00086f08e25d968b4b43c49`, read 2026-10-08 | GPL-2.0-or-later; behaviour only |
| q3map2 in [NetRadiant Custom](https://github.com/Garux/netradiant-custom/tree/68ecbed64b7be78741878c730279b5471d978c7c) (VibeMap3's upstream) | External lightmaps in `maps/<map>/lm_*.tga` and the generated `scripts/q3map2_<map>.shader`, which VibeMap3 keeps | `68ecbed64b7be78741878c730279b5471d978c7c`, read 2026-10-08 | GPL-2.0-or-later; behaviour only |
| [ericw-tools](https://github.com/ericwa/ericw-tools) (VibeMap2's upstream) | `.lit` and `.lux` lighting files beside Quake BSPs, which VibeMap2 keeps | `f80b1e216a415581aea7475cb52b16b8c4859084`, read 2026-10-08 | GPL-2.0-or-later; behaviour only |
| [Quake GPL source](https://github.com/id-Software/Quake) (`WinQuake/pr_cmds.c`, `WinQuake/snd_mem.c`) | The QuakeC builtins that load a file by name (`precache_model`, `precache_sound`, `precache_file` and registered Quake's `2` variants, `setmodel`, `sound`, `ambientsound`) and the `sound/` folder sounds load from (`src/core/release_script_refs.cpp`) | Read 2026-10-09 | GPL-2.0-or-later; names and behaviour only |
| [Quake III Arena GPL source](https://github.com/id-Software/Quake-III-Arena/tree/dbe4ddb10315479fc00086f08e25d968b4b43c49) (`code/ui/ui_shared.c`, `code/ui/ui_main.c`, `code/client/cl_cin.c`, `code/qcommon/cmd.c`, `code/client/snd_dma.c`, `code/renderer/tr_image.c`) | The menu keywords whose values are files, `loadMenu`, cinematics under `video/`, `exec` adding `.cfg`, the `play` and `music` commands, and a `.jpg` loaded in place of a missing `.tga` | `dbe4ddb10315479fc00086f08e25d968b4b43c49`, read 2026-10-09 | GPL-2.0-or-later; names and behaviour only |
| [ioquake3](https://github.com/ioquake/ioq3) (`code/client/snd_codec.c`) | A sound loaded in another format when the named file is missing, which is why `.ogg` and `.wav` are both tried | Read 2026-10-09 | GPL-2.0-or-later; behaviour only |
| [ZDoom wiki: DECORATE](https://zdoom.org/wiki/DECORATE), [ZScript](https://zdoom.org/wiki/ZScript), [SNDINFO](https://zdoom.org/wiki/SNDINFO), [MAPINFO](https://zdoom.org/wiki/MAPINFO) | Actor and class headers, `replaces`, sound properties and state sprites; logical sound names, `$random` and `$alias`; MAPINFO's map keys, `include` and `DoomEdNums`, old and new formats | Read 2026-10-09 | Facts from GFDL text |
| [Doom Wiki thing types](https://doomwiki.org/wiki/Thing_types) with the class names of the [ZDoom wiki's standard editor numbers](https://zdoom.org/wiki/Standard_editor_numbers) | The editor numbers of Doom and Doom II's monsters, weapons, ammunition, pickups, keys and barrel, so an actor that replaces one is followed from a map's things | Read 2026-10-09 | Facts from CC BY-SA and GFDL text; numbers and names only |

## AI Integration References
- [OpenAI API documentation](https://platform.openai.com/docs/quickstart), planned as the first optional general-purpose provider reference for prompt-based and agentic automation experiments.
- [Claude API documentation](https://platform.claude.com/docs/en/home), planned as an optional provider reference for reasoning, coding, long-context, and agentic planning workflows.
- [Gemini API documentation](https://ai.google.dev/api), planned as an optional provider reference for multimodal and large-context workflows.
- [ElevenLabs documentation](https://elevenlabs.io/docs/overview/intro), an optional provider reference; its [Sound Effects API](https://elevenlabs.io/docs/api-reference/text-to-sound-effects/convert) (`POST /v1/sound-generation`, reviewed 2026-10-06) is the request and answer shape of `src/core/ai_audio_transport.*`. Original implementation; no client code copied.
- [Meshy API documentation](https://docs.meshy.ai/en), planned as an optional provider reference for prompt/image-to-3D, AI texturing, and rapid placeholder asset workflows.
- [OpenAI Images API](https://platform.openai.com/docs/api-reference/images) and [structured outputs](https://platform.openai.com/docs/guides/structured-outputs), [Claude structured outputs](https://platform.claude.com/docs/en/build-with-claude/structured-outputs), [Gemini image generation](https://ai.google.dev/gemini-api/docs/image-generation) and [generateContent](https://ai.google.dev/api/generate-content), and the [Stable Diffusion web UI API](https://github.com/AUTOMATIC1111/stable-diffusion-webui/wiki/API) (AGPL-3.0 project; only its documented HTTP interface is used), reviewed 2026-10-06: the request and answer shapes of `src/core/ai_transport.*` structured output and `src/core/ai_image_transport.*`. Original implementations; no client code copied.

## Text Connectors: Harnesses And GGUF (2026-10-22)

- [llama.cpp](https://github.com/ggml-org/llama.cpp) and
  [ggml](https://github.com/ggml-org/ggml) (MIT), pinned at
  `c1d0e7a004015f23bc0233470b747b596f29b264`, linked only in builds with
  `-Dggml=enabled`; `scripts/build_ggml.py` builds them with the options
  Quake-MapGen's `CMakeLists.txt` used. `src/core/ai_gguf_runtime.cpp` is
  derived from Quake-MapGen's `src/llama_text_model.cpp` (MIT, the working tree
  of 2026-10-16): backend start-up, batches, chat templates, tokenizing, token
  pieces and grammar-constrained greedy decoding, with conversations, free
  text, cancellation and a model cache added. `src/core/ai_json_grammar.cpp`
  writes grammars in llama.cpp's GBNF syntax (`grammars/README.md` at the
  pinned revision); the conversion is original.
- [Claude Code](https://code.claude.com/docs/en/cli-reference) (Anthropic) and
  [Codex CLI](https://github.com/openai/codex) (OpenAI, Apache-2.0): their
  documented command-line options, JSON result and JSONL event formats, and
  status commands, reviewed 2026-10-22 against Claude Code 2.1.304 and Codex
  CLI 0.153.4, are the interface of `src/core/ai_harness.cpp`. Neither tool is
  bundled, linked or modified; no code is copied.

## Map Generator (Quake-MapGen, 2026-10-22)

- [Quake-MapGen](https://github.com/themuffinator/Quake-MapGen) (MIT, copyright
  2026 Quake-MapGen contributors), commit `1252548ad68f44df63d56f1c6c068c4fc6f6330c`
  (2026-08-26) plus its uncommitted work up to 2026-10-16, imported on
  2026-10-22 into `src/mapgen`: `include/qmg`, `src` (without `main.cpp` and
  `llama_text_model.cpp`), `data/profiles`, `schemas/semantic-plan-v1.schema.json`,
  `grammars/semantic-plan-v1.gbnf`, the C++ unit tests and golden maps in
  `tests`, `LICENSE` and `.clang-format`. `src/mapgen/UPSTREAM.json` lists each
  file's upstream SHA-256. `src/mapgen/generate_profile_tables.py` ports its
  `cmake/GenerateGameProfileTables.cmake`. VibeStudio's changes (the ggml-free
  `LayoutRanker`, connector-named planner models, the JSON Schema and first
  request, prompt overrides, the rules fallback, the optional ggml ranker
  (`QMG_HAVE_GGML`) and GGUF planning through the Local GGUF connector) are
  marked `VibeStudio:` and listed in `src/mapgen/VIBESTUDIO.md`. The licence
  is installed with the application.
- Quake-MapGen's research tooling, from the same working tree and imported the
  same day into `src/mapgen/tools`, `knowledge`, `docs` and `tests`: the map
  recipes, multiplayer portfolio tools, map study, benchmark, asset registry,
  profile and Quake Live atlas tools, the knowledge base, the study documents
  and their tests (MIT; each file's upstream SHA-256 is in
  `src/mapgen/UPSTREAM.json`). The native movement and teleport harnesses are
  kept as source only: they link GPL Quake II 3.19 code from a local checkout
  into private test programs, and Meson never builds or ships them.
- The scalar ranker reproduces the result of `ggml_mul_mat` in
  [ggml](https://github.com/ggml-org/ggml) (MIT) as bundled with
  [llama.cpp](https://github.com/ggml-org/llama.cpp) (MIT) at
  `c1d0e7a004015f23bc0233470b747b596f29b264`
  (`ggml/src/ggml-cpu/vec.cpp`, `ggml_vec_dot_f32`, reviewed 2026-10-22): an
  8-element float dot product accumulated with fused multiply-adds. The
  scalar ranker copies and links no ggml or llama.cpp code. The optional ggml
  ranker, in builds made with `-Dggml=enabled`, links ggml as described under
  [Text Connectors: Harnesses And GGUF](#text-connectors-harnesses-and-gguf-2026-10-22).
- Format and gameplay behaviour in the imported code was checked upstream
  against the official [Quake](https://github.com/id-Software/Quake),
  [Quake II](https://github.com/id-Software/Quake-2) and
  [Quake III Arena](https://github.com/id-Software/Quake-III-Arena) source
  releases (GPL-2.0, referenced, not copied), as its `THIRD_PARTY.md` records.

## Generative Level, Texture, And Sound Design (2026-10-06)

- [Quake-MapGen](https://github.com/themuffinator/Quake-MapGen) (MIT), `main` at
  `1252548` (2026-08-26) and its v0.4.0/M3 `README.md` and `docs/ARCHITECTURE.md`,
  reviewed 2026-10-06. `src/core/level_generation*.{h,cpp}` follow its pipeline
  design: a prompt normalized into a deterministic spec, a coordinate-free
  semantic plan made by rules or by a schema-constrained model and repaired,
  an integer-grid layout, lowering into sealed brushes, and validation before
  anything is written. The planner, layout, 2.5D cell builder, Doom sector
  tracing, map writers and preview are original VibeStudio code; no source,
  grammar, knowledge cards, maps or model weights were copied.
- [TexAI](https://github.com/themuffinator/TexAI) (GPL-3.0), `7f56a4b`
  (2026-02-14), `src/core/pbr_generator.cpp` and
  `src/core/openai_image_client.cpp`, reviewed 2026-10-06.
  `src/core/texture_generation.*` and `src/core/ai_image_transport.*` follow its
  per-surface prompt design (straight on, evenly lit, no framing or text, keep
  tileability), its use of the OpenAI Images edits endpoint as a multipart
  form, and the idea of deriving companion layers (normal, roughness/gloss,
  glow) from one texture. VibeStudio derives those layers deterministically
  (Sobel normals, gloss, glow masks) and adds seam blending, wrap-around
  resampling, palette conversion and game writers; no TexAI code was copied.
- Companion map names follow the ports that read them: DarkPlaces and FTE
  (`_norm` with height in alpha, `_gloss`, `_glow`), QuakeSpasm-family ports
  (`_glow`, then `_luma`, with `*` names read as `#`), and ioquake3's OpenGL2
  renderer (`_n`, `_s`), from their public source and documentation, reviewed
  2026-10-06. Quake II liquid flags (`CONTENTS_LAVA` 8, `CONTENTS_SLIME` 16,
  `CONTENTS_WATER` 32, `SURF_WARP` 8, `SURF_TRANS33` 16) are from
  [Quake II's qfiles.h](https://github.com/id-Software/Quake-2/blob/master/qcommon/qfiles.h)
  (GPL-2.0-or-later); entity class names from each game's released source and
  DoomEd numbers from the [Doom Wiki thing tables](https://doomwiki.org/wiki/Thing_types).
- [sfxr](https://drpetter.se/project_sfxr.html) by Tomas Pettersson
  (DrPetter), MIT, from Ludum Dare 10 (December 2007), reviewed 2026-10-06.
  The sound synthesizer in `src/core/sound_generation.cpp` follows its idea
  of preset kinds of game sound (pickup, laser, explosion, power-up, hurt,
  jump) made by randomizing a small voice model within each kind's ranges:
  a waveform (square with duty, saw, sine, noise), a pitch slide, vibrato,
  an arpeggio jump, an attack-sustain-punch-decay envelope, and filters.
  The kinds' parameters, layering, the state-variable filter, loops and
  delivery are VibeStudio's own; no sfxr code was copied.

## Accessibility And Localization References
- [WCAG 2.2](https://www.w3.org/TR/WCAG22/), the baseline accessibility reference where web-oriented guidance applies to desktop UI. The wide text spacing preference uses success criterion 1.4.12's letter (0.12em) and word (0.16em) spacing, and the status message durations answer 2.2.1 (Timing Adjustable).
- [Ethnologue 200](https://www.ethnologue.com/insights/ethnologue200/) (2025 figures) and the [Steam supported languages](https://partner.steamgames.com/doc/store/localization/languages) list, the references the 47-language target set was reviewed against on 2026-10-07.
- [Unicode CLDR](https://cldr.unicode.org/) parent locales and likely subtags, read through Qt's `QLocale`, for resolving regional tags (`es-MX` to `es-419`, `zh-HK` to Traditional Chinese). Data only, through Qt.
- [Primer Primitives](https://github.com/primer/primitives) (GitHub's design tokens, MIT), whose protanopia/deuteranopia and tritanopia themes mark success in blue: the pattern behind the red-green safe palette. Pattern only, reviewed 2026-10-07; VibeStudio's colours were chosen and contrast-checked independently.
- Gustavo M. Machado, Manuel M. Oliveira, and Leandro A. F. Fernandes, "A Physiologically-based Model for Simulation of Color Vision Deficiency", IEEE Transactions on Visualization and Computer Graphics 15(6), 2009: the severity 1.0 protanopia, deuteranopia, and tritanopia matrices `src/tests/studio_theme_smoke_test.cpp` uses to check that each colour-vision palette keeps success, warning, and danger apart. Published coefficients only; no code from the authors' materials.
- Microsoft Edge's Read Aloud shortcut (Ctrl+Shift+U), adopted as the default key of VibeStudio's Read Aloud command. Key choice only.
- Speech engines driven but not shipped: the Windows Speech API (SAPI 5, part of Windows; `sapi.h` from the Windows SDK), macOS `say`, [Speech Dispatcher](https://freebsoft.org/speechd) (`spd-say`, GPL-2.0-or-later), and [eSpeak NG](https://github.com/espeak-ng/espeak-ng) (GPL-3.0-or-later). The Unix engines run as separate programs; no code from any of them is included.

## Community Thanks
- The idTech mapping, modding, speedrunning, source-port, and preservation communities who kept these workflows usable and documented across decades.
- The maintainers and contributors of the imported compiler projects listed above, and of ericw-tools and NetRadiant Custom, on whose work VibeMap2 and VibeMap3 build.

## Doom Node Readiness

The node-readiness readers in `src/core/level_doom_nodes.cpp` implement the
classic and extended layouts checked against ZDBSP's
[`processor.cpp`](https://github.com/rheit/zdbsp/blob/bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659/processor.cpp)
(`WriteBSPX/Z`, `WriteGLBSPX/Z`, node/seg/subsector writers and GL group naming)
and [`doomdata.h`](https://github.com/rheit/zdbsp/blob/bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659/doomdata.h).
Revision `bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659`, reviewed 2026-10-05;
copyright Randy Heit (2002–2006), GPL-2.0-or-later, checked as compatible with
VibeStudio's GPL-3.0 before implementation. These are original readers and
generated fixtures, with no upstream implementation or game assets copied.
Compressed nodes use the existing RFC 1950/1951 decoder with cancellation.

## Doom Geometry Reflection

- [Ultimate Doom Builder's EditSelectionMode.cs](https://github.com/jewalky/UltimateDoomBuilder/blob/6d9f6038db30adfee0edd74221b74b2de4837f6f/Source/Plugins/BuilderModes/ClassicModes/EditSelectionMode.cs),
  `FlipLinedefs` and horizontal/vertical selection flips, informed the detached
  reflection rule in `src/core/level_doom_selection.cpp`: reverse endpoints while
  retaining side ownership. Revision `6d9f6038db30adfee0edd74221b74b2de4837f6f`,
  reviewed 2026-10-05; file copyright Pascal vd Heiden (2007). Upstream
  [GPL-3.0 licence](https://github.com/jewalky/UltimateDoomBuilder/blob/6d9f6038db30adfee0edd74221b74b2de4837f6f/LICENSE.txt)
  is compatible with VibeStudio's GPL-3.0 and was checked before implementation.
  Workflow reference only; no C# implementation copied. VibeStudio explicitly
  rejects attached partial reflection and provides its own connected-selection
  service, native undo, cancellation and WAD-preservation tests. The same
  reflection rules also apply to UDMF native transforms through the original
  lossless scalar writer; unknown extension fields remain unchanged.

## Level Document Creation

- Empty Hexen ACS behavior data follows the layout checked against
  [Chocolate Doom 3.1.0, `P_LoadACScripts`](https://github.com/chocolate-doom/chocolate-doom/blob/chocolate-doom-3.1.0/src/hexen/p_acs.c)
  (GPL-2.0-or-later, compatible with this repository's GPL-3.0; reviewed
  2026-10-04). The starter map and recovery implementation are original; no
  upstream source or commercial assets are incorporated.

## UDMF parser and property authoring

The original lossless parser/projection in `src/core/level_udmf.cpp` and UDMF WAD
integration use format facts from James Haley's
[UDMF v1.1 specification](https://github.com/rheit/zdoom/blob/master/specs/udmf.txt)
(2009-03-29, reviewed 2026-10-05; GFDL-1.2-or-later with no invariant sections).
The specification is a format reference only: no document text or implementation
is incorporated into GPL source. Its namespace, scalar and delimited-map rules
inform the original span-preserving parser and generated fixtures.

The compatible implementation interface was checked against
[ZDBSP `processor_udmf.cpp`](https://github.com/rheit/zdbsp/blob/bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659/processor_udmf.cpp)
and [`sc_man.cpp`](https://github.com/rheit/zdbsp/blob/bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659/sc_man.cpp),
revision `bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659`, reviewed 2026-10-05.
UDMF reader/writer copyright Christoph Oelckers (2009), GPL-2.0-or-later,
compatible with VibeStudio's GPL-3.0. No upstream implementation was copied;
existing submodule notices remain intact. Real-node tests invoke ZDBSP as a
separate executable against generated noncommercial room fixtures.

UDMF native transforms also use the polyobject thing-number/angle-role facts in
[ZDBSP `processor.cpp`, `GetPolySpots`](https://github.com/rheit/zdbsp/blob/bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659/processor.cpp)
at the same pinned revision, reviewed 2026-10-05; copyright Randy Heit (2002–2006),
GPL-2.0-or-later, compatible with GPL-3.0. The original transform code preserves
control identifiers and marks changed XY node inputs stale; no implementation
body is copied.

## PakFu Image Exchange and Capability Catalog

DDS decoding in `src/core/dds_image.cpp` derives from
[PakFu's DDS codec](https://github.com/themuffinator/PakFu/blob/13111e4c07513548a29fd7eb74fc9d004c44aa59/src/formats/dds_image.cpp).
FTX/SWL layouts and DDS/FTX writing in `src/core/extra_image.cpp` derive from
[PakFu's format sources](https://github.com/themuffinator/PakFu/tree/13111e4c07513548a29fd7eb74fc9d004c44aa59/src/formats)
(`ftx_image.cpp`, `swl_image.cpp`, `image_writer.cpp`). Reviewed local working-tree
snapshot 2026-10-05, based on `13111e4c07513548a29fd7eb74fc9d004c44aa59`; the
DDS/writer files contain local changes beyond that commit. PakFu's GPL-3.0 is
compatible with VibeStudio's GPL-3.0. VibeStudio adds dimension/payload bounds,
cancellation, supported-surface restrictions, mask validation, native export
integration and synthetic fixtures. No commercial assets are included.

The catalog's separation of inspection, decoding, conversion and runtime
availability is informed by PakFu's support matrix at the same snapshot.
The catalog and `.vibeworkspace` schema/implementation are VibeStudio-owned code.


### Level material gesture references

The material-name gesture defaults were checked on 2026-10-06 against
[Q3Radiant DRAG.CPP](https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/q3radiant/DRAG.CPP)
(revision `dbe4ddb10315479fc00086f08e25d968b4b43c49`),
[GtkRadiant drag.cpp](https://github.com/TTimo/GtkRadiant/blob/270af88f3c2471f6773bded0b5760a3115b52965/radiant/drag.cpp)
(`270af88f3c2471f6773bded0b5760a3115b52965`),
[NetRadiant selection.cpp](https://github.com/xonotic/netradiant/blob/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant/selection.cpp)
(`b4b295d7a37797cc2752e48aa8ce42492e7016f0`) and
[NetRadiant Custom selection.cpp](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/radiant/selection.cpp)
(`68ecbed64b7be78741878c730279b5471d978c7c`). These files are
GPL-2.0-or-later, compatible with this GPL-3.0 repository. The implementation is
original VibeStudio code: exact profile routing, semantic hit APIs and existing
material transactions. No upstream implementation or game assets were copied.
All four use middle-click sampling; Q3Radiant/GtkRadiant's Shift+middle material-
only face application supplies the paint default.

The same pinned Q3Radiant and GtkRadiant `DRAG.CPP`/`drag.cpp` files were reviewed
again on 2026-10-06 for middle-click texture-definition sampling, Ctrl+middle
whole-brush application and Ctrl+Shift+middle single-face application. Their
GPL-2.0-or-later notices were verified as compatible before implementation.
VibeStudio's original surface clipboard retains material, mapping and flags,
routes paste through its shared asynchronous edit plan, and keeps the copied
material fixed instead of adopting later changes in the texture picker.
Brush-primitive values are reused with that copied material. Explicit world
projection uses the already credited q3map2 mapping conventions in
`level_texture_mapping.cpp`. The same pinned [q3map2 ParseBrush](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/tools/quake3/q3map2/map.cpp)
selects classic/Valve syntax from the first face; this interface fact informs
the map-wide conversion rule, reviewed 2026-10-06 (GPL-2.0-or-later, compatible
with GPL-3.0). No additional upstream implementation was copied.
NetRadiant's Shift+middle parameter paste follows the pinned
[NetRadiant surface dialog](https://github.com/xonotic/netradiant/blob/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant/surfacedialog.cpp).
NetRadiant Custom's Ctrl+middle seamless paste, advancing source face and parallel
fallback follow `Face_setTexture` in its pinned
[surface dialog](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/radiant/surfacedialog.cpp).
`Texdef_ProjectTexture` in its pinned
[brush_primit.cpp](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/radiant/brush_primit.cpp)
was reviewed to distinguish native Project from the studio's world-UV mode.
All are GPL-2.0-or-later, verified compatible before implementation on 2026-10-06.
The double-precision hinge/Rodrigues solver, asynchronous publication, conversion
consent and independent synthetic tests are original VibeStudio implementations.
Custom's Shift+middle hit-and-selection values and Alt mapping-only variants
also reference `Scene_applyClosestTexture` in the same surface dialog,
[`select.cpp`](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/radiant/select.cpp),
`Texdef_Assign` in `brush_primit.cpp`, and texture-size scaling in
[`brush_primit.h`](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/radiant/brush_primit.h).
The same revision, GPL-2.0-or-later compatibility review and 2026-10-06 date apply.
Valve axes are retained for Values; selected patches receive only material;
selected brush flags remain unchanged unless explicitly hit. Matrix mapping-only
transfer preserves texel density using source and destination image sizes.
The bounded mixed-object plan, package-context capture and tests are original.
Native Ctrl+Shift and Alt+Ctrl+Shift Project behavior also follows the same
`Scene_applyClosestTexture`, `Select_ProjectTexture`, `Texdef_ProjectTexture`
and `BPTexdef_fromST011` functions, plus
[`Patch::ProjectTexture`](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/radiant/patch.cpp).
The GPL-2.0-or-later source was rechecked for GPL-3.0 compatibility on 2026-10-06.
Native classic/Valve assignments, matrix repeat normalization, patch control-point
projection and edge-on results inform the independent implementation; no code was
copied. Atomic mixed transactions, contextual dimension lookup and the synthetic
serialized/compiler oracles are original VibeStudio work.
Held Values/Project/Wrap ordering also follows `TexManipulator_` in the pinned
[`selection.cpp`](https://github.com/Garux/netradiant-custom/blob/68ecbed64b7be78741878c730279b5471d978c7c/radiant/selection.cpp):
selected objects on mouse-down only, current modifiers on subsequent hits,
advancing seamless source and one undo group. Its GPL-2.0-or-later licence was
rechecked for GPL-3.0 compatibility on 2026-10-06. The bounded private transaction,
worker snapshots, live preview/frozen picking and CLI replay are original
VibeStudio work. No upstream code was copied.
Patch-source copying/wrapping, NetRadiant's additional
paste aliases, work-zone depth and light-color sampling remain adaptations or gaps.

## Brush topology authoring

Explicit brush corner insertion/removal, edge/face splitting, plane extrusion and shared plan/camera
component selection and movement follow behavioural facts from
[TrenchBroom's vertex editing manual](https://trenchbroom.github.io/manual/latest/#vertex_editing)
and [extrusion manual](https://trenchbroom.github.io/manual/latest/#extrusion),
version 2026.2, reviewed 2026-10-18. The editor's
[GPL-3.0 licence](https://github.com/TrenchBroom/TrenchBroom/blob/v2026.2/LICENSE.txt)
is compatible with VibeStudio's GPL-3.0-or-later licence. No upstream source,
manual text or artwork is copied. The convex-hull implementation, face
inheritance, plane/split extrusion, normal drag mathematics, validation, GUI,
CLI and tests in `src/core/level_brush.*`, `src/core/level_brush_extrusion.cpp`,
`src/core/model_transform.*` and
`src/app/brush_editor_dialog.*`, `src/app/model_viewport_components.cpp` and
`src/app/level_camera_component_actions.cpp` are original VibeStudio code.
The bounded draft worker, source guards and shared plan/camera selection are
VibeStudio implementations, not copied upstream code or manual text.

## Shared clipper

Two/three-point interaction, moving/removing points, camera face placement
and exact face-plane matching follow behavioural facts from the
[TrenchBroom clipping manual](https://trenchbroom.github.io/manual/latest/#clipping),
version 2026.2, reviewed 2026-10-18. The project uses
[GPL-3.0](https://github.com/TrenchBroom/TrenchBroom/blob/v2026.2/LICENSE.txt).
Clip, split, flip, quick entry, caulk/custom material policy, independent reset
controls, two-point plan cycling and double-click preferences also follow
[NetRadiant Custom's clippertool.cpp](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/clippertool.cpp),
revision `449778b3840f8f2062daab15c87cb012f2ac945c`, GPL-2.0-or-later
(William Joseph / GtkRadiant notices). Both licences are compatible with
VibeStudio's GPL-3.0-or-later licence. These references were reviewed before
implementation; no upstream source or manual text was copied. The follow-up
review on 2026-10-19 covered
[selection_mtor_clip.h](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/selection_mtor_clip.h),
[selection_mtor_clip.cpp](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/selection_mtor_clip.cpp),
[csg.cpp](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/csg.cpp) and
[filterbar.cpp](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/filterbar.cpp)
at the same revision and compatible licence. VibeStudio retains its previous
defaults and uses explicit game-specific material names rather than assuming
that every game ships the same caulk resource.
`level_clip_state.h`, `map_viewport_clip.cpp`, `model_viewport_clip.cpp`,
`level_clip_actions.cpp`, `level_clipper_preferences.cpp`, `core/level_clipper.*`,
atomic core clipping and the generated fixture tests
are original VibeStudio implementations.

One-point and no-change-plane wrapping follows `CSG_WrapMerge` and
`Scene_BrushSplitByPlane` in the credited `radiant/csg.cpp` at the same revision,
reviewed 2026-10-19. The compatible GPL-2.0-or-later source was checked before
implementation. VibeStudio uses its own bounded convex-hull implementation;
no upstream hull code or QuickHull dependency was imported. The shared map
service, original format/undo/scene fixtures and `level_clip_wrap_smoke_test.cpp`
implement and test these behavioural facts independently.

Standalone Ctrl+U and component-only wrapping also follow `CSG_WrapMerge`,
`Scene_gatherSelectedComponents` and `ultimate_group_path` in that same
`radiant/csg.cpp`, reviewed 2026-10-19 before implementation under its compatible
GPL-2.0-or-later licence. These define source retention, brush and patch component
inputs, the current material on every component-hull face and the last-selected
destination. `core/level_wrap.*`, the hull insertion options, `app/level_wrap_*`,
the CLI command and component wrap fixtures are independent VibeStudio code.

NRC's two-point camera plane follows `viewdir_set`, `viewdir_fixup` and
`viewdir_make_cut_worthy` in the same `selection_mtor_clip.cpp`, with camera
direction conventions checked against
[`radiant/view.h`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/view.h)
and axial ties against
[`libs/math/vector.h`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/libs/math/vector.h).
These GPL-2.0-or-later sources at the pinned revision were reviewed on 2026-10-19
before the independent `core/level_clip_camera.*` implementation. The licence
is compatible with GPL-3.0-or-later; no upstream implementation was copied.
GUI/profile/CLI integration and camera-plane fixture tests are original code.

Modified camera-point movement follows `ClipManipulatorImpl::Transform` in
that clipper and `TranslateFreeXY_Z` in
[`radiant/selection_mtable_translate.h`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/selection_mtable_translate.h),
including its fixed press anchors, XY/Z and axial-view movement, Shift axis
constraint and relative grid. Nearest-even ties follow `float_snapped` in the
same pinned `libs/math/vector.h`. These William Joseph GPL-2.0-or-later sources
were reviewed on 2026-10-19 before the compatible, independent
`core/level_clip_drag.*` implementation. No upstream source was copied.
Viewport lifecycle, typed preferences, CLI settings and tests are original;
broader scene-point snapping remains a documented integration gap.

The original compiler acceptance harness reads IBSP v46 shader, brush-side,
plane and draw-surface records using the layouts in
[qfiles.h](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/tools/mbspc/qcommon/qfiles.h)
and the `SURF_NODRAW` value in
[surfaceflags.h](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/tools/mbspc/qcommon/surfaceflags.h),
GPL-2.0-or-later, id Software copyright, at the same revision (reviewed
2026-10-19; compatible with GPL-3.0-or-later). No upstream code or game assets
are copied into the tests.

## Linked object correspondence

The correspondence required by linked-instance operations was reviewed against
TrenchBroom 2026.2's
[`Map_Groups.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/Map_Groups.cpp),
revision `4c756be1a54a28d921d18c77716a5f170bdc31d0`, on 2026-10-19.
TrenchBroom's [GPL-3.0-or-later licence](https://github.com/TrenchBroom/TrenchBroom/blob/v2026.2/LICENSE.txt)
is compatible with VibeStudio's licence. The VibeStudio implementation in
`level_linked_objects.cpp`, its scene version 4 codec and transfer/history
integration are original code. No upstream code or metadata format was copied.
This establishes object correspondence; it does not claim extraction parity.

## Protected entity properties

Instance protection, protected absence, restoration from unprotected counterparts
and preservation after linked transforms were reviewed against TrenchBroom
2026.2's
[`Map_Entities.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/Map_Entities.cpp),
[`LinkedGroupUtils.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/LinkedGroupUtils.cpp)
and the protection column in
[`EntityPropertyModel.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/EntityPropertyModel.cpp),
revision `4c756be1a54a28d921d18c77716a5f170bdc31d0`, on 2026-10-19.
The [GPL-3.0-or-later licence](https://github.com/TrenchBroom/TrenchBroom/blob/v2026.2/LICENSE.txt)
is compatible with VibeStudio's GPL-3.0-or-later licence. The shared protection
service, native scene version 5, Qt inspector/dialog, CLI and tests are original
VibeStudio implementations. No upstream source or metadata format was copied.
VibeStudio retains its existing case-insensitive entity-key matching. Full
workflow, platform and assistive-technology parity remains unverified.

## Clipboard placement

Cursor/plane placement and profile dispatch were reviewed on 2026-10-20 against
TrenchBroom v2026.2 at `4c756be1a54a28d921d18c77716a5f170bdc31d0`:
[`Grid.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/Grid.cpp),
[`Grid.h`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/include/mdl/Grid.h),
[`MapView2D.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapView2D.cpp),
[`MapView3D.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapView3D.cpp)
and [`MapWindow.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapWindow.cpp).
The corner-selection and directional snapping algorithm is adapted from Grid;
its Kristian Duske copyright and GPL notice are preserved beside the adaptation.
TB uses [GPL-3.0-or-later](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/LICENSE.txt).

NRC's centre-to-camera translation and Shift+V binding were reviewed against
[`select.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/select.cpp)
at `449778b3840f8f2062daab15c87cb012f2ac945c`, under GPL-2.0-or-later
(id Software and contributors, 1999–2006). Both licences are compatible with
VibeStudio's GPL-3.0-or-later licence. The bounded parser, value-only requests,
Qt worker/actions, ray clipping, CLI and generated fixtures are VibeStudio code.
No game assets are included. See [Clipboard placement](LEVEL_PASTE.md) for
behavioural differences and unverified acceptance gates.

## Repeatable object commands

The record/replay sequence, selection boundary, clear and history behaviour was
reviewed against TrenchBroom v2026.2 at revision
`4c756be1a54a28d921d18c77716a5f170bdc31d0` on 2026-10-19:
[`RepeatStack.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/RepeatStack.cpp),
[`Map_Geometry.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/Map_Geometry.cpp),
[`Map_Nodes.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/Map_Nodes.cpp)
and the repeat bindings in
[`ActionManager.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp).
The source credits Kristian Duske (2020/2025) under
[GPL-3.0-or-later](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/LICENSE.txt),
compatible with this repository's GPL-3.0-or-later licence. The value-based core,
atomic worker/undo integration, JSON recipe, CLI and generated test fixtures are
original implementations. No upstream code or assets were copied. VibeStudio's
single-undo replay and bounded coverage differ from full TrenchBroom parity;
see [Repeat object commands](LEVEL_REPEAT.md).

## Linked group extraction

The partial-selection extraction workflow was reviewed against TrenchBroom
2026.2's
[`Map_Groups.cpp::extractLinkedGroups`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/Map_Groups.cpp),
revision `4c756be1a54a28d921d18c77716a5f170bdc31d0`, on 2026-10-19.
The source credits Kristian Duske (2025) under
[GPL-3.0-or-later](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/LICENSE.txt),
compatible with VibeStudio's GPL-3.0-or-later licence. The reference covers
duplicating each instance independently, partitioning two linked families,
preserving local properties and splitting partially selected brush entities.
`src/core/level_linked_extraction.*`, the cancellable Qt/CLI integration,
context-aware undo and generated compiler fixtures are original implementations.
No upstream code, UI assets or game content was copied. This bounded workflow
does not establish full TrenchBroom parity.

## Linked group frames, nesting and commands

Linked groups' terminology, command names and update rules were reviewed on
2026-10-23 against TrenchBroom 2026.2, revision
`4c756be1a54a28d921d18c77716a5f170bdc31d0`: the
[manual's Linked Groups chapter](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/app/TrenchBroom/resources/documentation/manual/index.md)
(linked duplicates, nesting, separating, extracting, visualisation);
[`ActionManager.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp) (the Groups menu:
Create Linked Duplicate on Ctrl+Shift+D, Select Linked Groups, Separate Linked
Groups, Extract Linked Groups, Clear Protected Properties);
[`Map_Groups.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/Map_Groups.cpp) (creating a linked
duplicate and separating selected members, which stay linked with each other);
[`LinkedGroupUtils.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/LinkedGroupUtils.cpp) (updating a
member by its transformation after the inverse of the source's, keeping nested
names and protected properties); and
[`UpdateLinkedGroupsHelper.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/UpdateLinkedGroupsHelper.cpp)
(ordering nested updates and refusing two changed members of one link set).
The sources credit Kristian Duske under
[GPL-3.0-or-later](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/LICENSE.txt), compatible with VibeStudio's
GPL-3.0-or-later licence. Behaviour and names only: `level_linked_groups.*`
(affine frames detected from each transform's recorded map, outermost-first
nested propagation, identity-based agreement checks and out-of-sync marks, where
TrenchBroom refuses the edit), the scene version 6 codec, the 2D-view boxes and
arrows, the Qt commands and the CLI are original VibeStudio work. No upstream
code was copied. TrenchBroom's `_tb_` map keys are read and written from its
documented format; see [TrenchBroom map keys](#trenchbroom-map-keys).

## TrenchBroom map keys

`src/core/level_scene_trenchbroom.*` reads and writes the keys TrenchBroom keeps
its layers, groups and linked groups in, so a map keeps them between the two
editors: `func_group` entities with `_tb_type` (`_tb_layer` or `_tb_group`),
`_tb_id`, `_tb_name`, `_tb_layer_sort_index`, `_tb_layer_hidden`,
`_tb_layer_locked`, `_tb_linked_group_id` and `_tb_transformation` (a 4x4 matrix
row by row), and `_tb_group`, `_tb_layer` and `_tb_protected_properties` on
member entities. The format comes from the
[TrenchBroom manual](https://trenchbroom.github.io/manual/latest/#linked_groups_map_file)
("Linked Groups in the Map File"), TrenchBroom 2026.2, read 2026-10-23, and from
the layout of maps TrenchBroom writes. TrenchBroom is
[GPL-3.0-or-later](https://github.com/TrenchBroom/TrenchBroom/blob/v2026.2/LICENSE.txt),
compatible with VibeStudio's GPL-3.0-or-later licence. The implementation is
independent: no TrenchBroom code was used. Taking the entities out of the text
and writing them back, matching corresponding objects by their place in each
group, the stable ids, the out-of-sync check, scene note version 7 and its
precedence, the Save As type, the CLI option and the tests are original
VibeStudio work.

## Group editing context

Closed-group unit selection, nested open/close editing, creation inside an open
group and double-click navigation follow the behaviour documented in the
[TrenchBroom groups manual](https://trenchbroom.github.io/manual/latest/#groups),
version 2026.2, reviewed 2026-10-18. TrenchBroom uses
[GPL-3.0](https://github.com/TrenchBroom/TrenchBroom/blob/v2026.2/LICENSE.txt),
compatible with VibeStudio's GPL-3.0-or-later licence. No upstream implementation
was copied. `src/core/level_scene_context.cpp`, shared transaction isolation,
linked-propagation scoping, GUI/CLI integration and tests are original VibeStudio
work. Complete-group duplication, portable group hierarchies, fresh linked-family
identities on clipboard/prefab transfer and linked ordinary duplicates also use
this behavioural reference (2026.2, reviewed 2026-10-18).
`src/core/level_scene_transfer.cpp`, map clipboard integration and prefab transfer
are original implementations. Linked frames and nesting, and protected
properties, are credited above.

## Component transforms and UV lock

Picked vertex/edge/face rotation, scaling and shear, and a separate UV-lock
policy for component editing, follow the
[TrenchBroom 2026.2 vertex editing manual](https://trenchbroom.github.io/manual/latest/#vertex_editing),
reviewed 2026-10-19. TrenchBroom is
[GPL-3.0](https://github.com/TrenchBroom/TrenchBroom/blob/v2026.2/LICENSE.txt).
Pivoted affine transform behaviour also uses
[NetRadiant Custom's selection.cpp](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/selection.cpp)
as a reference, revision `449778b3840f8f2062daab15c87cb012f2ac945c`, reviewed
2026-10-19. Its GPL-2.0-or-later licence and William Joseph 2001–2006 notices
were checked before implementation. Both licences are compatible with
VibeStudio's GPL-3.0-or-later licence. No source or manual text was copied.
`level_component_transform.cpp`, the corner-correspondence UV fitter in
`level_brush.cpp`, atomic batch publication, Qt controls, CLI and fixture tests
are original implementations. The shared direct rotate/scale/shear and pivot
controls in `src/app/level_component_gizmo.*` and its view/shell adapters reuse
VibeStudio's existing modeller drag mathematics. Their axis, pivot and
component behaviours reference the same upstream sources and licences above;
no upstream implementation was copied. Full native profile parity remains
separate work.

The fitted UV lock (**Fit UVs When Lock Fails**, `--uv-lock fit`) follows the
behaviour of NetRadiant Custom's texture vertex lock: `TogTexVertexLock` in
[`radiant/brushmanip.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brushmanip.cpp)
and the `Texdef_from_ST` fit in `Brush::vertexModeBuildHull`,
[`radiant/brush.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brush.cpp),
which keep three corners' texture coordinates and let the others drift, and of
TrenchBroom's UV Lock action (U) in
[`ActionManager.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp),
v2026.2, which never refuses a vertex edit. NetRadiant Custom revision
`449778b3840f8f2062daab15c87cb012f2ac945c` (GPL-2.0-or-later) and TrenchBroom
`4c756be1` (GPL-3.0-or-later) were reviewed on 2026-10-22. The choice of the
three corners (unmoved corners first), the least-squares nearest classic
mapping in `nearestClassicLevelTextureProjection()` and the tests are original
VibeStudio implementations; no upstream source was copied.

**Round Selection to Grid** (`src/core/level_grid_round.*`,
`src/app/level_grid_round_actions.cpp`, `src/cli/level_grid_round.*`) follows
the behaviour of NetRadiant Custom's Snap To Grid: `Selection_SnapToGrid`,
`Scene_SnapToGrid_Selected` and `Scene_SnapToGrid_Component_Selected` in
[`radiant/select.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/select.cpp)
at `449778b3840f8f2062daab15c87cb012f2ac945c` (GPL-2.0-or-later, reviewed
2026-10-22), which rounds brush vertices, patch control points and entity
origins one by one, or only the picked components, on Ctrl+G. The
implementation over VibeStudio's component transform service, its skip policy
for collapsing brushes and hidden objects, the CLI and the tests are original.

Shared component clumping and TrenchBroom's plain-toggle/Ctrl-add lasso policy
also follow the vertex, edge and face tool behaviour in the same 2026.2 manual,
reviewed 2026-10-19 under GPL-3.0. `src/core/level_component_selection.*` and
the viewport adapters are original code. The camera compares full component
geometry rather than handle centres; additive/subtractive alternatives and
command routing are VibeStudio adaptations. Native input parity is unverified.

Complete/touching mode conversion, topology Grow/Shrink/Connected queries and
patch-grid boundary erosion in `src/core/level_component_selection_topology.cpp`
are original VibeStudio extensions of that shared selection workflow. They
contain no imported implementation and do not claim an upstream-specific
gesture or shortcut match.

Component depth cycling extends the drill-selection workflow described in the
[TrenchBroom 2026.2 selection manual](https://trenchbroom.github.io/manual/latest/#selection),
reviewed 2026-10-19 under the GPL-3.0 licence linked above. The shared grouping,
ordering, wheel accumulation, Qt view adapters and keyboard commands are original
VibeStudio implementations. Applying this workflow to component handles and the
Alt+wheel default are VibeStudio choices, not claims of native TrenchBroom or
NetRadiant Custom component bindings. No upstream code or manual text is copied.

### Direct vertex insertion

Grid-point insertion on edges/faces, coincident component grouping and the Shift
entry gesture were reviewed in
[VertexTool.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/v2026.2/lib/TbUiLib/src/VertexTool.cpp)
and
[VertexToolController.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/v2026.2/lib/TbUiLib/src/VertexToolController.cpp),
TrenchBroom tag `v2026.2`, on 2026-10-19. These files carry Kristian Duske's
copyright and GPL-3.0-or-later notices; the
[licence](https://github.com/TrenchBroom/TrenchBroom/blob/v2026.2/LICENSE.txt)
was reviewed before implementation and is compatible with VibeStudio's
GPL-3.0-or-later licence. The picking, controller, asynchronous transaction and
fixture code here are original; no upstream source or manual prose was copied.
VibeStudio's explicit tool, optional view-plane movement, strict retained-corner policy
and atomic asynchronous release are its own choices; full native gesture
parity is not claimed.

### Component drag constraints

World-axis component movement and live constraint/snap behaviour were reviewed
against TrenchBroom tag `v2026.2` on 2026-10-19:
[MoveHandleDragTracker.h](https://github.com/TrenchBroom/TrenchBroom/blob/v2026.2/lib/TbUiLib/include/ui/MoveHandleDragTracker.h),
[HandleDragTracker.h](https://github.com/TrenchBroom/TrenchBroom/blob/v2026.2/lib/TbUiLib/include/ui/HandleDragTracker.h),
[HandleDragTracker.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/v2026.2/lib/TbUiLib/src/HandleDragTracker.cpp),
[NodeHandleToolBase.h](https://github.com/TrenchBroom/TrenchBroom/blob/v2026.2/lib/TbUiLib/include/ui/NodeHandleToolBase.h)
and [NodeHandleToolControllerParts.h](https://github.com/TrenchBroom/TrenchBroom/blob/v2026.2/lib/TbUiLib/include/ui/NodeHandleToolControllerParts.h).
These sources carry Kristian Duske's copyright and GPL-3.0-or-later notices,
compatible with VibeStudio's GPL-3.0-or-later licence. Licence review preceded
implementation. The controller, integration and regression fixtures are
original implementations of behavioural facts; no upstream code or prose is
copied. VibeStudio retains asynchronous validation and its existing numeric and
explicit-axis tools. Remaining differences are recorded in the
[behaviour audit](plans/level-editor-production.md#component-drag-behaviour-audit).

Exact vertex quick snapping additionally references
[VertexToolController.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/v2026.2/lib/TbUiLib/src/VertexToolController.cpp)
and [VertexTool.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/v2026.2/lib/TbUiLib/src/VertexTool.cpp)
at the same tag, under GPL-3.0-or-later, reviewed 2026-10-19. The single geometric
vertex, Alt+Shift click and exact target displacement are behavioural
references. The movement trace's successive labelled axis segments reference
`MoveHandleDragTracker.h` above. VibeStudio's core/CLI preparation, target
dialogue, profile migration and palette-aware trace layout are original code;
no upstream implementation or prose was copied.

### Level editor command catalogue

The source-discovery catalogue in `docs/plans/level-editor-commands.json` and its
generated report record command identifiers and locations from:

- [TrenchBroom's ActionManager.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp),
  tag `v2026.2`, revision `4c756be1a54a28d921d18c77716a5f170bdc31d0`.
  Kristian Duske's source carries GPL-3.0-or-later notices; the
  [licence](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/LICENSE.txt)
  is compatible with VibeStudio's GPL-3.0-or-later licence.
- [NetRadiant Custom's core registrations](https://github.com/Garux/netradiant-custom/tree/449778b3840f8f2062daab15c87cb012f2ac945c/radiant),
  including [filters](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/filters.cpp)
  and the [plugin registration bridge](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/pluginmanager.cpp),
  revision `449778b3840f8f2062daab15c87cb012f2ac945c`. These files carry
  GPL-2.0-or-later notices, compatible with VibeStudio's licence.
- The same NRC revision's [plugin](https://github.com/Garux/netradiant-custom/tree/449778b3840f8f2062daab15c87cb012f2ac945c/plugins)
  and [contributed plugin](https://github.com/Garux/netradiant-custom/tree/449778b3840f8f2062daab15c87cb012f2ac945c/contrib)
  command tables, including Joel Baxter's GPL-2.0-or-later
  [MeshTex menu generator](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/contrib/meshtex/MainMenu.cpp).
  bobToolz/ctfToolz (Gordon Biggans), GtkGenSurf (David Hyde, Loki Software
  and qeradiant.com), Portal Viewer (Geoffrey Dewan, Loki Software and
  qeradiant.com) and SunPlug (Topsun) carry LGPL-2.1-or-later headers;
  the other inspected plugin headers are GPL-2.0-or-later. These licences
  are compatible with VibeStudio's licence. The terrain generator has no
  file-level licence header; only its public command names and locations
  are recorded, with no implementation imported.

Reviewed 2026-10-19. The scanner, validation tests and parity reviews are original
VibeStudio code. No upstream implementation, artwork or game content is copied
or linked. Source locations retain access to upstream copyright/licence notices.
The catalogue includes optional and legacy source providers without claiming
that they build or work. Its pinned command-discovery baseline is separate from
the older editor-profile binding baselines credited above. Command registration
does not establish workflow parity or production acceptance.

## Brush face sweep

VibeStudio's sweep geometry and repetition follow behavioural references from
TrenchBroom `v2026.2`, revision
`4c756be1a54a28d921d18c77716a5f170bdc31d0`, reviewed 2026-10-19:
[SweepToolUtils.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/SweepToolUtils.cpp),
[SweepTool.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/SweepTool.cpp)
and [SweepToolPage.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/SweepToolPage.cpp).
These sources credit Jackson Palmer and Kristian Duske (2026) under
[GPL-3.0-or-later](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/LICENSE.txt),
compatible with VibeStudio's licence.

The reference covers face-bound centres and combined normals, straight/arc/
S-bend station interpolation, shortest rotation, world-axis taper, repeated
destination transforms, integer station alignment, skipped invalid sections,
source retention and current-material/default-face semantics. VibeStudio's
double-precision geometry, native map transaction, asynchronous numeric preview,
CLI and synthetic compiler fixtures are original implementations. No upstream
code, UI assets or game content is copied.

Destination interaction also references
[SweepToolController.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/SweepToolController.cpp),
[RotateHandleController.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/RotateHandleController.cpp)
and [Grid.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/Grid.cpp)
at the same revision, reviewed 2026-10-19: centre/ring/scale hit priority,
world-axis rotation composition, source-arm scaling, nearest grid-plane
snapping, increment/decrement and layered cancellation. These files credit
Jackson Palmer and Kristian Duske under GPL-3.0-or-later (the Grid copyright
starts in 2010), compatible with VibeStudio's GPL-3.0-or-later licence.
The double-precision controls, shared Qt view bindings, command routing,
asynchronous preview integration and synthetic tests are original code.


## Face clipboard attributes

Normal Copy/Paste dispatch, ordered face text and the last-record attribute rule
were reviewed on 2026-10-20 against TrenchBroom v2026.2, revision
`4c756be1a54a28d921d18c77716a5f170bdc31d0`:
[MapWindow.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapWindow.cpp),
[Map_CopyPaste.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/Map_CopyPaste.cpp),
[UpdateBrushFaceAttributes.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/UpdateBrushFaceAttributes.cpp)
and [ParallelUVCoordSystem.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/ParallelUVCoordSystem.cpp).
These are Kristian Duske and contributors' GPL-3.0-or-later sources, compatible
with VibeStudio's GPL-3.0-or-later licence. The attribute-copy and UV-normal
rotation behaviours are adapted; parser, Qt dispatch, CLI and synthetic fixtures
are VibeStudio implementations. No upstream implementation text or assets were
copied. Matrix transfer is a VibeStudio extension; optional colours, complete
foreign-format conversion and game-default semantics are not claimed equivalent.
See [face clipboard behaviour](LEVEL_PASTE.md#face-clipboard).

## Object clipboard ownership and cut

Partial brush-entity ownership and mixed-child ordering were reviewed on
2026-10-20 against TrenchBroom v2026.2, revision
`4c756be1a54a28d921d18c77716a5f170bdc31d0`, specifically
[NodeWriter.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/NodeWriter.cpp)
(`writeNodes` and `writeEntityNodes`). Modal-tool Cut eligibility and copy-before-delete
behaviour were checked in
[MapWindow.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapWindow.cpp).
These Kristian Duske sources are
[GPL-3.0-or-later](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/LICENSE.txt),
compatible with VibeStudio's GPL-3.0-or-later licence. The implementation is
original; no source was copied. Shared workers, incomplete native group handling,
clipboard race checks and two-file CLI publication are VibeStudio additions.
See [object clipboard behaviour](LEVEL_PASTE.md#copy-and-cut-objects).

NRC's selected-tree export was also reviewed on 2026-10-20 at revision
`449778b3840f8f2062daab15c87cb012f2ac945c`:
[select.cpp](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/select.cpp)
and [map.cpp](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/map.cpp)
(`Map_ExportSelected` / `IncludeSelectedWalker`). These files credit id Software
and contributors and permit GPL-2.0-or-later, compatible with VibeStudio's
GPL-3.0-or-later licence. No code was copied. Differences in worldspawn-property
export and child ordering remain explicit in the Partial command review.


## Brush-volume selection

Behaviour was reviewed on 2026-10-20 against TrenchBroom v2026.2,
`4c756be1a54a28d921d18c77716a5f170bdc31d0`:
[Map_Selection.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/Map_Selection.cpp),
[ModelUtils.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/ModelUtils.cpp),
[BrushNode.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/BrushNode.cpp),
[PatchNode.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/PatchNode.cpp),
[EntityNode.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/EntityNode.cpp)
and [MapWindow.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapWindow.cpp).
These Kristian Duske and contributors' sources are GPL-3.0-or-later.
Individual brush containment/intersection, consuming selection, closed-group
bounds, eight-step quadratic patch grids and entity-definition bounds informed
the implementation.

NetRadiant Custom's individual AABB tests and Inside/Touching source-retention
rules were reviewed in
[select.cpp](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/select.cpp)
at `449778b3840f8f2062daab15c87cb012f2ac945c`, copyright id Software and
contributors, GPL-2.0-or-later. Both licences are compatible with VibeStudio's
GPL-3.0-or-later licence. No implementation text was copied. Convex SAT, projected
hulls, cancellation, transactional history, Qt/CLI integration and synthetic
fixtures are original VibeStudio code. See the
[documented limits](manual/levels.md#select-by-brush-volume), including NRC model
bounds, TB world limits and native patch-grid intersection limitations.

## Selection history

Object-selection and group-navigation undo were reviewed on 2026-10-20 against
TrenchBroom v2026.2, revision `4c756be1a54a28d921d18c77716a5f170bdc31d0`:
[SelectionCommand.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/SelectionCommand.cpp),
[CommandProcessor.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/CommandProcessor.cpp)
and [Map_Groups.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/Map_Groups.cpp).
The upstream GPL-3.0-or-later licence is compatible with VibeStudio.
These are behavioural references; the implementation and fixtures are original.
The shared timeline distinguishes content changes from object-selection/context
changes, preserves reachable equivalent save points, and rejects stale staged
documents without forcing geometry caches to rebuild on selection.
Face/component selection, visibility/filter history and native gesture acceptance
remain incomplete; this is not a full TB command-history parity claim.

## Temporary level visibility

Hide, Show All and Isolate were reviewed on 2026-10-20 against TrenchBroom
v2026.2, revision `4c756be1a54a28d921d18c77716a5f170bdc31d0`:
[Map_NodeVisibility.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/Map_NodeVisibility.cpp),
[SetVisibilityCommand.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/SetVisibilityCommand.cpp)
and [ActionManager.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp)
(Kristian Duske and contributors, GPL-3.0-or-later). NetRadiant Custom's
[radiant/select.cpp](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/select.cpp)
at revision `449778b3840f8f2062daab15c87cb012f2ac945c` (GPL-2.0-or-later)
informed transient hiding, deselection, entity-label suppression and worldspawn
visibility. Both licences are compatible with VibeStudio's GPL-3.0-or-later.

Implementation and fixtures are original. Non-content TB history, whole-scene
isolation and profile bindings follow the references. Document-owned session
state, structural ID remapping, Doom dependencies, cancellable workers and CLI
queries are local adaptations. The 2026-10-21 inheritance review also used
[Node.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/Node.cpp)
and [Map_Nodes.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/Map_Nodes.cpp)
at that same GPL-3.0-or-later revision. They inform temporary Inherited/Hidden/
Shown states, resetting shown descendants when hiding a container, and ensuring
newly authored nodes are visible. VibeStudio retains saved scene visibility as
a separate mask and supports explicit
child memberships through a shared hierarchy resolver. Layer-transfer resets and
recursive attribute cloning follow those sources; the distinction between
ordinary duplication and linked updates follows
[LinkedGroupUtils.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/LinkedGroupUtils.cpp)
(`cloneAndTransformRecursive`, same GPL-3.0-or-later revision, reviewed
2026-10-21). Original fixtures exercise scene transfers, arrays, linked copies
and native Doom/Hexen/UDMF adaptations. Direct empty-container controls and
saved visibility interactions remain Partial. See the
[manual](manual/levels.md#temporary-visibility).

NRC insertion and clone visibility were reviewed on 2026-10-21 against
[`scene::Node`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/libs/scenelib.h),
[`BrushNode`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brushnode.h),
[`PatchNode`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/patch.h),
[`GroupNode`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/plugins/entity/group.cpp),
and the point-entity constructors in
[`generic.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/plugins/entity/generic.cpp)
and [`eclassmodel.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/plugins/entity/eclassmodel.cpp).
These GPL-2.0-or-later sources initialise fresh visibility flags on new and cloned
nodes. That licence is compatible with VibeStudio's GPL-3.0-or-later. The profile
policy, session revision handling and tests are original; no code was copied.
NRC uses fresh inherited insertion states, while TB/default retain the earlier
attribute-preserving root-reveal policy. Shared scene groups, native Doom formats
and linked copies are local adaptations, with saved-layer/filter equivalence and
native interaction still Partial. Hidden-parent culling was checked against
[`ForEachVisible`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/renderer.h)
at the same revision. NRC's `HideSelectedWalker` stores an owner flag when all
children become hidden; VibeStudio currently derives that label state, so the
later-child insertion interaction remains a separate parity gap.

## Hierarchy and source-line selection

Select Siblings and Select by Line Number were reviewed on 2026-10-20 against
TrenchBroom v2026.2, revision `4c756be1a54a28d921d18c77716a5f170bdc31d0`:
[Map_Selection.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/Map_Selection.cpp),
[ModelUtils.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/ModelUtils.cpp),
[EditorContext.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/EditorContext.cpp),
[Node.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/Node.cpp),
[ActionManager.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp)
and [MapWindow.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapWindow.cpp).
These Kristian Duske and contributors' sources are GPL-3.0-or-later, compatible
with VibeStudio's licence. Behaviour was used as a reference; implementation and
fixtures are original. Native parent traversal, entity-property fallback and
Ctrl+B informed the commands. Strict input validation, VibeStudio scene units,
worker cancellation, source-snapshot wording and CLI integration are local
adaptations. Foreign TB group/layer metadata and native-platform input remain
Partial; see [selection limits](manual/levels.md#select-by-hierarchy-or-source-line).

## Duplicate placement

Reviewed on 2026-10-21: TrenchBroom v2026.2
[`MapWindow::duplicateSelection`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapWindow.cpp)
(Kristian Duske and contributors, GPL-3.0-or-later) duplicates selected nodes
without displacement. NRC
[`Selection_Clone`, `AxisBase_forViewType` and `NudgeAfterClone`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/select.cpp)
(Id Software and contributors, GPL-2.0-or-later) require primitive mode and offer
a right/down grid nudge that defaults off. Both licences are compatible with
VibeStudio's GPL-3.0-or-later. No source was copied; profile data, preference
integration and tests are original. Per-profile portable preferences, stale
worker rejection and native Doom adaptation use VibeStudio services. Native
input, cross-platform and real-project acceptance remain Partial.

The unique-clone implementation was also reviewed against NRC's
[`Map_mergeClonedNames` and `BasicNamespace`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/map.cpp),
[`uniquenames.h`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/libs/uniquenames.h)
and [`namekeys.h`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/plugins/entity/namekeys.h)
at that revision on 2026-10-21 (William Joseph and other contributors,
GPL-2.0-or-later, compatible with VibeStudio's GPL-3.0-or-later). The original
implementation in `src/core/level_duplicate.cpp` and `level_map.cpp` follows the
partial-owner and shared name/reference namespace behaviour. It uses dynamic
strings and overflow-safe decimal suffix comparison instead of importing the
upstream integer parsing or fixed buffers. CLI, Repeat, scene/visibility and Qt
integration, plus synthetic tests, are VibeStudio adaptations. No source was copied.

## NRC entity connections

Reviewed on 2026-10-21: NRC's
[`Entity_connectSelected`, `Entity_killconnectSelected` and `game_has_killConnect`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/entity.cpp)
and [`Quake3EntityCreator::connectEntities` and `ConnectEntities`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/plugins/entity/entity.cpp),
revision `449778b3840f8f2062daab15c87cb012f2ac945c` (William Joseph and other
contributors, GPL-2.0-or-later). That licence is compatible with VibeStudio's
GPL-3.0-or-later. The original service in `level_entity_connections.cpp` follows
the exact two-instance selection, source-value priority, destination-class name
generation and Quake/Nexuiz/Xonotic KillConnect gate. It shares the independently
implemented namespace allocator credited above. No source was copied; atomic
history, scene locks, cancellation and project game context use VibeStudio
services. Doom 3's separate `name`/numbered-target scheme is outside the current
idTech1/2/3 authoring scope.

## Entity primitive ownership

Reviewed on 2026-10-21: NRC's
[`Entity_moveSelectedPrimitives`, `Entity_ungroupSelectedPrimitives` and the
`EntityMovePrimitivesToLast` Ctrl+M binding](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/entity.cpp),
[`Scene_parentSelectedBrushesToEntity`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/map.cpp)
and bobToolz's
[`DoExplodeEntity`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/contrib/bobtoolz/funchandlers-GTK.cpp),
revision `449778b3840f8f2062daab15c87cb012f2ac945c` (William Joseph, Gordon
Biggans and other contributors, GPL-2.0-or-later); and TrenchBroom's
[`MapViewBase::makeStructural` and the Alt+S "Make structural" action](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1/lib/TbUiLib/src/MapViewBase.cpp)
at `4c756be1` (v2026.2, Kristian Duske and contributors, GPL-3.0-or-later). Both
licences are compatible with VibeStudio's GPL-3.0-or-later. The original
service declared in `src/core/level_entity_ownership.h` and implemented in
`level_map.cpp` follows the last- or first-selected target, removal of emptied
source entities, selected-only ungrouping, TrenchBroom's reparenting to the
world or current group, and one entity copy per primitive with identical keys.
No source was copied. Primitives keep their ids through one undo step; scene
locks, linked groups, temporary visibility, the open group, the optional unique
`targetname` and the `map move-to-entity`/`map explode-entity` commands are
VibeStudio adaptations. TrenchBroom's smart-tag clearing beyond the detail flag
is not reproduced.

## NRC entity colour commands

Reviewed on 2026-10-21: NRC's
[`Entity_setColour`, `Entity_normalizeColor`, `NormalizeColor`, the
`EntityColorSet` K binding and the remembered `EntitySelectedColor`
preference](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/entity.cpp),
revision `449778b3840f8f2062daab15c87cb012f2ac945c` (William Joseph and other
contributors, GPL-2.0-or-later, compatible with VibeStudio's
GPL-3.0-or-later). The original service in `src/core/level_entity_colour.cpp`
follows the picker seeded from the primary entity's `_color` or the last
colour, one value written to the selected entities and the entities holding
selected brushes, and normalising so the brightest channel is 1 with black
becoming white. No source was copied. Keeping each key's 0-255 or 0-1 form
and trailing text, leaving worldspawn alone when only a world brush is
selected, skipping hidden entities, Normalise Each, the shared Inspector
colour parser and the `map entity-colour` command are VibeStudio adaptations;
scene locks, linked copies, the open group and one undo step come from the
shared key-editing service. The NetRadiant profile's K binding follows
[Xonotic NetRadiant's `EntityColor` command](https://github.com/xonotic/netradiant/blob/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant/entity.cpp),
revision `b4b295d7a37797cc2752e48aa8ce42492e7016f0` (GPL-2.0-or-later); only
the key was used.

## TB generated Create entity commands

Reviewed on 2026-10-22: TrenchBroom's
[`ActionManager::createEntityDefinitionActions`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp),
which generates an `Entities/<class>/Create` action for every loaded entity
definition except worldspawn, and
[`MapViewBase::createEntity`, `createPointEntity`, `createBrushEntity` and
`canCreateBrushEntity`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapViewBase.cpp)
with `computePointEntityPosition` in
[`MapView2D.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapView2D.cpp)
and [`MapView3D.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapView3D.cpp),
revision `4c756be1a54a28d921d18c77716a5f170bdc31d0` (v2026.2, Kristian Duske and
contributors, GPL-3.0-or-later, compatible with VibeStudio's licence). The
original service in `src/core/level_entity_create.cpp` and the Levels commands
in `src/app/level_entity_create_actions.cpp` follow the behaviour only: a
bindable Create command per point and brush class, without default keys; a
point class at the pointer or the view position, its definition bounds placed
by the cursor placement already adapted from TrenchBroom's Grid (see
[Clipboard placement](#clipboard-placement)); a brush class from the selected
brushes. No source was copied. Commands regenerated when the definitions
change, keys kept for classes not loaded, the Create Entity… picker, patches
and selected brush entities as brush-class input, refusing hidden, locked or
out-of-group brushes, and the `map create-entity` command are VibeStudio
adaptations.

## NRC layer commands and TB Move to Layer

Reviewed on 2026-10-22: NRC's
[`Scene_ExpandSelectionToLayers`, `context_moveto_menu`, `LayerAssignVisitor`,
`LayerSelectWalker` and the `ExpandSelectionToLayers` Ctrl+Q and `LayersMenu`
Shift+Q bindings in `Layers_registerCommands`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/layerswindow.cpp),
revision `449778b3840f8f2062daab15c87cb012f2ac945c` (Id Software, Garux
and other contributors, GPL-2.0-or-later), and TrenchBroom's
[Move to Layer submenu of `MapViewBase`'s context menu](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapViewBase.cpp)
at `4c756be1a54a28d921d18c77716a5f170bdc31d0` (v2026.2, Kristian Duske and
contributors, GPL-3.0-or-later). Both licences are compatible with
VibeStudio's GPL-3.0-or-later. The original service in
`src/core/level_scene_layers.cpp` and the Levels commands in
`src/app/level_scene_layer_actions.cpp` follow the behaviour only: the layers
of the selected objects expanded to every visible object in them, and a menu
of layers at the pointer, greyed out without a selection, that moves the
selection into the chosen layer. No source was copied. Layers holding nested
groups rather than nested layers, closed groups kept whole, brush entities
moved whole as TrenchBroom moves them, locks, the open group, one scene undo
step, Ctrl+Shift+Q instead of Quit's Ctrl+Q, Doom maps, and the `editor scene
select --expand-layers` and `editor scene move-to-layer` commands are
VibeStudio adaptations.

## NRC connected-entity selection

Reviewed on 2026-10-21: NRC's
[`Select_ConnectedEntities`, `EntityGetSelectedPropertyValuesWalker_nonEmpty`
and `EntityFindByPropertyValueWalker`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/select.cpp),
revision `449778b3840f8f2062daab15c87cb012f2ac945c` (Id Software and contributors,
GPL-2.0-or-later, compatible with VibeStudio's GPL-3.0-or-later). The original
`level_entity_selection.cpp` follows additive, one-hop incoming/outgoing
`target`/`targetname` matching with exact keys and values, owner-based seeds,
worldspawn exclusion and visible result traversal. No source was copied.
Closed groups, locks, open-group boundaries, cancellable preparation and
selection-only undo use VibeStudio services. A whole-owner selector is omitted
when it would implicitly select excluded children; visible children can still
be added. Loaded mixed primitive order uses source spans; new mixed primitives
without spans use stable native-array order. Doom 3's `name` key is outside the
current idTech1/2/3 authoring scope.

## NRC type selection

Reviewed on 2026-10-21: NRC's
[`Select_AllOfType` and `EntityGetSelectedPropertyValuesWalker`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/select.cpp),
[`Scene_BrushSelectByShader`, `Scene_BrushSelectByShader_Component` and `FaceSelectByShader`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brushmanip.cpp),
[`Scene_PatchSelectByShader`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/patchmanip.cpp), and
[`shader_equal`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/libs/shaderlib.h),
revision `449778b3840f8f2062daab15c87cb012f2ac945c` (Id Software, William Joseph
and contributors, GPL-2.0-or-later). The licence is compatible with VibeStudio's
GPL-3.0-or-later. No source was copied. The original shared entity-selection
service follows owner-class collection, exact class comparison, material
fallback, case-insensitive shader comparison, face replacement and vertex/edge
no-op behaviour. The current texture browser and material picker share the
material source; NRC Shift+A uses the separate **Select All of Type** action.

VibeStudio's group, hidden-object, filter, lock, cancellation and history rules
remain shared studio services. Face selection includes brush geometry implied
by an entity selector, uses validated solved-face identities, and excludes
hidden, locked and closed-group geometry. NRC's face walker visits directly
selected brush instances without an extra visibility filter; the studio's
stronger selection boundary is intentional. The GUI refuses a partial result
that exceeds its component-handle limits. Source-span ordering and no-span
primitive ordering follow the
[connected-entity selection adaptation](#nrc-connected-entity-selection).
The original studio integration now resolves active component-face picks for
surface adjustments, alignment and both clipboard routes. It preserves exact
face identities, selected-versus-explicit-hit flag rules, atomic undo and stale
result rejection. The camera/Inspector model remains the object-mode source;
independent additive surface selection for NRC Select Textured has a separate
query service and viewport state.

## NRC textured surface query

Reviewed on 2026-10-21: NRC's
[`Select_FacesAndPatchesByShader` and `SelectTextured` registration](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/select.cpp),
[`Scene_BrushFacesSelectByShader` and `FaceSelectByShader`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brushmanip.cpp),
[`Scene_PatchSelectByShader`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/patchmanip.cpp),
the [brush face-instance traversal](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brush.h)
and [shader comparison](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/libs/shaderlib.h),
revision `449778b3840f8f2062daab15c87cb012f2ac945c` (Id Software, William Joseph
and contributors, GPL-2.0-or-later). This licence is compatible with VibeStudio's
GPL-3.0-or-later. No source was copied.

The original `level_textured_selection.cpp` service follows additive independent
face picks, whole-patch picks, case-insensitive material matching and no-match
preservation. It does not turn face matches into parent-brush selection. The
CLI, exact geometry fingerprints, complete-result limits, cancellation and
selection history use original studio implementations. New brush and patch
matches both respect studio visibility, locks and open-group boundaries; closed
groups must be opened. This deliberately differs from the upstream unfiltered
brush walker. Existing picks are retained and authoring consumers must recheck
their editability. The viewport action and NRC Ctrl+Shift+A binding now use this
service. Staged deselection follows `Selection_Deselect` in the same pinned
`radiant/select.cpp`: clear surface/component picks, then leave component mode,
then clear whole objects. The independent state, geometry retirement, capacity
preflight, dashed plan overlay, camera highlight lookup and accessible counts
are original Qt implementations.

Independent face manipulation was reviewed on 2026-10-21 against NRC's
[`FaceInstance::transformComponents` and `BrushInstance::transformComponents`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brush.h),
the face-mode toggle [`SelectFaceMode`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/tools.cpp)
and the component branches of `translate`, `rotate`, `scale` and `skew` in
[`RadiantSelectionSystem`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/selection.cpp),
at the same revision and licence, and against the face tool behaviour of
TrenchBroom v2026.2 (`4c756be1`, GPL-3.0-or-later) already credited above. No
source was copied. In NRC's face mode, faces selected by Select Textured
transform with the component selection without selecting their brushes; the
studio follows that workflow in **Edit Faces** mode. The merge into component
sets, the corner-based transform through the shared component service (as
TrenchBroom's face tool moves face corners, rather than NRC's plane
transform), the plane-based pick remapping, the undo/redo pick restoration, the
arrow-key nudge, the CLI `--face` route and the scene-lock, visibility and
group checks are original implementations.

## Reveal material and Find and Replace Textures

Reviewed on 2026-10-22: TrenchBroom v2026.2, revision
`4c756be1a54a28d921d18c77716a5f170bdc31d0`,
[`MapWindow::revealMaterial` and `materialToReveal`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapWindow.cpp),
[`MaterialBrowser::revealMaterial`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MaterialBrowser.cpp)
and the "Reveal in texture browser" action in
[ActionManager.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp)
(Kristian Duske, GPL-3.0-or-later); and NetRadiant Custom revision
`449778b3840f8f2062daab15c87cb012f2ac945c`,
[`FindTextureDialog` and its focus tracker](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/findtexturedialog.cpp),
[`FindReplaceTextures`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/select.cpp),
[`Scene_BrushFindReplaceShader*`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brushmanip.cpp),
[`Scene_PatchFindReplaceShader*`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/patchmanip.cpp)
and [`TextureBrowser_SetSelectedShader`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/texwindow.cpp)
(Id Software, Leonardo Zide, TTimo and contributors, GPL-2.0-or-later). Both
licences are compatible with VibeStudio's GPL-3.0-or-later. No source was copied.

**Reveal Material** follows TrenchBroom's rule that the selected faces, or the
faces of the selected brushes, must all share one material, and its browser
reveal clearing the filter before scrolling to the material. Patches, Doom
walls and flats, the Textures-tab tile selection that makes the material
current, the early stop on a second material and the status-tip explanation
are VibeStudio additions. **Find and Replace Textures** follows NetRadiant
Custom's modeless window: a texture-browser pick fills the field that last had
focus, a pick does not texture the selection while the window is open, an
empty replacement means search, and a replacement is one undo step across the
map or within the selection. The three-way scope with picked faces, separate
Find and Replace buttons, the live preview, skipping hidden, filtered, locked
and out-of-context surfaces, Find reusing the Select Textured service and the
`map replace-texture --face` and `map select-textured --within-selection` CLI
routes are original implementations (`core/level_material_find.cpp`,
`app/level_find_replace_dialog.cpp`, `app/level_find_replace_actions.cpp`).

## Brush file order on save

Reviewed on 2026-10-21: TrenchBroom v2026.2, revision
`4c756be1a54a28d921d18c77716a5f170bdc31d0`,
[VertexTool.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/VertexTool.cpp),
[ExtrudeTool.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ExtrudeTool.cpp)
and [ClipTool.cpp](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ClipTool.cpp)
(Kristian Duske, GPL-3.0-or-later), and NetRadiant Custom revision
`449778b3840f8f2062daab15c87cb012f2ac945c`,
[`BrushSplitByPlaneSelected` and `SubtractBrushesFromUnselected`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/csg.cpp)
(GPL-2.0-or-later). Both licences are compatible with VibeStudio's
GPL-3.0-or-later licence. Both editors keep an edited brush in its parent's
child order, so its place in the saved file stays; NRC's clipper keeps the
clipped brush in place and adds only the split fragment to the parent. No
source was copied. `levelMapBrushSaveLine()`, the splice fields on
`LevelMapBrush`, the replacement heir rule (identity first, then the first
recorded piece) and `level_brush_file_order_smoke_test.cpp` are original
VibeStudio implementations; giving the first CSG fragment the original's place
is a VibeStudio choice that keeps more brush numbers stable than appending
every fragment.

## Familiar keys for registered Levels commands

Reviewed on 2026-10-21. The TrenchBroom profile's V, E and F (Vertex, Edge
and Face tools), Ctrl+Alt+Shift+V (Snap Vertices to Grid) and Ctrl+Shift+D
(Create Linked Duplicate) are default bindings read from TrenchBroom v2026.2
[`ActionManager.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp)
(Kristian Duske and contributors, GPL-3.0-or-later). The NetRadiant Custom
profile's V, E and F (`DragVertices`, `DragEdges`, `DragFaces`,
[`radiant/tools.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/tools.cpp)),
the keypad patch row and column keys, Ctrl+I, Ctrl+Shift+M, Ctrl+T, Ctrl+Shift+W
and Shift+I
([`radiant/patchmanip.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/patchmanip.cpp)),
Alt+D, Alt+S and Shift+T
([`radiant/brushmanip.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brushmanip.cpp))
and Ctrl+Shift+Tab (`CenterXYView`,
[`radiant/xywindow.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/xywindow.cpp))
are default bindings read from revision
`449778b3840f8f2062daab15c87cb012f2ac945c` (Id Software, William Joseph and
contributors, GPL-2.0-or-later). Both licences are compatible with
VibeStudio's GPL-3.0-or-later licence. Only key facts were used; the commands
they reach, the shared command registry entries in `src/core/studio_semantics.cpp`
and `level_palette_coverage_ui_smoke_test.cpp` are original VibeStudio work.
Keys the studio already gives window-wide commands (Ctrl+Shift+I, Ctrl+W)
are left unbound.

## Large working grids

Reviewed on 2026-10-22. The Levels grid sizes 512 and 1024, the 1024-unit
upper limit of **Larger Grid**, and the policy that choosing or stepping the
grid turns snapping on in the NetRadiant Custom profile follow `GridPower`,
`GridPrev`, `GridNext` and `setGridPower` in
[`radiant/grid.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/grid.cpp)
of NetRadiant Custom revision `449778b3840f8f2062daab15c87cb012f2ac945c`
(William Joseph and contributors, GPL-2.0-or-later). The standalone NetRadiant
profile keeps the same policy because
[`radiant/grid.cpp`](https://github.com/xonotic/netradiant/blob/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant/grid.cpp)
at Xonotic NetRadiant `b4b295d7a37797cc2752e48aa8ce42492e7016f0`
(GPL-2.0-or-later) also sets snapping in those commands. TrenchBroom v2026.2
(`4c756be1`, GPL-3.0-or-later) stops at 256 in `ActionManager.cpp`, so its
profile's keys are unchanged. The Eureka profile's 9 and 0 keys for the 512-
and 1024-unit grids are the `GRID_Set` lines of Eureka's
[`bindings.cfg`](https://github.com/ioan-chera/eureka-editor/blob/f951281878f1f8f2619e0073592b6995aeea0dfc/bindings.cfg)
at `f9512818` (GPL-2.0-or-later per upstream README). All three licences are
compatible with VibeStudio's GPL-3.0-or-later licence. Only behavioural and key
facts were used; no source was copied. `levelEditorGridSizes()`,
`LevelEditorControls::gridChangeEnablesSnap`, the `editing.gridChangeEnablesSnap`
gesture preference, `src/app/level_grid_actions.cpp`, the plan-view grid lines in
`src/app/map_grid.cpp` and the `level-large-grid` tests are original VibeStudio
work.

## Fractional working grids

Reviewed on 2026-10-22. The Levels grid sizes 0.125, 0.25 and 0.5, Smaller
Grid continuing below 1 unit down to 0.125, and the three commands having no
default key follow `GridPower` (`GRIDPOWER_0125` to `GRIDPOWER_05`), `GridPrev`
and the `SetGrid0.125`, `SetGrid0.25` and `SetGrid0.5` registrations in
[`radiant/grid.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/grid.cpp)
of NetRadiant Custom revision `449778b3840f8f2062daab15c87cb012f2ac945c`
(William Joseph and contributors, GPL-2.0-or-later), and the Decrease Grid
Size and Set Grid Size 0.125 to 0.5 actions (grid exponents -3 to -1, no
default key) in
[`lib/TbUiLib/src/ActionManager.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp)
and `canDecGridSize` in `lib/TbUiLib/src/MapWindow.cpp` of TrenchBroom
v2026.2 (`4c756be1a54a28d921d18c77716a5f170bdc31d0`, Kristian Duske and
contributors, GPL-3.0-or-later). Both licences are compatible with VibeStudio's
GPL-3.0-or-later licence. Only behavioural facts were used; no source was
copied. Keeping binary Doom and Hexen maps on whole units, the thinned-line
status text, `levelEditorFinestGridUnits()`, `levelEditorSteppedGridUnits()`,
`levelEditorGridCommandId()`, `ApplicationShell::levelMapGridUnits()`,
`refreshLevelMapGridChoices()`, `mapGridDrawnSpacing()` and the
`level-fractional-grid` tests are original VibeStudio work.

## World-axis quick transforms and the quick-turn pivot

Reviewed on 2026-10-22. **Flip X/Y/Z** and **Rotate X/Y/Z**, their signs
(X -90, Y +90, Z -90), their place under Edit > Transform and their lack of a
default key follow `Selection_Flipx` to `Selection_Rotatez`,
`Selection_RotateClockwise` and the `MirrorSelectionX` to `RotateSelectionZ`
registrations in
[`radiant/select.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/select.cpp),
and the grid-snapped quick-turn pivot follows `rotateSelected` and
`scaleSelected` with `snapOrigin` in
[`radiant/selection.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/selection.cpp)
of NetRadiant Custom revision `449778b3840f8f2062daab15c87cb012f2ac945c`
(William Joseph and other contributors, GPL-2.0-or-later), and `MapViewBase::rotate`
(the grid reference point of the selection bounds) and `MapViewBase::flip` (the
half-grid reference point, TrenchBroom issue 1495) in
[`lib/TbUiLib/src/MapViewBase.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapViewBase.cpp)
of TrenchBroom v2026.2 (`4c756be1a54a28d921d18c77716a5f170bdc31d0`, Kristian
Duske and contributors, GPL-3.0-or-later). Both licences are compatible with
VibeStudio's GPL-3.0-or-later licence. Only behavioural facts were used; no
source was copied. `src/core/level_quick_transform.*`,
`src/app/level_axis_transform_actions.cpp`,
`LevelEditorControls::quickTurnSnapsPivot`, the `editing.quickTurnSnapsPivot`
gesture preference, `map rotate --pivot-grid`, keeping flips on the exact
centre outside the TrenchBroom profile (see the next-but-one entry) and the
`level-axis-transform` tests are original VibeStudio work.

## Directional and depth nudges

Reviewed on 2026-10-22. The **Nudge** commands follow TrenchBroom's "Move
objects" actions (Up, Down, Left, Right, Page Up and Page Down) and their order
of targets (rotation centre, node handles, sweep centre, objects) in
`MapViewBase::move`, `moveRotationCenter`, `moveSweepCenter` and
`moveNodeHandles` in
[`lib/TbUiLib/src/ActionManager.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp)
and
[`lib/TbUiLib/src/MapViewBase.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapViewBase.cpp),
and the plan and camera axes of `MapView2D::moveDirection` and
`MapView3D::moveDirection` (the nearest horizontal axis, the camera's up axis
when it looks straight down or up, and right turned a quarter about Z when it
snaps onto forward) in `lib/TbUiLib/src/MapView2D.cpp` and
`lib/TbUiLib/src/MapView3D.cpp` of TrenchBroom v2026.2
(`4c756be1a54a28d921d18c77716a5f170bdc31d0`, Kristian Duske and contributors,
GPL-3.0-or-later). The Radiant profiles' <kbd>Alt</kbd>+arrow and keypad plus
and minus keys, their use of the current plan view's axes from any window and
their single undo step follow `NudgeSelection`, `AxisBase_forViewType`,
`Selection_NudgeLeft` to `Selection_NudgeDown`, `Selection_MoveUp`,
`Selection_MoveDown` and the `SelectNudgeLeft` to `SelectNudgeDown`,
`MoveSelectionUP` and `MoveSelectionDOWN` registrations in
[`radiant/select.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/select.cpp)
of NetRadiant Custom revision `449778b3840f8f2062daab15c87cb012f2ac945c`
(William Joseph and other contributors, GPL-2.0-or-later), which keeps them
from GtkRadiant. Both licences are compatible with VibeStudio's
GPL-3.0-or-later licence. Only behavioural and key facts were used; no source
was copied. `src/core/level_nudge.*`, `src/app/level_nudge_actions.cpp`, the
views claiming nudge keys ahead of window-wide commands
(`StudioCommandRegistry::setShortcutScopeClaims`), Doom maps keeping
<kbd>Page Up</kbd> and <kbd>Page Down</kbd> for Raise Floor and Lower Floor,
the <kbd>Shift</kbd> eight-step nudge, `map nudge` and the `level-nudge` tests
are original VibeStudio work.

### Duplicate and Move, Move to Camera and Repeat Transforms

Reviewed on 2026-10-22. **Duplicate and Move Left**, **Right**, **Forward**,
**Back**, **Depth Up** and **Depth Down**, their <kbd>Ctrl</kbd>+arrow and
<kbd>Ctrl</kbd>+<kbd>Page Up</kbd>/<kbd>Page Down</kbd> keys in the TrenchBroom
profile, and their one-transaction copy along the move direction follow the
"Duplicate and move objects" actions in
[`lib/TbUiLib/src/ActionManager.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp)
and `MapViewBase::duplicateAndMoveObjects` in
[`lib/TbUiLib/src/MapViewBase.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapViewBase.cpp)
of TrenchBroom v2026.2 (`4c756be1a54a28d921d18c77716a5f170bdc31d0`, Kristian
Duske and contributors, GPL-3.0-or-later). **Move to Camera** (its
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>V</kbd> key, the snapped translation from
the selection's bounds centre to the camera and its single undo step) follows
`MoveToCamera` and `TranslateToCamera`, and **Repeat Transforms** (its
<kbd>Ctrl</kbd>+<kbd>R</kbd> key, the clone in object mode, the last
translation, rotation, scale and skew applied about the current pivot, their
survival across selection changes, and the component-mode transform) follows
the `"RepeatTransforms"` and `"ResetTransforms"` registrations in
[`radiant/select.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/select.cpp)
and `RadiantSelectionSystem::repeatTransforms`, `resetTransforms` and the
`TransformsObserved` slots in
[`radiant/selection.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/selection.cpp)
of NetRadiant Custom revision `449778b3840f8f2062daab15c87cb012f2ac945c`
(William Joseph and other contributors, GPL-2.0-or-later). Both licences are
compatible with VibeStudio's GPL-3.0-or-later licence. Only behavioural and key
facts were used; no source was copied. `levelDuplicateMoveCommands()` in
`src/core/level_nudge.*`, the repeatable-transform slots and replay in
`src/core/level_repeat.*`, the Affine component transform,
`moveLevelMapSelectionToCamera()` in `src/core/level_paste.*`,
`src/app/level_duplicate_move_actions.cpp`, `src/cli/level_move_repeat.*`,
`map nudge --duplicate`, `map move --camera`, `map repeat-transforms` and the
`level-duplicate-move` tests are original VibeStudio work.

## View-relative turns and flips

Reviewed on 2026-10-22. **Roll Clockwise** and **Counter-clockwise**, yaw on
**Rotate 90° Right** and **Left**, **Pitch Clockwise** and
**Counter-clockwise**, and the view-relative **Flip Horizontal** and **Flip
Vertical** follow TrenchBroom's "Roll", "Yaw" and "Pitch objects clockwise /
counter-clockwise" and "Flip objects horizontally / vertically" actions and
their default keys (<kbd>Alt</kbd>+arrows, <kbd>Alt</kbd>+<kbd>Page Up</kbd> and
<kbd>Page Down</kbd>, <kbd>Ctrl</kbd>+<kbd>F</kbd> and
<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F</kbd>) in
[`lib/TbUiLib/src/ActionManager.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp),
the turn axes (roll about the reversed forward direction, pitch about right,
yaw about up, reversed for clockwise), the sweep cap turned by the grid angle
and the half-grid flip pivot of `MapViewBase::rotate`, `rotationAxis` and
`flip` in
[`lib/TbUiLib/src/MapViewBase.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapViewBase.cpp),
and the plan and camera flip axes of `MapView2D::flipAxis` and
`MapView3D::flipAxis` in `lib/TbUiLib/src/MapView2D.cpp` and
`lib/TbUiLib/src/MapView3D.cpp` of TrenchBroom v2026.2
(`4c756be1a54a28d921d18c77716a5f170bdc31d0`, Kristian Duske and contributors,
GPL-3.0-or-later), whose licence is VibeStudio's own. Only behavioural and key
facts were used; no source was copied. The view frames in
`src/core/level_quick_transform.*` (built on the nudge axes of
`src/core/level_nudge.*`), `flipLevelMapSelection()` with a pivot,
`LevelEditorControls::flipSnapsPivot` and the `editing.flipSnapsPivot` gesture
preference, `LevelSweepGizmo::rotateDestination()`,
`ApplicationShell::applyLevelViewTransformFromUi()`, the views claiming the
keys ahead of Go Back, Go Forward and Find, `map view-transform` and the
`level-view-transform` tests are original VibeStudio work.

## View filter commands and Radiant's world rule

Reviewed on 2026-10-22. The Levels view filters as checkable commands,
**Invert Filters** (every filter flag toggled in turn), **Show Every Kind**
(Radiant's reset), the func_group kind and the NetRadiant Custom profile's
filter keys (<kbd>Alt+1</kbd> World, <kbd>Alt+2</kbd> Entities,
<kbd>Alt+5</kbd> Liquids, <kbd>Alt+6</kbd> Caulk, <kbd>Alt+7</kbd> Clips,
<kbd>Alt+8</kbd> Paths, <kbd>Alt+0</kbd> Lights, <kbd>Ctrl+D</kbd> Details,
<kbd>Ctrl+H</kbd> Hints, <kbd>Shift+M</kbd> Models and
<kbd>Ctrl+Shift+T</kbd> Triggers) follow `ToggleFilterFlag`, `InvertFilters`,
`ResetFilters` and `ConstructFilters` in
[`radiant/filters.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/filters.cpp);
the world rule (worldspawn, func_group and func_detail*), the Entities filter
as its inverse and the point-entity rule that leaves lights and misc models to
their own filters follow `filter_entity_world`, `filter_entity_point` and
`Entity_InitFilters` in
[`plugins/entity/entity.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/plugins/entity/entity.cpp)
of NetRadiant Custom revision `449778b3840f8f2062daab15c87cb012f2ac945c`
(William Joseph and other contributors, GPL-2.0-or-later). The NetRadiant
profile keeps the same keys because
[`radiant/filters.cpp`](https://github.com/xonotic/netradiant/blob/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant/filters.cpp)
at Xonotic NetRadiant `b4b295d7a37797cc2752e48aa8ce42492e7016f0`
(GPL-2.0-or-later) binds them too. <kbd>Ctrl+P</kbd> (Patches) stays unbound
because the studio keeps it for **Go to File**. The Brushes kind, every brush
whatever its owner and no patches, follows the "View Filter > Toggle show
brushes" action in
[`lib/TbUiLib/src/ActionManager.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp)
and its use in `lib/TbUiLib/src/MapViewBase.cpp` of TrenchBroom v2026.2
(`4c756be1a54a28d921d18c77716a5f170bdc31d0`, Kristian Duske and contributors,
GPL-3.0-or-later). All three licences are compatible with VibeStudio's
GPL-3.0-or-later licence. Only behavioural and key facts were used; no source
was copied. `levelViewFilterKinds()`, `levelViewFilterCommandId()`,
`levelViewFilterWorldClass()`, `levelViewFiltersInverted()`, the saved-list
helpers in `src/core/level_view_filters.*`, `src/app/level_view_filter_actions.cpp`,
`map filters` (`src/cli/level_view_filters.*`) and the `level-view-filters`
tests are original VibeStudio work.

Reviewed on 2026-10-24. The texture kinds follow the same revisions. Area
Portals (any areaportal face), Cluster Portals, Light Grid and Bot Clip (every
face botclip or donotenter), Decals (any face under `textures/decals/`), the
caulk brush and face filters with Quake's skip as caulk, and the keys
<kbd>Alt+3</kbd> Areaportals, <kbd>Alt+9</kbd> Clusterportals,
<kbd>Ctrl+Shift+D</kbd> Structural, <kbd>Alt+M</kbd> BotClips and
<kbd>Shift+D</kbd> Decals follow `BrushFilters_construct` in
[`radiant/brushmanip.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brushmanip.cpp)
and `ConstructFilters` in
[`radiant/filters.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/filters.cpp);
Structural as the inverse of Detail, by entity on Quake maps, follows
`filter_entity_not_func_detail` in
[`plugins/entity/entity.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/plugins/entity/entity.cpp);
and the camera leaving filtered caulk faces out while the 2D wireframes keep
every edge follows `FaceInstance::render` and `BrushInstance::evaluateViewDependent`
in [`radiant/brush.h`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brush.h)
(all NetRadiant Custom `449778b3840f8f2062daab15c87cb012f2ac945c`,
GPL-2.0-or-later). Xonotic NetRadiant's
[`radiant/filters.cpp`](https://github.com/xonotic/netradiant/blob/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant/filters.cpp)
at `b4b295d7a37797cc2752e48aa8ce42492e7016f0` binds the same five keys, so the
NetRadiant profile keeps them. The content bits CONTENTS_AREAPORTAL,
CONTENTS_CLUSTERPORTAL, CONTENTS_DONOTENTER and CONTENTS_BOTCLIP are the values
of Quake III Arena's `code/game/surfaceflags.h` (id Software, GPL-2.0-or-later);
only the numbers are used. The matching rules, `levelViewFilterFaceMatches()`,
`levelViewFilteredFaces()`, the camera's `hiddenFaces` option and the
`level-filter-texture-kinds` tests are original VibeStudio work.

Reviewed on 2026-10-24. The material kinds follow the same NetRadiant Custom
revision. Translucent (any face whose shader carries QER_TRANS or
QER_ALPHATEST, and translucent patches), Liquids (QER_LIQUID and the
`textures/liquids/` prefix), Sky (QER_SKY), and Clip taking clip-shaded
patches and `textures/common/weapclip`, `common/clip` and `common/fullclip`
brushes follow `BrushFilters_construct` in
[`radiant/brushmanip.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brushmanip.cpp)
and `PatchFilters_construct` in
[`radiant/patchmanip.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/patchmanip.cpp);
which shader keywords set those flags (qer_trans and surfaceparm fog for
QER_TRANS, qer_alphafunc for QER_ALPHATEST, skyparms for QER_SKY, surfaceparm
water, lava and slime for QER_LIQUID, playerclip, botclip and nodraw) follows
the Quake III shader parser in
[`plugins/shaders/shaders.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/plugins/shaders/shaders.cpp);
and the <kbd>Alt+4</kbd> FilterTranslucent key follows `ConstructFilters` in
[`radiant/filters.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/filters.cpp)
(all NetRadiant Custom `449778b3840f8f2062daab15c87cb012f2ac945c`,
GPL-2.0-or-later), which Xonotic NetRadiant's
[`radiant/filters.cpp`](https://github.com/xonotic/netradiant/blob/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant/filters.cpp)
at `b4b295d7a37797cc2752e48aa8ce42492e7016f0` binds too. The Nodraw kind and
the player clip part follow the NoDraw and ActorClip toggles of NRC's UFO:AI
plugin in
[`contrib/ufoaiplug/ufoai.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/contrib/ufoaiplug/ufoai.cpp)
and [`ufoai_filters.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/contrib/ufoaiplug/ufoai_filters.cpp)
(SURF_NODRAW 0x80 and CONTENTS_ACTORCLIP 0x10000), judged here on every face. Quake II's
SURF_TRANS33, SURF_TRANS66 and SURF_NODRAW and the CONTENTS_PLAYERCLIP and
CONTENTS_MONSTERCLIP values are those of the id Software Quake II source
(`qcommon/qfiles.h`, GPL-2.0-or-later); only the numbers are used. VibeStudio's
additions (a stage's alphaFunc, surfaceparm monsterclip and donotenter, images
without a script as plain shaders, Quake's `{` fence textures, the player,
monster and weapon clip split), `levelViewFilterShader()`,
`levelViewFilterMaterials()`, `scanLevelViewFilterMaterials()`, the shell's
background read and status line, `map filters --package` and the
`level-filter-material-kinds` tests are original VibeStudio work over its own
material library; no source was copied.

Reviewed on 2026-10-26. Filter tags that game or project data define, and one
filter per entity class, follow TrenchBroom v2026.2
(`4c756be1a54a28d921d18c77716a5f170bdc31d0`, Kristian Duske and contributors,
GPL-3.0-or-later): the generated "Toggle *tag* visible" and "Toggle *class*
visible" actions of `ActionManager::createTagActions` and
`createEntityDefinitionActions` in
[`lib/TbUiLib/src/ActionManager.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp),
`MapViewBase::toggleTagVisible` and `toggleEntityDefinitionVisible` in
[`lib/TbUiLib/src/MapViewBase.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapViewBase.cpp),
and the tag check boxes and the prefix-grouped `EntityDefinitionCheckBoxList`
with Show all and Hide all in
[`lib/TbUiLib/src/ViewEditor.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ViewEditor.cpp).
The tag layout VibeStudio reads (`"tags"` with `"brush"` and `"brushface"`
lists of `name`, `match` = material/texture, surfaceparm, contentflag,
surfaceflag or classname, `pattern` and `flags`, and `"faceattribs"` flag names
in bit order) is TrenchBroom's documented game configuration format, read so
that a game configuration can be used as it is; no TrenchBroom game
configuration ships with VibeStudio. The text those configurations are written
in is TrenchBroom's expression language, a superset of JSON; VibeStudio reads
the parts its game configurations use (`//` and `/* */` comments, bare names,
a comma before a closing bracket, and a backslash JSON does not define kept as
written, as TrenchBroom keeps `"\**"`) by turning the text into strict JSON in
an original reader, and the test configuration is original data in that
layout. Hiding a brush whose faces all carry hidden tags or kinds follows
TrenchBroom's `EditorContext` visibility check
(`BrushNode::allFacesHaveAnyTagInMask`). NetRadiant Custom's Hints filter taking a
hint face only follows `g_filter_brush_hint` in
[`radiant/brushmanip.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brushmanip.cpp)
(`449778b3840f8f2062daab15c87cb012f2ac945c`, GPL-2.0-or-later). CONTENTS_FOG
(0x40) and SURF_HINT/SURF_SKIP (0x100, 0x200) are the values of id Software's
Quake II and Quake III sources (GPL-2.0-or-later); only the numbers are used.
Only behaviour and data layout were used; no source was copied.
`src/core/level_filter_tags.*`, the Skip and Fog kinds, the glob matcher, the
configuration text reader, the face-by-face hiding of skip and face tags,
`src/app/level_filter_tag_actions.cpp`,
`map filters --tags/--project/--class/--classes`, `map visibility --class` and
the `level-filter-tags` tests are original VibeStudio work.

## Level camera levelling, focus and zoom reset

Reviewed on 2026-10-22. **Level Camera** (pitch to zero, heading to the nearest
22.5-degree step, position kept) follows `GlobalCamera_ResetAngles` and its
`CenterView` command on <kbd>End</kbd>; **Focus Camera on Selection** (heading
and pitch kept, the camera backed off along the view until every corner of the
selection's box fits the view's side planes, at least 64 units from its centre,
with the last selection's box when nothing is selected) follows
`Camera_getFocusPos` and `GlobalCamera_FocusOnSelected`; and the held free-focus
key with its wheel offset follows `CameraFreeFocus` (<kbd>Tab</kbd> during free
move) and `wheelmove_scroll` in
[`radiant/camwindow.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/camwindow.cpp)
of NetRadiant Custom revision `449778b3840f8f2062daab15c87cb012f2ac945c`
(GPL-2.0-or-later). The NetRadiant profile keeps <kbd>End</kbd> and
<kbd>Tab</kbd> because
[`radiant/camwindow.cpp`](https://github.com/xonotic/netradiant/blob/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant/camwindow.cpp)
at Xonotic NetRadiant `b4b295d7a37797cc2752e48aa8ce42492e7016f0`
(GPL-2.0-or-later) binds them too. The Radiant family's <kbd>End</kbd> for the
GtkRadiant, Q3Radiant and DoomEdit profiles follows `ID_VIEW_CENTER` and
`OnViewCenter` in Q3Radiant's
[`MainFrm.cpp`](https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/q3radiant/MainFrm.cpp)
(`dbe4ddb10315479fc00086f08e25d968b4b43c49`, id Software, GPL-2.0-or-later) and
`Camera_CenterView` in Doom 3's
[`MainFrm.cpp`](https://github.com/id-Software/DOOM-3/blob/a9c49da5afb18201d31e3f0a429a037e56ce2b9a/neo/tools/radiant/MainFrm.cpp)
(`a9c49da5afb18201d31e3f0a429a037e56ce2b9a`, id Software, GPL-3.0-or-later with
additional terms). **Reset Camera Zoom** on <kbd>Ctrl+Alt+Z</kbd> follows the
"Controls/Map view/Reset camera zoom" action in
[`lib/TbUiLib/src/ActionManager.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp)
and `MapViewBase::resetCameraZoom` of TrenchBroom v2026.2
(`4c756be1a54a28d921d18c77716a5f170bdc31d0`, Kristian Duske and contributors,
GPL-3.0-or-later). All of these licences are compatible with VibeStudio's
GPL-3.0-or-later licence. Only behavioural and key facts were used; no source
was copied. The frustum fit, the near clearance, the rounding of exact half
steps, the geometric wheel offset (eight notches halve or double the distance),
Tab acting only during mouse look, `src/core/level_camera_focus.*`,
`src/app/level_camera_focus_actions.cpp`, `map camera-focus`
(`src/cli/level_camera_focus.*`) and the `level-camera-focus` tests are
original VibeStudio work.

## Level camera display modes

Reviewed on 2026-10-25. **Show Textures in Camera**, **Hide Textures in
Camera** and **Hide Faces in Camera** (textured, flat or no faces, with no
default keys) and **Edges in Camera** (an edge overlay over the faces) follow
the "Controls/Map view/View Filter > Show textures", "Hide textures", "Hide
faces" and "Show edges" actions in
[`lib/TbUiLib/src/ActionManager.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp)
and `MapViewBase::showMaterials`, `hideMaterials`, `hideFaces` and
`toggleShowEdges` of TrenchBroom v2026.2
(`4c756be1a54a28d921d18c77716a5f170bdc31d0`, Kristian Duske and contributors,
GPL-3.0-or-later). **Next Camera Mode** and **Previous Camera Mode** (a
wrapping cycle of wireframe, flat, textured and textured with edges, saved, on
<kbd>Shift</kbd>+<kbd>]</kbd> and <kbd>Shift</kbd>+<kbd>[</kbd>) follow
`CameraModeNext`, `CameraModePrev` and the `CameraRenderMode` preference in
[`radiant/camwindow.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/camwindow.cpp)
of NetRadiant Custom revision `449778b3840f8f2062daab15c87cb012f2ac945c`
(GPL-2.0-or-later). Both licences are compatible with VibeStudio's
GPL-3.0-or-later licence. Only behavioural and key facts were used; no source
was copied. The three saved choices, 3D Wireframe keeping the textures and
edges choices, the status announcements, polygon-edge outlines that leave fan
diagonals out of the edge overlay and wireframe (`ModelViewport::setPolygonEdges`
and the surface shader's diagonal bits), `src/core/level_camera_render.*`,
`src/app/level_camera_render_actions.cpp` and the `level-camera-render` tests
are original VibeStudio work.

## Plan pane visibility

Reviewed on 2026-10-22. **Show Top View**, **Show Front View** and **Show Side
View** follow the checkable `ToggleView`, `ToggleFrontView` and
`ToggleSideView` toggles (`g_xy_top_shown`, `g_xz_front_shown`,
`g_yz_side_shown`, persisted as `XYVIS`, `XZVIS` and `YZVIS`, no default keys)
in
[`radiant/xywindow.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/xywindow.cpp)
and their **View** menu items in
[`radiant/mainframe.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/mainframe.cpp)
of NetRadiant Custom revision `449778b3840f8f2062daab15c87cb012f2ac945c`
(William Joseph and contributors, GPL-2.0-or-later, compatible with
VibeStudio's GPL-3.0-or-later licence). Only the behaviour was used; no source
was copied. NRC offers the toggles for floating windows only; applying them to
fixed splitter panes, keeping one pane shown, moving the active plan, per
combination splitter proportions, revealing a pane from **Top/Front/Side View**,
`LevelPlanPanes` in `src/core/level_editor_controls.*`,
`src/app/level_plan_pane_actions.cpp`, `editor layout --panes` and the
`level-plan-panes` tests are original VibeStudio work.

## Grid coordinates and block grid

Reviewed on 2026-10-26. **Show Grid Coordinates** (numbers on the 2D grid
lines, at least 32 pixels apart) and **Show Block Grid** (the compiler block
grid from worldspawn `_blocksize`: one or three numbers, 1024 by default, 0
leaving an axis unsplit, kept inside the region, no default keys) follow the
`ShowCoordinates` and `ShowBlocks` toggles, `XYWnd::XY_DrawGrid` and
`XYWnd::XY_DrawBlockGrid` in
[`radiant/xywindow.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/xywindow.cpp)
and their **View** > **Show** menu items in
[`radiant/mainframe.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/mainframe.cpp)
of NetRadiant Custom revision `449778b3840f8f2062daab15c87cb012f2ac945c`
(GPL-2.0-or-later, compatible with VibeStudio's GPL-3.0-or-later licence).
How the key is read (`_blocksize`, then `blocksize` and `chopsize`, `%d %d %d`
with the legacy one- and two-number forms) follows `ProcessWorldModel` in
`tools/quake3/q3map2/bsp.cpp` of VibeMap3 (the q3map2 fork pinned under
`external/compilers/vibemap3`, GPL-2.0). Only behaviour was used; no source was
copied. Unlike NetRadiant Custom, the views draw the size the compiler uses
rather than clamping it to 256-65536, thin dense block lines onto real
boundaries, dash them, keep coordinate labels clear of the status tags and of
each other, put the vertical labels on the leading edge with numbers read left
to right, and leave both overlays off by default. `src/core/level_block_grid.*`,
the label and block-line helpers in `src/app/map_grid.*`,
`src/app/map_viewport_grid_overlays.cpp`, `src/app/level_grid_overlay_actions.cpp`,
`map render --block-grid` and the `level-block-grid` and `level-grid-overlays`
tests are original VibeStudio work.

## Radiant texture steps

NetRadiant Custom's texture step commands were reviewed on 2026-10-22 at revision
`449778b3840f8f2062daab15c87cb012f2ac945c` (id Software and contributors,
GPL-2.0-or-later):
`TexRotateClock`, `TexRotateCounter` and `TexScaleUp`/`Down`/`Left`/`Right` with
`Texdef_Rotate*`, `Texdef_Scale*` and `Select_RotateTexture`/`Select_ScaleTexture` in
[radiant/select.cpp](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/select.cpp);
`Texdef_Scale`, `Texdef_Rotate`, `Valve220_rotate`, `BPTexdef_Scale`,
`BPTexdef_Rotate` and `TexMatToFakeTexCoords` in
[radiant/brush_primit.cpp](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brush_primit.cpp);
`removeScale`/`addScale` in
[radiant/brush_primit.h](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brush_primit.h)
and [radiant/brush.h](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brush.h);
`Patch::ScaleTexture` and `Patch::RotateTexture` in
[radiant/patch.cpp](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/patch.cpp);
`Patch_convertScale` in
[radiant/patchdialog.cpp](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/patchdialog.cpp);
and the `si_globals_t` increments in
[radiant/surfacedialog.h](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/surfacedialog.h).
The licence is compatible with VibeStudio's GPL-3.0-or-later licence. Only the
arithmetic, defaults and key bindings were used as behavioural facts; no source
was copied. `RadiantScale`, `RadiantRotate`, `levelSurfaceRadiantScaleStep()` and
the shared centre turn in `src/core/level_surface.*`, the Surfaces tab controls,
the `map align-textures --scale-step` and `--anchor` options and the
`level-surface-step` tests are original VibeStudio work. Patches turn in texels
rather than NRC's normalised coordinates; see
[EDITOR_PROFILES.md](EDITOR_PROFILES.md#nrc-texture-steps).

## Camera-relative texture moves and flips

Reviewed on 2026-10-24. **Move Textures Up**, **Down**, **Left** and **Right**,
their **(Coarse)** and **(Fine)** forms, and **Flip Textures Horizontally** and
**Vertically** follow TrenchBroom's "Move textures ..." and "Flip textures ..."
actions, their 3D-view, face-selection context and their default keys (the
arrows, <kbd>Shift</kbd>+arrows, <kbd>Ctrl</kbd>+arrows,
<kbd>Ctrl</kbd>+<kbd>F</kbd> and <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F</kbd>) in
[`lib/TbUiLib/src/ActionManager.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp),
and the distances (the grid size, twice it, or one unit) and the camera up and
right vectors handed on by `MapViewBase::moveUV`, `moveUVOffset`,
`moveUVDistance` and `flipUV` in
[`lib/TbUiLib/src/MapViewBase.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapViewBase.cpp)
of TrenchBroom v2026.2 (`4c756be1a54a28d921d18c77716a5f170bdc31d0`, Kristian
Duske and contributors, GPL-3.0-or-later), whose licence is VibeStudio's own.
Only behavioural and key facts were used; no source was copied. The pinned
checkout does not include TrenchBroom's model library, so its per-face axis
choice and mirror anchor were not compared. The axis choice in
`src/core/level_camera_texture.*`, the `CameraShift` and `CameraFlip`
operations in `src/core/level_surface.*` (patches included), the camera claim
layer in `StudioCommandRegistry`, `app/level_camera_texture_actions.cpp`,
`map align-textures --camera-shift` and `--camera-flip`, and the
`level-camera-texture` tests are original VibeStudio work.

**Rotate Textures Clockwise** and **Counter-clockwise**, with their
**(Coarse)** and **(Fine)** forms, were added on 2026-10-25 from the same
revision: the "Rotate textures clockwise / counter-clockwise" actions and
their keys (<kbd>Page Up</kbd> and <kbd>Page Down</kbd>, with
<kbd>Shift</kbd> and <kbd>Ctrl</kbd>) in `ActionManager.cpp`, and the angles
(the grid angle, 15°, then 90° and 1°) and sign in `MapViewBase::rotateUV` and
`rotateUVAngle` in `MapViewBase.cpp`. Only those behavioural and key facts
were used; no source was copied. TrenchBroom's `mdl::rotateUV` is not in the
pinned checkout, so its per-format direction was not compared.
`ClockwiseRotate` in `src/core/level_surface.*`, `levelTextureTurnSense()` in
`src/core/level_camera_texture.*`, `map align-textures --clockwise` and the
on-screen direction checks are original VibeStudio work.

## Texture resets

The texture resets were added on 2026-10-25 from behavioural and key facts
only; no source was copied. **Reset Texture Alignment** follows TrenchBroom
v2026.2 (`4c756be1a54a28d921d18c77716a5f170bdc31d0`, GPL-3.0-or-later): the
"Reset texture alignment" (<kbd>Shift</kbd>+<kbd>R</kbd>) and "Reset texture
alignment to world aligned" (<kbd>Shift</kbd>+<kbd>Alt</kbd>+<kbd>R</kbd>)
actions in
[`lib/TbUiLib/src/ActionManager.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp)
and `MapViewBase::resetUV` and `resetUVToWorld` in
[`lib/TbUiLib/src/MapViewBase.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapViewBase.cpp);
TrenchBroom's `mdl::resetAll` is not in the pinned checkout, so its per-format
axes were not compared. **Radiant Texture Reset** and **Reset Textures…**
follow NetRadiant Custom at `449778b3840f8f2062daab15c87cb012f2ac945c`
(GPL-2.0-or-later, compatible with VibeStudio's GPL-3.0-or-later licence):
`SurfaceInspector_ResetTexture` and its "TextureReset/Cap"
(<kbd>Shift</kbd>+<kbd>N</kbd>) registration in
[`radiant/surfacedialog.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/surfacedialog.cpp),
`Face::SetTexdef` and `FaceTexdef::setBasis` in
[`radiant/brush.h`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brush.h),
`ComputeAxisBase` in
[`radiant/brush_primit.h`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brush_primit.h),
`TexDef_Construct_Default`, `FakeTexCoordsToTexMat`, `TexMatToFakeTexCoords`,
`Texdef_Assign`, `BPTexdef_Assign` and `Valve220_rotate` in
[`radiant/brush_primit.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brush_primit.cpp),
`Patch::CapTexture` in
[`radiant/patch.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/patch.cpp),
and bobToolz's Reset Textures window and edit (`DoResetTextureBox`,
`DoResetTextures`, `DBrush::ResetTextures`, `DEntity::ResetTextures`,
`DMap::ResetTextures` and `DPatch::ResetTextures`) in
[`contrib/bobtoolz`](https://github.com/Garux/netradiant-custom/tree/449778b3840f8f2062daab15c87cb012f2ac945c/contrib/bobtoolz).
The `Reset` and `RadiantReset` operations and `prepareLevelTextureReset` in
`src/core/level_surface.*`, `levelPlaneBasisTextureProjection()` in
`src/core/level_texture_mapping.*`, the Reset Textures window in
`src/app/level_texture_reset_dialog.*`, the `map align-textures` options and
the `level-texture-reset` tests are original VibeStudio work; the plane basis
reuses the `ComputeAxisBase` convention already credited for
`level_texture_mapping.cpp`.

## Proportional fits and texture projections

**Fit Texture Width**, **Fit Texture Height** and the **Project Texture**
commands were added on 2026-10-26 from behavioural facts only; no source was
copied. They follow NetRadiant Custom at
`449778b3840f8f2062daab15c87cb012f2ac945c` (GPL-2.0-or-later, compatible with
VibeStudio's GPL-3.0-or-later licence): the "FitTextureWidth",
"FitTextureHeight", "FitTextureWidthOnly", "FitTextureHeightOnly",
"TextureProjectAxial", "TextureProjectOrtho" and "TextureProjectCam"
registrations and `SurfaceInspector_ProjectTexture` in
[`radiant/surfacedialog.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/surfacedialog.cpp),
`Texdef_FitTexture`, `Texdef_ProjectTexture`,
`Texdef_Construct_local2tex4projection`, `Normal_GetTransform` and
`BPTexdef_fromST011` in
[`radiant/brush_primit.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brush_primit.cpp),
`projectionaxis_for_normal` in
[`radiant/winding.h`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/winding.h),
and `Patch::ProjectTexture`, `Patch::SetTextureRepeat` and
`Patch::Calculate_AvgNormal` in
[`radiant/patch.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/patch.cpp).
VibeStudio departs from it in three documented places: classic faces take a
projection wherever the classic format holds it exactly, rather than never;
brush-primitive faces and patches take the Valve 220 direction projection,
which reads the right way round, rather than one mirrored down V; and a
proportional fit keeps a patch's proportions rather than re-tiling it to one
repeat. The `keepProportions` fit, the `ProjectAxial` and `ProjectDirection`
operations in `src/core/level_surface.*`, `axialLevelPatchTexture()` and
`projectedLevelPatchTexture()` in `src/core/level_patch_tools.*`, the Levels
commands, the `map align-textures` options and the `level-texture-projection`
tests are original VibeStudio work; the projection axes reuse the
`TextureAxisFromPlane` and `ComputeAxisBase` conventions already credited for
`level_texture_mapping.cpp`.

## Face flags and the Show Surfaces toggle

The **Flags** section of the Levels **Surfaces** tab, **Clear Face Flags**,
`map face-flags` and the **Show Surfaces** toggle were added on 2026-10-26 from
behavioural facts only; no source was copied. They follow NetRadiant Custom at
`449778b3840f8f2062daab15c87cb012f2ac945c` (GPL-2.0-or-later, compatible with
VibeStudio's GPL-3.0-or-later licence): the Surface Inspector's flag boxes,
Value field and Unset button (`SurfaceInspector::ApplyFlags`, `OnBtnUnsetFlags`)
and the "SurfaceInspector" toggle on <kbd>S</kbd>
(`SurfaceInspector_toggleShown`) in
[`radiant/surfacedialog.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/surfacedialog.cpp),
and the detail bit kept through every flag edit by `ContentsFlagsValue_assignMasked`
in [`radiant/brush.h`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brush.h).
The same <kbd>S</kbd> in Xonotic NetRadiant `b4b295d7a37797cc2752e48aa8ce42492e7016f0`
[`radiant/surfacedialog.cpp`](https://github.com/xonotic/netradiant/blob/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant/surfacedialog.cpp)
(GPL-2.0-or-later) keeps the key in the NetRadiant profile. TrenchBroom's Face
Inspector surface and content flag editors (`FaceAttribsEditor` in
`lib/TbUiLib/src/FaceAttribsEditor.cpp` at `4c756be1`, GPL-3.0-or-later) were
read for the mixed state across several faces. The flag values and names are
facts from id Software's
[Quake II `qcommon/qfiles.h`](https://github.com/id-Software/Quake-2/blob/master/qcommon/qfiles.h)
(through the Materials module's existing lists) and
[Quake III Arena `code/game/surfaceflags.h`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/game/surfaceflags.h)
(GPL-2.0, read 2026-10-26), and the note that a Quake III face's flags reach
the compiler only as the detail bit follows `ParseRawBrush` in VibeMap3's
`tools/quake3/q3map2/map.cpp` (GPL-2.0, pinned under `external/compilers`).
Unlike NetRadiant Custom, one box changes only its own flag on every targeted
face, so faces whose flags differ keep the rest (NetRadiant Custom writes the
whole set from its boxes), the boxes show flags set on some faces as partly
ticked, Quake III maps get flag names too, and Show Surfaces hides the tab by
going back to the tab it replaced or folding the sidebar it opened.
`src/core/level_face_flags.*`, `src/app/level_face_flags_actions.cpp`, the
Flags section in `src/app/level_surface_tools.cpp`, `src/cli/level_face_flags.*`
and the `level-face-flags` tests are original VibeStudio work.

## UV Editor

The Levels **UV Editor** and `map edit-uv` were added on 2026-10-26 from
behavioural facts only; no source was copied. They follow NetRadiant Custom's
TexTool plugin at `449778b3840f8f2062daab15c87cb012f2ac945c`
(GPL-2.0-or-later, compatible with VibeStudio's GPL-3.0-or-later licence; Id
Software and contributors, GtkRadiant port by Leonardo Zide, original by
Timothee Besset): the face winding or patch control grid drawn over the tiled
texture, a dragged face point that moves the whole texture and becomes the
anchor for turns of the next dragged point, patch points dragged one at a
time, snapping to whole texels, the 1.2 framing margin, the right-drag pan,
<kbd>Insert</kbd> and <kbd>Delete</kbd> zoom, the right-click menu and the live
camera update with commit or cancel, in
[`plugins/textool/TexTool.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/plugins/textool/TexTool.cpp),
[`plugins/textool/ControlPointsManager.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/plugins/textool/ControlPointsManager.cpp)
and
[`plugins/textool/2DView.cpp`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/plugins/textool/2DView.cpp).
Unlike TexTool, which needs brush primitives, the editor works on classic,
Valve 220 and brush-primitive faces and on patches: a move changes only a
face's offsets, a Valve 220 face keeps its scale values, a classic face that
cannot hold a turn takes the nearest classic mapping (reusing
`nearestClassicLevelTextureProjection()`) or converts to Valve 220 when asked,
patch selections move and turn together, and the editor adds keyboard nudges,
turns, point stepping, its own undo and redo, a point list with exact
coordinates and an embedded preview. `src/core/level_uv_edit.*`,
`src/app/level_uv_view.*`, `src/app/level_uv_editor_dialog.*`,
`src/app/level_uv_editor_actions.cpp`, `src/cli/level_uv_edit.*` and the
`level-uv-edit` and `level-uv-editor-ui` tests are original VibeStudio work.

## Arbitrary rotation and scale

NetRadiant Custom's **Arbitrary Rotation** and **Arbitrary Scale** windows were
reviewed on 2026-10-24 at revision `449778b3840f8f2062daab15c87cb012f2ac945c`
(William Joseph and other contributors, GPL-2.0-or-later): `RotateDialog`,
`ScaleDialog`, `DoRotateDlg`, `DoScaleDlg`, `Select_Scale` and the
`"ArbitraryRotation"` (<kbd>Shift</kbd>+<kbd>R</kbd>) and `"ArbitraryScale"`
(<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd>) registrations in
[radiant/select.cpp](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/select.cpp),
the `SaveMapAs` registration without a key in
[radiant/map.cpp](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/map.cpp),
and the X-then-Y-then-Z order of `matrix4_rotation_for_euler_xyz` in
[libs/math/matrix.h](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/libs/math/matrix.h).
The licence is compatible with VibeStudio's GPL-3.0-or-later licence. Only the
window workflow (X, Y and Z fields, Apply that keeps the window open, OK that
applies and closes, negative factors that mirror), the rotation order and the
key bindings were used as behavioural facts; no source was copied. The Euler
path of `rotateLevelMapSelection()` and `scaleLevelMapSelection()` in
`src/core/level_map.*`, the Euler slot of the repeatable transforms in
`src/core/level_repeat.*`, `src/app/level_transform_dialog.*`,
`src/app/level_rotation_dialog.*`, `src/app/level_scale_dialog.*`,
`src/app/level_rotation_actions.cpp`, `map rotate --euler`,
`map resize --factors` and the `level-arbitrary-transform` tests are original
VibeStudio work.

## Smart-tag authoring

**Make &lt;tag&gt;** and **Make non-&lt;tag&gt;** follow the behavioural review of
[TrenchBroom 2026.2 tag actions](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/ActionManager.cpp)
and game-configuration tags at revision `4c756be1a54a28d921d18c77716a5f170bdc31d0`
(reviewed 2026-10-26, GPL-3.0-or-later, compatible with VibeStudio's licence).
The reviewed behaviour includes matching material/shader choices, content and
surface masks, classname conversion and optional tag textures. No source was
copied. The shared tag matcher, candidate transaction, Qt commands, CLI and
fixtures in `level_smart_tags.*` and `level_smart_tags_*_test.cpp` are original
VibeStudio code.
