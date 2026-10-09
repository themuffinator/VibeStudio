# Welcome to VibeStudio

VibeStudio is a free, open-source (GPLv3) studio for making content for idTech1
(Doom family), idTech2 (Quake and Quake II) and idTech3 (Quake III family)
games. It brings level editing, models, textures, audio, packages, code and
shaders, compiling and game launching together in one window on Windows, macOS
and Linux, with an optional AI assistant and a command line that uses the same
services as the studio.

> [!WARNING]
> **VibeStudio is pre-alpha and highly untested.** It is not ready for
> production work. Most features have automated tests, but very few have been
> used in real projects, and many planned parts of the studio do not exist yet.
> Work on copies, and back up your maps, packages and projects before you open
> them in VibeStudio. [Project status](status.md) explains what is and is not
> tested.

## What you can do today

**Available** means implemented and covered by automated tests, but not yet
proven in real projects. **Partial** means some formats or workflows work; the
linked page lists the gaps. **Planned** means not built yet.

| Area | What works today | Status |
| --- | --- | --- |
| [Workspace and projects](projects.md) | Open a project folder and write its manifest, check project health, find files by name, add or detect game installations, save and reopen workspaces | Available |
| [Level editing: Doom family](levels.md) | Open, edit and save Doom and Hexen maps in WADs, including UDMF; draw and shape sectors; build nodes with ZDBSP or ZokumBSP | Partial |
| [Level editing: Quake family](levels.md) | Edit Quake and Quake II `.map` brushes, entities and texture alignment in a four-view workspace; prefabs; compile with VibeMap2 | Partial |
| [Level editing: Quake III](levels.md) | Brushes, curved patches and shader images in the 3D view; compile with VibeMap3 and package the result as a PK3 | Partial |
| [Familiar editor controls](editor-profiles.md) | 19 editor profiles bring the keys, mouse gestures, camera and layout of editors such as TrenchBroom, NetRadiant Custom, GtkRadiant, Hammer and Ultimate Doom Builder | Partial |
| [Models](models.md) | View and animate MDL, MD2 and MD3 models; edit meshes, including imported OBJ files, and export MDL, MD2 or MD3; design simple props; assemble tagged models; build Quake III player packages | Partial |
| [Textures](textures.md) | Browse images, sprites and palettes; paint in the layered Texture Editor; export PNG, TGA, PCX, Quake WAD2, Quake II WAL and Doom flats and patches | Partial |
| [Audio](audio.md) | Preview sounds in packages; edit WAV, MP3, FLAC, Ogg Vorbis and Doom sounds; multitrack sessions; deliver sounds in each game's format. Linux builds cannot play or record audio yet | Partial |
| [Packages](packages.md) | Browse, extract, validate and compare PAK, WAD, ZIP and PK3 files and plain folders; stage changes with undo; save a new package; drafts and automatic recovery | Available |
| [Package and release](releases.md) | Package a project, map, model or textures with only your own files, leaving out what the game already has; keep a changelog; generate release notes and a readme; publish with a distribution archive and a record of each release | Partial |
| [Code and scripts](code.md) | Edit QuakeC, shader scripts, configs and other text with highlighting, project-wide search and replace, and optional local language servers | Partial |
| [Materials and shaders](materials.md) | Every texture, Quake III shader and Doom 3 material of idTech 1 to 4: a live, animated preview drawn by each engine's rules, editing as text or as nodes, checks the game would make, and saving into the package | Partial |
| [Build and launch](build-and-launch.md) | Run chained compile pipelines with VibeMap2, VibeMap3, ZDBSP or ZokumBSP, jump to problems and leaks, and launch the game with your map. You provide the compiler programs | Partial |
| [AI assistant (optional)](ai.md) | Off by default (AI-free mode). Opt in to ask a model about your map, code or build in the Assistant, and to use AI when generating levels, textures and sounds | Partial |
| [Command line](cli.md) | Scriptable commands for projects, packages, maps, assets, builds and settings, with JSON output and stable exit codes; `cli commands` lists them all | Available |
| [Accessibility and languages](accessibility.md) | High-contrast themes, text up to 200%, colour-vision options, every command reachable from the keyboard, screen reader announcements and spoken status. 47 interface languages are registered, but no translation is finished yet | Partial |

Not built yet: a sprite animation editor, and agentic AI workflows that carry
out multi-step work for you. The
[roadmap](../ROADMAP.md) tracks them.

## Start here

1. [Install VibeStudio](install.md): download, check and install a release, or
   run it from a folder.
2. [First-run setup](first-run.md): choose your language, accessibility
   options, editor profile, games and compilers.
3. [A tour of the studio](tour.md): find your way around the window, the pages
   and the panels.
4. [Project status](status.md): what is tested, what is not, and how VibeStudio
   protects your files.

## Get help

- [Troubleshooting](troubleshooting.md) covers common problems and where to
  look when something goes wrong.
- Report bugs and request features on
  [GitHub issues](https://github.com/themuffinator/VibeStudio/issues).
  [Report a problem](status.md#report-a-problem) explains what to include.
