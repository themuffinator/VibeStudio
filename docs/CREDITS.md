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

Behaviour only; no Blender code, icons or assets are used. The implementation
in `src/core/model_geometric_topology.*`, `src/core/model_mesh_tools*.*`,
`src/core/model_mesh_decimate.cpp`, `src/core/model_selection_tools.*`,
`src/cli/model_tools.*`, `src/app/model_editor_tools.*` and
`src/app/model_editor_modal.cpp` is VibeStudio's own.

Decimation implements the quadric error metric published by Michael Garland and
Paul S. Heckbert, ["Surface Simplification Using Quadric Error Metrics"](https://doi.org/10.1145/258734.258849)
(SIGGRAPH 1997), as half-edge collapses with quadrics summed over sampled
animation poses. Polygon filling uses ear clipping after G. H. Meisters,
"Polygons Have Ears" (American Mathematical Monthly 82, 1975). Both are
published algorithms; no third-party code is used.

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
| [TrenchBroom](https://trenchbroom.github.io/) | CSG Intersect; the shear tool; linked groups, copies that take on each other's edits while each keeps its place; one inspector of Map, Entity and Face tabs; bundled definitions standing in when a game has none | The release credited under Editor Workflow Inspirations (`90de03c`) | GPL-3.0; behaviour only, no code or `_tb_` keys |
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

The game asset index, release planning, release notes and publishing
(`src/core/game_asset_register.*`, `src/core/project_content.*`,
`src/core/release_plan.*`, `src/core/release_notes.*`,
`src/core/release_publish.*`, `src/cli/release.*`,
`src/app/release_dialog.*`, `src/app/release_actions.cpp`) are original
VibeStudio code. The references below supplied file formats, conventions and
engine behaviour only; no upstream code, text or game data is copied, and the
tests generate every package they index. Packages are read and written with
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

## AI Integration References
- [OpenAI API documentation](https://platform.openai.com/docs/quickstart), planned as the first optional general-purpose provider reference for prompt-based and agentic automation experiments.
- [Claude API documentation](https://platform.claude.com/docs/en/home), planned as an optional provider reference for reasoning, coding, long-context, and agentic planning workflows.
- [Gemini API documentation](https://ai.google.dev/api), planned as an optional provider reference for multimodal and large-context workflows.
- [ElevenLabs documentation](https://elevenlabs.io/docs/overview/intro), an optional provider reference; its [Sound Effects API](https://elevenlabs.io/docs/api-reference/text-to-sound-effects/convert) (`POST /v1/sound-generation`, reviewed 2026-10-06) is the request and answer shape of `src/core/ai_audio_transport.*`. Original implementation; no client code copied.
- [Meshy API documentation](https://docs.meshy.ai/en), planned as an optional provider reference for prompt/image-to-3D, AI texturing, and rapid placeholder asset workflows.
- [OpenAI Images API](https://platform.openai.com/docs/api-reference/images) and [structured outputs](https://platform.openai.com/docs/guides/structured-outputs), [Claude structured outputs](https://platform.claude.com/docs/en/build-with-claude/structured-outputs), [Gemini image generation](https://ai.google.dev/gemini-api/docs/image-generation) and [generateContent](https://ai.google.dev/api/generate-content), and the [Stable Diffusion web UI API](https://github.com/AUTOMATIC1111/stable-diffusion-webui/wiki/API) (AGPL-3.0 project; only its documented HTTP interface is used), reviewed 2026-10-06: the request and answer shapes of `src/core/ai_transport.*` structured output and `src/core/ai_image_transport.*`. Original implementations; no client code copied.

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
