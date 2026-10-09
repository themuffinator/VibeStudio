# Project Releases And The Game Asset Index

Package and Release turns a project, a map, a model or a set of texture
folders into a release players can install: a package holding only the
project's own files, a readme, release notes, a distribution archive, a
release record and an updated changelog. Telling the project's files from the
game's own needs a register of what each game installation ships, the game
asset index. This record covers both, and how they connect the Workspace,
Levels, Packages and Build pages with the `release` and `install register`
command families. The user-facing guide is
[Package and release](manual/releases.md).

Status: **Partial**. Implemented with automated tests on generated fixtures
only; not yet tried with real game installations or real projects. The gaps
are listed under [Limits](#limits).

## Shape Of The Module

| Layer | Files | Role |
|---|---|---|
| Asset index | `src/core/game_asset_register.*` | Reads an installation's stock packages into a register of files (path, size, CRC-32), Quake III shader declarations and Doom-family names; saves, loads, checks freshness |
| Project content | `src/core/project_content.*` | `ProjectContentReader` presents project folders as a package; `LayeredPackageReader` stacks the project, package folders and an optional open package |
| Planning | `src/core/release_plan.*` | Catalogue of releasable items, the plan (what ships, what the game provides, what blocks), per-game rules, stock context |
| Notes | `src/core/release_notes.*` | Keep a Changelog reading and writing, version bumps, release records, inventory diffs, generated notes and readme |
| Publishing | `src/core/release_publish.*` | Writes the package, readme, notes and archive, records the release and updates the changelog |
| Dependencies | `src/core/level_dependencies.*` | Map and model reference resolution, now aware of the game's own files (`LevelDependencyStatus::Stock`) |
| Manifest | `src/core/project_manifest.*` | Schema 2: `game`, the `release` object, unknown keys kept |
| CLI | `src/cli/release.*` | `release plan`, `publish`, `notes`, `changelog`, `history`, `catalog`; `install register build`, `info`, `check`, `export` |
| GUI | `src/app/release_dialog.*`, `src/app/release_actions.cpp` | The **Package and Release** window, the Workspace **Releases** card, indexing, commands and shell hooks |

The window and the CLI call the same functions: `releaseInstallationFor`,
`releaseCatalog`, `prepareReleaseStock`, `planRelease`, `releaseNotesInput`,
`releaseNotesMarkdown`, `releaseReadmeText` and `publishRelease`. Planning and
publishing run on worker threads in the window, with progress and
cancellation, and the shell supplies the project, installation, indexing,
Activity and navigation through `ReleaseDialogHooks`, so the window never
edits shell state itself.

## The Game Asset Index

### What is indexed

`discoverGameStockSources` finds each game's standard packages, matching path
components without regard to case:

| Game | Base | Expansions |
|---|---|---|
| Quake | `id1/pak0.pak`, `id1/pak1.pak` | `hipnotic/pak0.pak`, `rogue/pak0.pak` |
| Quake II | `baseq2/pak0.pak` to `pak2.pak` | `xatrix/pak0.pak`, `rogue/pak0.pak` |
| Quake III Arena | `baseq3/pak0.pk3` to `pak8.pk3` | `missionpack/pak0.pk3` to `pak3.pk3` (Team Arena) |
| Doom | The first IWAD found of `doom2.wad`, `doom.wad`, `tnt.wad`, `plutonia.wad`, `freedoom2.wad`, `freedoom1.wad`, `doom1.wad`; the others are alternatives | |
| Heretic and Hexen | `heretic.wad`, `hexen.wad` | `hexdd.wad` |

Base packages saved in the installation profile are added as base sources.
`install register build --package <path>` replaces discovery with explicit
packages or folders, for other games and mods. Packages are read in engine
search order, so a later package shadows an earlier one as the engine would.

For every file the index keeps its path, size, CRC-32, source package and a
type hint. Quake III shader scripts are parsed for their declarations, so a
map naming `base_wall/glass` resolves to the script that declares
`textures/base_wall/glass`. Doom-family WADs are walked in directory order
(`sourceOrdinal`), not name order, and their names are recorded per namespace:
textures (from `TEXTURE1`/`TEXTURE2`), patches, flats, sprites, sounds, music
and maps.

### Where it lives and when it is stale

The index is saved as `asset-registers/<installation-id>.json` beside the
settings: in the application's local data folder, or beside the file given
with `--settings-file`. Never in the game folder.

```json
{
  "format": "vibestudio-asset-register",
  "version": 1,
  "game": "quake3",
  "installation": { "id": "quake3-games-quake3", "name": "Quake III Arena", "root": "C:/Games/Quake3" },
  "createdUtc": "2026-10-08T12:00:00.000Z",
  "studioVersion": "0.1.0-alpha.1",
  "sources": [
    { "id": "baseq3/pak0.pk3", "path": "baseq3/pak0.pk3", "role": "base", "label": "Quake III Arena",
      "format": "PK3", "bytes": 479493658, "modifiedUtc": "1999-12-02T00:00:00.000Z", "files": 3539 }
  ],
  "files": [["textures/base_wall/basewall01.tga", 49196, "1a2b3c4d", "baseq3/pak0.pk3"]],
  "shaders": [["textures/base_wall/glass", "baseq3/pak0.pk3", "scripts/base_wall.shader"]],
  "doom": {}
}
```

Files are compact rows (path, size, CRC-32 in hex, source id, optional type
hint), which keeps a 4,000-file index near 300 KB. Limits: 64 sources,
250,000 files, 500,000 names and a 96 MiB index file.

`gameAssetRegisterStatus` compares the saved sources with the installation:
a missing or changed package (size or modification time), a new standard
package, or a moved installation makes the index stale, and each reason is
reported. A stale index is still used, with a warning, because it is usually
nearly right. Status checks are cached by the index file's size and
modification time, so the Workspace page can show the state of every
installation cheaply.

### Which sources count

`releaseInstallationFor` picks the project's installation the same way in
the window, the Workspace page and the CLI: the one the manifest links; else,
for a project that names its game, the installation in use when it plays that
game, then the first saved one that does; else the installation in use.
`prepareReleaseStock` loads that installation's index (or an exported index
file given with `--register`), drops it with a warning when it
was built for another game, and keeps the base sources unless
`release.stockSources` names others. Each entry of `release.requires` adds a
register built from that folder or package, relative to the installation, so
a Team Arena map can treat Team Arena's files as provided. The result is one
merged register used by planning and by the dependency check.

