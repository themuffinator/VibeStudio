# Dependencies

Session curve automation and its graphical/native editor use the existing C++20,
Qt Core and Qt Widgets stack. Interpolation and binding are original code; no
new library is linked. The existing reverb attribution also covers its updated
fractional taps.

Session reverb/modulation and `.vsfx` presets add no runtime dependency. Reverb
topology/tuning follows the reviewed public-domain Freeverb reference;
[Credits](CREDITS.md#audio-reverb-reference) records revision and notices.
Modulation, factory recipes, bounded JSON and file publication use C++20 and
existing Qt6 Core/Widgets. No Freeverb binary, plugin SDK or processing framework
is linked.

Explicit Quake III `.skin` assignment import uses original bounded C++ code and
existing Qt, package readers and document workers. It adds no library, runtime
tool or source schema dependency. Reference/licence review is recorded in
[Model Skin Bindings](CREDITS.md#model-skin-bindings).

The preferred stack and rationale live in [`docs/STACK.md`](STACK.md). This
file tracks dependency status, integration notes, and the rule that every new
library must have a documented role, license, platform impact, and attribution
path.

## Dependency Status

The optional [model engine acceptance workflow](MODEL_ENGINE_ACCEPTANCE.md)
accepts supplied FTE dedicated-server and FTEQCC executables, alongside the
existing ericw-tools/q3map2 compilers. FTE remains an external GPL tool; none of
its engine or compiler code is linked, vendored into production, downloaded by
the test, or required for ordinary builds. The Linux server uses `env`/GNU
`timeout`; Windows can invoke it through a named WSL distribution. Interface
references and licence review are in [Credits](CREDITS.md#model-native-engine-acceptance).
The optional rendering companion accepts an FTE client built with `egl_headless`,
external Mesa EGL/llvmpipe and the existing optional Pillow installation. All
remain supplied test tools; the runner neither installs nor bundles them. The
recorded Linux client uses Mesa 25.2.8/LLVM 20.1.2. No graphics dependency or
renderer changes in the Qt application result from this acceptance workflow.

The Audio Editor uses pinned **r8brain-free-src 7.5** for sample-rate conversion.
Native WAV/DMX readers and pinned dr_libs/Xiph compressed decoders feed the in-tree `audio_clip` sample processing service;
the optional Qt Multimedia module auditions exact float32 selections through an
in-memory WAV buffer and the system output device. The browser shares that device
lifecycle while retaining native WAV/compressed bytes and wrapping DMX as PCM16.
Its bounded preview/audition workers add no dependency. Editing, resampling, CLI processing, and export/staging remain available
when that module is disabled.

Initial recording reuses optional Qt Multimedia's QAudioSource. Original
C++20 workers and the existing Qt Core hash/file APIs implement the bounded
capture queue and recoverable `.vstake` journal. Windows `_commit` and POSIX
`fsync` are guarded system-runtime calls. macOS links system CoreFoundation to
check the microphone usage description before permission requests; it also needs
Qt 6.5+ permission support and a deployed Qt microphone permission backend.
There is no new bundled library and no global minimum-Qt change. Take review,
CLI export and session editing remain available without Multimedia. Actual
microphone permission/deployment acceptance is still open on native platforms.
Public API provenance is in [Recording APIs](CREDITS.md#recording-apis).

Stereo bus/send routing adds no library or platform dependency. Original
`audio_routing` code validates the graph and processes bounded double-precision
buffers using the existing C++20/Qt Core services; native Qt Widgets supplies
the staged routing inspector. Version-3 sessions retain version-1/2 read support.
Built-in insert chains add original C++ DSP and native Qt Widgets controls,
without another linked library. Biquad mathematics follows the 2021-06-08 W3C
Audio EQ Cookbook under its GPL-compatible Software and Document License 2015;
the source, review and required notice live in [Credits](CREDITS.md#audio-session-eq).

The transport also handles backends that announce end-of-media despite infinite
looping, observed with Qt 6.4.2's FFmpeg backend. Its deferred rewind/restart uses
the existing seek/play API, session guards and loading timeout. Qt versions with
working native looping keep that path. No additional dependency is required.

The optional Windows runtime packaging profile was verified with Qt 6.10.1 and
its dynamically linked FFmpeg 7.1.2. It preserves Qt's LGPL-3.0 option and the
vendor's FFmpeg LGPL-2.1-or-later/permissive attribution records, full notices,
SPDX inventories and exact SDK-copy hashes. No new library is linked into
VibeStudio. Windows ICU and the compiler runtime are explicit system/install
prerequisites; unrelated SDK or system DLLs are not redistributed. See
[runtime packaging](PACKAGING.md#windows-qt-and-audio-runtime) and
[provenance and licence review](CREDITS.md#runtime-distribution).

The optional release source companion adds unchanged QtTools and QtShaderTools
6.10.1 source archives for rebuilding Linguist and shader tools. They retain
their GPL-3.0-with-Qt-exception / LGPL-3.0 alternatives and original notices;
they are not linked into VibeStudio. Source collection/packaging uses Python's
standard library and pinned hashes, with no new installed library dependency.
See the [source-companion workflow](PACKAGING.md#source-companion).

Fresh-build evidence and Windows binary/source pairing use Python's standard
library plus the existing Meson/Ninja, Qt deployment and Linguist tools. They
add no linked dependency or Python package requirement. The workflow consumes
the Qt prefix exported by its existing MIT-licensed install-qt action; native
acceptance and release publication remain separate.

The image
decoders, map geometry and rendering, BSP inspection, build pipelines, DEFLATE
codec, painted widgets, charts, syntax highlighting, and runtime translation
loading are all built on the existing C++20 and Qt 6 baseline plus code written
in this repository. Audio and automatic UV atlas generation add the following
source-only dependencies.

### dr_libs and Xiph Vorbis

- Role: memory-backed MP3, native FLAC and Ogg Vorbis decoding in
  `src/core/audio_decode.cpp`, shared through `decodeAudioClip` by GUI and CLI.
- Sources: [dr_libs](https://github.com/mackron/dr_libs/tree/dfe8377631000664666519fdb83da193fd8037f4),
  revision `dfe8377631000664666519fdb83da193fd8037f4` (dr_mp3 0.7.4,
  dr_flac 0.13.4), [libvorbis](https://github.com/xiph/vorbis/tree/c2aa86b05e981c96bf381fc6aa11cdd03eccc2fb),
  revision `c2aa86b05e981c96bf381fc6aa11cdd03eccc2fb`, and
  [libogg 1.3.6](https://github.com/xiph/ogg/tree/be05b13e98b048f0b5a0f5fa8ce514d56db5f822),
  revision `be05b13e98b048f0b5a0f5fa8ce514d56db5f822`.
- Licences: MIT-0 for dr_libs and BSD-3-Clause for Xiph, reviewed for GPLv3 compatibility
  2026-10-04. Original source notices, including minimp3 ancestry, are preserved.
- Platform impact: dr_libs compiles within the C++ core; Xiph's original C
  sources use the toolchain's C compiler in a private Meson static library. No
  system codec install, extra DLL, device access, download or network requirement.
  Decoder allocations are capped at 16 MiB per import; input/output bounds and
  container verification are enforced by the wrapper. A forced-include adapter
  maps Xiph allocation macros to a guarded per-thread allocation region, leaving
  original sources unchanged. Qt playback remains optional.
- Packaging: licences, integration notes and pinned hash manifests ship through
  Meson and portable bundles. Credits validation detects changed upstream bytes.
- Selection: direct decoder APIs expose native PCM rate/channel data and bounded
  memory without miniaudio's unused playback, mixing and resampling layers.
  The existing native WAV/DMX parsers and r8brain converter remain authoritative.
  stb_vorbis was evaluated but excluded after multichannel verification and
  upstream memory-safety review; none of its code is included in the product.
- Tests: checked-in synthetic fixtures with independent FFmpeg and libsndfile
  float oracles; these tools are needed only for regeneration, not the product.

### PortAudio

- Purpose: optional native full-duplex device binding for the prepared recording
  engine; standalone playback/capture retains Qt Multimedia.
- Pin: [PortAudio](https://github.com/PortAudio/portaudio/tree/873e3c83fbe2f57ebcf59083e627a3f8fa051ffe),
  revision `873e3c83fbe2f57ebcf59083e627a3f8fa051ffe` (development snapshot).
- License: MIT-style permissive, reviewed 2026-10-06 for GPLv3 compatibility.
  Copyright and complete notices remain unchanged; no ASIO SDK is included.
- Build: private C11 static library under Meson/Ninja, `audio_duplex=auto` by
  default. Windows uses the system WASAPI SDK (`winmm`, `ole32`, `uuid`); macOS
  uses CoreAudio/AudioToolbox/AudioUnit/CoreFoundation/CoreServices; Linux uses
  `libasound` development headers/library. Auto disables Linux duplex when ALSA
  is absent; enabled requires it. Device-free builds disable both `audio_duplex`
  and `audio_playback`. No downloads occur during configuration or compilation.
- Review: immutable upstream hashes, exact checked host patches and limitations
  are in the [integration record](../external/audio/portaudio/VIBESTUDIO.md).
  WASAPI input has packet timestamps; output timing remains estimated. Hardware
  acceptance, recording controls and grouped take review remain open.
- Packaging: original license, attribution and hash manifest accompany binaries;
  the source companion includes the original sources, patches and Meson files.

### libebur128

- Role: BS.1770 integrated loudness in the shared
  `audio_analysis` service, used by the editor and `asset audio-analyze`.
- Source: [libebur128 1.2.6](https://github.com/jiixyj/libebur128/tree/67b33abe1558160ed76ada1322329b0e9e058b02),
  pinned revision `67b33abe1558160ed76ada1322329b0e9e058b02`.
- Licences: MIT (library and R128Scan notice) and BSD-3-Clause queue header,
  reviewed for GPLv3 compatibility on 2026-10-04. All original notices remain.
- Platform impact: private Meson C static library with no new DLL, system
  install, device, network access or Qt module. Metering also works with
  `audio_playback=disabled`. The C++ wrapper validates 1–8 channels and 8–384 kHz,
  processes bounded chunks on the existing worker, and serializes library calls
  because this revision initializes process-wide tables. Unscaled loudness
  retains the absolute gate. True peak uses the existing r8brain converter,
  with streamed double-precision output and explicit silence on both sides.
- Packaging/update: [integration record](../external/audio/libebur128/VIBESTUDIO.md),
  the source hash manifest and all three licence notices ship through Meson and
  portable bundles. Credits validation checks original source bytes.
- Limits: no certification claim, automatic surround inference, loudness range,
  short-term/momentary metering or loudness normalization. Source and delivery
  checks share existing paths; analysis creates no document revision.

### r8brain-free-src

- Role: whole-document high-quality linear-phase conversion in
  `src/core/audio_resample.cpp`, and streamed true-peak reconstruction in
  `src/core/audio_analysis.cpp`, shared by the GUI and CLI.
- Source: [r8brain-free-src 7.5](https://github.com/avaneev/r8brain-free-src/tree/cb2abb9977efe2471979b380ed95daa56ab4fdb9),
  pinned revision `cb2abb9977efe2471979b380ed95daa56ab4fdb9`.
- Licence: MIT plus Ooura's permissive FFT terms, reviewed 2026-10-04 for
  compatibility with GPL-3.0. See [credits](CREDITS.md#audio-sample-rate-conversion).
- Platform impact: header-only standard C++ using the double-precision Ooura
  backend; no extra binary, system installation, network fetch, or C compiler.
  Meson uses checked-in headers on Windows, macOS, and Linux.
- Packaging: both licences, attribution, and the hash manifest ship in portable
  licence bundles and Meson installs. Optional PFFFT/IPP code is not imported.
- Maintenance: [integration and update procedure](../external/audio/r8brain-free-src/VIBESTUDIO.md).

### xatlas

- Role: automatic UV chart generation and shape-preserving island packing in
  `core/model_uv_atlas`, shared by mesh document workers and the CLI.
- Source: [xatlas](https://github.com/jpcy/xatlas/tree/f700c7790aaa030e794b52ba7791a05c085faf0c),
  revision `f700c7790aaa030e794b52ba7791a05c085faf0c`.
- Licences: MIT and embedded BSD-3-Clause OpenNL, reviewed for GPLv3 compatibility
  2026-10-04. Original notices and binary attribution are preserved.
- Platform impact: private standard C++ static library on Windows, macOS and
  Linux; no runtime DLL, network fetch, GPU API or system install. Runs on the
  existing document worker with upstream thread pools disabled. A per-operation
  allocation region caps library storage at 256 MiB. Source/build adaptations
  preserve indexed boundaries, disable independent-axis texel rounding and
  support independent rectangular atlas limits in a generated header/source.
  Both original upstream files remain hash-verified and unchanged. Rectangular
  atlas proportions and padding are evaluated in texture pixel space.
- Packaging/update: [integration record](../external/modelling/xatlas/VIBESTUDIO.md),
  pinned source hashes, and both licences ship with Meson and portable bundles.
- Verification: animated curved meshes, coplanar seams, partial selection,
  deterministic output, overlap refusal, cancellation, allocation limits,
  history, source persistence, native export and CLI parity.

### Optional Local Language Servers

The Code language client uses existing Qt Core process, JSON and timer APIs;
semantic completion uses the existing Qt Widgets completer and in-tree validated
edit parser. Reference previews use the existing text codec and background
search workbench. Completion resolution reuses the client and Qt completer,
validating deferred metadata and imports before one document Undo block.
Completion snippets use an in-tree bounded C++ parser and Qt document cursors;
linked fields, native choices and Undo add no library or executable dependency.
Document diagnostic pulls and successful-save notifications use the same Qt
process, timer and JSON facilities, adding no production dependency.
Parameter hints use the same client, a native Qt overload selector and the
shared resource-isolated documentation renderer used by Quick Info.
Quick Info uses existing Qt text documents/widgets for Markdown,
with resource loading and link activation disabled; builds without Qt's Markdown
reader use literal text. Formatting reuses Qt document undo, the Activity model
and shared text-file encoding/atomic saves. Rename and code actions reuse the exact TextEdit
parser, Search Results, project replacement preflight/writer and document undo.
Code actions use a native Qt Widgets picker and shared WorkspaceEdit validation;
lazy resolution travels through the existing bounded client.
They add no linked dependency. A user may explicitly select an independently
installed stdio LSP executable such as clangd. Servers are not downloaded,
bundled or auto-started. Their own licensing and runtime dependencies apply to
their separate installations. The original client follows LSP 3.17 protocol
facts; its CC-BY-4.0 specification reference is credited in [Credits](CREDITS.md).
See [Local Language Services](LANGUAGE_SERVICES.md) for supported capabilities.
Optional pull-diagnostics checks use [Ruff 0.16.4](https://github.com/astral-sh/ruff/tree/0.16.4)
as a separate MIT-licensed process in isolated test output. Its executable and
upstream license notices are retained there; it is not bundled, linked or required.
Optional interoperability checks used [Pyright 1.1.414](https://github.com/microsoft/pyright/tree/1.1.414)
under MIT as a separate test process in isolated output. It is not a build or
runtime dependency and is not included in application packages.

## Required For VibeStudio
- C++20 compiler and a matching C compiler for the unchanged Xiph decoder sources.
- [Meson](https://mesonbuild.com/).
- [Ninja](https://ninja-build.org/).
- [Qt 6](https://www.qt.io/product/qt6): Core, Gui, Widgets, Network.
- Python 3 for the pinned UV-library build adaptation, validation scripts and CI helpers.

### Qt Modules In Use

`meson.build` resolves three Qt dependency sets. `qt6_core_modules` is
`Core, Gui, Network`, used by the `vibestudio_core` static library and by every
core test executable. `qt6_modules` is `Core, Gui, Widgets, Network`, used by the
`vibestudio_app` static library and the `vibestudio` application binary.
`qt6_test_modules` is `Core, Gui, Widgets, Test`, used only by the
`shell-interaction-smoke` test; it is resolved with `required: false`, so a Qt
install without Qt Test builds everything else and skips that test.

| Module | Used by | What for |
|---|---|---|
| Qt Core | Core library, CLI, application | Strings, containers, JSON, files, `QSettings`, `QProcess`, date/time, `QCoreApplication::translate` for core strings. |
| Qt Gui | Core library, application | `QImage`, `QRgb`, `QPainter`, `QIcon`, `QColor`. Core needs it because the idTech image decoders return `QImage` and the palette tools render swatches; the application needs it for every painted widget. |
| Qt Widgets | Application only | The whole shell: `QMainWindow`, `QStackedWidget`, dialogs, the command palette, and the custom `QWidget` subclasses for the map viewport, asset views, and charts. |
| Qt Test | `shell-interaction-smoke` only | `QTest::keyClick`, `QTest::qWaitForWindowActive`, and `QTest::qSleep` to drive the real window in the GUI interaction test. Nothing that ships links it. |
| Qt Multimedia | Application, **optional** | `QMediaPlayer` and `QAudioOutput` play browser/waveform audio from memory. The multitrack worker uses the Qt 6.4-compatible `QAudioSink` push API for bounded stereo float blocks, explicit output selection and processed-time estimates. No input device is opened. All processing/CLI paths remain device-independent. Found through the `audio_playback` feature option (`auto`: linked when the module is present); `-Daudio_playback=enabled` makes it required, `disabled` leaves it out. Without it `VIBESTUDIO_HAVE_AUDIO_PLAYBACK` is 0 and the transport stays on the page, disabled. It is a Qt add-on module, so CI installs `qtmultimedia` on Windows and macOS; the Linux job builds the no-playback path, which keeps that branch compiling. |
| Qt Network | Core library, CLI, application | `QNetworkAccessManager` in `core/ai_transport` sends the opt-in AI text questions (the Assistant panel, `ai ask`, `ai test-connection`) and nothing else; AI-free mode, the default, sends nothing. `QTcpServer` stands in for a provider in `ai-transport-smoke` and the shell interaction test, so those run offline. HTTPS needs one of Qt's TLS backend plugins (`plugins/tls`, such as `qschannelbackend` on Windows or `qopensslbackend`), which `windeployqt` stages with the Network module; a Qt runtime staged any other way must include it, or HTTPS providers fail with a TLS error while local `http://` runtimes still work. |

Required Qt modules should stay minimal in `meson.build` until code uses them.
Planned modules
include SQL, Concurrent, OpenGLWidgets, and TextToSpeech. None of them are
linked today.

## Optional Build-Time Tools
- **`lrelease`** (Qt Linguist release tool), looked up as `lrelease-qt6`,
  `lrelease6`, or `lrelease`. `i18n/meson.build` uses it to compile each
  checked-in `i18n/vibestudio_<locale>.ts` catalog into a `.qm` that
  `QTranslator` loads at run time, installed under
  `<datadir>/vibestudio/i18n`. It is resolved with `required: false`: when it is
  absent, Meson prints a message, no catalogs are compiled, and the application
  runs in the source language instead of failing to build. This is a build-time
  tool only — nothing links against Qt Linguist, and no run-time behaviour
  depends on it beyond the presence of the `.qm` files.
- **`lupdate`**, used by `scripts/extract_translations.py --check` to validate
  dry-run extraction in tests and CI. Also optional.
- **Python 3**, required for the pinned xatlas build adaptation, validation and packaging scripts, and for the
  documentation, layout, credits, and translation-extraction tests registered in
  `meson.build`.
- **Markdown and Pygments** (`scripts/requirements-docs.txt`), used by
  `scripts/build_docs_site.py` to render the user manual as HTML. The
  `documentation-site` Meson test skips when they are missing.
- **fontTools** (and **Pillow**, **NumPy** and **Brotli** to write raster files
  and web fonts), used by `scripts/generate_branding.py`. `--check` needs
  fontTools only; the `branding-validation` Meson test skips without it.

## Release Packaging Tools

Used by `.github/workflows/release.yml` and the `scripts/package_*.py` helpers
([Releasing](RELEASING.md)); none is linked into VibeStudio.

| Tool | Platform | Role | Licence |
| --- | --- | --- | --- |
| Inno Setup 6 | Windows | Builds `VibeStudio-<v>-windows-x64-setup.exe` from `packaging/windows/vibestudio.iss`; the installer carries Inno's setup runtime | Inno Setup License |
| `windeployqt`, `macdeployqt` | Windows, macOS | Copy the Qt runtime beside the binary or into the app bundle | Qt (GPL-3.0 with exception / LGPL-3.0) |
| create-dmg | macOS | Lays out the disk image window; plain `hdiutil` is the fallback | MIT |
| linuxdeploy and linuxdeploy-plugin-qt | Linux | Assemble the AppImage from `meson install`; the AppImage embeds the type 2 runtime | MIT |

Brand assets in `assets/branding/` include the Manrope typeface (OFL-1.1), see
[Credits](CREDITS.md#branding-documentation-and-release-tooling-2026-10-07).

## In-Tree Implementations Instead Of Dependencies

Saved-draft maintenance uses guarded native directory handles on Windows and
`flock` on Unix in `core/package_draft_access`. These operating-system interfaces
add no bundled dependency. Unsupported reader exclusion disables maintenance;
portable read/save behavior remains where available. See [credits](CREDITS.md#package-draft-reader-protection).

These are deliberate decisions to write code rather than add a library. Each one
is recorded with the specification it was implemented from, so the choice can be
audited and, if it ever stops paying off, reversed knowingly.

### ZIP filename mapping — `src/core/package_zip.cpp`

Legacy ZIP names use a fixed 128-entry CP437 high-byte table derived from
[Unicode table 2.00 (1996-04-24)](https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/PC/CP437.TXT).
It adds no runtime library. The GPL-compatible [Unicode License V3](licenses/UNICODE-LICENSE.txt)
notice is retained and installed with the application. See [Credits](CREDITS.md#compression-and-archive-formats).

### DEFLATE codec — `src/core/deflate.{h,cpp}`

Reading PK3 and ZIP packages requires inflate; writing them well requires
deflate. The obvious candidates were [zlib](https://zlib.net/) and
[miniz](https://github.com/richgel999/miniz). Both were declined: a compression
library adds a build dependency on every platform target, a license file in
every portable release bundle, a credits entry, and an update path to track,
while the codec itself is a bounded, well-specified, fixture-testable piece of
work.

The implementation is written from the public specifications, not adapted from
any existing library:

- [RFC 1951 — DEFLATE Compressed Data Format Specification version 1.3](https://www.rfc-editor.org/rfc/rfc1951)
- [RFC 1950 — ZLIB Compressed Data Format Specification version 3.3](https://www.rfc-editor.org/rfc/rfc1950)
- CRC-32 as specified by ITU-T V.42 and used by
  [RFC 1952](https://www.rfc-editor.org/rfc/rfc1952) and the
  [PKWARE .ZIP File Format Specification](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT)
- Adler-32, from RFC 1950 section 9

Scope and limit: the decoder handles stored, fixed-Huffman, and dynamic-Huffman
blocks, is bounds-checked throughout, never trusts a length or distance taken
from the stream, caps output growth, and reports malformed input as an error.
The encoder emits stored and fixed-Huffman blocks only and does not build
dynamic Huffman tables, so archives VibeStudio writes are larger than the same
content packed by zlib; entries that would not shrink are stored verbatim.
Output is deterministic for a given input and level so archives reproduce. If
archive size becomes a real problem, the fix is a dynamic-Huffman encoder in the
same file, not a new dependency. See [`docs/STACK.md`](STACK.md) for the wider
rationale.

### idTech image, map, and BSP parsers — `src/core/`

`idtech_image`, `level_map`, `map_geometry`, `map_render`, and `bsp_inspect`
are native parsers written from public specifications and released id Software
sources, with the references named in each header. They exist so core formats
are understood on their own terms rather than through a generic library, and
they add no dependency. Qt's own image plugins still handle PNG, JPEG, GIF,
BMP, TIFF, and WebP.

### Application icon and fallback palettes

The application icon is the brand artwork in `assets/branding/icons/png`,
compiled into the app library as a Qt resource (`assets/branding/vibestudio.qrc`)
and into `vibestudio.exe` as a Windows icon. Every size is generated by
`scripts/generate_branding.py` from one geometry description, so the files are
reproducible rather than opaque; if the resource is missing, the studio paints
the same mark at run time. The fallback palettes used before a real game palette
is available are procedurally generated, keeping the repository free of
commercial game data.

## Imported Compiler Source Dependencies
The external compiler submodules keep their own build systems and dependency
requirements. VibeStudio does not build them by default yet.

- [ericw-tools](https://github.com/ericwa/ericw-tools): CMake, Embree, oneTBB, optional Qt6 for `lightpreview`, and bundled third-party libraries documented upstream.
- [q3map2-nrc](https://github.com/Garux/netradiant-custom): q3map2 compiler source imported from NetRadiant Custom; upstream Makefile-based build and dependencies documented in its `COMPILING` file.
- [ZDBSP](https://github.com/rheit/zdbsp): CMake and zlib-oriented source tree as documented upstream.
- [ZokumBSP](https://github.com/zokum-no/zokumbsp): upstream source/build instructions in its README and `doc` directory.

## Planned Dependencies
Likely future additions:
- [CLI11](https://github.com/CLIUtils/CLI11): evaluated for the full CLI parser/completion layer and deliberately deferred. The active in-process command registry now covers testable command metadata without adding a dependency.
- [SQLite](https://sqlite.org/) through Qt SQL, with [FTS5](https://sqlite.org/fts5.html) where available: asset index, dependency search, diagnostics, recent activity, and project metadata.
- Text to speech uses no extra library. On Windows `src/app/studio_speech.cpp`
  drives the Speech API (SAPI 5) through COM, using `sapi.h` from the Windows
  SDK and linking `ole32`; both are part of Windows. On macOS it runs the system
  `say` command, and elsewhere [Speech Dispatcher](https://freebsoft.org/speechd)'s
  `spd-say` (GPL-2.0-or-later) or [eSpeak NG](https://github.com/espeak-ng/espeak-ng)
  (GPL-3.0-or-later) when installed, as separate programs; none is shipped.
  [Qt TextToSpeech](https://doc.qt.io/qt-6/qttexttospeech-index.html) remains a
  possible future backend.
- [bgfx](https://bkaradzic.github.io/bgfx/overview.html): long-term renderer backend behind a VibeStudio render abstraction. Still not linked, and deliberately not pulled forward by the 2D work: the map viewport, asset views, and charts are `QPainter` widgets, and the headless map renderer emits SVG, so nothing currently needs a GPU abstraction.
- Qt OpenGLWidgets: early MVP 3D preview backend while the renderer abstraction matures.
- [KSyntaxHighlighting](https://api.kde.org/frameworks/syntax-highlighting/html/index.html): reusable syntax highlighting definitions for editor surfaces. Deferred: highlighting is currently `QSyntaxHighlighter` with data-driven language descriptors, which adds no dependency and no KDE Frameworks packaging burden.
- [Tree-sitter](https://tree-sitter.github.io/tree-sitter/): incremental parsing for scripts, shader files, configs, and AI/editor context where useful.
- [miniaudio](https://miniaud.io/): small portable audio fallback for playback, decoding, and waveform-oriented workflows.
- [Assimp](https://www.assimp.org/): optional adjacent model import/export, never the authoritative parser for core idTech formats. Polygonal OBJ uses the original bounded `core/model_obj` parser and needs no importer dependency; MTL conversion remains open.
- Additional image codecs and palette tooling for deeper texture/sprite editing
  beyond the active Qt image conversion and palette metadata path.
- Optional provider-neutral AI connector layer for AI-assisted workflows, implemented through Qt Network unless a future SDK clearly improves maintainability for a specific connector.
- Parser hardening and fuzzing toolchains before broad write-back support.

## Evaluated And Declined
- [zlib](https://zlib.net/) and [miniz](https://github.com/richgel999/miniz): evaluated for ZIP/PK3 compression and declined in favour of the in-tree DEFLATE codec documented above. Revisit only if archive size or compression throughput becomes a real user complaint that a dynamic-Huffman encoder in `src/core/deflate.cpp` cannot answer.
- Qt Graphics View and an early GPU backend for 2D editor surfaces: declined in favour of plain `QPainter` widgets, which need no Qt module beyond Widgets.

Any new dependency must be recorded here with its role, license, platform notes,
whether it is required, optional, bundled, or external, and any credits updates
required in `README.md` and [`docs/CREDITS.md`](CREDITS.md).

## Optional Verification Tools

Independent texture-format verification can use external [Pillow](https://python-pillow.org/)
(tested 11.3.0, MIT-CMU, compatible with GPL-3.0) through
`src/tests/texture_export_readers.py`. It is an optional Python test dependency,
available on Windows/macOS/Linux; it is not bundled, linked, required by Meson,
or used by the editor or CLI. Native byte fixtures remain in the C++ smoke suite.
The runtime stack and required dependencies are unchanged.

## Optional Service Integrations

- [OpenAI API](https://platform.openai.com/docs/quickstart): optional, user-configured integration for prompt-based automation. VibeStudio now has the first OpenAI connector scaffold for configuration, credential discovery through redacted environment references, model routing, and manifest-backed no-write experiments. Core editing, packaging, compiling, and launching work without an API key.
- [OpenAI Responses API](https://platform.openai.com/docs/api-reference/responses): intended future API surface for model responses and tool-using workflows once network invocation is enabled.
- [OpenAI function calling / tools](https://developers.openai.com/api/docs/guides/tools): active architectural pattern for safe VibeStudio tool descriptors; provider calls remain future work.
- [Claude API](https://platform.claude.com/docs/en/home): optional connector target for reasoning, coding, review, long-context, and agentic planning workflows.
- [Gemini API](https://ai.google.dev/api): optional connector target for multimodal and large-context workflows.
- [ElevenLabs API](https://elevenlabs.io/docs/overview/intro): optional, user-configured connector; its [Sound Effects API](https://elevenlabs.io/docs/api-reference/text-to-sound-effects/convert) makes sounds for the Sound Generator and `asset audio-generate`, reached through Qt Network with no SDK or new dependency, its MP3 answers decoded by the existing dr_mp3. Voice, speech-to-text, and narration remain targets.
- [Meshy API](https://docs.meshy.ai/en): optional connector target for prompt/image-to-3D, AI texturing, and rapid placeholder asset workflows.
- Local/offline model connectors: optional user-configured connectors for AI-free-from-cloud workflows where compatible local runtimes or command-line tools are available.
- Image generation (OpenAI Images API, Gemini image output, and a local [Stable Diffusion web UI](https://github.com/AUTOMATIC1111/stable-diffusion-webui/wiki/API) such as AUTOMATIC1111 or Forge): optional, user-configured HTTP services for the Texture Generator and `ai image`, reached through Qt Network with no SDK or new dependency. The web UI is a separate program the user runs; VibeStudio only speaks its documented API.

## Additional image codecs and workspace JSON

DDS decoding is adapted from PakFu's GPLv3 `src/formats/dds_image.cpp`; FTX/SWL
layout and DDS/FTX encoding use its format implementations as references. The
2026-10-05 working snapshot and license review are recorded in
[Credits](CREDITS.md#pakfu-image-exchange-and-capability-catalog).
These built-in C++/Qt codecs add no linked library. Workspace persistence uses
existing Qt Core JSON, hashing and atomic-file APIs; no runtime or cloud service
dependency is introduced.
