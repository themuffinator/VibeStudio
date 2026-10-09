# Package and release

**Package and Release** turns your project, a map, a model or a set of textures into something players
can install. VibeStudio works out which files are yours and which the game already has, gathers every
custom texture, shader, model, sound and script your work uses, and writes the package with a readme,
release notes and a distribution archive. It keeps a changelog and a record of every release, so the
next one can say what changed.

> [!NOTE]
> **Status: Partial.** Planning, release notes and publishing work for Quake, Quake II, Quake III
> Arena, Doom, Heretic and Hexen projects, with automated tests built from generated files. They have
> not been tried with real game installations or real projects yet. A release finds the files that
> maps, models and shader scripts name; files that game code or scripts load by name must be added by
> hand (see [Choose what ships](#choose-what-ships)). Doom 3-era games are not indexed automatically.

## Before you start

1. Open your project on the Workspace page (see [Projects and game installations](projects.md)).
2. Link the game it plays in: select the installation under **Game Installations** and choose
   **Use**, or record it in the project manifest.
3. Index the game's own files: choose **Index Assets** under **Game Installations**, or **Project** >
   **Index Game Assets**. VibeStudio reads the game's own packages once and remembers what they hold.
   Nothing in the installation is changed.

The index is how a release leaves the game's own files out. Without it, VibeStudio cannot tell a
stock texture from yours: anything the project lacks is assumed to come with the game, and the
release says the game's files were not checked. The installation's row on the Workspace page shows
**assets indexed**, **assets not indexed** or **asset index out of date**; an out-of-date index
means the game's packages changed since, so index it again.

## Package a map

1. Open the map on the Levels page and save it.
2. Build it, so the compiled BSP is up to date. The release ships the build, not the `.map` file.
3. Choose **Package Map** on the Levels header, or **Project** > **Package This Map…**. **Package
   Map** on the Build header releases the pipeline's map instead.

The **Package and Release** window opens with the map ticked and reviews the release straight away.
It includes:

- the compiled BSP and the files the engine loads beside it: lighting (`.lit`, `.lux`), bot
  navigation (`.aas`), external lightmaps, the level shot and the `.arena` script;
- every custom texture, shader script, model, skin, sound and music track the map uses, and the
  images inside those shaders and model skins;
- for Doom-family maps, the map's lumps and the custom textures, flats, patches, sprites, sounds and
  music it needs, merged into one WAD.

Files the game provides stay out. A project file with the same path as one of the game's own files
ships, because it replaces the game's version; the review marks it so you can decide whether that is
what you want. An identical copy of a stock file stays out.

## Package a project, model or textures

Choose **Package and Release…** on the Workspace page's **Releases** card, or **Project** >
**Package and Release…**, then pick **What to Release**:

| Choice | Ships |
| --- | --- |
| **Whole project** | Everything in the project the game does not already have: built maps, textures, shaders, models, sounds and scripts. |
| **Maps** | Each ticked map with its build, companions and custom assets. |
| **Models** | Each ticked model with its skins, shader scripts and their images. |
| **Textures** | Each ticked texture folder with its images and the shader scripts that declare shaders in it. |

Tick the items in the list; **Add Files…** adds maps, models or texture folders the list does not
show. Ticks are kept when you switch between choices. **Package This Model…** and **Package These
Textures…** on the **Project** menu start from the model or texture selected in a folder package.

**Also use the open package** layers the package open on the Packages page over the project, for work
you keep in a package rather than in folders. Leave it off unless that package holds your own work.

## Review what ships

The review beside the options updates as you change them:

- The status line says **Ready to publish** or **Not ready**, with the file count, size and package
  name.
- Chips count **Included** files, files **From the game**, files that **Replace** the game's own, and
  **Problems**. Select a chip to open its list.
- **Contents by kind** charts the release by maps, textures, shaders, models, sounds and other files.
- **Included** lists every file with its kind, size and the map or model that needs it. A warning
  icon marks a file that replaces one of the game's; a muted icon marks a file that ships beside the
  package, such as native game code, which engines cannot load from a package.
- **From the Game** lists the references the game provides and which of its packages holds each.
- **Problems** lists what blocks publishing first, such as a missing texture or a map that has not
  been built, then advisories. Activate an unbuilt or out-of-date map to open it on the Build page.
- **Details** is the whole plan as text.

## Choose what ships

Some files cannot be found by reading maps and models, such as sounds that game code plays or
textures a script picks at run time. The project's release settings handle them:

- **Include sources** ships the `.map` files and source art too.
- In the project manifest, `release.include` lists patterns that always ship, and `release.exclude`
  patterns that never do, such as `"docs/*.txt"` or `"**/*.psd"`. See
  [Release settings](#release-settings).

Some files never ship from a project: sources such as `.map`, `.psd` and `.blend` (unless you include
sources), compiler leftovers such as `.prt`, `.lin`, `.log` and `.bak`, programs, the project's
`.vibestudio` folder, its output and temporary folders, and Quake-family texture WADs, which the
compiled BSP already contains.

## Describe the release

The **Release** card holds the **Title**, **Version**, **Authors**, **Description**, **Website** and
**Licence**; the **Package** card holds the **Format**, **File name**, **Game folder** and
**Compression**. They start from the project manifest, and publishing saves them back to it.

**Format** offers what the game can load. **Automatic** picks the usual choice:

| Game | Automatic format |
| --- | --- |
| Quake III Arena | PK3 |
| Quake and Quake II | PAK for a whole project; a ZIP of loose files for maps, models and textures, because the engines only load numbered PAK files and a new one could clash with other releases |
| Doom, Heretic and Hexen | WAD, with other files, such as a readme or native code, beside it |
| Other games | ZIP of loose files |

**Next** beside the version offers the next patch, minor or major version. A version that has
already been released cannot be published again unless you tick **Replace an earlier release of this
version**.

## Write the release notes

The **Notes** tab shows the project's unreleased changes, the release notes and, on the **Readme**
tab, the text file players read.

- To record a change, choose its kind (**Added**, **Changed**, **Deprecated**, **Removed**,
  **Fixed** or **Security**), describe it, and choose **Record Change**. The change goes into the
  Unreleased section of the project's `CHANGELOG.md` straight away. **Project** > **Record a
  Change…** and **Record Change…** on the Workspace **Releases** card do the same from anywhere.
- With no changes recorded, VibeStudio suggests them from what changed since the last release, such
  as maps or textures added and updated, or "First release." for the first one.
- The notes and readme are generated from the plan and the changelog. Edit them freely before
  publishing; **Regenerate** discards your edits and builds them again.
- `{{package-sha256}}` and `{{package-size}}` are filled in with the package's SHA-256 hash and size
  when it is written.

The changelog follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and VibeStudio
rewrites only the parts it changes, so you can also edit it by hand. The readme follows the layout of
the text files that accompany releases in the [/idgames archive](https://www.doomworld.com/idgames/).

## Publish

1. Check the **Output folder**. It defaults to `build/releases/<name>-<version>` in the project.
2. Choose whether to **Write a distribution archive (.zip)** and to **Move unreleased changes under
   this version**.
3. Choose **Publish Release**, or press <kbd>Ctrl</kbd>+<kbd>Enter</kbd> (<kbd>Cmd</kbd>+<kbd>Enter</kbd>
   on macOS).

Publishing runs in the background with progress and **Cancel**, and the Activity Center records it.
The output folder receives:

| File | What it is |
| --- | --- |
| `<name>.pk3`, `.pak`, `.wad` or `.zip` | The package, written and then verified. |
| `<name>.txt` | The readme for players. |
| `RELEASE_NOTES.md` | The release notes, for a website or forum post. |
| `<name>-<version>.zip` | The distribution archive: the package, the readme and any files that ship beside the package, in the layout players extract into the game folder. |

VibeStudio also records the release in the project's `.vibestudio/releases/<version>.json`, with the
package's hash and a list of every file, and moves the changelog's unreleased changes under the new
version. Nothing is written into the game installation, and an existing release is never overwritten
unless you choose to replace it; replaced files are kept with `.bak` added to their names.

When it finishes, **Show Folder** opens the output folder and **Open Package** opens the package on
the Packages page. The version field then says the version has been released: choose **Next** to
start the next one.

## Release history

The Workspace page's **Releases** card shows the number of unreleased changes and the latest
releases, with their package, date and size. Activate a release to open its folder. The next release
compares itself with the latest one recorded, so its notes can list what was added, updated and
removed.

## Check a map's dependencies

**Dependencies** on the Levels header lists what the open map uses, also when no package is open: it
then reads the project's folders, as a release does, and offers **Package Map…** in place of
**Export Assets…**. With an indexed installation, references the game provides show as **Provided by
the game**. See [Level editing](levels.md#check-maps-and-entities).

## Requirements and expansions

By default only the base game counts as provided. A release for an expansion or another mod, such as
Team Arena, names it as a requirement in the project manifest:

```json
"release": {
  "requires": [
    { "name": "Team Arena", "path": "missionpack", "url": "https://example.com/team-arena" }
  ]
}
```

Files under the requirement's folder or package, relative to the installation, then count as
provided, and the notes and readme tell players to install it first. `release.stockSources` instead
sets exactly which of the game's packages count as provided, by their path in the installation, such
as `baseq3/pak0.pk3` and `missionpack/pak0.pk3`; `install register info` lists them.

## Release settings

The project manifest's `release` object holds the release settings. Every field is optional; empty
ones use the project's and game's defaults. Publishing from the window saves the fields you changed.

<details>
<summary>All release fields</summary>

| Field | Meaning |
| --- | --- |
| `title`, `version`, `description`, `website`, `license` | How the release is described. The title defaults to the project name, the version to `1.0.0`. |
| `authors` | A list such as `["Ada <ada@example.com>"]`; the readme splits names from email addresses. |
| `packageName` | The package file name without its extension. Defaults to a short form of the title. |
| `packageFormat` | `pk3`, `pak`, `wad` or `zip`. Empty chooses automatically. |
| `gameFolder` | The folder players install into, such as `baseq3`, `id1` or a mod folder. |
| `stockSources` | The game's packages that count as provided. Empty uses the base game. |
| `requires` | Content a release needs but does not ship: `name`, `path` and `url`. |
| `include`, `exclude` | Patterns of project files that always or never ship. |
| `outputFolder` | Where releases are written. Defaults to `build/releases`. |
| `changelog` | The changelog file. Defaults to `CHANGELOG.md`. |
| `includeSources` | `true` ships `.map` files and source art too. |

</details>

## Command-line equivalents

| Task | Command |
| --- | --- |
| Index the game's own files | `vibestudio --cli install register build <installation>` |
| Check the index, or whether a file is the game's | `install register info <installation>`, `install register check <installation> <path>` |
| Save the index for another machine | `install register export <installation> --output <file>` |
| List what a project can release | `vibestudio --cli release catalog <project>` |
| Review a release | `vibestudio --cli release plan <project> --map <map>` |
| Print the notes or readme | `vibestudio --cli release notes <project> --map <map>`, add `--readme` for the readme |
| Show or record changes | `vibestudio --cli release changelog <project>`, `--add "<change>" --category added` |
| Publish | `vibestudio --cli release publish <project> --map <map> --release-version <version>` |
| List published releases | `vibestudio --cli release history <project>` |
| Check a map against the game | `vibestudio --cli map dependencies <map> --package <project> --installation <installation>` |

For example:

```sh
vibestudio --cli install register build quake3-games-quake3
vibestudio --cli release plan ./mymod --map maps/arena1.map --json
vibestudio --cli release changelog ./mymod --add "New arena: The Pit" --category added
vibestudio --cli release publish ./mymod --map maps/arena1.map --release-version 1.0.0 --dry-run
vibestudio --cli release publish ./mymod --release-version 1.1.0 --output ./out/1.1.0
```

Use `--model` or `--texture` instead of `--map` for models and texture folders, and nothing for the
whole project. `--installation <id>` picks the installation, `--register <file>` uses an exported
index instead, for example on a build server without the game, and `--no-stock` skips the game
check. `release plan` and `install register info` exit with code 4 when something needs attention.
`release publish` writes nothing with `--dry-run`, and `--no-archive`, `--no-readme`, `--no-notes`,
`--no-record` and `--no-changelog` leave those files out.

## Learn more

- [Project releases](../PROJECT_RELEASES.md): how releases are planned, the asset index format and
  every rule.
- [Packages](packages.md): open, check and edit packages by hand.
- [CLI strategy](../CLI_STRATEGY.md): every `release` and `install register` option.