## Planning A Release

### Reading the project

`projectContentRoots` turns the manifest into readers: the project folder,
minus its output, temporary and release output folders and its package
folders; then each package folder as a layer of its own. Folders whose names
start with `.`, and `.vibepackage` drafts, are never content. The open package
on the Packages page can be layered on top when the user asks.

Every file is classified:

| Class | Examples | Ships |
|---|---|---|
| Content | Images, shaders, models, sounds, scripts, BSPs | When reachable from what is released, or always for a whole-project release |
| Source | `.map`, `.psd`, `.xcf`, `.blend`, `.fbx`, QuakeC `.qc`, editor JSON, and texture WADs of the Quake family | Only with **Include sources** |
| Intermediate | `.prt`, `.lin`, `.pts`, `.srf`, `.log`, `.bak`, autosaves | Never |
| Document | `CHANGELOG.md`, `RELEASE_NOTES.md` | Never; the release writes its own |
| Native code | `.dll`, `.so`, `.dylib` | Beside the package, with a warning |
| Executable | `.exe`, `.bat`, `.sh`, `.ps1` | Never |

`release.include` patterns always ship and `release.exclude` patterns never
do; both are project-relative globs with `/` separators.

### What each scope follows

- **Maps.** The compiled BSP is found beside the map, in the project's
  `maps` folder, in the output folder or among the registered compiler
  outputs. It ships with the files the engine loads beside it: `.lit`, `.lux`,
  `.aas`, `.vis`, external `lm_*` lightmaps, the level shot and `.arena`
  scripts. The map is read with `inspectLevelDependencies` (Quake textures are
  embedded in the BSP, so Quake maps skip texture references), and every
  resolved texture, shader script, shader image, model, skin, sound and the
  worldspawn music follows. Sky shaders bring their sky boxes. A map without a
  build blocks publishing; one older than its source is an advisory.
