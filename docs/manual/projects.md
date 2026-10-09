# Projects and game installations

A project is a folder that holds your mod or map sources. This page shows how to open one, describe it
with a project manifest, tell VibeStudio where your games are installed, search and replace text
across the project, and save your place as a workspace file.

> [!NOTE]
> **Status: Available.** Project folders, manifests, game installation profiles, Steam and GOG
> detection, project search and replace, and workspace files work and have automated tests, but have
> not been proven in real projects. Detection only looks in common Steam and GOG folders, so games
> from other stores need a profile added by hand.

## The Workspace page

The Workspace page is the studio's start page. Choose **Workspace** on the left rail or press
<kbd>Ctrl</kbd>+<kbd>1</kbd>. On macOS, press <kbd>Cmd</kbd> wherever this page says <kbd>Ctrl</kbd>.

- The page header holds **Open Project**, **Initialize Manifest** and **Detect Installs**.
- A tile for each work page, from **Levels** to **Build**, says what that page holds now, such as the
  open map with its entity and brush counts. Select a tile to go to its page.
- **Project Health** collects the open project's state in five tabs (see the table below).
- **Recent Projects** and **Game Installations** list the folders and games VibeStudio remembers.
- **Releases** shows the project's unreleased changes and latest releases, with **Record Change…** and
  **Package and Release…**. See [Package and release](releases.md).
- **Workspace Details** shows the manifest and health details of the open or selected project.
- **Recent Activity** charts recent tasks with their state and duration. Select one to see its full
  log in the Activity Center.

| Tab | What it shows |
| --- | --- |
| **Problems** | Health warnings and the next step for each. Press <kbd>Enter</kbd> on a row to open the file it names. |
| **Search** | Project files and entries of the open package whose path contains what you type. **Reveal** opens the containing folder; **Copy Path** copies the project or package path. |
| **Changes** | Files Git reports as changed or staged, when the project is a Git repository and Git is installed. |
| **Graph** | A list of how the project links to its manifest, folders, game installation and compilers. |
| **Timeline** | Recent project, package, setup and task events. |

## Open a project

1. Choose **Open Project** on the Workspace header, or **File** > **Open Project Folder…**
   (<kbd>Ctrl</kbd>+<kbd>O</kbd>).
2. Choose the folder that holds your sources, such as your mod folder.
   The folder becomes the current project and is added to **Recent Projects**.
3. If **Problems** says the project manifest is missing, create one as described in the next section.

Opening a folder only reads it. To return to a project later, double-click it in **Recent Projects**
or choose it under **File** > **Open Recent**. You can also drop a project folder that already has a
manifest onto the window. **Remove** forgets the selected recent project and **Clear** forgets them
all; neither touches your files. **File** > **Close Project** closes the current project, which stays
in the recent list.

> [!TIP]
> Portable builds of VibeStudio include a `samples` folder with small Doom, Quake and Quake III-family
> projects. They are a safe place to try the studio without risking your own work.

## Describe the project with a manifest

The project manifest is the file `.vibestudio/project.json` inside the project folder. It records the
project's game and folders, its game installation, compiler paths, release settings and per-project
settings. The Workspace, Build and release pages and the command line read it.

To create or refresh it, open the project and choose **Initialize Manifest** on the Workspace header,
or **File** > **Initialize Project Manifest** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd>). A new
manifest treats the whole project folder as source, uses `build` as the output folder and keeps
temporary files in `.vibestudio/tmp`. Refreshing an existing manifest keeps its folders and records
the game installation and editor profile you are using now, and the game when the manifest names
none.

The manifest is plain JSON that you can edit in any text editor, and the Workspace page refreshes when
it changes on disk. Paths are relative to the project folder. This is the manifest of the Quake sample
project:

```json
{
  "schemaVersion": 1,
  "projectId": "sample-quake-minimal",
  "displayName": "Sample Quake Minimal",
  "sourceFolders": ["maps", "scripts"],
  "packageFolders": ["packages"],
  "outputFolder": "out",
  "tempFolder": ".vibestudio/cache",
  "selectedInstallationId": "",
  "createdUtc": "2026-04-30T00:00:00Z",
  "updatedUtc": "2026-04-30T00:00:00Z"
}
```

<details>
<summary>All manifest fields</summary>

