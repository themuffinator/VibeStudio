# Dependencies

The preferred stack and rationale live in [`docs/STACK.md`](STACK.md). This
file tracks dependency status, integration notes, and the rule that every new
library must have a documented role, license, platform impact, and attribution
path.

## Required For VibeStudio
- C++20 compiler.
- [Meson](https://mesonbuild.com/).
- [Ninja](https://ninja-build.org/).
- [Qt 6](https://www.qt.io/product/qt6): Core, Gui, Widgets, Network.
- Python 3 for validation scripts and CI helpers.

Required Qt modules should stay minimal in `meson.build` until code uses them.
Planned modules include SQL, Multimedia, Concurrent, and OpenGLWidgets.

## Imported Compiler Source Dependencies
The external compiler submodules keep their own build systems and dependency
requirements. VibeStudio does not build them by default yet.

- [ericw-tools](https://github.com/ericwa/ericw-tools): CMake, Embree, oneTBB, optional Qt6 for `lightpreview`, and bundled third-party libraries documented upstream.
- [NetRadiant Custom](https://github.com/Garux/netradiant-custom): upstream Makefile-based build and dependencies documented in its `COMPILING` file.
- [ZDBSP](https://github.com/rheit/zdbsp): CMake and zlib-oriented source tree as documented upstream.
- [ZokumBSP](https://github.com/zokum-no/zokumbsp): upstream source/build instructions in its README and `doc` directory.

## Planned Dependencies
Likely future additions:
- [CLI11](https://github.com/CLIUtils/CLI11): command parser for the full CLI.
- [SQLite](https://sqlite.org/) through Qt SQL, with [FTS5](https://sqlite.org/fts5.html) where available: asset index, dependency search, diagnostics, recent activity, and project metadata.
- [bgfx](https://bkaradzic.github.io/bgfx/overview.html): long-term renderer backend behind a VibeStudio render abstraction.
- Qt OpenGLWidgets: early MVP 3D preview backend while the renderer abstraction matures.
- [KSyntaxHighlighting](https://api.kde.org/frameworks/syntax-highlighting/html/index.html): reusable syntax highlighting definitions for editor surfaces.
- [Tree-sitter](https://tree-sitter.github.io/tree-sitter/): incremental parsing for scripts, shader files, configs, and AI/editor context where useful.
- LSP client support: language tooling bridge for QuakeC, C/C++, shader/config helpers, and future source-port workflows.
- Qt Multimedia: simple playback and device integration for audio previews.
- [miniaudio](https://miniaud.io/): small portable audio fallback for playback, decoding, and waveform-oriented workflows.
- [Assimp](https://www.assimp.org/): optional adjacent model import/export, never the authoritative parser for core idTech formats.
- Image codecs and palette tooling for texture/sprite editing.
- Optional provider-neutral AI connector layer for AI-assisted workflows, implemented through Qt Network unless a future SDK clearly improves maintainability for a specific connector.
- Parser hardening and fuzzing toolchains before broad write-back support.

Any new dependency must be recorded here with its role, license, platform notes,
whether it is required, optional, bundled, or external, and any credits updates
required in `README.md` and `docs/CREDITS.md`.

## Optional Service Integrations
- [OpenAI API](https://platform.openai.com/docs/quickstart): optional, user-configured integration for prompt-based automation. Core editing, packaging, compiling, and launching must work without an API key.
- [OpenAI Responses API](https://platform.openai.com/docs/api-reference/responses): likely initial API surface for model responses and tool-using workflows.
- [OpenAI function calling / tools](https://developers.openai.com/api/docs/guides/tools): planned pattern for exposing safe VibeStudio actions to AI-assisted workflows.
- [Claude API](https://platform.claude.com/docs/en/home): optional connector target for reasoning, coding, review, long-context, and agentic planning workflows.
- [Gemini API](https://ai.google.dev/api): optional connector target for multimodal and large-context workflows.
- [ElevenLabs API](https://elevenlabs.io/docs/overview/intro): optional connector target for voice, speech-to-text, sound effects, narration, and audio ideation.
- [Meshy API](https://docs.meshy.ai/en): optional connector target for prompt/image-to-3D, AI texturing, and rapid placeholder asset workflows.
- Local/offline model connectors: optional user-configured connectors for AI-free-from-cloud workflows where compatible local runtimes or command-line tools are available.