- **Models.** Each model's skins, shader scripts and the images they use,
  through `inspectModelMaterialDependencies`.
- **Textures.** Each folder's images, and the shader scripts that declare
  shaders in it.
- **Whole project.** Every content file the game does not already have.

For the Doom family the release is one PWAD: each project WAD with maps is
checked against its IWAD's names, then the project's WADs are merged in name
order, each keeping its own lumps in directory order so every map's lumps stay
together. Files that are not lumps ship beside the WAD.

### Stock, overrides and identical copies

Every candidate file is checked against the merged register:

- a reference the project cannot resolve but the game provides is listed as
  **provided by the game**, with the package that holds it;
- a project file with the same path and the same size and CRC-32 as a stock
  file is an identical copy and stays out;
- a project file with the same path but other contents ships, because it
  replaces the game's file, and is flagged (`replacesStock`) with a warning;
- without an index, references the project lacks are assumed to be the
  game's and reported as unverified advisories, and the plan says the game's
  files were not checked.

### Problems

Problems carry a kind, the reference and what needs it. Blocking kinds stop
**Publish**: `empty`, `format` (for example a WAD for a Quake game),
`missing`, `ambiguous`, `unreadable`, `unsafe`, `unbuilt-map` and
`outside-project`. Advisories do not: `unverified`, `stale-map` and some
`incomplete` scans. Warnings explain choices, such as a Quake map released as a
numbered PAK, which would clash with other releases.

### Package formats

| Game | Automatic format |
|---|---|
| Quake III Arena | PK3 |
| Quake, Quake II | PAK for a whole project; ZIP of loose files otherwise |
| Doom, Heretic, Hexen | WAD |
| Others | ZIP |

The package name is `release.packageName`, or a slug of the title.

## Release Notes And The Changelog

