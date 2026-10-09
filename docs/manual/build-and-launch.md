# Build and launch

The Build page compiles your map with VibeStudio's own VibeMap2 and VibeMap3 compilers or the Doom
node builders, shows each problem where it happens, and starts the game with the result. This page
covers setting up the compilers, running a build, fixing problems and leaks, and launching a test.

> [!NOTE]
> **Status: Partial.** Build pipelines for Quake, Quake III and Doom maps run VibeMap2, VibeMap3,
> ZDBSP and ZokumBSP and report problems, leaks and outputs. VibeStudio does not include the
> compiler programs yet, and only a few compiler and game combinations have been tested end to end.
> On 2026-10-08 VibeMap2 and VibeMap3 passed VibeStudio's Quake, Quake II and Quake III prepared-build
> tests on Windows; other platforms, games and the older stock-compiler tests are still to be repeated.

## Install the compilers

VibeStudio runs four compilers. VibeMap2 and VibeMap3 are VibeStudio's own, developed as part of the
project: VibeMap2 is derived from ericw-tools and VibeMap3 continues q3map2 from NetRadiant Custom.
ZDBSP and ZokumBSP are external projects. The source code of all four is kept with the VibeStudio
source, but VibeStudio builds do not include the compiler programs yet: build or download the ones
you need from their own projects.

