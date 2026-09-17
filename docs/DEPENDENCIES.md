# Dependencies

The preferred stack and rationale live in [`docs/STACK.md`](STACK.md). This
file tracks dependency status, integration notes, and the rule that every new
library must have a documented role, license, platform impact, and attribution
path.

## Dependency Status

**No new third-party dependency was added in the current round.** The image
decoders, map geometry and rendering, BSP inspection, build pipelines, DEFLATE
codec, painted widgets, charts, syntax highlighting, and runtime translation
loading are all built on the existing C++20 and Qt 6 baseline plus code written
in this repository. The dependency list below is the same one the previous
milestone had, with the Qt module usage stated more precisely.

## Required For VibeStudio
- C++20 compiler.
- [Meson](https://mesonbuild.com/).
- [Ninja](https://ninja-build.org/).
- [Qt 6](https://www.qt.io/product/qt6): Core, Gui, Widgets, Network.
- Python 3 for validation scripts and CI helpers.

### Qt Modules In Use

`meson.build` resolves two Qt dependency sets. `qt6_core_modules` is
`Core, Gui`, used by the `vibestudio_core` static library and by every core
test executable. `qt6_modules` is `Core, Gui, Widgets, Network`, used by the
`vibestudio` application binary.

| Module | Used by | What for |
|---|---|---|
| Qt Core | Core library, CLI, application | Strings, containers, JSON, files, `QSettings`, `QProcess`, date/time, `QCoreApplication::translate` for core strings. |
| Qt Gui | Core library, application | `QImage`, `QRgb`, `QPainter`, `QIcon`, `QColor`. Core needs it because the idTech image decoders return `QImage` and the palette tools render swatches; the application needs it for every painted widget. |
| Qt Widgets | Application only | The whole shell: `QMainWindow`, `QStackedWidget`, dialogs, the command palette, and the custom `QWidget` subclasses for the map viewport, asset views, and charts. |
| Qt Network | Linked, **not used** | Listed in `qt6_modules` and linked into the application binary, but no source file includes a Qt Network header or references a Qt Network class. It is reserved for the AI connector layer's future HTTP work. Drop it from `meson.build` if that work is deferred further, rather than carrying an unused link. |

Required Qt modules should stay minimal in `meson.build` until code uses them;
Qt Network is the one row above that currently breaks that rule, and it is
recorded here so it does not quietly become load-bearing. Planned modules
include SQL, Multimedia, Concurrent, OpenGLWidgets, and TextToSpeech. None of
them are linked today.

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
- **Python 3**, required for the validation and packaging scripts and for the
  documentation, layout, credits, and translation-extraction tests registered in
  `meson.build`.

## In-Tree Implementations Instead Of Dependencies

These are deliberate decisions to write code rather than add a library. Each one
is recorded with the specification it was implemented from, so the choice can be
audited and, if it ever stops paying off, reversed knowingly.

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

The application icon is painted at several sizes at run time rather than shipped
as a binary asset, and the fallback palettes used before a real game palette is
available are procedurally generated. Both keep the repository free of opaque
image blobs and free of any commercial game data.

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
- [Qt TextToSpeech](https://doc.qt.io/qt-6/qttexttospeech-index.html): OS-backed TTS for setup guidance, task summaries, diagnostics, and optional spoken status.
- [bgfx](https://bkaradzic.github.io/bgfx/overview.html): long-term renderer backend behind a VibeStudio render abstraction. Still not linked, and deliberately not pulled forward by the 2D work: the map viewport, asset views, and charts are `QPainter` widgets, and the headless map renderer emits SVG, so nothing currently needs a GPU abstraction.
- Qt OpenGLWidgets: early MVP 3D preview backend while the renderer abstraction matures.
- [KSyntaxHighlighting](https://api.kde.org/frameworks/syntax-highlighting/html/index.html): reusable syntax highlighting definitions for editor surfaces. Deferred: highlighting is currently `QSyntaxHighlighter` with data-driven language descriptors, which adds no dependency and no KDE Frameworks packaging burden.
- [Tree-sitter](https://tree-sitter.github.io/tree-sitter/): incremental parsing for scripts, shader files, configs, and AI/editor context where useful.
- LSP client support: language tooling bridge for QuakeC, C/C++, shader/config helpers, and future source-port workflows.
- Qt Multimedia: simple playback and device integration for audio previews.
- [miniaudio](https://miniaud.io/): small portable audio fallback for playback, decoding, and waveform-oriented workflows.
- [Assimp](https://www.assimp.org/): optional adjacent model import/export, never the authoritative parser for core idTech formats.
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

## Optional Service Integrations
- [OpenAI API](https://platform.openai.com/docs/quickstart): optional, user-configured integration for prompt-based automation. VibeStudio now has the first OpenAI connector scaffold for configuration, credential discovery through redacted environment references, model routing, and manifest-backed no-write experiments. Core editing, packaging, compiling, and launching work without an API key.
- [OpenAI Responses API](https://platform.openai.com/docs/api-reference/responses): intended future API surface for model responses and tool-using workflows once network invocation is enabled.
- [OpenAI function calling / tools](https://developers.openai.com/api/docs/guides/tools): active architectural pattern for safe VibeStudio tool descriptors; provider calls remain future work.
- [Claude API](https://platform.claude.com/docs/en/home): optional connector target for reasoning, coding, review, long-context, and agentic planning workflows.
- [Gemini API](https://ai.google.dev/api): optional connector target for multimodal and large-context workflows.
- [ElevenLabs API](https://elevenlabs.io/docs/overview/intro): optional connector target for voice, speech-to-text, sound effects, narration, and audio ideation.
- [Meshy API](https://docs.meshy.ai/en): optional connector target for prompt/image-to-3D, AI texturing, and rapid placeholder asset workflows.
- Local/offline model connectors: optional user-configured connectors for AI-free-from-cloud workflows where compatible local runtimes or command-line tools are available.