| Field | Meaning |
| --- | --- |
| `schemaVersion` | Manifest format version, currently 2. Version 1 manifests still open, and are saved as version 2. |
| `projectId` | Stable identifier. Made from the folder name when missing. |
| `displayName` | The name the studio shows. |
| `game` | The game the project targets, such as `quake`, `quake2`, `quake3` or `doom`. Empty uses the game of the linked installation. |
| `sourceFolders` | Folders that hold your sources. |
| `packageFolders` | Asset folders. `map textures --project-root` checks a map's textures against them. |
| `outputFolder` | Where the Build page writes its command manifests. |
| `tempFolder` | Folder for temporary files. |
| `selectedInstallationId` | The game installation profile this project uses. |
| `compilerSearchPaths` | Extra folders to search for compiler programs. |
| `compilerToolOverrides` | Exact compiler programs, as a list of `toolId` and `executablePath` pairs. These win over paths chosen on the Build page while the project is open. |
| `registeredOutputPaths` | Compiled outputs recorded for the project. |
| `settingsOverrides` | Per-project choices: `selectedInstallationId`, `editorProfileId`, `paletteId`, `compilerProfileId` and `aiFreeMode`. Setting `aiFreeMode` to `true` keeps AI off while the project is open; `false` cannot turn AI on. |
| `release` | How the project is packaged and described when released; see [Release settings](releases.md#release-settings). |
| `createdUtc`, `updatedUtc` | When the manifest was created and last saved. |

Keys VibeStudio does not know, for example from a newer version, are kept when it saves the manifest.

</details>

**File** > **Copy Project Manifest** (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>M</kbd>) copies the resolved
manifest, for example to attach to a bug report.

## Check project health

Choose **Project** > **Validate Project** to run the health checks. VibeStudio switches to the
Workspace page and lists anything that needs attention under **Problems**: a missing manifest or
source folder, a newer manifest version, no linked game installation, or a game whose assets are not
indexed or whose index is out of date. Activate the asset index problem to index the game. Output
and temporary folders that do not exist yet are not problems: they are created when first needed.
Warnings alone do not block your work. `vibestudio --cli project validate` exits with code 4 only for blocking problems, and with code 3
when the folder has no manifest yet.

## Add your game installations

A game installation profile tells VibeStudio where a game is installed, which game it is, and which
packages it loads. Profiles tell the Build page which game to launch, and a Doom installation's base
packages give your PWADs their palette. VibeStudio never writes into a game folder without your
permission.

### Detect Steam and GOG games

1. Choose **Detect Installs** on the Workspace header, or **Project** > **Detect Game Installations**
   (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>G</kbd>).
   Candidates appear under **Detected candidates** in **Game Installations**, with their store and a
   match percentage.
2. Select a candidate and choose **Import Detected**.
   The candidate is saved as a read-only profile.

Detection looks in common Steam library folders, including extra libraries listed by Steam, and in
common GOG folders. It recognises a game by its folder name, its program and its data files, such as
`id1/pak0.pak` for Quake. Detection only reads; nothing is saved until you import a candidate.

### Add an installation by hand

1. In **Game Installations**, choose **Add**.
2. Choose the game's root folder.
3. Choose the game from the list, for example **Quake [quake]** or **Doom-family [doom]**. Choose
   **Custom / Unknown [custom]** for anything else.
4. Enter a name, or keep the suggested one.
   The profile is saved as read-only.

### Choose the installation to use

Select a profile and choose **Use** to make it the default; its row shows **in use**, and the Build
page launches it. Each row also shows **Ready**, or **Needs Review** when a check fails, such as a
missing root folder. **Remove** deletes the profile only, never game files. A project manifest can
also record an installation for the project, which the project health checks report.

### Index the game's assets

Select a profile and choose **Index Assets**, or **Project** > **Index Game Assets**. VibeStudio reads
the game's own packages once, in the background, and remembers every file they hold, the Quake III
shaders they declare and the Doom-family names they define. Releases use this index to leave the
game's own files out, and the Levels page's **Dependencies** uses it to mark references the game
provides. The index is kept with VibeStudio's own data, never in the game folder.

Each row says **assets indexed**, **assets not indexed** or **asset index out of date**; an index
goes out of date when the game's packages change, for example after a patch. Indexing knows the
standard packages of Quake, Quake II, Quake III Arena, Doom, Heretic and Hexen and their official
expansions, plus any base packages saved in the profile. See [Package and release](releases.md).

### Allow test maps

Profiles are read-only to start with. Quake-family engines only load maps from a game folder's `maps`
folder, so launching a build from the Build page needs to copy the map there. The first time, the
Build page asks whether to **Allow Test Maps** for that installation; if you allow it, the profile
keeps that permission. The **Allow test maps** check box in **Game Installations** grants or
withdraws the same permission for the selected profile, and rows with permission show
**test maps allowed**. Only the built map and its lighting files are copied. See
[Build and launch](build-and-launch.md).

## Search and replace across the project

Project search looks for text in your project's source files and in the documents open on the Code
page, including unsaved changes.

1. Choose **Edit** > **Find In Project Files…** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd>), or
   **Find in Project** on the Code page.
   The **Search Results** tab opens on the Code page.