`parseProjectChangelog` reads [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
1.1.0 files and `projectChangelogText` prints them back byte for byte when
nothing changed: the preamble, headings, link references and line endings
are kept, and only the sections an edit touches are rewritten.
`addChangelogEntry` adds a line under `## [Unreleased]`, and
`releaseProjectChangelog` turns the Unreleased section into
`## [<version>] - <date>` with a fresh, empty Unreleased section above it.
Categories are Added, Changed, Deprecated, Removed, Fixed and Security.

Every published release writes `.vibestudio/releases/<version>.json` with the
title, version, game, scope, format, package name, SHA-256 and size, the maps,
the notes, the output folder (relative to the project when inside it) and an
inventory of every file with its role, size and CRC-32. The next release
diffs its inventory with the latest earlier record; with nothing recorded in
the changelog, the diff suggests entries such as "Maps added: arena2" or
"Textures updated: 3", and the first release suggests "First release.".

`releaseNotesMarkdown` writes the title and version, the description, a table
of game, version, date, authors, package and hash, **What's new** from the
changelog, **Contents**, **Requirements**, **Installation** steps for the game
and format, and **Licence and permissions**. `releaseReadmeText` writes a
plain-text readme in the field layout of the
[/idgames archive](https://www.doomworld.com/idgames/)'s text files (Title,
Filename, Author, Description, the Doom map slots, construction details and
the package with its hash), with CRLF line endings. Both may contain
`{{package-sha256}}` and `{{package-size}}`, which `fillReleasePackageTokens`
replaces once the package exists, so edited notes still get the real hash.

`isValidReleaseVersion` accepts letters, digits, `.`, `-`, `+` and `_`;
`bumpReleaseVersion` follows [Semantic Versioning](https://semver.org/) when
the version is one (a pre-release such as `2.0.0-beta` bumps to `2.0.0`),
keeps a leading `v`, and otherwise appends `.1`.

## Publishing

`publishRelease` writes, in order: the package (through the package staging
model, so every entry is verified as it is written), the readme, the notes,
the distribution archive (the package, the readme and loose files in the
folder layout players extract into the game folder), the release record and
the changelog. Files go through the package publication service, which writes
beside the destination and moves a file into place only when it is complete,
so a failed or cancelled step leaves no partial file. Replacing an earlier
release is opt-in and keeps the old files as `.bak`. The output folder must be
absolute and outside the project's content folders, so a release never ships
itself. `--dry-run` reports the same paths without writing.

## Integration

| Surface | What it does |
|---|---|
| Workspace, **Game Installations** | Shows each installation's index state; **Index Assets** indexes the selected one in the background with an Activity task and **Cancel** |
| Workspace, **Project Health** | Warns when the linked game is not indexed or its index is out of date; activating the row indexes it |
| Workspace, **Releases** card | Unreleased changes, the five latest releases, **Record Change…** and **Package and Release…** |
| Workspace, **Graph** | Project > Game, Installation > Asset Index and Project > Releases nodes |
| Levels header | **Package Map**; **Dependencies** reads the project when no package is open and marks references the game provides |
| Packages | **Also use the open package** layers a package over the project; **Open Package** after publishing opens the release |
| Build | **Package Map** on the Build header releases the pipeline's input map; activating an unbuilt or out-of-date map in **Problems** opens it on the Build page |
| Project menu | **Package and Release…**, **Package This Map…**, **Package This Model…**, **Package These Textures…**, **Record a Change…**, **Index Game Assets** |

| Window | Command line |
|---|---|
| **Index Assets** | `install register build <installation>` |
| The installation's index state | `install register info <installation>` |
| **From the Game** tab | `install register check <installation> <path>`, `release plan --json` (`stock`) |
| The item list | `release catalog <project>` |
| The review | `release plan <project> [--map|--model|--texture <item>]` |
| **Notes** and **Readme** tabs | `release notes <project> [--readme]` |
| **Record Change** | `release changelog <project> --add <text> --category <kind>` |
| **Publish Release** | `release publish <project> --release-version <version>` |
| **Releases** card | `release history <project>` |
| **Dependencies** | `map dependencies <map> --package <folder> --installation <installation>` |

`install register export` writes the index to a file, and
`release plan --register <file>` uses it on a machine without the game, such
as a build server.

## Limits

- Releases follow references that maps, models and shader scripts make.
  Files that game code, QuakeC, console scripts or menus load by name are not
  found and need `release.include` patterns.
- Doom 3-era games, Return to Castle Wolfenstein and other idTech 3 games
  have no standard package list; index them with `--package` or the
  profile's base packages. Their planner rules are those of Quake III.
- Quake and Quake II single-map releases default to ZIPs of loose files,
  because the engines only load numbered PAKs.
- Doom-family releases produce one PWAD; ZDoom-family PK3 layouts are written
  as plain PK3s without lump namespace checks.
- The window keeps notes edits until **Regenerate**; changing the plan after
  editing does not update the edited text.
- None of this has been tried with real installations or real projects.

## Tests

| Test | Covers |
|---|---|
| `game-asset-register-smoke` | Discovery, building, WAD directory order, shader declarations, saving, loading, freshness and explicit packages |
| `release-smoke` | Catalogue, plans for every scope in Quake III, Quake and Doom projects, stock, identical copies, overrides, problems, notes, changelog round trips, records, diffs and publishing |
| `release-cli-smoke` | Every `release` and `install register` command, exit codes, exported indexes, dry runs, a second release's diff, and `map dependencies --installation` |
| `release-dialog-ui-smoke` | The window: review, recording a change, switching scope, publishing, the next version, indexing from the notice, accessible names; in dark, light, high-contrast, 150 and 200% text and right-to-left |
| `project-manifest-smoke` | Schema 2 fields, defaults, unknown keys kept, health checks |

The fixtures in `src/tests/release_test_fixtures.h` generate every byte of the
fake games and projects; no game data is used.
