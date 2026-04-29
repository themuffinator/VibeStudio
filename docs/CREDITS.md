# VibeStudio Credits

## Project
- Creator and lead: [themuffinator](https://github.com/themuffinator) (DarkMatter Productions)

## Core Technology
- [Qt 6](https://www.qt.io/product/qt6)
  - Application framework and primary UI toolkit.
- [Qt Accessibility](https://doc.qt.io/qt-6/accessible.html), [Qt TextToSpeech](https://doc.qt.io/qt-6/qttexttospeech-index.html), and [Qt internationalization](https://doc.qt.io/qt-6/internationalization.html)
  - Planned accessibility, OS-backed speech, and localization support.
- [Meson](https://mesonbuild.com/) and [Ninja](https://ninja-build.org/)
  - Build configuration and build execution.

## PakFu Lineage
VibeStudio intentionally uses [PakFu](https://github.com/themuffinator/PakFu)
as a reference for repository layout, Meson/Qt conventions, README style,
GitHub Actions structure, archive/package handling, game installation
detection, parser hardening, and credits practice.

When PakFu code is moved into VibeStudio, this file must name the source module
and revision when practical.

## Imported Compiler Toolchains

| Tool | Role | Upstream | Imported revision | License notes |
|---|---|---|---|---|
| ericw-tools | Quake/idTech2-style `qbsp`, `vis`, `light`, `bspinfo`, `bsputil` | [ericwa/ericw-tools](https://github.com/ericwa/ericw-tools) | `f80b1e216a415581aea7475cb52b16b8c4859084` | GPL-2.0-or-later; upstream notes GPL-3.0+ compatibility for Embree-enabled builds. See `external/compilers/ericw-tools/COPYING` and `gpl_v3.txt`. |
| NetRadiant Custom q3map2 | idTech3 BSP, lighting, conversion, and packaging compiler | [Garux/netradiant-custom](https://github.com/Garux/netradiant-custom) | `68ecbed64b7be78741878c730279b5471d978c7c` | Mixed GPL/LGPL/BSD by file; upstream marks Quake III tools, including q3map2, as GPL. See `external/compilers/netradiant-custom/LICENSE`, `GPL`, and `LGPL`. |
| ZDBSP | Doom-family node builder | [rheit/zdbsp](https://github.com/rheit/zdbsp) | `bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659` | GPL-2.0-or-later. See `external/compilers/zdbsp/COPYING`. |
| ZokumBSP | Doom-family node/blockmap/reject builder | [zokum-no/zokumbsp](https://github.com/zokum-no/zokumbsp) | `22af6defeb84ce836e0b184d6be5e80f127d9451` | GPL-2.0 text in `external/compilers/zokumbsp/src/COPYING`; based on ZenNode lineage credited upstream. |

These compiler projects are imported as submodules so their history and license
files remain intact. VibeStudio should invoke them as external tools until a
specific source-level integration has a documented compatibility review.

## Format And Engine Lineage
VibeStudio targets public idTech-era formats and workflows from the Doom,
Quake, Quake II, and Quake III ecosystems. Format behavior should be credited
to public specifications, open-source engine/tool references, or source-port
documentation whenever implementation details are derived from them.

## Editor Workflow Inspirations
VibeStudio's adaptable level-editor profiles are intended to help users feel at
home without copying third-party assets or proprietary content. Profile
inspiration and compatibility research should credit:
- [GtkRadiant](https://github.com/TTimo/GtkRadiant), especially the GtkRadiant 1.6.0-era layout and control expectations.
- [NetRadiant Custom](https://github.com/Garux/netradiant-custom), for modern Radiant-family workflow refinements and q3map2-oriented editing expectations.
- [TrenchBroom](https://trenchbroom.github.io/), for modern single-window brush editing and project workflow expectations.
- [QuArK](https://quark.sourceforge.io/), for integrated object/package/map editing lineage.

## AI Integration References
- [OpenAI API documentation](https://platform.openai.com/docs/quickstart), planned as the first optional general-purpose provider reference for prompt-based and agentic automation experiments.
- [Claude API documentation](https://platform.claude.com/docs/en/home), planned as an optional provider reference for reasoning, coding, long-context, and agentic planning workflows.
- [Gemini API documentation](https://ai.google.dev/api), planned as an optional provider reference for multimodal and large-context workflows.
- [ElevenLabs documentation](https://elevenlabs.io/docs/overview/intro), planned as an optional provider reference for voice, speech, sound effects, and audio experiments.
- [Meshy API documentation](https://docs.meshy.ai/en), planned as an optional provider reference for prompt/image-to-3D, AI texturing, and rapid placeholder asset workflows.

## Accessibility And Localization References
- [WCAG 2.2](https://www.w3.org/TR/WCAG22/), planned as the baseline accessibility reference where web-oriented guidance applies to desktop UI.
- [Ethnologue 200](https://www.ethnologue.com/insights/ethnologue200/), used as one reference point for reviewing the initial 20-language localization target set.

## Community Thanks
- The idTech mapping, modding, speedrunning, source-port, and preservation communities who kept these workflows usable and documented across decades.
- The maintainers and contributors of the imported compiler projects listed above.