2. Type the text in **Find** and choose **Search**.
   The search runs in the background; **Cancel** stops it.
3. Optionally tick **Match case** or **Whole words**, or open **File filters** and enter **Include**
   and **Exclude** patterns, separated by semicolons. A pattern with a slash, such as `scripts/*.cfg`,
   matches a path in the project; otherwise it matches a file name, such as `*.qc`.
4. Activate a result to open the file at that line.

To replace, tick **Replace with**, type the new text and choose **Search** again to see each change
before and after. An empty replacement deletes the matches. Choose **Apply Preview…** and confirm.
Open documents get the change as an unsaved edit you can undo; files that are not open are saved
straight away, keeping their encoding and line endings. If a file changed since the preview, the whole
batch is refused, and a failure part-way does not undo files already saved: the report lists them.

Search skips untitled documents, package entries and folders such as `.git`, `.vibestudio`, `build`
and `external`. See [Code and scripts](code.md) for the editor itself.

## Save and restore a workspace

A workspace file remembers what you were working on: the project, the open package or package draft,
the map, the saved Code tabs, the selected texture, model and sound, and the page you were on.

- Choose **File** > **Save Workspace As…** to write a `.vibeworkspace` file.
- Choose **File** > **Open Workspace…**, drop the file on the window, or start VibeStudio with
  `--open <file>` to restore it.

A workspace stores references, not contents: save your maps, packages and other documents first.
It does not keep unsaved edits, undo history or camera positions, and opening one never runs a
compiler, launches a game or installs anything. Paths are stored relative to the workspace file, so
you can move a folder that contains both.

## Command-line equivalents

| Task | Command |
| --- | --- |
| Create or refresh a manifest | `vibestudio --cli project init <folder>` |
| Show the manifest and health | `vibestudio --cli project info <folder>` |
| Check health | `vibestudio --cli project validate <folder>` |
| List project files | `vibestudio --cli project files <folder> --where "kind=image"` |
| List, detect or add installations | `vibestudio --cli install list`, `install detect`, `install add <root>` |
| Use, check or remove a profile | `vibestudio --cli install select <id>`, `install validate <id>`, `install remove <id>` |
| Index a game's assets, or check its index | `vibestudio --cli install register build <id>`, `install register info <id>` |
| Package and release the project | `vibestudio --cli release plan <folder>`, `release publish <folder>` |
| Search project text | `vibestudio --cli asset find <folder> --find <text>` |
| Replace project text | `vibestudio --cli asset replace <folder> --find <old> --replace <new>` |
| Save or check a workspace | `vibestudio --cli workspace create <file>`, `workspace inspect <file>` |

For example:

```sh
vibestudio --cli install detect --root "D:/SteamLibrary"
vibestudio --cli install add "C:/Games/Quake" --install-game quake --install-name "Quake" --dry-run
vibestudio --cli asset find ./mymod --find player --whole-word --include "*.qc;*.qh" --json
vibestudio --cli asset replace ./mymod --find old_shader --replace new_shader --include "scripts/*"
vibestudio --cli workspace create ./work.vibeworkspace --project ./mymod --active-module levels
```

`asset replace` only previews unless you add `--write`. `install detect` never saves profiles. See
[Command line](cli.md) for running these commands.

## Learn more

- [Package and release](releases.md): package the project for players.
- [Game installations](../GAME_INSTALLATIONS.md): profile data, detection sources, the asset index and
  safety rules.
- [Project search](../PROJECT_SEARCH.md): matching rules, limits and partial-write reporting.
- [Portable workspaces](../WORKSPACES.md): the `.vibeworkspace` format.
- [CLI strategy](../CLI_STRATEGY.md): every `project`, `install` and `workspace` option.
