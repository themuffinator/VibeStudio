# VibeStudio documentation

Start with the **[user manual](manual/index.md)**. It explains what VibeStudio
does today, how to install it, and how to use each part of the studio. Every
release also ships the manual as offline HTML (open `docs/html/index.html` in a
package, or **VibeStudio Documentation** in the Start menu on Windows).

> [!WARNING]
> VibeStudio is pre-alpha and largely untested in real projects. Read
> [Project status](manual/status.md) before you trust it with your work.

## User manual

| Get started | Use the studio | Reference |
| --- | --- | --- |
| [Welcome](manual/index.md) | [Projects and game installations](manual/projects.md) | [Accessibility and languages](manual/accessibility.md) |
| [Project status](manual/status.md) | [Level editing](manual/levels.md) | [Command line](manual/cli.md) |
| [Install VibeStudio](manual/install.md) | [Editor profiles and controls](manual/editor-profiles.md) | [Troubleshooting and FAQ](manual/troubleshooting.md) |
| [First-run setup](manual/first-run.md) | [Packages](manual/packages.md) | [Build from source](manual/building-from-source.md) |
| [A tour of the studio](manual/tour.md) | [Package and release](manual/releases.md) | [Changelog](../CHANGELOG.md) |
| | [Textures and sprites](manual/textures.md) | |
| | [Models](manual/models.md) | |
| | [Audio](manual/audio.md) | |
| | [Code, scripts and shaders](manual/code.md) | |
| | [Build and launch](manual/build-and-launch.md) | |
| | [AI assistant (optional)](manual/ai.md) | |

[Support matrix](SUPPORT_MATRIX.md) is the detailed, per-format answer to
"does VibeStudio handle this file?".

## Contributing and maintaining

| Document | What it covers |
| --- | --- |
| [Contributing](CONTRIBUTING.md) | How to propose changes, and what every change must keep up to date |
| [Architecture](ARCHITECTURE.md) | How the core, CLI and studio shell fit together |
| [Technology stack](STACK.md) | The stack decision record: C++20, Qt 6 Widgets, Meson |
| [Dependencies](DEPENDENCIES.md) | Every library and tool, with versions and licences |
| [Credits](CREDITS.md) | Borrowed code, formats, patterns and assets, with upstream links |
| [Releasing](RELEASING.md) | Versioning, the changelog, and cutting a release |
| [Branding](BRANDING.md) | Logo, colours, typography, icons, and the writing style for docs |
| [Packaging](PACKAGING.md) | Portable packages, the Windows runtime, and release staging |
| [Runtime source build](RUNTIME_SOURCE_BUILD.md) | Rebuilding from a release's source companion |
| [Roadmap](ROADMAP.md) | Planned work and its current state |

## Design records

Detailed engineering notes for each part of the studio. They record decisions,
acceptance criteria and implementation detail, and can run ahead of (or behind)
the code; the manual and the support matrix describe what ships.

| Area | Records |
| --- | --- |
| Shell and experience | [UX design](UX_DESIGN.md), [Efficiency](EFFICIENCY.md), [Initial setup](INITIAL_SETUP.md), [Accessibility and localisation](ACCESSIBILITY_LOCALIZATION.md), [Portable workspaces](WORKSPACES.md) |
| Levels | [Level editor](LEVEL_EDITOR.md), [Level scenes](LEVEL_SCENE.md), [Placed model appearances](LEVEL_MODEL_APPEARANCE.md), [Editor profiles](EDITOR_PROFILES.md) |
| Models | [Editable meshes](MODEL_MESH.md), [Native formats and skeletons](MODEL_FORMATS.md), [Modeller profiles and layout](MODELLER_PROFILES.md), [Mesh tools](MODEL_TOOLS.md), [Model design](MODEL_DESIGN.md), [Assemblies](MODEL_ASSEMBLY.md), [Surfaces](MODEL_SURFACES.md), [Material slots](MODEL_MATERIAL_SLOTS.md), [Collision](MODEL_COLLISION.md), [Engine acceptance](MODEL_ENGINE_ACCEPTANCE.md), [Modeller release gate](MODELLER_RELEASE.md) |
| Textures and audio | [Texture editor](TEXTURE_EDITOR.md), [Audio editor](AUDIO_EDITOR.md), [Duplex recording](AUDIO_DUPLEX.md), [Shared asset formats](ASSET_FORMATS.md) |
| Packages and code | [Package manager](PACKAGE_MANAGER.md), [Project releases and the game asset index](PROJECT_RELEASES.md), [Code editor](CODE_EDITOR.md), [Language services](LANGUAGE_SERVICES.md), [Project search](PROJECT_SEARCH.md) |
| Build, games and automation | [Compiler integration](COMPILER_INTEGRATION.md), [Game installations](GAME_INSTALLATIONS.md), [CLI strategy and reference](CLI_STRATEGY.md), [AI automation](AI_AUTOMATION.md) |
| Release history and audits | [0.1.0 foundation cut](RELEASE_CANDIDATE.md), [audit plans](plans/package-manager-release-candidate.md) in `docs/plans/` |

The single-file [offline guide](OFFLINE_USER_GUIDE.md) is generated from the
manual by `scripts/generate_offline_guide.py`; edit the manual pages instead.
