# Troubleshooting and FAQ

This page helps when something goes wrong: VibeStudio will not start, a game or compiler is not found,
a package will not open, or the studio crashed. It also shows where the logs are and what to include
in a bug report. VibeStudio is pre-alpha software, so if nothing here helps, please report the problem.

## VibeStudio will not start

VibeStudio builds are not signed or notarised yet, so your system may warn you before the first start.
Only continue if you got VibeStudio from the project's own
[GitHub repository](https://github.com/themuffinator/VibeStudio) or its
[releases page](https://github.com/themuffinator/VibeStudio/releases).

### Windows

- **Windows protected your PC** (SmartScreen): choose **More info**, then **Run anyway**.
- A message that `VCRUNTIME140.dll` or `MSVCP140.dll` is missing: install the Microsoft Visual C++
  Redistributable for x64. The portable Windows packages do not include it.
- A message that `Qt6Core.dll` or another Qt file is missing, or a `vibestudio.exe` you built yourself
  exits at once without a message: Windows cannot find Qt. Add Qt's `bin` folder to PATH, as shown in
  [Build from source](building-from-source.md#run-from-the-build-folder). Windows release packages
  include their own Qt files, so this only affects builds you made yourself.

### macOS

- If macOS says the app cannot be opened because Apple cannot check it for malicious software, or
  because the developer cannot be verified (Gatekeeper), open **System Settings** >
  **Privacy & Security** and choose **Open Anyway**. On macOS 13 and 14 you can also Control-click
  the app and choose **Open**. [Install VibeStudio](install.md) has the details.
- A message about missing Qt libraries from a nightly package: nightly packages for macOS do not
  include the Qt frameworks yet. Use a release, or
  [build VibeStudio from source](building-from-source.md).

### Linux

- If the AppImage does not start, it usually lacks FUSE 2. Install your distribution's `libfuse2`
  package (`libfuse2t64` on Ubuntu 24.04 and later), or run the AppImage with
  `--appimage-extract-and-run`. Make sure the file is executable first:
  `chmod +x VibeStudio-<version>-linux-x86_64.AppImage`.
- A message that a `GLIBC` version was not found means your system's C library is older than the one
  the build needs. [Install VibeStudio](install.md) lists the systems the AppImage supports; on an
  older system, build from source.
- "error while loading shared libraries: libQt6Widgets.so.6" from a nightly package: nightly packages
  for Linux do not include the Qt libraries yet and need Qt 6.10.1 or newer on the system. Use the
  AppImage, or build VibeStudio from source.

The command line (`vibestudio --cli ...`) needs no display, so it also works over SSH and in CI.
Commands that draw in 3D (`material render`, `render backends`, `render test`) can use only Vulkan
when there is no display; see [3D views stay empty](#3d-views-stay-empty).

## 3D views stay empty

The Levels camera, the model views and material previews draw with OpenGL or Vulkan. When neither
works, a view shows **The 3D renderer could not draw this view** with the reason, and material
swatches stay blank.

1. Open **Settings** > **Appearance and Language** > **3D Rendering**. **Status** says what each
   renderer found, or why it could not start.
2. Select **Check Renderers**. It starts both again and draws a test image on each.
3. Choose the other renderer under **Renderer**, or **Automatic**.
4. Update your graphics driver. Vulkan needs a Vulkan 1.0 driver; OpenGL needs OpenGL 3.3 (or
   OpenGL ES 3.0). On macOS, OpenGL works without extra software; Vulkan needs MoltenVK.
5. On Linux without a GPU driver, Mesa's software drivers (`mesa-vulkan-drivers` for Vulkan,
   llvmpipe for OpenGL) work, but slowly. **Status** says **runs on the processor** for them.

From the command line:

```sh
vibestudio --cli render backends
vibestudio --cli render test
vibestudio --cli render set opengl
```

`render backends` lists each renderer with its device and driver, or why it is unavailable.
`render test` exits 4 when a renderer draws its test image wrongly and 5 when none can start. To try
a renderer for one run without changing the setting, add `--renderer vulkan` to a command, or set
the environment variable `VIBESTUDIO_RENDER_BACKEND` to `opengl` or `vulkan` before starting the
studio.

On a computer with two graphics processors, Vulkan picks the discrete one. To use another, set
`VIBESTUDIO_VULKAN_DEVICE` to part of its name, such as `Intel`, or to its place in the system's list
of Vulkan devices, counting from 0; `render backends` then names the one in use. OpenGL uses the
graphics processor your operating system assigns to VibeStudio. `VIBESTUDIO_VULKAN_VALIDATION=1`
turns on the Khronos validation layer, where it is installed, for reports to the developers.

## Find the logs

Each day's sessions write to a log file named `vibestudio-<date>.log`, with the date as `YYYYMMDD` in
UTC, in a `logs` folder in VibeStudio's per-user data folder. The log records the studio's messages,
warnings and errors.

| System | Log folder |
| --- | --- |
| Windows | `%APPDATA%\DarkMatterProductions\VibeStudio\logs` |
| macOS | `~/Library/Application Support/DarkMatterProductions/VibeStudio/logs` |
| Linux | `~/.local/share/DarkMatterProductions/VibeStudio/logs` |

`vibestudio --cli diagnostics crashes` prints the folder on any system. Crash reports are kept in the
same folder. [Install VibeStudio](install.md) lists where settings and recovery copies are kept.

## Create a diagnostics bundle

A diagnostics bundle describes your setup for a bug report.

- In the studio, choose **Tools** > **Copy Diagnostic Bundle**
  (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>D</kbd>; on macOS, <kbd>Cmd</kbd> takes the place of
  <kbd>Ctrl</kbd>). It copies to the clipboard the version, Qt runtime, platform, language and theme,
  the 3D renderer and what OpenGL and Vulkan found, the open project and package, the log file's path,
  the number of crash reports, where each compiler was found, any keyboard shortcut conflicts and the
  last 60 lines of the session log.
- From the command line, run `vibestudio --cli diagnostics bundle --output ./diagnostics`. It writes
  `vibestudio-diagnostics.json` with the version, Qt and system details, the 3D renderer choice, and
  the studio's command, module, operation-state, interface and localization reports. Add the output of
  `vibestudio --cli render backends` when 3D views are the problem. It leaves out secrets, API keys,
  environment values, home-directory contents and project files.

> [!IMPORTANT]
> The bundle copied from the studio includes file paths, which can contain your user name, and recent
> log lines. Read it before you post it anywhere public.

## Recover after a crash

If VibeStudio closed unexpectedly, the next start shows the notice **The studio closed unexpectedly
last time.** It does not reopen the files that were open, in case one of them caused the crash.

- **Reopen Last Session** opens them again when you are ready.
- **View Report** shows the crash report, or **Details** when none could be written.
- **Help** > **Crash Reports…** lists every kept report, newest first, with **Copy Report** and
  **Show in Folder**. `vibestudio --cli diagnostics crashes` lists them too.

Crash reports stay on your computer and are never sent anywhere. To recover unsaved work, use
**File** > **Recover Maps…**, **Recover Packages…**, **Recover Audio…** or **Recover Text Documents…**;
each restores a local checkpoint as an unsaved copy and never replaces your original file. The
choices **Keep a crash report on this machine** and **Reopen the last session at start** are in
**Settings** > **Appearance and Language** under **Startup and Recovery**.

## Start with clean settings

To rule out a settings problem without losing your own settings, start VibeStudio with a new
settings file:

```sh
vibestudio --settings-file ./clean-settings.ini
```

```powershell
.\vibestudio.exe --settings-file "$env:TEMP\vibestudio-clean.ini"
```

The studio starts with default settings and saves its settings only to that file, and crash reports
from that run go to a `crash-reports` folder beside it. Start without the option to return to your
normal settings, which stay untouched. The option works with `--cli` too, and
`vibestudio --cli --settings-report` prints where the settings in use are stored.

## The interface is still in English

VibeStudio is prepared for many languages, but the translations are not finished yet, so the
interface stays in English whichever language you choose. A new language takes effect after a
restart. If you choose a right-to-left language such as Arabic, Hebrew, Persian or Urdu, the layout
mirrors even though the text is still English. To go back, choose English or the
**System language** entry under **Settings** > **Appearance and Language** > **Language**, or run
`vibestudio --cli --set-locale en`. `vibestudio --cli localization report --locale de` shows how far
a language's translation has come.

## Text is too small or too large

- **View** > **Larger Text**, **Smaller Text** and **Reset Text Size** step the text scale between
  100% and 200%.
- **Settings** > **Appearance and Language** > **Text scale** offers 100%, 125%, 150%, 175% and 200%,
  and **Density** makes the layout **Comfortable**, **Standard** or **Compact**.
- VibeStudio also follows your system's display scaling.

From the command line, `vibestudio --cli --set-text-scale 150` sets the scale. See
[Accessibility](accessibility.md) for the other display and reading options.

## Compilers are not found

VibeStudio does not include the compiler programs yet; install VibeMap2, VibeMap3, ZDBSP or
ZokumBSP yourself. Then open the **Toolchain** tab on the Build page: a tool that shows **Not found**
needs **Locate…**, and **Rescan** looks again after you install one. On macOS and Linux, the program
must be executable. VibeStudio finds VibeMap2 and VibeMap3 under their current names
(`vibemap2-bsp`, `vibemap3`) and their earlier ones (`vmt-bsp`, `q3mapx`), but not stock ericw-tools
or q3map2: choose those with **Locate…**. Because stock ericw-tools uses the common program names
`vis` and `light`, check that **Executable** points at the ericw-tools programs and not something
else. Scripts must use the new tool ids, such as `vibemap2-bsp` and `vibemap3`: a `compiler`
command given an old id, such as `ericw-qbsp` or `q3map2`, stops with a message that names the new
one. If **Path from** says **Project manifest (missing)**, fix `compilerToolOverrides` in the
project's manifest.
`vibestudio --cli compiler list` shows where each tool was found and its version. See
[Build and launch](build-and-launch.md).

## A game is not detected

**Detect Installs** on the Workspace page looks only in common Steam folders, including extra Steam
libraries, and common GOG folders. It recognises a game by its folder name, its program and its data files, such as
`doom2.wad`, `id1/pak0.pak`, `baseq2/pak0.pak` or `baseq3/pak0.pk3`. For a game installed elsewhere,
from another store or as a source port, choose **Add** in **Game Installations** and pick its
folder. To scan another Steam library or game folder, run
`vibestudio --cli install detect --root "<folder>"`. Detected games are not saved until you choose
**Import Detected**. See [Projects and game installations](projects.md#add-your-game-installations).

## A release includes the game's own files

A release can only leave out what it knows the game ships. If **Package and Release** says the
game's own files were not checked, or the **From the game** chip shows nothing:

- Link the project to its game installation: select it under **Game Installations** on the
  Workspace page and choose **Use**.
- Choose **Index Assets** for that installation, or **Index Game Assets** in the release window's
  notice. Index it again after the game is patched; its row then says **asset index out of date**.
- A file in your project with the same path as one of the game's ships on purpose, because it
  replaces the game's version. The **Included** list marks it with a warning icon; delete or rename
  your copy if that is not what you want.
- For an expansion or another mod, name it as a requirement in the project manifest. See
  [Package and release](releases.md#requirements-and-expansions).

## A release is missing a file

A release gathers what your maps, models and shader scripts name. Sounds, models or textures that
game code, QuakeC or scripts load by name are not found that way: add them with `release.include`
patterns in the project manifest. See [Package and release](releases.md#choose-what-ships).

## A package will not open

- Check that the format is supported; see [Packages](packages.md#supported-formats).
- Packages with more than 250,000 entries, folders included, or paths more than 128 folders deep are
  refused rather than half loaded. Open a smaller archive or a narrower folder.
- Encrypted ZIP entries, ZIP entries that are neither stored nor DEFLATE-compressed, and compressed
  WAD2 and WAD3 lumps are listed but cannot be read. Split, multi-disk ZIP archives are refused.
- The Activity Center keeps the reason for every failed open. For a damaged archive, run
  `vibestudio --cli package validate <package> --json` to see which entries fail.

## Common questions

### Game folders stay unchanged

Detection and indexing a game's assets only read, and installation profiles start read-only.
VibeStudio writes into a game folder only for test-map copies you allow and prepared-build deployments
you review, though the game itself may write its own configuration and logs when you launch it.
Releases are written to your project's output folder, never into the game. Removing a profile never
deletes game files.

### Nothing is sent without your consent

AI features are off by default, and they are the only part of VibeStudio that connects to the
network. Crash reports and diagnostics bundles stay on your computer unless you share them. See
[AI assistant](ai.md).

## Report a bug

Search the [issue tracker](https://github.com/themuffinator/VibeStudio/issues) first, then open a new
issue with:

- the version, from **Help** > **About VibeStudio** or `vibestudio --cli --version`;
- your operating system and how you installed VibeStudio, or that you built it from source;
- what you did, what you expected, and what happened, step by step;
- the diagnostics bundle, and the crash report from **Copy Report** if there was a crash;
- the relevant lines from the session log;
- for a command-line problem, the exact command, its `--json` output and its exit code;
- for a build problem, the list from **Copy All Problems**, on the right-click menu of the Build
  page's **Problems** tab.

Attach maps, packages or other files only if you made them or may share them. Never attach game data
from commercial games.