| Compiler | Used for | Programs |
| --- | --- | --- |
| [VibeMap2](https://github.com/themuffinator/VibeyMapTools) | Quake and Quake II maps | `vibemap2-bsp`, `vibemap2-vis`, `vibemap2-light` |
| [VibeMap3](https://github.com/themuffinator/q3mapx) | Quake III-family maps | `vibemap3` |
| [ZDBSP](https://github.com/rheit/zdbsp) | Doom-family nodes | `zdbsp` |
| [ZokumBSP](https://github.com/zokum-no/zokumbsp) | Doom-family nodes, blockmap and reject | `zokumbsp` |

VibeMap2 releases come as `vibemap2-windows-<version>.zip`, `vibemap2-linux-<version>.tar.gz` and
`vibemap2-macos-<version>.tar.gz`, with every program at the top of the archive. Add the extracted
folder to your PATH or to the project's compiler search paths, and VibeStudio finds the programs.

VibeStudio also finds VibeMap2 and VibeMap3 under their earlier names (`vmt-bsp` and the other
`vmt-` programs, and `q3mapx`), and in their build folders when you build them from the copies in
`external/compilers`. Stock ericw-tools (`qbsp`, `vis`, `light`) and q3map2 are not found
automatically; to use one, choose its program with **Locate…** as described below.

## Point VibeStudio at your compilers

Open the Build page (<kbd>Ctrl</kbd>+<kbd>9</kbd>) and choose the **Toolchain** tab. On macOS, press
<kbd>Cmd</kbd> wherever this page says <kbd>Ctrl</kbd>. **Compiler tools** lists every tool with its
**Status** (**Ready** or **Not found**), the **Executable** it runs and where that path comes from in
**Path from**. A tool on your system PATH, or in the project's compiler search paths, is found
automatically. Otherwise:

1. Select the tool.
2. Choose **Locate…** and pick its program.
   **Path from** changes to **Chosen path**, and the path applies to every project.
3. To undo the choice, select the tool and choose **Use Automatic**. After installing a tool, choose
   **Rescan**.

A project manifest can set its own compiler programs and search folders. While that project is open
they win over your choice, and **Path from** shows **Project manifest**; see
[Projects and game installations](projects.md#describe-the-project-with-a-manifest).

**Compiler profiles**, below the tools, lists every single compiler step, such as
**VibeMap2 bsp** or **VibeMap3 light**, and whether its tool is ready. **Run Profile**
(<kbd>Ctrl</kbd>+<kbd>R</kbd>) runs the selected step, **Copy CLI**
(<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd>) copies a matching `vibestudio --cli compiler run`
command, and **Copy Manifest** copies the step's command manifest.

## Build a map

1. Open the map on the Levels page.
   The **Input** field on the Build page follows the open map. To build another file, type its path
   or choose it with the folder button beside the field. Until a map is chosen, opening a project
   picks a **Pipeline** for the project's game.
2. Choose a **Pipeline**.
3. Choose **Run Pipeline** (<kbd>F7</kbd>).
   The stages run in order, each feeding the next. **Pipeline Stages** shows their progress, and the
   Activity Center keeps the full log.

If the map has unsaved edits, VibeStudio asks first, because the compilers read the file on disk:
**Save** includes your edits, **Build Saved File** builds what is on disk, and **Cancel** stops.
**Cancel** on the Build header stops a running build after the current stage.

| Pipeline | Stages | Input |
| --- | --- | --- |
| **Quake full compile** | QBSP, then VIS and LIGHT (VibeMap2) | `.map` |
| **Quake fast iteration** | QBSP and LIGHT; VIS is skipped | `.map` |
| **Quake BSP only** | QBSP | `.map` |
| **Quake III full compile** | BSP, then VIS and LIGHT (VibeMap3) | `.map` |
| **Quake III BSP only** | BSP | `.map` |
| **Doom nodes (ZDBSP)** | Nodes | `.wad` |
| **Doom nodes (ZokumBSP)** | Nodes | `.wad` |

The **Stages** tab lists each stage's input, output, tool and any reason it was skipped. Press
<kbd>Enter</kbd> on a stage, or double-click it, to see its command, captured output and diagnostics
in **Build Details**. A Quake-family build writes the BSP beside the source map, for example
`maps/start.bsp`, and **Show Output** opens the folder that holds the compiled map. With a project
open, the command manifests go to the project's output folder.

## Fix build problems

After a run, the **Problems** tab lists every warning and error the compilers reported, with its stage,
file and line. Press <kbd>Enter</kbd> on a problem, or double-click it, to go to it:

- A line inside a brush, patch or entity of the open map selects that object on the Levels page.
- A line in another text file, such as a shader script, opens the file on the Code page at that line.
- Anything else shows the output of the stage that reported it.

Right-click a problem for **Copy Message**, which copies the line exactly as the compiler printed it,
and **Copy All Problems**. Press <kbd>F4</kbd> and <kbd>Shift</kbd>+<kbd>F4</kbd> on any page to step
through the last build's problems. **Explain** on the toolbar opens the Assistant with the problems
ready to ask about; AI stays off unless you turn it on, as described in [AI assistant](ai.md).

### Find a leak

A leaking map, one that is not sealed from the void, puts the leak at the top of **Problems**. With
the map open on the Levels page, activate the leak to draw the compiler's leak trail over the map and
frame it. If the compiler wrote no leak point file, the problem says so. A leak also stops the VIS
stage, because the compiler keeps no portal file to work from.

To draw a trail yourself, choose **Build** > **Load Leak Trail…** and pick a `.pts` or `.lin` file;
**Build** > **Clear Leak Trail** removes it. **Inspect Artifacts** on the Build header reads the
compiled BSP and any leak or portal file beside it.

## Launch the game

Launching needs a game installation profile; add one on the Workspace page first, as described in
[Projects and game installations](projects.md#add-your-game-installations). VibeStudio launches the
installation marked **in use**, or the first one when none is. Then open the **Launch and Test** tab:

1. **Launch profile**: keep **Automatic (matches the installation)**, or choose a source port style.
2. **Map**: the map to load. It defaults to the map the build produces.
3. **Game folder**: the folder inside the installation the game loads, such as your mod's folder.
   Leave it empty for the base game. VibeStudio remembers it for each installation.
4. Keep **Copy build to game** ticked to copy the built map into the game first.
5. Choose **Launch Game** (<kbd>Ctrl</kbd>+<kbd>G</kbd>).
   VibeStudio shows the exact command line, working folder and any file copies, and starts the game
   only when you choose **Launch**.

**Launch plan** shows the program, arguments and working folder, the copy into the game, and anything
that blocks the launch. **Copy Command Line** copies the command for a shell or script.

<details>
<summary>What each launch profile passes to the game</summary>

| Launch profile | Arguments |
| --- | --- |
| **Quake source port** | `-basedir <installation> -game <game folder> +map <map>` |
| **Quake II source port** | `+set basedir <installation> +set game <game folder> +map <map>` |
| **Quake III source port** | `+set fs_basepath <installation> +set fs_game <game folder> +devmap <map>` |
| **Doom source port** | `-iwad <IWAD> -file <built WAD> -warp <map>`, where `MAP07` becomes `-warp 07` and `E2M3` becomes `-warp 2 3` |
| **Custom launch** | Only the extra arguments you supply |

</details>

### Allow test maps

Quake-family engines only load maps from a game folder's `maps` folder. With **Copy build to game**
ticked, VibeStudio copies the built BSP, and any `.lit` or `.lux` lighting file beside it, into
`<installation>/<game folder>/maps` before launching, using the base game's folder when
**Game folder** is empty. Doom source ports read the built WAD where it is, so nothing is copied for
Doom.

Installation profiles are read-only to start with. The first time a launch needs to copy a map,
VibeStudio asks whether to **Allow Test Maps** for that installation. If you allow it, the profile
keeps the permission and VibeStudio does not ask again. Nothing else in the installation is written.
To take the permission back, clear **Allow test maps** for the installation on the Workspace page.

## Build and launch in one step

**Build and Launch** (<kbd>F5</kbd>) on the Build header runs the pipeline and, when it succeeds,
launches the game with the built map, with the same confirmation as **Launch Game**. This is the
quickest edit, build and test loop. The keys on this page are the defaults: an editor profile or your
own key choices can change them, and **Help** > **Keyboard Shortcuts** lists the keys in effect.

## Share the result

- **Package Map** on the Build header releases the pipeline's map for players: its build, the files
  the engine loads beside it and every custom asset it uses, without the game's own files, with a
  readme and release notes. See [Package and release](releases.md).
- **Add to Package** stages the built map into the open package under `maps/`, ready for Save As; see
  [Packages](packages.md#save-a-new-package).
- **Copy Commands** copies every stage's command line, so the same build can run from a shell or CI.

For Quake-family maps, **Build** > **Prepare Build Workspace…** captures the current map, including
unsaved edits, together with the open package or draft, as verified copies in a new folder. Choose
**Use in Build** to build from that snapshot, so the build uses exactly the assets you staged.
**Build** > **Publish Prepared Build…** then writes a PAK or PK3, and **Build** >
**Deploy Prepared Build…** reviews and installs that package into a game folder, optionally
launching it. See [prepared builds](../LEVEL_EDITOR.md#prepared-builds-with-current-assets) for the
details.

## Command-line equivalents

| Task | Command |
| --- | --- |
| List pipelines | `vibestudio --cli build list` |
| Plan a build without running it | `vibestudio --cli build plan <pipeline> --input <map>` |
| Run a build | `vibestudio --cli build run <pipeline> --input <map> --watch` |
| Show compiler discovery | `vibestudio --cli compiler list` |
| Choose or forget a compiler | `vibestudio --cli compiler set-path <tool> --executable <path>`, `compiler clear-path <tool>` |
| Run one compiler step | `vibestudio --cli compiler run <profile> --input <map>` |
| Inspect a compiled map | `vibestudio --cli bsp inspect <bsp>` |
| Draw a leak trail to a picture | `vibestudio --cli map render <map> --leak <file.pts> --output <file.svg>` |
| Plan or start a launch | `vibestudio --cli launch plan --map <name>`, `launch run --map <name>` |
| Release the built map | `vibestudio --cli release publish <project> --map <map> --release-version <version>` |

For example:

```sh
vibestudio --cli build plan quake-full --input ./maps/start.map
vibestudio --cli build run quake-full --input ./maps/start.map --watch
vibestudio --cli compiler set-path vibemap2-bsp --executable /opt/vibemap2/vibemap2-bsp
vibestudio --cli launch plan --bsp ./maps/start.bsp --deploy
vibestudio --cli launch run --bsp ./maps/start.bsp --deploy --allow-test-maps
```

The pipeline names are `quake-full`, `quake-fast`, `quake-bsp-only`, `quake3-full`,
`quake3-bsp-only`, `doom-zdbsp` and `doom-zokumbsp`. `build plan` and `build run` look for compilers
on your PATH, in the folders given with `--compiler-search-paths`, and in the manifest of a project
given with `--workspace-root`. `launch` uses the installation selected on the Workspace page unless
you pass `--installation <id>`. `launch run --deploy --allow-test-maps` saves the test-map permission
on the profile, as the Build page does.

## Learn more

- [Compiler integration](../COMPILER_INTEGRATION.md): profiles, diagnostics, leak detection and the
  licence boundary.
- [Game installations](../GAME_INSTALLATIONS.md): profiles and the read-only rule.
- [CLI strategy](../CLI_STRATEGY.md): every `build`, `compiler` and `launch` option.
